#pragma once
#include "GeneratedRules.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace BasicFPS {
struct State { double x=0,z=0,vx=0,vz=0; };
struct Input { std::int64_t seq=0; int keys=0; double yaw=0,pitch=0,viewTime=0; int epoch=0; };
inline bool Clear(double x,double z) {
    if(std::abs(x)>19.5 || std::abs(z)>19.5) return false;
    for(const auto& box:Boxes) if(std::abs(x-box.x)<box.w/2+Radius && std::abs(z-box.z)<box.d/2+Radius) return false;
    return true;
}
inline void Move(State& p,const Input& input) {
    const double forward=((input.keys&1)?1:0)-((input.keys&2)?1:0);
    const double right=((input.keys&8)?1:0)-((input.keys&4)?1:0);
    const double length=std::hypot(forward,right), speed=(input.keys&32)?WalkSpeed:Speed;
    const double sine=std::sin(input.yaw), cosine=std::cos(input.yaw);
    const double targetX=length?(-sine*forward+cosine*right)*speed/length:0;
    const double targetZ=length?(-cosine*forward-sine*right)*speed/length:0;
    const double dx=targetX-p.vx,dz=targetZ-p.vz,difference=std::hypot(dx,dz);
    const double change=(length?Acceleration:Deceleration)*Step;
    const double ratio=difference?std::min(1.0,change/difference):1;
    p.vx+=dx*ratio;p.vz+=dz*ratio;
    const double x=p.x+p.vx*Step;
    if(Clear(x,p.z))p.x=x;else p.vx=0;
    const double z=p.z+p.vz*Step;
    if(Clear(p.x,z))p.z=z;else p.vz=0;
}
inline bool RunGoldenCases() {
    constexpr double Pi=3.14159265358979323846;
    for(const auto& expected:GoldenCases) {
        std::uint32_t random=expected.seed;
        State state{-17,-17,0,0};
        for(int step=0;step<1200;step++) {
            random=random*1664525u+1013904223u;
            Input input;input.keys=(random>>24)&47;
            input.yaw=(static_cast<double>(random)/4294967296.0)*Pi*2-Pi;
            Move(state,input);
        }
        if(std::abs(state.x-expected.x)>1e-8 || std::abs(state.z-expected.z)>1e-8 ||
           std::abs(state.vx-expected.vx)>1e-8 || std::abs(state.vz-expected.vz)>1e-8) return false;
    }
    return true;
}
}
