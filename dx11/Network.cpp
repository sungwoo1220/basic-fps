#include "Network.h"
#include "Protocol.h"
#include <winhttp.h>
#include <array>
#include <cwctype>

namespace Trial {
namespace {
struct Internet {
    HINTERNET h;
    explicit Internet(HINTERNET value):h(value) {}
    ~Internet() { reset(); }
    void reset(HINTERNET value=nullptr) { if(h) WinHttpCloseHandle(h); h=value; }
    Internet(const Internet&)=delete;
    Internet& operator=(const Internet&)=delete;
};
struct Endpoint { std::wstring host,path; INTERNET_PORT port=0; bool secure=false; };
bool ParseUrl(const std::wstring& url,Endpoint& result) {
    if(url.empty() || url.size()>2048 || url.find_first_of(L"@\\#")!=std::wstring::npos ||
        std::any_of(url.begin(),url.end(),[](wchar_t c){ return c<0x20 || std::iswspace(c); })) return false;
    URL_COMPONENTS parts{};
    parts.dwStructSize=sizeof parts;
    parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts) || !parts.dwHostNameLength ||
        (parts.nScheme!=INTERNET_SCHEME_HTTP && parts.nScheme!=INTERNET_SCHEME_HTTPS)) return false;
    result.host.assign(parts.lpszHostName,parts.dwHostNameLength);
    result.path=parts.dwUrlPathLength?std::wstring(parts.lpszUrlPath,parts.dwUrlPathLength):L"/";
    if(parts.dwExtraInfoLength) result.path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    result.port=parts.nPort;
    result.secure=parts.nScheme==INTERNET_SCHEME_HTTPS;
    return result.port!=0;
}
}

std::string ToUtf8(const std::wstring& text) {
    const auto size=static_cast<int>(text.size());
    const int length=WideCharToMultiByte(CP_UTF8,0,text.data(),size,nullptr,0,nullptr,nullptr);
    std::string result(static_cast<size_t>(length),'\0');
    if(length) WideCharToMultiByte(CP_UTF8,0,text.data(),size,result.data(),length,nullptr,nullptr);
    return result;
}
std::wstring ToWide(const std::string& text) {
    const auto size=static_cast<int>(text.size());
    const int length=MultiByteToWideChar(CP_UTF8,0,text.data(),size,nullptr,0);
    std::wstring result(static_cast<size_t>(length),L'\0');
    if(length) MultiByteToWideChar(CP_UTF8,0,text.data(),size,result.data(),length);
    return result;
}

Network::~Network() { stop(); if(worker_.joinable()) worker_.join(); }
bool Network::start(const std::wstring& url,const std::wstring& nick,unsigned waitMs) {
    if(running_) return false;
    wait_=waitMs;
    Endpoint endpoint;
    if(!ParseUrl(url,endpoint)) { report(L"http:// 또는 https://로 시작하는 방 주소를 입력해 주세요."); return false; }
    if(worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        incoming_.clear(); outgoing_.clear(); status_=L"방에 연결하고 있어요…";
    }
    stopping_=false; running_=true;
    try {
        worker_=std::thread([this,url,nick] {
            try { run(url,ToUtf8(nick)); }
            catch(...) { report(L"연결을 시작하거나 유지하지 못했어요."); }
            open_=false; running_=false;
        });
    } catch(...) {
        running_=false; report(L"연결을 시작하지 못했어요."); return false;
    }
    return true;
}
void Network::stop() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_=true; open_=false; }
    changed_.notify_all();
}
bool Network::send(std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!open_ || outgoing_.size()>=24) return false;
    outgoing_.push_back(std::move(message)); changed_.notify_one(); return true;
}
std::deque<std::string> Network::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::deque<std::string> packets; packets.swap(incoming_); return packets;
}
std::wstring Network::status() { std::lock_guard<std::mutex> lock(mutex_); return status_; }
void Network::report(std::wstring message) { std::lock_guard<std::mutex> lock(mutex_); status_=std::move(message); }

