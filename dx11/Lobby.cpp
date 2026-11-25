#include "Lobby.h"
#include "Network.h"
#include "Protocol.h"
#include <objbase.h>
#include <oleauto.h>
#include <natupnp.h>
#include <winhttp.h>
#include <chrono>
#include <cstdlib>

namespace Trial {
namespace {
struct Internet {
    HINTERNET h=nullptr;
    explicit Internet(HINTERNET value=nullptr):h(value) {}
    ~Internet() { if(h) WinHttpCloseHandle(h); }
    Internet(const Internet&)=delete;
    Internet& operator=(const Internet&)=delete;
};
struct Board { std::wstring host; INTERNET_PORT port=INTERNET_DEFAULT_HTTPS_PORT; bool secure=true; };
// BASIC_FPS_LOBBY=http://127.0.0.1:8787 points a test build at a local board.
Board Where() {
    Board board{L"basic-fps.baze.cloud"};
    wchar_t* custom=nullptr; size_t size=0;
    if(_wdupenv_s(&custom,&size,L"BASIC_FPS_LOBBY")==0 && custom) {
        std::wstring url=custom; free(custom);
        URL_COMPONENTSW parts{}; parts.dwStructSize=sizeof parts; parts.dwHostNameLength=static_cast<DWORD>(-1);
        if(WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts) && parts.dwHostNameLength) {
            board.host.assign(parts.lpszHostName,parts.dwHostNameLength);
            board.port=parts.nPort; board.secure=parts.nScheme==INTERNET_SCHEME_HTTPS;
        }
    }
    return board;
}
int Request(const wchar_t* method,const std::wstring& path,const std::string& body,const std::string& token,std::string& reply) {
    reply.clear();
    const Board board=Where();
    Internet session(WinHttpOpen(L"Basic FPS DX11 trial/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session.h) session.h=WinHttpOpen(L"Basic FPS DX11 trial/1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session.h || !WinHttpSetTimeouts(session.h,4000,4000,8000,8000)) return 0;
    const Internet connection(WinHttpConnect(session.h,board.host.c_str(),board.port,0));
    if(!connection.h) return 0;
    const Internet request(WinHttpOpenRequest(connection.h,method,path.c_str(),nullptr,WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,board.secure?WINHTTP_FLAG_SECURE:0));
    if(!request.h) return 0;
    std::wstring headers=L"Content-Type: application/json\r\n";
    if(!token.empty()) headers+=L"Authorization: Bearer "+ToWide(token)+L"\r\n";
    const DWORD length=static_cast<DWORD>(body.size());
    if(!WinHttpSendRequest(request.h,headers.c_str(),static_cast<DWORD>(-1),length?const_cast<char*>(body.data()):WINHTTP_NO_REQUEST_DATA,length,length,0) ||
        !WinHttpReceiveResponse(request.h,nullptr)) return 0;
    DWORD status=0, statusSize=sizeof status;
    if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&statusSize,WINHTTP_NO_HEADER_INDEX)) return 0;
    for(;;) {
        DWORD available=0;
        if(!WinHttpQueryDataAvailable(request.h,&available) || available==0) break;
        const size_t at=reply.size();
        if(at+available>65536) return 0;
        reply.resize(at+available);
        DWORD read=0;
        if(!WinHttpReadData(request.h,reply.data()+at,available,&read)) return 0;
        reply.resize(at+read);
        if(read==0) break;
    }
    return static_cast<int>(status);
}
std::string Quote(const std::string& text) {
    std::string out="\"";
    for(const char c:text) {
        if(static_cast<unsigned char>(c)<32) continue;
        if(c=='"' || c=='\\') out+='\\';
        out+=c;
    }
    return out+'"';
}
std::wstring Text(const Json& value) { return value.kind==Json::String?ToWide(value.text):std::wstring{}; }
bool Forward(int port,const std::wstring& ip,bool add) {
    IUPnPNAT* nat=nullptr;
    if(FAILED(CoCreateInstance(__uuidof(UPnPNAT),nullptr,CLSCTX_INPROC_SERVER,__uuidof(IUPnPNAT),reinterpret_cast<void**>(&nat))) || !nat) return false;
    IStaticPortMappingCollection* maps=nullptr;
    bool ok=false;
    if(SUCCEEDED(nat->get_StaticPortMappingCollection(&maps)) && maps) {
        BSTR tcp=SysAllocString(L"TCP");
        if(add) {
            BSTR client=SysAllocString(ip.c_str()), label=SysAllocString(L"Basic FPS room");
            IStaticPortMapping* mapping=nullptr;
            ok=SUCCEEDED(maps->Add(port,tcp,port,client,VARIANT_TRUE,label,&mapping)) && mapping;
            if(mapping) mapping->Release();
            SysFreeString(client); SysFreeString(label);
        } else ok=SUCCEEDED(maps->Remove(port,tcp));
        SysFreeString(tcp); maps->Release();
    }
    nat->Release();
    return ok;
}
}

