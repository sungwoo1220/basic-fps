#pragma once
#include <windows.h>
#include <string>

namespace Trial {
// Runs the bundled room (room\node.exe room\server.js) as a child that dies with this app.
class Room {
public:
    Room()=default;
    ~Room();
    Room(const Room&)=delete;
    Room& operator=(const Room&)=delete;
    bool start(std::wstring& error);
    void stop();
    bool running();
    int port() const { return port_; }
    std::wstring local() const { return L"http://127.0.0.1:"+std::to_wstring(port_); }
    std::wstring lan() const;
    std::wstring lanIp() const;
private:
    HANDLE job_=nullptr, process_=nullptr;
    int port_=0;
};
}
