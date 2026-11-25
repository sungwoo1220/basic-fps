#pragma once
#include "Simulation.h"
#include <charconv>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Trial {
struct Json {
    enum Kind { Null, Boolean, Number, String, Array, Object } kind=Null;
    double number=0;
    std::string text;
    std::vector<Json> array;
    std::map<std::string,Json> object;
    const Json& get(const char* key) const {
        static const Json missing;
        const auto it=object.find(key);
        return it==object.end()?missing:it->second;
    }
    double num(double fallback=0) const { return kind==Number?number:fallback; }
    bool flag() const { return kind==Boolean && number!=0; }
};

inline void Utf8(std::string& s,unsigned code) {
    if(code<=0x7f) s+=static_cast<char>(code);
    else if(code<=0x7ff) { s+=static_cast<char>(0xc0|(code>>6)); s+=static_cast<char>(0x80|(code&63)); }
    else if(code<=0xffff) { s+=static_cast<char>(0xe0|(code>>12)); s+=static_cast<char>(0x80|((code>>6)&63)); s+=static_cast<char>(0x80|(code&63)); }
    else { s+=static_cast<char>(0xf0|(code>>18)); s+=static_cast<char>(0x80|((code>>12)&63)); s+=static_cast<char>(0x80|((code>>6)&63)); s+=static_cast<char>(0x80|(code&63)); }
}

struct Reader {
    std::string_view s;
    size_t p=0;
    unsigned nodes=0;
    char peek() const { return p<s.size()?s[p]:'\0'; }
    bool take(char c) { if(peek()!=c || p>=s.size()) return false; ++p; return true; }
    void space() { while(peek()==' ' || peek()=='\r' || peek()=='\n' || peek()=='\t') ++p; }
    bool hex(unsigned& code) {
        code=0;
        for(int i=0;i<4;++i) {
            const char c=peek();
            if(c>='0' && c<='9') code=code*16+static_cast<unsigned>(c-'0');
            else if(c>='a' && c<='f') code=code*16+static_cast<unsigned>(c-'a'+10);
            else if(c>='A' && c<='F') code=code*16+static_cast<unsigned>(c-'A'+10);
            else return false;
            ++p;
        }
        return true;
    }
    bool str(std::string& out) {
        if(!take('"')) return false;
        while(!take('"')) {
            const char c=peek();
            if(static_cast<unsigned char>(c)<32) return false;
            ++p;
            if(c!='\\') { out+=c; continue; }
            switch(peek()) {
            case '"': case '\\': case '/': out+=s[p++]; break;
            case 'b': ++p; out+='\b'; break;
            case 'f': ++p; out+='\f'; break;
            case 'n': ++p; out+='\n'; break;
            case 'r': ++p; out+='\r'; break;
            case 't': ++p; out+='\t'; break;
            case 'u': {
                ++p;
                unsigned code=0;
                if(!hex(code)) return false;
                if(code>=0xd800 && code<=0xdbff) {
                    const size_t next=p;
                    unsigned low=0;
                    if(take('\\') && take('u') && hex(low) && low>=0xdc00 && low<=0xdfff) code=0x10000+((code-0xd800)<<10)+(low-0xdc00);
                    else { p=next; code=0xfffd; }
                } else if(code>=0xdc00 && code<=0xdfff) code=0xfffd;
                Utf8(out,code);
                break;
            }
            default: return false;
            }
        }
        return true;
    }
    static bool digit(char c) { return c>='0' && c<='9'; }
    bool value(Json& out,unsigned depth=0) {
        if(depth>16 || ++nodes>4096) return false;
        space();
        if(peek()=='"') { out.kind=Json::String; return str(out.text); }
        if(take('{') || take('[')) {
            const bool obj=s[p-1]=='{';
            const char end=obj?'}':']';
            out.kind=obj?Json::Object:Json::Array;
            space();
            if(take(end)) return true;
            do {
                space();
                std::string key;
                if(obj) { if(!str(key)) return false; space(); if(!take(':')) return false; }
                Json child;
                if(!value(child,depth+1)) return false;
                if(obj) { if(!out.object.emplace(std::move(key),std::move(child)).second) return false; }
                else out.array.push_back(std::move(child));
                space();
                if(take(end)) return true;
            } while(take(','));
            return false;
        }
        for(const std::string_view token:{"true","false","null"}) {
            if(s.substr(p,token.size())==token) {
                p+=token.size(); out.kind=token=="null"?Json::Null:Json::Boolean;
                out.number=token=="true"?1:0; return true;
            }
        }
        const size_t start=p;
        take('-');
        if(!take('0')) { if(!digit(peek())) return false; while(digit(peek())) ++p; }
        if(take('.')) { if(!digit(peek())) return false; while(digit(peek())) ++p; }
        if(take('e') || take('E')) { if(!take('+')) take('-'); if(!digit(peek())) return false; while(digit(peek())) ++p; }
        const auto r=std::from_chars(s.data()+start,s.data()+p,out.number);
        out.kind=Json::Number;
        return r.ec==std::errc{} && r.ptr==s.data()+p && std::isfinite(out.number);
    }
};