void Network::run(std::wstring url,std::string nick) {
    Endpoint endpoint;
    if(!ParseUrl(url,endpoint)) return;
    const Internet session(WinHttpOpen(L"Basic FPS DX11 trial/1",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,0));
    if(!session.h || !WinHttpSetTimeouts(session.h,3000,3000,3000,5000)) { report(L"Windows 연결 도구를 준비하지 못했어요."); return; }
    // A room started by this app needs a moment to listen, so retry until the caller's wait expires.
    const auto deadline=GetTickCount64()+wait_;
    Internet connection(nullptr),socket(nullptr);
    for(;;) {
        std::wstring failure=L"방에 연결하지 못했어요. 방이 열려 있는지와 주소를 확인해 주세요.";
        socket.reset(); connection.reset(WinHttpConnect(session.h,endpoint.host.c_str(),endpoint.port,0));
        if(connection.h) {
            // Keep the request alive until upgrade; never follow redirects or weaken TLS validation.
            const Internet request(WinHttpOpenRequest(connection.h,L"GET",endpoint.path.c_str(),nullptr,nullptr,
                WINHTTP_DEFAULT_ACCEPT_TYPES,endpoint.secure?WINHTTP_FLAG_SECURE:0));
            DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER,status=0,size=sizeof status;
            if(request.h && WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof redirects) &&
                WinHttpSetOption(request.h,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,nullptr,0) &&
                WinHttpSendRequest(request.h,nullptr,0,nullptr,0,0,0) && WinHttpReceiveResponse(request.h,nullptr)) {
                if(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX) && status==101) {
                    socket.reset(WinHttpWebSocketCompleteUpgrade(request.h,0));
                    failure=L"게임 방 통신을 시작하지 못했어요.";
                } else failure=L"이 주소에서 게임 방 응답을 받지 못했어요.";
            }
        } else failure=L"방 주소에 연결하지 못했어요.";
        if(stopping_) return;
        if(socket.h) break;
        if(GetTickCount64()>=deadline) { report(failure); return; }
        Sleep(250);
    }
    DWORD closeTimeout=750;
    if(!WinHttpSetOption(socket.h,WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT,&closeTimeout,sizeof closeTimeout)) {
        report(L"게임 방 연결을 초기화하지 못했어요."); return;
    }
    auto intro=Join(nick);
    if(WinHttpWebSocketSend(socket.h,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,intro.data(),static_cast<DWORD>(intro.size()))!=NO_ERROR) {
        report(L"방에 닉네임을 보낼 수 없어요."); return;
    }
    std::thread receive([this,&socket] {
        try { reader(socket.h); } catch(...) { stop(); }
    });
    { std::lock_guard<std::mutex> lock(mutex_); if(!stopping_) open_=true; }
    try {
        while(!stopping_) {
            std::string message;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock,[&]{ return stopping_ || !outgoing_.empty(); });
                if(stopping_) break;
                message=std::move(outgoing_.front()); outgoing_.pop_front();
            }
            if(WinHttpWebSocketSend(socket.h,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,message.data(),static_cast<DWORD>(message.size()))!=NO_ERROR) break;
        }
    } catch(...) { stop(); }
    stop();
    WinHttpWebSocketClose(socket.h,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,nullptr,0);
    receive.join();
    report(L"방 연결이 종료됐어요. 다시 연결할 수 있어요.");
}
void Network::reader(void* socket) {
    std::array<char,8192> buffer{};
    std::string message;
    while(!stopping_) {
        DWORD count=0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
        if(WinHttpWebSocketReceive(socket,buffer.data(),static_cast<DWORD>(buffer.size()),&count,&type)!=NO_ERROR ||
            (type!=WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE && type!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)) break;
        if(message.size()+count>65536) break;
        message.append(buffer.data(),count);
        if(type==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
            std::lock_guard<std::mutex> lock(mutex_);
            if(incoming_.size()>=96) break;
            incoming_.push_back(std::move(message)); message.clear();
        }
    }
    stop();
}
}
