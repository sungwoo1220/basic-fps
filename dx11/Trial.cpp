#include "Network.h"
#include "Renderer.h"
#include "Resources.h"
#include <windowsx.h>
#include <shellapi.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace Trial {
namespace {
using Clock=std::chrono::steady_clock;
double Now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
constexpr double Pi=3.14159265358979323846;
constexpr DWORD WindowStyle=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;
constexpr wchar_t ClassName[]=L"BasicFPS.DX11.Trial";
constexpr int Limits[]={60,144,240,300,0};
constexpr Rect Resume{66,341,438,383}, Connect{66,393,438,435}, Exit{66,445,438,487};
constexpr Rect Mode{588,189,930,230}, Samples{588,248,930,289}, Limit{588,307,930,348}, Bots{588,366,930,407};
constexpr Rect Keep{269,334,488,382}, Undo{512,334,731,382};
constexpr Rect Buttons[]={Resume,Connect,Exit,Mode,Samples,Limit,Bots};
enum class Display { Windowed, Borderless, Exclusive };
const wchar_t* Name(Display mode) {
    switch(mode) {
    case Display::Windowed: return L"창모드";
    case Display::Borderless: return L"전체화면 창모드";
    default: return L"독점 전체화면";
    }
}
struct ConnectForm { std::wstring address=L"http://127.0.0.1:3000", nick=L"Player"; };
INT_PTR CALLBACK ConnectDialog(HWND window,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_INITDIALOG) {
        auto* form=reinterpret_cast<ConnectForm*>(l);
        SetWindowLongPtrW(window,DWLP_USER,l);
        SendDlgItemMessageW(window,ID_ADDRESS,EM_SETLIMITTEXT,2048,0);
        SendDlgItemMessageW(window,ID_NICKNAME,EM_SETLIMITTEXT,16,0);
        SetDlgItemTextW(window,ID_ADDRESS,form->address.c_str()); SetDlgItemTextW(window,ID_NICKNAME,form->nick.c_str());
        return TRUE;
    }
    if(msg!=WM_COMMAND) return FALSE;
    if(LOWORD(w)==IDOK) {
        auto* form=reinterpret_cast<ConnectForm*>(GetWindowLongPtrW(window,DWLP_USER));
        wchar_t address[2049]{},nick[17]{};
        GetDlgItemTextW(window,ID_ADDRESS,address,static_cast<int>(std::size(address)));
        GetDlgItemTextW(window,ID_NICKNAME,nick,static_cast<int>(std::size(nick)));
        form->address=address; form->nick=nick;
        if(form->nick.empty()) form->nick=L"Player";
        if(form->nick.back()>=0xd800 && form->nick.back()<=0xdbff) form->nick.pop_back();
        EndDialog(window,IDOK); return TRUE;
    }
    if(LOWORD(w)==IDCANCEL) { EndDialog(window,IDCANCEL); return TRUE; }
    return FALSE;
}

