#pragma once
#include "Protocol.h"
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <DirectXMath.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <functional>

namespace Trial {
using Microsoft::WRL::ComPtr;
struct Rect { float l,t,r,b; };
constexpr unsigned Ink=0x111c29, Chalk=0xf3f7f7, Aqua=0x75dbdc, Amber=0xefb478, Muted=0xaabcc9;

class Renderer {
public:
    Renderer(HWND hwnd,bool software=false);
    ~Renderer();
    bool resize();
    bool exclusive() const;
    bool setExclusive(bool on);
    bool setSamples(UINT count);
    bool supports(UINT count) const;
    UINT samples() const { return samples_; }
    bool setAnisotropy(UINT level);
    UINT anisotropy() const { return anisotropy_; }
    std::wstring gpu;
    void frame(const Game& game,double now,double yaw,double pitch,const std::function<void()>& overlay,bool present=true);
    void readback(const std::wstring& bmp);
    void line(float a,float b,float c,float d,unsigned color,float width=1,float opacity=1);
    void rect(Rect box,unsigned color,float opacity=1);
    void label(Rect box,const std::wstring& text,int size,unsigned color,bool bold=false);
    bool hover(Rect box,POINT client) const;
private:
    struct Vertex { float x,y,z,r,g,b,u,v,textured; };
    HWND hwnd_;
    UINT width_=0,height_=0,samples_=1,anisotropy_=0,flags_=0,mapCount_=0;
    bool resizeNeeded_=true,tearing_=false;
    float scale_=1,originX_=0,originY_=0;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID3D11Texture2D> back_,msaa_;
    ComPtr<ID3D11RenderTargetView> target_;
    ComPtr<ID3D11DepthStencilView> depth_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11VertexShader> vertex_;
    ComPtr<ID3D11PixelShader> pixel_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11Buffer> map_,objects_,camera_;
    ComPtr<ID3D11ShaderResourceView> surface_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID2D1DeviceContext> overlay_;
    ComPtr<ID2D1Bitmap1> bitmap_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteFactory> fonts_;
    std::map<int,ComPtr<IDWriteTextFormat>> formats_;
    void clearViews();
    void views(UINT count);
    void makePipeline();
    void makeArena();
    void makeSurface();
    void draw(ID3D11Buffer* buffer,UINT count);
    static void box(std::vector<Vertex>& dst,float x,float y,float z,float w,float h,float d,unsigned color,bool textured=false);
    static void trace(std::vector<Vertex>& dst,const Trace& shot);
};
}
