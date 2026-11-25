#pragma once
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Trial {
// One open room as the board at basic-fps.baze.cloud lists it. The host's own PC runs the room.
struct LobbyRoom {
    std::wstring name, host, address, lan;
    int players=0, max=16;
    bool sameNetwork=false, reachable=false;
    std::wstring join() const { return sameNetwork && !lan.empty()?lan:address; }
};
bool FetchRooms(std::vector<LobbyRoom>& rooms,std::wstring& error);

// Puts the hosted room on the board, refreshes it every 15 s and takes it off on stop.
// Also asks the router (UPnP) to forward the room's port to this PC while it is open.
class Lobby {
public:
    enum class Phase { Off, Listing, Listed, Failed };
    struct State { Phase phase=Phase::Off; bool reachable=false, upnp=false; std::wstring address; };
    Lobby()=default;
    ~Lobby();
    Lobby(const Lobby&)=delete;
    Lobby& operator=(const Lobby&)=delete;
    void start(const std::wstring& nick,int port,const std::wstring& lanIp,const std::wstring& lan);
    void stop();
    void players(int count) { players_=count; }
    State state();
private:
    void run(std::wstring nick,int port,std::wstring lanIp,std::wstring lan);
    std::mutex mutex_;
    std::condition_variable changed_;
    bool stopping_=false;
    std::atomic<int> players_{1};
    State state_;
    std::thread worker_;
};
}