inline bool Parse(std::string_view s,Json& out) {
    if(s.empty() || s.size()>65536) return false;
    Reader reader{s}; Json value;
    if(!reader.value(value)) return false;
    reader.space();
    if(reader.p!=s.size()) return false;
    out=std::move(value); return true;
}
inline std::string Join(std::string_view nick) {
    std::string result="{\"type\":\"join\",\"name\":\"";
    for(char c:nick) {
        if(static_cast<unsigned char>(c)<32) continue;
        if(c=='"' || c=='\\') result+='\\';
        result+=c;
    }
    return result+"\"}";
}
inline void WriteNumber(std::string& out,double value) {
    char buffer[64];
    const auto r=std::to_chars(buffer,buffer+sizeof buffer,value);
    if(r.ec==std::errc{}) out.append(buffer,r.ptr);
    else out+='0';
}
inline std::string Inputs(const std::vector<BasicFPS::Input>& items) {
    std::string s="{\"type\":\"inputs\",\"items\":[";
    for(const auto& i:items) {
        if(s.back()!='[') s+=',';
        s+="{\"seq\":"+std::to_string(i.seq)+",\"keys\":"+std::to_string(i.keys)+",\"epoch\":"+std::to_string(i.epoch)+",\"yaw\":";
        WriteNumber(s,i.yaw); s+=",\"pitch\":"; WriteNumber(s,i.pitch);
        s+=",\"viewTime\":"; WriteNumber(s,i.viewTime); s+='}';
    }
    return s+"]}";
}
inline int Int(const Json& j,int minimum,int maximum,int fallback=0) {
    return static_cast<int>(std::clamp(j.num(fallback),static_cast<double>(minimum),static_cast<double>(maximum)));
}