class App {
public:
    explicit App(HINSTANCE instance):instance_(instance) {}
    ~App() { releaseMouse(); gfx_.reset(); if(hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_); if(timer_) CloseHandle(timer_); }
    void create(bool test=false) {
        WNDCLASSEXW cls{}; cls.cbSize=sizeof cls; cls.hInstance=instance_; cls.lpfnWndProc=Dispatch;
        cls.lpszClassName=ClassName; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
        if(!RegisterClassExW(&cls)) throw std::runtime_error("Register window");
        RECT window{0,0,test?1000:1160,test?600:696}; AdjustWindowRectEx(&window,WindowStyle,FALSE,0);
        hwnd_=CreateWindowExW(0,ClassName,L"Basic FPS  |  DX11 시험판",WindowStyle,CW_USEDEFAULT,CW_USEDEFAULT,
            window.right-window.left,window.bottom-window.top,nullptr,nullptr,instance_,this);
        if(!hwnd_) throw std::runtime_error("Create window");
        gfx_=std::make_unique<Renderer>(hwnd_,test);
        if(test) return;
        GetWindowRect(hwnd_,&windowed_);
        monitor_=MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST);
        RAWINPUTDEVICE mouse{0x01,0x02,0,hwnd_};
        if(!RegisterRawInputDevices(&mouse,1,sizeof mouse)) throw std::runtime_error("Register raw mouse");
        // 0x2 is CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, supported by Windows 10/11.
        timer_=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);
        ShowWindow(hwnd_,SW_SHOW); UpdateWindow(hwnd_);
    }
    void start(std::wstring address) { form_.address=std::move(address); connect(); }
    void capture(const std::wstring& directory) {
        Game sample;
        sample.id="1"; sample.predicted={-12,-12,0,0};
        Player own; own.id="1"; own.ammo=9; sample.players.emplace("1",own);
        Player other; other.id="bot-1"; other.state={-9,-3,0,0}; sample.players.emplace("bot-1",other);
        game_=std::move(sample); fps_=0;
        for(UINT count:{1u,2u,4u}) {
            if(!gfx_->setSamples(count) || !gfx_->resize()) throw std::runtime_error("Software MSAA render target not available");
            menu_=count==1;
            const auto path=(std::filesystem::path(directory)/(L"dx11-msaa-"+std::to_wstring(count)+L".bmp")).wstring();
            gfx_->frame(game_,Now(),menu_?Pi*.74:Pi*.82,0,[this]{paint(Now());},false);
            gfx_->readback(path);
        }
    }
    int loop() {
        double last=Now(), due=last, nextFrame=last, lastFps=last;
        unsigned frames=0;
        while(!closing_) {
            MSG m{};
            while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {
                if(m.message==WM_QUIT) return 0;
                TranslateMessage(&m); DispatchMessageW(&m);
            }
            if(!fault_.empty()) throw std::runtime_error(fault_);
            if(closing_) break;
            const double now=Now(), dt=std::clamp(now-last,0.,.1); last=now;
            tick(now,dt,due);
            if(now-lastFps>=.5) { fps_=static_cast<int>(std::lround(frames/(now-lastFps))); frames=0; lastFps=now; }
            const bool visible=!IsIconic(hwnd_);
            const int cap=visible&&active_?(menu_?30:Limits[limit_]):4;
            if(visible && (cap==0 || now>=nextFrame)) {
                if(gfx_->resize()) { gfx_->frame(game_,now,yaw_,pitch_,[&]{paint(now);}); ++frames; }
                const double period=1./std::max(1,cap);
                nextFrame=cap==0?now:((now-nextFrame>period)?now+period:nextFrame+period);
            }
            if(visible && cap==0) continue;
            const auto wake=std::min(due,visible?nextFrame:now+.05);
            wait(std::clamp(wake-Now(),.00015,.05));
        }
        return 0;
    }
