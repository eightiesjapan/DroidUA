#ifndef DROIDUA_GAMEPAD_CONFIG_H
#define DROIDUA_GAMEPAD_CONFIG_H
#include <array>
#include <cstdint>
#include <istream>
#include <string>
namespace AndroidControls {
enum Action { None,Pause,Brake,Leader,Lead,Click,Mode,Fire,Alt,ZoomIn,ZoomOut,CycleUnit,CycleLeader,Aggro1,Aggro5,OpenMap,OpenMenu,ActionCount };
constexpr int ButtonCount=10;
struct Config { std::array<int,ButtonCount> actions={{Pause,Brake,Leader,Lead,Click,Fire,Mode,Alt,None,None}}; };
inline uint32_t Bit(int action) { return action>None && action<ActionCount ? uint32_t(1)<<action:0; }
inline int Hotkey(int action) {
    const int ids[ActionCount]={-1,32,-1,23,45,-1,-1,-1,-1,16,17,22,20,38,42,8,24};
    return action>=None && action<ActionCount?ids[action]:-1;
}
inline bool ReadConfig(std::istream& in,Config& out) {
    std::string magic;int version=0;Config parsed;
    if(!(in>>magic>>version) || magic!="droidua-gamepad" || version!=1) return false;
    for(int i=0;i<ButtonCount;++i) {int id=-1;if(!(in>>id>>parsed.actions[i]) || id!=i || parsed.actions[i]<None || parsed.actions[i]>=ActionCount) return false;}
    if(in>>magic) return false;out=parsed;return true;
}
inline uint32_t Actions(const Config& config,uint32_t physical) {
    // SDL A/B/X/Y, L1, digital/analog L2, R1, R2, L3, R3. Start/Select are deliberately absent.
    const int buttons[ButtonCount]={0,1,2,3,9,24,10,25,7,8};uint32_t actions=0;
    for(int i=0;i<ButtonCount;++i) if(physical&(uint32_t(1)<<buttons[i])) actions|=Bit(config.actions[i]);
    return actions;
}
}
#endif
