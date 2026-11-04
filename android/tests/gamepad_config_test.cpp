#include "../app/jni/android_gamepad_config.h"
#include <cassert>
#include <sstream>
#include <iostream>
using namespace AndroidControls;
int main() {
    Config defaults;
    assert(Actions(defaults,1u<<0)==Bit(Pause));assert(Actions(defaults,1u<<1)==Bit(Brake));
    assert(Actions(defaults,1u<<24)==Bit(Fire));assert(Actions(defaults,1u<<25)==Bit(Alt));
    assert(Actions(defaults,(1u<<4)|(1u<<6))==0); // Select/Start excluded.
    Config custom=defaults;custom.actions[0]=Aggro5;custom.actions[8]=ZoomIn;custom.actions[9]=CycleUnit;
    assert(Actions(custom,(1u<<0)|(1u<<7)|(1u<<8))==(Bit(Aggro5)|Bit(ZoomIn)|Bit(CycleUnit)));
    assert(Actions(custom,(1u<<24)|(1u<<25))==(Bit(Fire)|Bit(Alt))); // Face remapping cannot steal triggers.
    assert(Hotkey(ZoomIn)==16 && Hotkey(ZoomOut)==17 && Hotkey(CycleUnit)==22 && Hotkey(CycleLeader)==20);
    assert(Hotkey(Aggro1)==38 && Hotkey(Aggro5)==42 && Hotkey(Leader)==23 && Hotkey(Lead)==45);
    std::ostringstream saved;saved<<"droidua-gamepad 1\n";for(int i=0;i<ButtonCount;++i) saved<<i<<" "<<custom.actions[i]<<"\n";
    Config restored;std::istringstream good(saved.str());assert(ReadConfig(good,restored));assert(restored.actions==custom.actions);
    for(const char* bad:{"","droidua-gamepad 2","droidua-gamepad 1 10 1","droidua-gamepad 1 0 100"}) {
        std::istringstream in(bad);assert(!ReadConfig(in,restored));
    }
    std::cout<<"PASS: independent triggers, remapping/L3/R3, fixed Select/Start, semantic hotkeys, strict config parsing\n";
}
