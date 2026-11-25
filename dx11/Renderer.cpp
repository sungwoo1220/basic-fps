#include "Renderer.h"
#include <d3dcompiler.h>
#include <d2d1_1helper.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace DirectX;
namespace Trial {
namespace {
constexpr auto ColorFormat=DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr auto DepthFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;
constexpr UINT ObjectCapacity=16384;
void Check(HRESULT hr,const char* operation) {
    if(FAILED(hr)) throw std::runtime_error(std::string(operation)+" ("+std::to_string(hr)+")");
}
float Channel(unsigned color,unsigned shift) { return static_cast<float>((color>>shift)&255)/255.0f; }
D2D1_COLOR_F Color(unsigned value,float alpha) { return D2D1::ColorF(Channel(value,16),Channel(value,8),Channel(value,0),alpha); }
constexpr char Shader[]=R"(
cbuffer Camera : register(b0) { row_major float4x4 vp; };
struct V { float3 position : POSITION; float3 color : COLOR; };
struct P { float4 pos : SV_POSITION; float3 color : COLOR; float dist : TEXCOORD; };
P VS(V v) { P o; o.pos=mul(float4(v.position,1),vp); o.color=v.color; o.dist=o.pos.w; return o; }
float4 PS(P p) : SV_TARGET {
    float haze=saturate((p.dist-12)/68);
    return float4(lerp(p.color,float3(.19,.27,.34),haze*.67),1);
})";
}

