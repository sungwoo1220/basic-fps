#include <winsock2.h>
#include <ws2tcpip.h>
#include "Room.h"
#include <cwctype>
#include <filesystem>
#include <vector>

namespace Trial {
namespace {
std::filesystem::path Folder() {
    std::wstring path(MAX_PATH,L'\0');
    for(;;) {
        const DWORD n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if(n==0) return {};
        if(n<path.size()) { path.resize(n); break; }
        path.resize(path.size()*2);
    }
    return std::filesystem::path(path).parent_path();
}
struct Winsock {
    bool ok;
    Winsock() { WSADATA data{}; ok=WSAStartup(MAKEWORD(2,2),&data)==0; }
    ~Winsock() { if(ok) WSACleanup(); }
};
// Prefer 3000 (the usual room address); fall back to any free port if another room already uses it.
int FreePort() {
    const Winsock ws;
    if(!ws.ok) return 0;
    for(const u_short wanted:{u_short(3000),u_short(0)}) {
        const SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(s==INVALID_SOCKET) continue;
        BOOL exclusive=TRUE;
        setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof exclusive);
        sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(wanted); a.sin_addr.s_addr=htonl(INADDR_ANY);
        int port=0;
        if(bind(s,reinterpret_cast<sockaddr*>(&a),sizeof a)==0) {
            int size=sizeof a;
            if(getsockname(s,reinterpret_cast<sockaddr*>(&a),&size)==0) port=ntohs(a.sin_port);
        }
        closesocket(s);
        if(port) return port;
    }
    return 0;
}
bool Skip(const std::wstring& entry) {
    std::wstring name=entry.substr(0,entry.find(L'='));
    for(auto& c:name) c=static_cast<wchar_t>(std::towupper(c));
    return name==L"PORT" || name==L"NODE_PATH" || name==L"NODE_OPTIONS";
}
std::vector<wchar_t> Environment(int port) {
    std::vector<wchar_t> block;
    if(auto* env=GetEnvironmentStringsW()) {
        for(const wchar_t* p=env;*p;p+=std::wcslen(p)+1) {
            const std::wstring entry=p;
            if(entry.empty() || (entry[0]!=L'=' && Skip(entry))) continue;
            block.insert(block.end(),entry.begin(),entry.end()); block.push_back(L'\0');
        }
        FreeEnvironmentStringsW(env);
    }
    const std::wstring own=L"PORT="+std::to_wstring(port);
    block.insert(block.end(),own.begin(),own.end());
    block.push_back(L'\0'); block.push_back(L'\0');
    return block;
}
}

Room::~Room() { stop(); }
bool Room::running() {
    if(!process_) return false;
    if(WaitForSingleObject(process_,0)==WAIT_TIMEOUT) return true;
    stop();
    return false;
}
void Room::stop() {
    if(job_) { TerminateJobObject(job_,0); CloseHandle(job_); job_=nullptr; }
    if(process_) { WaitForSingleObject(process_,2000); CloseHandle(process_); process_=nullptr; }
}
bool Room::start(std::wstring& error) {
    if(running()) return true;
    const auto room=Folder()/L"room";
    const auto node=room/L"node.exe", script=room/L"server.js";
    std::error_code ec;
    if(!std::filesystem::exists(node,ec) || !std::filesystem::exists(script,ec) || !std::filesystem::exists(room/L"node_modules"/L"ws",ec)) {
        error=L"방 파일을 찾지 못했어요. ZIP 전체를 한 폴더에 풀어 주세요."; return false;
    }
    port_=FreePort();
    if(!port_) { error=L"방에 쓸 포트를 찾지 못했어요."; return false; }
    job_=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job_ || !SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limit,sizeof limit)) {
        stop(); error=L"방 실행을 준비하지 못했어요."; return false;
    }
    std::wstring log(MAX_PATH+1,L'\0');
    log.resize(GetTempPathW(static_cast<DWORD>(log.size()),log.data()));
    log+=L"BasicFPS-DX11-room.log";
    SECURITY_ATTRIBUTES inherit{sizeof inherit,nullptr,TRUE};
    const HANDLE out=CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&inherit,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    STARTUPINFOW info{}; info.cb=sizeof info;
    if(out!=INVALID_HANDLE_VALUE) {
        info.dwFlags=STARTF_USESTDHANDLES; info.hStdOutput=info.hStdError=out; info.hStdInput=nullptr;
    }
    std::wstring command=L"\""+node.wstring()+L"\" \""+script.wstring()+L"\"";
    auto env=Environment(port_);
    PROCESS_INFORMATION child{};
    const BOOL made=CreateProcessW(node.c_str(),command.data(),nullptr,nullptr,out!=INVALID_HANDLE_VALUE,
        CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,env.data(),room.c_str(),&info,&child);
    if(out!=INVALID_HANDLE_VALUE) CloseHandle(out);
    if(!made) { stop(); error=L"방을 실행하지 못했어요."; return false; }
    if(!AssignProcessToJobObject(job_,child.hProcess)) {
        TerminateProcess(child.hProcess,1); CloseHandle(child.hThread); CloseHandle(child.hProcess);
        stop(); error=L"방 실행을 준비하지 못했어요."; return false;
    }
    ResumeThread(child.hThread); CloseHandle(child.hThread);
    process_=child.hProcess;
    return true;
}
std::wstring Room::lan() const {
    const Winsock ws;
    char name[256]{};
    if(!ws.ok || gethostname(name,sizeof name)!=0) return {};
    addrinfo hints{}, *found=nullptr;
    hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(name,nullptr,&hints,&found)!=0) return {};
    std::wstring result;
    for(auto* a=found;a && result.empty();a=a->ai_next) {
        const auto* v4=reinterpret_cast<const sockaddr_in*>(a->ai_addr);
        const auto first=v4->sin_addr.S_un.S_un_b.s_b1, second=v4->sin_addr.S_un.S_un_b.s_b2;
        if(first==127 || (first==169 && second==254)) continue;
        wchar_t text[INET_ADDRSTRLEN]{};
        if(InetNtopW(AF_INET,&v4->sin_addr,text,INET_ADDRSTRLEN)) result=L"http://"+std::wstring(text)+L":"+std::to_wstring(port_);
    }
    freeaddrinfo(found);
    return result;
}
}