private:
    HINSTANCE instance_;
    HWND hwnd_=nullptr;
    HANDLE timer_=nullptr;
    HMONITOR monitor_=nullptr;
    RECT windowed_{};
    bool active_=true, menu_=true, clipped_=false, closing_=false, wasConnected_=false, fire_=false, reload_=false;
    bool pendingDisplay_=false, needsRevert_=false;
    Display display_=Display::Windowed, previous_=Display::Windowed;
    double deadline_=0, connectDeadline_=0, yaw_=0, pitch_=0;
    int limit_=3, fps_=0, focus_=-1, bots_=3;
    POINT pointer_{-10000,-10000};
    std::array<bool,256> keys_{};
    ConnectForm form_;
    Game game_;
    Network network_;
    std::unique_ptr<Renderer> gfx_;
    std::wstring note_;
    std::string fault_;

    static LRESULT CALLBACK Dispatch(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
        if(msg==WM_NCCREATE) {
            auto* self=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->hwnd_=hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(auto* self=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA))) {
            try { return self->message(msg,w,l); }
            catch(const std::exception& e) { self->fault_=e.what(); return 0; }
        }
        return DefWindowProcW(hwnd,msg,w,l);
    }
    void wait(double seconds) const {
        if(timer_) {
            LARGE_INTEGER when{}; when.QuadPart=-static_cast<LONGLONG>(seconds*10000000);
            if(SetWaitableTimer(timer_,&when,0,nullptr,nullptr,FALSE)) {
                MsgWaitForMultipleObjectsEx(1,&timer_,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE); return;
            }
        }
        MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(std::ceil(seconds*1000)),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
    bool playing() const { return active_ && !menu_ && !IsIconic(hwnd_) && game_.own(); }
    void releaseMouse() {
        if(!clipped_) return;
        ClipCursor(nullptr); clipped_=false; SetCursor(LoadCursorW(nullptr,IDC_ARROW));
    }
    void updateMouse() {
        if(!playing()) { releaseMouse(); return; }
        RECT client{};
        if(!GetClientRect(hwnd_,&client) || client.right<=0 || client.bottom<=0) { releaseMouse(); return; }
        POINT lt{client.left,client.top},rb{client.right,client.bottom};
        ClientToScreen(hwnd_,&lt); ClientToScreen(hwnd_,&rb);
        const POINT center{(lt.x+rb.x)/2,(lt.y+rb.y)/2};
        RECT spot{center.x,center.y,center.x+1,center.y+1};
        clipped_=ClipCursor(&spot)!=FALSE;
        if(clipped_) SetCursor(nullptr);
    }
    void showMenu(bool on) {
        menu_=on; focus_=-1; fire_=reload_=false; keys_.fill(false); updateMouse();
    }
    void connect() {
        note_.clear(); game_=Game{}; wasConnected_=false;
        if(network_.start(form_.address,form_.nick)) connectDeadline_=Now()+12;
    }
    bool localRoom() const {
        for(const auto* base:{L"http://127.0.0.1",L"http://localhost",L"http://[::1]"}) {
            const size_t n=std::wcslen(base);
            if(form_.address.compare(0,n,base)==0 && (form_.address.size()==n || form_.address[n]==L':' || form_.address[n]==L'/')) return true;
        }
        return false;
    }
    void tick(double now,double dt,double& due) {
        for(const auto& packet:network_.drain()) game_.receive(packet,now);
        if(game_.own()) { wasConnected_=true; connectDeadline_=0; }
        if(connectDeadline_ && now>connectDeadline_ && network_.busy()) { network_.stop(); connectDeadline_=0; note_=L"방 응답 시간이 초과됐어요."; }
        if(wasConnected_ && !network_.busy()) { wasConnected_=false; game_=Game{}; showMenu(true); }
        if(pendingDisplay_ && (needsRevert_ || now>=deadline_ || (display_==Display::Exclusive && !gfx_->exclusive()))) confirm(false);
        if(!pendingDisplay_ && display_==Display::Exclusive && !gfx_->exclusive()) {
            apply(Display::Windowed); showMenu(true); note_=L"독점 상태가 해제돼 창모드로 돌아왔어요.";
        }
        const double settle=std::exp(-dt*14);
        game_.xOffset*=settle; game_.zOffset*=settle;
        while(!game_.traces.empty() && game_.traces.front().until<=now) game_.traces.pop_front();
        while(!game_.feed.empty() && game_.feed.back().second<=now) game_.feed.pop_back();
        if(now<due) return;
        if(!game_.own()) { due=now+.016; return; }
        if(now-due>BasicFPS::Step*4) due=now;
        std::vector<BasicFPS::Input> batch;
        const bool enabled=playing();
        const int keys=enabled?((keys_['W']?1:0)|(keys_['S']?2:0)|(keys_['A']?4:0)|(keys_['D']?8:0)|(fire_?16:0)|(keys_[VK_SHIFT]?32:0)):0;
        while(due<=now && batch.size()<5) {
            if(game_.pending.size()+batch.size()<100) batch.push_back({game_.seq+1+static_cast<std::int64_t>(batch.size()),keys,yaw_,pitch_,game_.viewTime(now),game_.epoch});
            due+=BasicFPS::Step;
        }
        if(!batch.empty() && network_.send(Inputs(batch))) for(const auto& input:batch) game_.step(input);
        if(reload_ && network_.send("{\"type\":\"reload\"}")) reload_=false;
    }
    bool apply(Display target) {
        if(display_==Display::Windowed && target!=Display::Windowed) {
            GetWindowRect(hwnd_,&windowed_); monitor_=MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST);
        }
        if(target!=Display::Exclusive && !gfx_->setExclusive(false)) return false;
        if(target==Display::Windowed) {
            SetWindowLongPtrW(hwnd_,GWL_STYLE,WindowStyle|WS_VISIBLE);
            SetWindowPos(hwnd_,HWND_NOTOPMOST,windowed_.left,windowed_.top,windowed_.right-windowed_.left,windowed_.bottom-windowed_.top,
                SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        } else {
            MONITORINFO mon{}; mon.cbSize=sizeof mon;
            if(!GetMonitorInfoW(monitor_,&mon)) return false;
            SetWindowLongPtrW(hwnd_,GWL_STYLE,WS_POPUP|WS_CLIPCHILDREN|WS_VISIBLE);
            SetWindowPos(hwnd_,HWND_TOP,mon.rcMonitor.left,mon.rcMonitor.top,mon.rcMonitor.right-mon.rcMonitor.left,
                mon.rcMonitor.bottom-mon.rcMonitor.top,SWP_FRAMECHANGED|SWP_NOACTIVATE);
            if(target==Display::Exclusive && !gfx_->setExclusive(true)) return false;
        }
        display_=target;
        return true;
    }
    void chooseDisplay() {
        previous_=display_;
        const auto next=display_==Display::Windowed?Display::Borderless:(display_==Display::Borderless?Display::Exclusive:Display::Windowed);
        if(!apply(next)) {
            if(!apply(previous_)) apply(Display::Windowed);
            note_=L"이 화면에서는 해당 모드를 적용하지 못했어요."; return;
        }
        pendingDisplay_=true; needsRevert_=false; deadline_=Now()+15; focus_=0;
    }
    void confirm(bool keep) {
        if(!pendingDisplay_) return;
        pendingDisplay_=needsRevert_=false; focus_=3;
        if(!keep) {
            if(!apply(previous_) && !apply(Display::Windowed)) note_=L"화면을 복구하지 못했어요. 게임을 다시 실행해 주세요.";
            else note_=L"이전 화면 모드로 돌아왔어요.";
        } else note_=L"화면 모드를 유지했어요.";
    }
    void action(int button) {
        if(pendingDisplay_) { confirm(button==0); return; }
        switch(button) {
        case 0: if(game_.own()) showMenu(false); break;
        case 1:
            if(network_.busy()) { network_.stop(); game_=Game{}; note_=L"방 연결을 종료하고 있어요…"; break; }
            if(display_==Display::Exclusive) { note_=L"다른 방에 연결하려면 먼저 창모드로 바꿔 주세요."; break; }
            if(DialogBoxParamW(instance_,MAKEINTRESOURCEW(ID_CONNECT),hwnd_,ConnectDialog,reinterpret_cast<LPARAM>(&form_))==IDOK) connect();
            break;
        case 2: closing_=true; break;
        case 3: chooseDisplay(); break;
        case 4: {
            auto next=gfx_->samples();
            for(int i=0;i<3;++i) {
                next=next==4?1:next*2;
                if(gfx_->supports(next)) { if(!gfx_->setSamples(next)) note_=L"MSAA를 적용하지 못했어요."; break; }
            }
            break;
        }
        case 5: limit_=(limit_+1)%static_cast<int>(std::size(Limits)); break;
        case 6:
            if(game_.own() && localRoom()) {
                int count=0; for(const auto& p:game_.players) if(p.first.rfind("bot-",0)==0) ++count;
                bots_=(count+1)%7; network_.send("{\"type\":\"bots\",\"count\":"+std::to_string(bots_)+"}");
            }
            break;
        default: break;
        }
    }
    LRESULT message(UINT msg,WPARAM w,LPARAM l) {
        switch(msg) {
        case WM_CLOSE: closing_=true; return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_GETMINMAXINFO: {
            auto* info=reinterpret_cast<MINMAXINFO*>(l);
            info->ptMinTrackSize={680,440}; return 0;
        }
        case WM_ACTIVATEAPP:
            active_=w!=FALSE;
            if(!active_) { if(pendingDisplay_) needsRevert_=true; showMenu(true); }
            return 0;
        case WM_MOVE: case WM_SIZE: if(clipped_) updateMouse(); return 0;
        case WM_SETCURSOR:
            if(LOWORD(l)==HTCLIENT && clipped_) { SetCursor(nullptr); return TRUE; }
            break;
        case WM_MOUSEMOVE: pointer_={GET_X_LPARAM(l),GET_Y_LPARAM(l)}; break;
        case WM_LBUTTONDOWN:
            if(playing()) { fire_=true; return 0; }
            if(menu_ && gfx_) {
                const POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
                if(pendingDisplay_) {
                    if(gfx_->hover(Keep,p)) action(0);
                    else if(gfx_->hover(Undo,p)) action(1);
                } else for(size_t i=0;i<std::size(Buttons);++i) if(gfx_->hover(Buttons[i],p)) { action(static_cast<int>(i)); break; }
            }
            return 0;
        case WM_LBUTTONUP: fire_=false; return 0;
        case WM_INPUT:
            if(playing()) {
                RAWINPUT data{}; UINT bytes=sizeof data;
                const auto read=GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,&data,&bytes,sizeof(RAWINPUTHEADER));
                if(read!=static_cast<UINT>(-1) && data.header.dwType==RIM_TYPEMOUSE && !(data.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)) {
                    yaw_=std::remainder(yaw_-data.data.mouse.lLastX*.0025,Pi*2);
                    pitch_=std::clamp(pitch_-data.data.mouse.lLastY*.0025,-1.45,1.45);
                }
            }
            break;
        case WM_KEYDOWN:
            if(w<keys_.size()) keys_[w]=true;
            if((l&(1LL<<30))!=0) return 0;
            if(w==VK_ESCAPE) {
                if(pendingDisplay_) confirm(false);
                else if(!menu_ || game_.own()) showMenu(!menu_);
            } else if(menu_ && w==VK_TAB) {
                const int count=pendingDisplay_?2:static_cast<int>(std::size(Buttons));
                const int direction=GetKeyState(VK_SHIFT)<0?-1:1;
                focus_=focus_<0?(direction>0?0:count-1):(focus_+count+direction)%count;
            } else if(menu_ && (w==VK_RETURN || w==VK_SPACE) && focus_>=0) action(focus_);
            else if(w=='R' && playing()) reload_=true;
            return 0;
        case WM_KEYUP: if(w<keys_.size()) keys_[w]=false; return 0;
        default: break;
        }
        return DefWindowProcW(hwnd_,msg,w,l);
    }
    void button(Rect r,const std::wstring& text,int index,bool enabled=true,bool primary=false) {
        const bool hover=enabled&&(gfx_->hover(r,pointer_)||index==focus_);
        gfx_->rect(r,primary?(hover?0xa4e7e4:Aqua):(hover?0x365466:0x223548),enabled?1.f:.65f);
        if(hover) gfx_->line(r.l,r.b-1,r.r,r.b-1,Aqua,2);
        gfx_->label({r.l+12,r.t,r.r-8,r.b},text,17,!enabled?Muted:(primary?Ink:Chalk),primary);
    }
    void paint(double now) {
        const auto* player=game_.own();
        if(!menu_) {
            const unsigned reticle=game_.hitUntil>now?Amber:Chalk;
            for(float side:{-1.f,1.f}) {
                gfx_->line(500+side*4,300,500+side*10,300,reticle,1.4f);
                gfx_->line(500,300+side*4,500,300+side*10,reticle,1.4f);
            }
            if(player) {
                gfx_->rect({34,531,189,573},Ink,.88f);
                gfx_->label({47,532,186,571},L"체력   "+std::to_wstring(player->hp),20,Chalk,true);
                gfx_->rect({775,519,966,573},Ink,.88f);
                const auto ammo=std::to_wstring(player->ammo)+L" / 12";
                gfx_->label({790,524,956,568},player->reload>now?L"재장전 중":ammo,25,player->reload>now?Amber:Chalk,true);
            }
            gfx_->label({33,22,260,48},std::to_wstring(fps_)+L" FPS",16,Chalk,true);
            int row=0;
            for(const auto& item:game_.feed) if(item.second>now && row<4) {
                const auto y=22.f+25.f*static_cast<float>(row++);
                gfx_->rect({730,y,966,y+23},Ink,.84f); gfx_->label({739,y,962,y+23},ToWide(item.first),13,Chalk);
            }
            if(keys_[VK_TAB]) scoreboard();
            return;
        }
        gfx_->rect({0,0,1000,600},Ink,.55f);
        gfx_->rect({40,42,456,554},Ink,.96f);
        gfx_->rect({551,108,960,554},Ink,.96f);
        gfx_->line(66,71,126,71,Aqua,3);
        gfx_->label({65,81,444,142},L"Basic FPS",51,Chalk,true);
        gfx_->label({67,145,435,179},L"DX11 Windows 시험판",21,Aqua,true);
        gfx_->label({67,195,438,222},std::to_wstring(fps_)+L" FPS   /   "+gfx_->gpu,14,Chalk);
        gfx_->line(66,236,430,236,0x40566a);
        gfx_->label({67,246,437,271},player?L"방에 연결됐어요.":L"방 연결",16,Chalk,true);
        const auto status=!note_.empty()?note_:(!game_.error.empty()?ToWide(game_.error):(player?form_.address:network_.status()));
        gfx_->label({67,275,437,307},status.empty()?L"같은 PC 또는 친구가 연 방에 접속해요.":status,14,Muted);
        button(Resume,L"계속하기",0,player!=nullptr,true);
        button(Connect,network_.busy()?L"연결 끊기":L"방에 연결",1);
        button(Exit,L"종료",2);
        gfx_->label({588,130,930,175},L"화면과 연습",29,Chalk,true);
        std::wstring m=Name(display_);
        if(display_==Display::Exclusive && gfx_->exclusive()) m+=L" · DXGI 확인";
        button(Mode,L"화면 모드   /   "+m,3);
        const auto samples=gfx_->samples()==1?std::wstring(L"끔"):std::to_wstring(gfx_->samples())+L"배";
        button(Samples,L"MSAA   /   "+samples,4,gfx_->supports(2)||gfx_->supports(4));
        button(Limit,L"FPS 제한   /   "+(Limits[limit_]?std::to_wstring(Limits[limit_]):std::wstring(L"무제한")),5);
        int count=0; for(const auto& p:game_.players) if(p.first.rfind("bot-",0)==0) ++count;
        button(Bots,player&&localRoom()?L"연습 봇   /   "+std::to_wstring(count)+L"명":L"연습 봇   /   내 PC에서만",6,player&&localRoom());
        gfx_->label({589,430,927,454},L"WASD 이동  ·  Shift 걷기  ·  클릭 사격",15,Muted);
        gfx_->label({589,460,927,484},L"R 재장전  ·  Tab 점수판  ·  Esc 메뉴",15,Muted);
        gfx_->label({589,497,927,522},L"렌더 해상도 100% · DX11 포워드",13,Aqua);
        if(pendingDisplay_) {
            gfx_->rect({0,0,1000,600},Ink,.83f); gfx_->rect({247,208,753,404},0x203449);
            gfx_->line(247,208,753,208,Aqua,3);
            gfx_->label({269,230,729,278},L"이 화면 모드를 유지할까요?",26,Chalk,true);
            gfx_->label({269,283,729,316},std::to_wstring(static_cast<int>(std::ceil(std::max(0.,deadline_-now))))+L"초 뒤 이전 화면으로 돌아가요.",16,Muted);
            button(Keep,L"유지",0,true,true); button(Undo,L"되돌리기",1);
        }
    }
    void scoreboard() {
        std::vector<const Player*> scores;
        for(const auto& entry:game_.players) scores.push_back(&entry.second);
        std::sort(scores.begin(),scores.end(),[](const auto* a,const auto* b) {
            return a->kills==b->kills?(a->deaths==b->deaths?a->id<b->id:a->deaths<b->deaths):a->kills>b->kills;
        });
        gfx_->rect({258,56,742,119+static_cast<float>(scores.size())*25},Ink,.96f);
        gfx_->label({280,62,630,99},L"점수판",25,Aqua,true);
        gfx_->label({596,64,652,99},L"처치",14,Muted); gfx_->label({667,64,724,99},L"사망",14,Muted);
        float y=103;
        for(const auto* row:scores) {
            const auto color=row->id==game_.id?Aqua:Chalk;
            gfx_->line(280,y,724,y,0x415369);
            gfx_->label({281,y,585,y+24},ToWide(row->name),15,color);
            gfx_->label({597,y,650,y+24},std::to_wstring(row->kills),15,color);
            gfx_->label({668,y,722,y+24},std::to_wstring(row->deaths),15,color);
            y+=25;
        }
    }
};