bool FetchRooms(std::vector<LobbyRoom>& rooms,std::wstring& error) {
    rooms.clear();
    std::string reply;
    const int status=Request(L"GET",L"/api/rooms",{}, {},reply);
    Json data;
    if(status!=200 || !Parse(reply,data)) { error=L"방 목록을 불러오지 못했어요. 인터넷 연결을 확인해 주세요."; return false; }
    for(const auto& item:data.get("rooms").array) {
        LobbyRoom room;
        room.name=Text(item.get("name")); room.host=Text(item.get("host"));
        room.address=Text(item.get("address")); room.lan=Text(item.get("lan"));
        room.players=static_cast<int>(item.get("players").num());
        room.max=static_cast<int>(item.get("max").num(16));
        room.sameNetwork=item.get("sameNetwork").flag(); room.reachable=item.get("reachable").flag();
        if(!room.address.empty()) rooms.push_back(std::move(room));
    }
    return true;
}

Lobby::~Lobby() { stop(); }
void Lobby::start(const std::wstring& nick,int port,const std::wstring& lanIp,const std::wstring& lan) {
    stop();
    { std::lock_guard lock(mutex_); stopping_=false; state_=State{Phase::Listing}; }
    players_=1;
    worker_=std::thread(&Lobby::run,this,nick,port,lanIp,lan);
}
void Lobby::stop() {
    { std::lock_guard lock(mutex_); stopping_=true; }
    changed_.notify_all();
    if(worker_.joinable()) worker_.join();
    std::lock_guard lock(mutex_); state_=State{};
}
Lobby::State Lobby::state() { std::lock_guard lock(mutex_); return state_; }
void Lobby::run(std::wstring nick,int port,std::wstring lanIp,std::wstring lan) {
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    const bool upnp=!lanIp.empty() && Forward(port,lanIp,true);
    { std::lock_guard lock(mutex_); state_.upnp=upnp; }
    std::string id,token,reply;
    for(;;) {
        bool listed=false, reachable=false;
        std::wstring address;
        if(id.empty()) {
            const std::string body="{\"host\":"+Quote(ToUtf8(nick))+",\"port\":"+std::to_string(port)+",\"lan\":"+Quote(ToUtf8(lan))+
                ",\"players\":"+std::to_string(players_.load())+",\"upnp\":"+(upnp?"true":"false")+"}";
            Json data;
            if(Request(L"POST",L"/api/rooms",body,{},reply)==201 && Parse(reply,data) && data.get("id").kind==Json::String && data.get("token").kind==Json::String) {
                id=data.get("id").text; token=data.get("token").text;
                listed=true; reachable=data.get("reachable").flag(); address=Text(data.get("address"));
            }
        } else {
            Json data;
            const int status=Request(L"POST",L"/api/rooms/"+ToWide(id)+L"/beat","{\"players\":"+std::to_string(players_.load())+"}",token,reply);
            if(status==404) { id.clear(); token.clear(); continue; }
            listed=status==200 && Parse(reply,data);
            reachable=listed && data.get("reachable").flag();
        }
        {
            std::unique_lock lock(mutex_);
            if(listed) {
                state_.phase=Phase::Listed; state_.reachable=reachable;
                if(!address.empty()) state_.address=address;
            } else if(state_.phase!=Phase::Listed) state_.phase=Phase::Failed;
            if(changed_.wait_for(lock,std::chrono::seconds(15),[this]{ return stopping_; })) break;
        }
    }
    if(!id.empty()) Request(L"DELETE",L"/api/rooms/"+ToWide(id),{},token,reply);
    if(upnp) Forward(port,lanIp,false);
    if(SUCCEEDED(com)) CoUninitialize();
}
}
