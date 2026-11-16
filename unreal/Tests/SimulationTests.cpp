#include "../Source/BasicFPS/Public/Simulation.h"
#include <iostream>
int main() {
    if(!BasicFPS::RunGoldenCases()) { std::cerr<<"FAIL: native prediction differs from authoritative JS rules\n";return 1; }
    BasicFPS::State p{-17,-17,0,0}; BasicFPS::Input i;i.keys=1;
    for(int n=0;n<600;n++)BasicFPS::Move(p,i);
    if(p.z < -19.5 || !BasicFPS::Clear(p.x,p.z))return 2;
    std::cout<<"PASS: native movement matches 7,200 JavaScript prediction steps and wall limits.\n";
}