int Smoke(const std::wstring& url,const std::wstring& path) {
    std::ofstream log{std::filesystem::path(path)};
    if(!log) return 2;
    Network net; Game game;
    if(!BasicFPS::RunGoldenCases() || !net.start(url,L"DX11 smoke")) { log<<"Startup checks failed\n"; return 2; }
    const auto end=Now()+18;
    double stageTime=0, x=0,z=0,next=Now();
    int stage=0, direction=0;
    bool reloading=false;
    while(Now()<end && net.busy()) {
        const auto now=Now();
        for(const auto& packet:net.drain()) game.receive(packet,now);
        if(const auto* player=game.own()) {
            if(stage==0 && player->name=="DX11 smoke" && player->ammo==12) {
                x=player->state.x; z=player->state.z;
                for(int key:{1,2,4,8}) {
                    auto probe=player->state;
                    for(int i=0;i<25;++i) BasicFPS::Move(probe,{0,key,0,0,0,player->epoch});
                    if(std::hypot(probe.x-x,probe.z-z)>1) { direction=key; break; }
                }
                stage=1; stageTime=now; log<<"JOIN "<<game.id<<'\n';
            }
            if(stage==1 && std::hypot(player->state.x-x,player->state.z-z)>.25 && player->ack>0) {
                stage=2; stageTime=now; log<<"MOVE ack="<<player->ack<<'\n';
            }
            if(stage==2 && player->ammo<=11 && net.send("{\"type\":\"reload\"}")) {
                stage=3; stageTime=now; log<<"FIRE ammo="<<player->ammo<<'\n';
            }
            if(stage==3) {
                if(player->reload>now) reloading=true;
                if(reloading && player->ammo==12 && player->reload<=now) {
                    log<<"PASS: Windows DX11 client joined, moved, fired and manually reloaded. GPU not tested.\n"; return 0;
                }
            }
            if(stage && now-stageTime>7) break;
            if(stage && now>=next) {
                const BasicFPS::Input input{game.seq+1,stage==1?direction:(stage==2?16:0),0,0,game.viewTime(now),game.epoch};
                if(net.send(Inputs({input}))) game.step(input);
                next=now+BasicFPS::Step;
            }
        }
        Sleep(5);
    }
    log<<"FAIL stage="<<stage<<" error="<<game.error<<" network="<<ToUtf8(net.status())<<'\n'; return 3;
}
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    using namespace Trial;
    int argc=0;
    auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    std::vector<std::wstring> args;
    if(argv) { args.assign(argv,argv+argc); LocalFree(argv); }
    const bool graphicsSmoke=args.size()==3 && args[1]==L"--render-smoke";
    const bool networkSmoke=args.size()==4 && args[1]==L"--smoke";
    try {
        if(networkSmoke) return Smoke(args[2],args[3]);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        App app(instance);
        if(graphicsSmoke) { app.create(true); app.capture(args[2]); return 0; }
        app.create();
        if(args.size()==3 && args[1]==L"--server") app.start(args[2]);
        return app.loop();
    } catch(const std::exception& e) {
        if(networkSmoke || graphicsSmoke) {
            const auto file=graphicsSmoke?std::filesystem::path(args[2])/L"render-error.txt":std::filesystem::path(args[3]);
            std::ofstream log(file,std::ios::app); log<<e.what()<<'\n';
        } else MessageBoxW(nullptr,(L"DX11 시험판을 실행하지 못했어요.\n\n"+ToWide(e.what())).c_str(),L"Basic FPS",MB_OK|MB_ICONERROR);
        return 1;
    }
}
