#include "Protocol.h"
#include <iostream>
#include <stdexcept>

using namespace Trial;
void require(bool valid,const char* name) { if(!valid) throw std::runtime_error(name); }
int main() {
    try {
        require(BasicFPS::RunGoldenCases(),"existing server movement golden cases");
        Json j;
        require(Parse(R"({"name":"\uac00\ud83c\udfaf","n":-1.25e+2,"a":[null,true,false,0]})",j),"Unicode and valid number packet");
        require(j.get("name").text=="\xea\xb0\x80\xf0\x9f\x8e\xaf", "UTF-8 surrogate pair");
        require(j.get("n").number==-125 && j.get("a").array[1].flag() && !j.get("a").array[3].flag(),"number and true types kept separate");
        for(const auto* invalid:{"", "01", "+1", "1.", "1e+", "1e999", "[1,]", "{'a':1}", "{\"a\":1,\"a\":2}", "true false", "\"\\x\""})
            require(!Parse(invalid,j),"reject malformed input");
        require(!Parse(std::string(30,'[')+std::string(30,']'),j),"reject deep packet");
        require(!Parse(std::string(65537,' '),j),"reject oversized packet");
        require(Parse(Join("a\"\\\nb"),j) && j.get("name").text=="a\"\\b","nickname encoding");
        const BasicFPS::Input first{1,8,0,.2,20,1},second{2,8,0,-.2,30,1};
        require(Parse(Inputs({first,second}),j) && j.get("items").array.size()==2 && j.get("items").array[1].get("pitch").num()==-.2,"batched input serialization");
        Game game;
        game.receive(R"({"type":"welcome","id":"1"})",2);
        game.receive(R"({"type":"state","serverTime":100,"players":[{"id":"1","epoch":1,"x":-16,"z":-16,"ammo":12,"hp":100},{"id":"bot-0","epoch":1,"x":0,"z":4}]})",2);
        require(game.own() && game.own()->ammo==12 && game.predicted.x==-16,"first authoritative state");
        game.step(first); game.step(second);
        require(game.pending.size()==2 && game.predicted.x>-16,"predict before ack");
        game.receive(R"({"type":"state","serverTime":150,"players":[{"id":"1","epoch":1,"ack":1,"x":-15.985,"z":-16,"vx":0.9,"ammo":11,"hp":75,"reloadRemaining":2300},{"id":"bot-0","epoch":1,"x":1,"z":4}]})",2.05);
        require(game.pending.size()==1 && game.pending.front().seq==2 && game.predicted.x>-15.985,"replay unacknowledged input");
        require(game.own()->ammo==11 && game.own()->hp==75 && game.own()->reload>4.3,"authoritative HUD");
        require(game.players.at("bot-0").pose(125).x==.5,"interpolate other player");
        game.receive(R"({"type":"state","serverTime":100,"players":[]})",2.06);
        require(game.own()!=nullptr,"ignore stale state");
        game.receive(R"({"type":"state","serverTime":200,"players":[{"id":"1","epoch":2,"x":16,"z":16}]})",2.1);
        require(game.pending.empty() && game.predicted.x==16 && game.epoch==2 && game.players.size()==1,"respawn clears prediction and departed players");
        game.receive(R"({"type":"shot","from":[0,1.6,0],"to":[0,1.5,3],"blood":true,"impact":true})",2.2);
        game.receive(R"({"type":"hit","damage":25})",2.2);
        require(game.traces.size()==1 && game.traces.front().blood && game.hitUntil>2.2,"visual events");
        std::cout<<"PASS: DX11 client protocol and 7,200 shared movement steps\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
