#pragma once
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace Trial {
std::string ToUtf8(const std::wstring& text);
std::wstring ToWide(const std::string& text);

class Network {
public:
    Network()=default;
    ~Network();
    Network(const Network&)=delete;
    Network& operator=(const Network&)=delete;
    bool start(const std::wstring& url,const std::wstring& nick);
    bool send(std::string message);
    void stop();
    bool busy() const { return running_; }
    std::deque<std::string> drain();
    std::wstring status();
private:
    void run(std::wstring url,std::string nick);
    void reader(void* socket);
    void report(std::wstring message);
    std::atomic<bool> running_{false},stopping_{false},open_{false};
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::string> incoming_,outgoing_;
    std::wstring status_;
    std::thread worker_;
};
}