struct Point { float x=0,y=0,z=0; };
struct Sample { double t=0,x=0,z=0; };
struct Player {
    std::string id,name;
    BasicFPS::State state;
    int epoch=-1,hp=100,ammo=12,kills=0,deaths=0;
    std::int64_t ack=0;
    double reload=0;
    bool shield=false;
    std::deque<Sample> samples;
    Sample pose(double time) const {
        if(samples.empty()) return {time,state.x,state.z};
        const auto it=std::lower_bound(samples.begin(),samples.end(),time,[](const auto& a,double t){return a.t<t;});
        if(it==samples.begin()) return *it;
        if(it==samples.end()) return samples.back();
        const auto& a=*(it-1); const auto& b=*it;
        const auto f=(time-a.t)/(b.t-a.t);
        return {time,a.x+f*(b.x-a.x),a.z+f*(b.z-a.z)};
    }
};
struct Trace { Point from,to; double until; bool impact,blood; };
struct Game {
    std::string id,error;
    std::map<std::string,Player> players;
    BasicFPS::State predicted;
    std::deque<BasicFPS::Input> pending;
    std::deque<Trace> traces;
    std::deque<std::pair<std::string,double>> feed;
    std::int64_t seq=0;
    int epoch=-1;
    double offset=0,stamp=-1,hitUntil=0,xOffset=0,zOffset=0;
    const Player* own() const {
        const auto p=players.find(id);
        return p!=players.end()?&p->second:nullptr;
    }
    double viewTime(double now) const { return std::min(stamp,now*1000+offset-100); }
    void step(const BasicFPS::Input& input) {
        seq=input.seq; pending.push_back(input); BasicFPS::Move(predicted,input);
    }
    void receive(std::string_view packet,double now) {
        Json msg;
        if(!Parse(packet,msg) || msg.kind!=Json::Object) return;
        const auto& type=msg.get("type").text;
        if(type=="welcome") { id=msg.get("id").text; return; }
        if(type=="error") { error=msg.get("message").text.substr(0,300); return; }
        if(type=="hit") { hitUntil=now+.16; return; }
        if(type=="kill") {
            feed.emplace_front(msg.get("killer").text.substr(0,80)+"  /  "+msg.get("victim").text.substr(0,80),now+4);
            if(feed.size()>5) feed.pop_back();
            return;
        }
        if(type=="shot") {
            const auto point=[](const Json& j,Point& p) {
                if(j.array.size()!=3) return false;
                for(const auto& v:j.array) if(v.kind!=Json::Number || std::abs(v.number)>200) return false;
                p={static_cast<float>(j.array[0].number),static_cast<float>(j.array[1].number),static_cast<float>(j.array[2].number)}; return true;
            };
            Trace trace{{},{},now+.085,msg.get("impact").flag(),msg.get("blood").flag()};
            if(point(msg.get("from"),trace.from) && point(msg.get("to"),trace.to)) traces.push_back(trace);
            if(traces.size()>32) traces.pop_front();
            return;
        }
        if(type!="state") return;
        const auto& members=msg.get("players");
        const auto& time=msg.get("serverTime");
        if(members.kind!=Json::Array || members.array.size()>16 || time.kind!=Json::Number || time.number<=stamp) return;
        stamp=time.number; offset=stamp-now*1000;
        std::map<std::string,Player> updated;
        for(const auto& item:members.array) {
            const auto& key=item.get("id").text;
            const auto& x=item.get("x"); const auto& z=item.get("z");
            if(key.empty() || key.size()>32 || x.kind!=Json::Number || z.kind!=Json::Number || std::abs(x.number)>30 || std::abs(z.number)>30 || updated.count(key)) continue;
            Player p;
            if(const auto old=players.find(key);old!=players.end()) p=std::move(old->second);
            const auto next=Int(item.get("epoch"),0,2147483647);
            if(p.epoch!=next) p.samples.clear();
            p.id=key; p.name=item.get("name").text.substr(0,80); p.epoch=next;
            p.state={x.number,z.number,std::clamp(item.get("vx").num(),-12.,12.),std::clamp(item.get("vz").num(),-12.,12.)};
            p.ack=static_cast<std::int64_t>(std::clamp(item.get("ack").num(),0.,9007199254740991.));
            p.hp=Int(item.get("hp"),0,100,100); p.ammo=Int(item.get("ammo"),0,12,12);
            p.kills=Int(item.get("kills"),0,999999); p.deaths=Int(item.get("deaths"),0,999999);
            p.reload=now+std::clamp(item.get("reloadRemaining").num(),0.,2300.)/1000.; p.shield=item.get("shield").flag();
            p.samples.push_back({stamp,x.number,z.number}); if(p.samples.size()>10) p.samples.pop_front();
            if(key==id) {
                const auto previous=predicted;
                predicted=p.state;
                if(epoch!=p.epoch) { epoch=p.epoch; pending.clear(); xOffset=zOffset=0; }
                else {
                    while(!pending.empty() && pending.front().seq<=p.ack) pending.pop_front();
                    for(const auto& input:pending) BasicFPS::Move(predicted,input);
                    const double dx=previous.x-predicted.x,dz=previous.z-predicted.z;
                    if(dx*dx+dz*dz<1) { xOffset=std::clamp(xOffset+dx,-1.,1.); zOffset=std::clamp(zOffset+dz,-1.,1.); }
                    else xOffset=zOffset=0;
                }
            }
            updated.emplace(key,std::move(p));
        }
        players=std::move(updated);
    }
};
}