Renderer::Renderer(HWND hwnd,bool software):hwnd_(hwnd) {
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    Check(D3D11CreateDevice(nullptr,software?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE,nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,2,D3D11_SDK_VERSION,&device_,nullptr,&ctx_),"D3D11CreateDevice");
    ComPtr<IDXGIDevice1> dxgi;
    Check(device_.As(&dxgi),"DXGI device");
    dxgi->SetMaximumFrameLatency(1);
    ComPtr<IDXGIAdapter> adapter;
    Check(dxgi->GetAdapter(&adapter),"GPU adapter");
    DXGI_ADAPTER_DESC info{};
    if(SUCCEEDED(adapter->GetDesc(&info))) gpu=info.Description;
    ComPtr<IDXGIFactory2> factory;
    Check(adapter->GetParent(IID_PPV_ARGS(&factory)),"DXGI factory");
    ComPtr<IDXGIFactory5> factory5;
    BOOL allow=FALSE;
    if(!software && SUCCEEDED(factory.As(&factory5)) &&
        SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,&allow,sizeof allow))) tearing_=allow!=FALSE;
    flags_=DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH|(tearing_?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0u);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Format=ColorFormat; desc.SampleDesc={1,0}; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=2; desc.Scaling=DXGI_SCALING_STRETCH; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE; desc.Flags=flags_;
    Check(factory->CreateSwapChainForHwnd(device_.Get(),hwnd_,&desc,nullptr,nullptr,&swap_),"DXGI swap chain");
    Check(factory->MakeWindowAssociation(hwnd_,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES),"Disable automatic Alt+Enter");

    ComPtr<ID2D1Factory1> d2d;
    Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf()),"Direct2D factory");
    ComPtr<ID2D1Device> overlayDevice;
    Check(d2d->CreateDevice(dxgi.Get(),&overlayDevice),"Direct2D device");
    Check(overlayDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&overlay_),"Direct2D context");
    Check(overlay_->CreateSolidColorBrush(Color(Chalk,1),&brush_),"Direct2D brush");
    Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(fonts_.GetAddressOf())),"DirectWrite");
    makePipeline(); makeArena(); resize();
}
Renderer::~Renderer() { if(swap_ && exclusive()) swap_->SetFullscreenState(FALSE,nullptr); }
bool Renderer::exclusive() const {
    BOOL state=FALSE;
    return swap_ && SUCCEEDED(swap_->GetFullscreenState(&state,nullptr)) && state!=FALSE;
}
bool Renderer::setExclusive(bool active) {
    if(exclusive()==active) return true;
    clearViews();
    const HRESULT result=swap_->SetFullscreenState(active?TRUE:FALSE,nullptr);
    resizeNeeded_=true;
    return result==S_OK && exclusive()==active;
}
bool Renderer::supports(UINT count) const {
    if(count==1) return true;
    if(count!=2 && count!=4) return false;
    UINT c=0,d=0;
    return SUCCEEDED(device_->CheckMultisampleQualityLevels(ColorFormat,count,&c)) && c &&
        SUCCEEDED(device_->CheckMultisampleQualityLevels(DepthFormat,count,&d)) && d;
}
bool Renderer::setSamples(UINT count) {
    if(!supports(count)) return false;
    if(samples_==count) return true;
    const UINT previous=samples_;
    samples_=count;
    try { if(!resizeNeeded_ && back_) views(count); }
    catch(...) { samples_=previous; views(previous); return false; }
    return true;
}
void Renderer::clearViews() {
    overlay_->SetTarget(nullptr); bitmap_.Reset();
    ctx_->OMSetRenderTargets(0,nullptr,nullptr);
    target_.Reset(); depth_.Reset(); msaa_.Reset(); back_.Reset();
    ctx_->Flush();
}
bool Renderer::resize() {
    RECT r{};
    if(!GetClientRect(hwnd_,&r) || r.right<=0 || r.bottom<=0 || IsIconic(hwnd_)) return false;
    const auto w=static_cast<UINT>(r.right),h=static_cast<UINT>(r.bottom);
    if(w==width_ && h==height_ && !resizeNeeded_) return true;
    clearViews();
    Check(swap_->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,flags_),"Resize swap chain");
    width_=w; height_=h;
    Check(swap_->GetBuffer(0,IID_PPV_ARGS(&back_)),"Back buffer");
    ComPtr<IDXGISurface> surface;
    Check(back_.As(&surface),"Back buffer surface");
    const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(ColorFormat,D2D1_ALPHA_MODE_IGNORE),96,96);
    Check(overlay_->CreateBitmapFromDxgiSurface(surface.Get(),&properties,&bitmap_),"Direct2D back buffer");
    overlay_->SetTarget(bitmap_.Get());
    overlay_->SetDpi(96,96);
    overlay_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    views(samples_);
    scale_=std::min(static_cast<float>(w)/1000.f,static_cast<float>(h)/600.f);
    originX_=(static_cast<float>(w)-1000*scale_)/2;
    originY_=(static_cast<float>(h)-600*scale_)/2;
    resizeNeeded_=false; return true;
}
void Renderer::views(UINT count) {
    ctx_->OMSetRenderTargets(0,nullptr,nullptr);
    ComPtr<ID3D11Texture2D> msaa;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depth;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=width_; desc.Height=height_; desc.MipLevels=1; desc.ArraySize=1;
    desc.SampleDesc={count,0}; desc.Usage=D3D11_USAGE_DEFAULT;
    if(count!=1) {
        desc.Format=ColorFormat; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        Check(device_->CreateTexture2D(&desc,nullptr,&msaa),"MSAA color target");
    }
    Check(device_->CreateRenderTargetView(count==1?back_.Get():msaa.Get(),nullptr,&target),"Render target");
    desc.Format=DepthFormat; desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depthTexture;
    Check(device_->CreateTexture2D(&desc,nullptr,&depthTexture),"Depth target");
    Check(device_->CreateDepthStencilView(depthTexture.Get(),nullptr,&depth),"Depth view");
    msaa_=std::move(msaa); target_=std::move(target); depth_=std::move(depth);
}
void Renderer::makePipeline() {
    const auto compile=[](const char* entry,const char* profile) {
        ComPtr<ID3DBlob> blob,errors;
        const HRESULT hr=D3DCompile(Shader,sizeof Shader-1,"BasicFPS-DX11.hlsl",nullptr,nullptr,entry,profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
        if(FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        Check(hr,"Build DX11 shader");
        return blob;
    };
    const auto vs=compile("VS","vs_5_0"),ps=compile("PS","ps_5_0");
    Check(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_),"Vertex shader");
    Check(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel_),"Pixel shader");
    const D3D11_INPUT_ELEMENT_DESC input[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}
    };
    Check(device_->CreateInputLayout(input,2,vs->GetBufferPointer(),vs->GetBufferSize(),&layout_),"Vertex layout");
    D3D11_BUFFER_DESC matrix{};
    matrix.ByteWidth=sizeof(XMFLOAT4X4); matrix.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    Check(device_->CreateBuffer(&matrix,nullptr,&camera_),"Camera buffer");
    D3D11_BUFFER_DESC dynamic{};
    dynamic.ByteWidth=ObjectCapacity*sizeof(Vertex); dynamic.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    dynamic.Usage=D3D11_USAGE_DYNAMIC; dynamic.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    Check(device_->CreateBuffer(&dynamic,nullptr,&objects_),"Object buffer");
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE; raster.MultisampleEnable=TRUE;
    Check(device_->CreateRasterizerState(&raster,&raster_),"Rasterizer");
}
void Renderer::box(std::vector<Vertex>& dst,float x,float y,float z,float w,float h,float d,unsigned color) {
    const float a=w*.5f,b=h*.5f,c=d*.5f;
    const Point corners[]={{x-a,y-b,z-c},{x+a,y-b,z-c},{x+a,y+b,z-c},{x-a,y+b,z-c},
        {x-a,y-b,z+c},{x+a,y-b,z+c},{x+a,y+b,z+c},{x-a,y+b,z+c}};
    const int faces[][6]={{0,1,2,0,2,3},{5,4,7,5,7,6},{4,0,3,4,3,7},{1,5,6,1,6,2},{3,2,6,3,6,7},{4,5,1,4,1,0}};
    constexpr float shade[]={.89f,.77f,.83f,.68f,1.f,.51f};
    for(size_t face=0;face<6;++face) for(const int i:faces[face]) {
        const auto p=corners[i]; const auto s=shade[face];
        dst.push_back({p.x,p.y,p.z,s*Channel(color,16),s*Channel(color,8),s*Channel(color,0)});
    }
}
void Renderer::makeArena() {
    std::vector<Vertex> vertices;
    box(vertices,0,-.14f,0,40,.28f,40,0x354c59);
    for(int i=-20;i<=20;i+=2) {
        box(vertices,static_cast<float>(i),.003f,0,.019f,.006f,40,0x405967);
        box(vertices,0,.003f,static_cast<float>(i),40,.006f,.019f,0x405967);
    }
    for(float side:{-20.f,20.f}) {
        box(vertices,side,1.6f,0,.35f,3.2f,40,0x263949);
        box(vertices,0,1.6f,side,40,3.2f,.35f,0x263949);
        box(vertices,side*.987f,.16f,0,.055f,.06f,40,0x7bb0b2);
        box(vertices,0,.16f,side*.987f,40,.06f,.055f,0x7bb0b2);
    }
    for(const auto& b:BasicFPS::Boxes) {
        const auto x=static_cast<float>(b.x),z=static_cast<float>(b.z),w=static_cast<float>(b.w),h=static_cast<float>(b.h),d=static_cast<float>(b.d);
        box(vertices,x,h*.5f,z,w,h,d,(b.x==0&&b.z==0)?0x795949:0x5c696b);
        for(float side:{-1.f,1.f}) {
            box(vertices,x,h-.20f,z+side*d*.5f,w,.065f,.018f,0xd8bb88);
            box(vertices,x+side*w*.5f,h-.20f,z,.018f,.065f,d,0xd8bb88);
        }
    }
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth=static_cast<UINT>(vertices.size()*sizeof(Vertex)); desc.BindFlags=D3D11_BIND_VERTEX_BUFFER; desc.Usage=D3D11_USAGE_IMMUTABLE;
    const D3D11_SUBRESOURCE_DATA data{vertices.data(),0,0};
    Check(device_->CreateBuffer(&desc,&data,&map_),"Arena buffer");
    mapCount_=static_cast<UINT>(vertices.size());
}
void Renderer::trace(std::vector<Vertex>& dst,const Trace& shot) {
    const auto a=XMVectorSet(shot.from.x,shot.from.y,shot.from.z,0);
    const auto b=XMVectorSet(shot.to.x,shot.to.y,shot.to.z,0);
    const auto delta=b-a;
    const auto length=XMVectorGetX(XMVector3Length(delta));
    if(length<.07f) return;
    const auto start=a+delta*(std::min(.48f,length*.5f)/length);
    auto right=XMVector3Cross(delta,XMVectorSet(0,1,0,0));
    if(XMVectorGetX(XMVector3LengthSq(right))<.0001f) right=XMVectorSet(1,0,0,0);
    right=XMVector3Normalize(right)*.017f;
    const auto up=XMVector3Normalize(XMVector3Cross(right,delta))*.017f;
    for(const auto& axis:{right,up}) {
        const XMVECTOR quad[]={start-axis,start+axis,b+axis,start-axis,b+axis,b-axis};
        for(const auto& v:quad) { XMFLOAT3 p; XMStoreFloat3(&p,v); dst.push_back({p.x,p.y,p.z,1,.73f,.33f}); }
    }
    if(shot.impact || shot.blood) box(dst,shot.to.x,shot.to.y,shot.to.z,.07f,.07f,.07f,shot.blood?0xe65449:0xfae0ac);
}
void Renderer::draw(ID3D11Buffer* buffer,UINT count) {
    const UINT stride=sizeof(Vertex),offset=0;
    ctx_->IASetVertexBuffers(0,1,&buffer,&stride,&offset); ctx_->Draw(count,0);
}
void Renderer::frame(const Game& game,double now,double yaw,double pitch,const std::function<void()>& paint,bool present) {
    const auto x=static_cast<float>(game.predicted.x+game.xOffset),z=static_cast<float>(game.predicted.z+game.zOffset);
    const auto cp=std::cos(pitch);
    const auto direction=XMVectorSet(static_cast<float>(-std::sin(yaw)*cp),static_cast<float>(std::sin(pitch)),static_cast<float>(-std::cos(yaw)*cp),0);
    const auto eye=game.own()?XMVectorSet(x,1.6f,z,1):XMVectorSet(13,9,14,1);
    const auto view=XMMatrixLookToRH(eye,game.own()?direction:XMVectorSet(-13,-7,-14,0),XMVectorSet(0,1,0,0));
    const auto projection=XMMatrixPerspectiveFovRH(XMConvertToRadians(72.f),static_cast<float>(width_)/static_cast<float>(height_),.06f,105.f);
    XMFLOAT4X4 matrix;
    XMStoreFloat4x4(&matrix,view*projection);
    ctx_->UpdateSubresource(camera_.Get(),0,nullptr,&matrix,0,0);
    const float sky[]={.19f,.27f,.34f,1};
    ctx_->ClearRenderTargetView(target_.Get(),sky);
    ctx_->ClearDepthStencilView(depth_.Get(),D3D11_CLEAR_DEPTH,1,0);
    ID3D11RenderTargetView* rtv=target_.Get();
    ctx_->OMSetRenderTargets(1,&rtv,depth_.Get());
    const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};
    ctx_->RSSetViewports(1,&viewport); ctx_->RSSetState(raster_.Get());
    ctx_->IASetInputLayout(layout_.Get()); ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vertex_.Get(),nullptr,0); ctx_->PSSetShader(pixel_.Get(),nullptr,0);
    ID3D11Buffer* uniform=camera_.Get(); ctx_->VSSetConstantBuffers(0,1,&uniform);
    draw(map_.Get(),mapCount_);
    std::vector<Vertex> transient;
    transient.reserve(game.players.size()*360+game.traces.size()*48);
    for(const auto& [id,player]:game.players) if(id!=game.id) {
        const auto pose=player.pose(game.viewTime(now));
        for(const auto& p:BasicFPS::Parts) {
            const auto px=static_cast<float>(pose.x+p.x),pz=static_cast<float>(pose.z+p.z);
            const auto py=static_cast<float>(p.y),w=static_cast<float>(p.w),h=static_cast<float>(p.h),d=static_cast<float>(p.d);
            const unsigned color=player.shield?0x83cbd7:(p.y>1.5?0xf0ad84:(p.y<.5?0x63424f:0xbc6255));
            box(transient,px,py,pz,w,h,d,color);
        }
    }
    for(const auto& shot:game.traces) if(shot.until>now) trace(transient,shot);
    if(!transient.empty() && transient.size()<=ObjectCapacity) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(ctx_->Map(objects_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Map game objects");
        std::memcpy(mapped.pData,transient.data(),transient.size()*sizeof(Vertex));
        ctx_->Unmap(objects_.Get(),0); draw(objects_.Get(),static_cast<UINT>(transient.size()));
    }
    ctx_->OMSetRenderTargets(0,nullptr,nullptr);
    if(msaa_) ctx_->ResolveSubresource(back_.Get(),0,msaa_.Get(),0,ColorFormat);
    overlay_->BeginDraw();
    overlay_->SetTransform(D2D1::Matrix3x2F(scale_,0,0,scale_,originX_,originY_));
    paint();
    const auto overlayResult=overlay_->EndDraw();
    if(overlayResult==D2DERR_RECREATE_TARGET) resizeNeeded_=true;
    else Check(overlayResult,"HUD drawing");
    if(present) Check(swap_->Present(0,tearing_&&!exclusive()?DXGI_PRESENT_ALLOW_TEARING:0u),"Present");
}
void Renderer::readback(const std::wstring& bmp) {
    // Only the optional software smoke test calls this; the gameplay loop never reads GPU surfaces.
    D3D11_TEXTURE2D_DESC d{}; back_->GetDesc(&d);
    d.BindFlags=0; d.MiscFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> copy;
    Check(device_->CreateTexture2D(&d,nullptr,&copy),"Readback texture");
    ctx_->CopyResource(copy.Get(),back_.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    Check(ctx_->Map(copy.Get(),0,D3D11_MAP_READ,0,&m),"Readback");
    const auto pitch=width_*4;
    BITMAPFILEHEADER file{};
    file.bfType=0x4d42; file.bfOffBits=sizeof file+sizeof(BITMAPINFOHEADER); file.bfSize=file.bfOffBits+pitch*height_;
    BITMAPINFOHEADER image{};
    image.biSize=sizeof image; image.biWidth=static_cast<LONG>(width_); image.biHeight=-static_cast<LONG>(height_);
    image.biPlanes=1; image.biBitCount=32; image.biCompression=BI_RGB; image.biSizeImage=pitch*height_;
    std::ofstream out(std::filesystem::path(bmp),std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file),sizeof file); out.write(reinterpret_cast<const char*>(&image),sizeof image);
    const auto* data=static_cast<const char*>(m.pData);
    for(UINT y=0;y<height_;++y) out.write(data+static_cast<size_t>(y)*m.RowPitch,pitch);
    ctx_->Unmap(copy.Get(),0);
    if(!out) throw std::runtime_error("Could not save render smoke image");
}
void Renderer::rect(Rect r,unsigned color,float opacity) {
    brush_->SetColor(Color(color,opacity)); overlay_->FillRectangle(D2D1::RectF(r.l,r.t,r.r,r.b),brush_.Get());
}
void Renderer::line(float a,float b,float c,float d,unsigned color,float width,float opacity) {
    brush_->SetColor(Color(color,opacity)); overlay_->DrawLine(D2D1::Point2F(a,b),D2D1::Point2F(c,d),brush_.Get(),width);
}
void Renderer::label(Rect r,const std::wstring& text,int size,unsigned color,bool bold) {
    if(text.empty()) return;
    auto& format=formats_[bold?-size:size];
    if(!format) {
        Check(fonts_->CreateTextFormat(L"Malgun Gothic",nullptr,bold?DWRITE_FONT_WEIGHT_BOLD:DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,static_cast<float>(size),L"ko-KR",&format),"HUD font");
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    brush_->SetColor(Color(color,1));
    overlay_->DrawText(text.c_str(),static_cast<UINT32>(text.size()),format.Get(),D2D1::RectF(r.l,r.t,r.r,r.b),brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
bool Renderer::hover(Rect r,POINT p) const {
    const auto x=(static_cast<float>(p.x)-originX_)/scale_,y=(static_cast<float>(p.y)-originY_)/scale_;
    return x>=r.l&&x<r.r&&y>=r.t&&y<r.b;
}
}
