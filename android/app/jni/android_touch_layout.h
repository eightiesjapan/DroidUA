#ifndef OPENUA_ANDROID_TOUCH_LAYOUT_H
#define OPENUA_ANDROID_TOUCH_LAYOUT_H
#include <array>
#include <algorithm>
#include <cmath>
#include <istream>
#include <string>
#include "android_gamepad_config.h"
namespace AndroidTouch {
constexpr size_t Count=14;
// r: radius (circles) or half-width (capsule); hh>r makes a vertical capsule
// (the MOVE slider) whose half-height is hh.
struct Control { float x,y,r; const char *label; bool enabled=true; float hh=0; };
inline float Extent(const Control &c) { return std::max(c.r,c.hh); }
inline float Straight(const Control &c) { return std::max(0.0f,c.hh-c.r); }
// Distance between the centre lines of two (capsule or circle) controls.
inline float Distance(const Control &a,float ax,float ay,const Control &b) {
    float dx=ax-b.x, dy=std::max(0.0f,std::fabs(ay-b.y)-Straight(a)-Straight(b));
    return std::sqrt(dx*dx+dy*dy);
}
struct Placement { int right=0; float edge=0,bottom=0; };
struct Config {
    bool custom=false; std::array<Placement,Count> positions;std::array<bool,Count> enabled;
    Config() {for(size_t i=0;i<Count;++i) enabled[i]=i<8;}
};
inline int Action(size_t control) {
    using namespace AndroidControls;
    const int actions[Count]={None,None,Fire,Alt,Mode,Brake,Leader,Lead,ZoomIn,ZoomOut,CycleUnit,CycleLeader,Aggro1,Aggro5};
    return control<Count?actions[control]:None;
}
inline bool ReadConfig(std::istream &in,Config &out) {
    Config parsed; std::string magic,key; int version=0,custom=0;
    if(!(in>>magic>>version>>key>>custom) || magic!="droidua-touch-layout" || (version!=1 && version!=2) || key!="custom" || (custom!=0 && custom!=1)) return false;
    parsed.custom=custom;
    const size_t rows=version==2?Count:(custom?8:0);
    for(size_t i=0;i<rows;++i) {
        int id=-1,enabled=1; auto &p=parsed.positions[i];
        if(!(in>>id) || (version==2 && !(in>>enabled)) || !(in>>p.right>>p.edge>>p.bottom) || id!=int(i) || (enabled!=0 && enabled!=1) ||
           (p.right!=0 && p.right!=1) || !std::isfinite(p.edge) || !std::isfinite(p.bottom) || p.edge<0 || p.edge>4 || p.bottom<0 || p.bottom>1) return false;
        parsed.enabled[i]=enabled;
    }
    if(in>>key) return false;
    out=parsed; return true;
}
inline std::array<Control,Count> Layout(float w,float h,bool pointer,bool mapOpen,const Config *config=nullptr) {
    // Left side: a slim vertical MOVE slider with Fire / Alt / Brake stacked
    // beside it; right side: Look pad with its mirrored action buttons.
    // MOVE and Look/Pan keep the same 17 px gap to their screen edges.
    std::array<Control,Count> controls={{{41,h-122,24,"Move"},{w-65,h-90,48,pointer&&mapOpen?"Pan":"Look"},
        {103,h-178,25,"Fire"},{103,h-122,25,"Alt"},{w-150,h-65,25,pointer?"VIEW":"SELECT"},
        {103,h-66,25,"Brake"},{w-130,h-150,25,"Leader"},{w-60,h-175,25,"Lead"},
        {40,85,25,"+"},{100,85,25,"-"},{160,85,25,"Unit"},{w-160,85,25,"Squad"},{w-100,85,25,"A1"},{w-40,85,25,"A5"}}};
    controls[0].hh=72;
    for(size_t i=0;i<Count;++i) {
        auto &c=controls[i];c.enabled=config?config->enabled[i]:i<8;
        if(config && config->custom && (i<8 || config->positions[i].edge || config->positions[i].bottom)) {
            const auto &p=config->positions[i];c.x=p.right?w-p.edge*h:p.edge*h;c.y=h-p.bottom*h;
        }
        const float e=Extent(c);
        c.x=std::max(c.r+8,std::min(w-c.r-8,c.x));c.y=std::max(e+44,std::min(h-e-40,c.y));
    }
    // Hidden optional controls never displace active ones.
    std::array<bool,Count> placed={};
    for(int pass=0;pass<2;++pass) for(size_t i=0;i<Count;++i) {
        auto &c=controls[i];if(c.enabled!=(pass==0)) continue;
        auto free=[&](float x,float y) {for(size_t j=0;j<Count;++j) if(placed[j]) {
            if(Distance(c,x,y,controls[j])<c.r+controls[j].r+6) return false;
        } return true;};
        if(!free(c.x,c.y)) {
            const float e=Extent(c);
            float best=1e30f,bx=c.x,by=c.y;
            for(float y=e+44;y<=h-e-40;y+=8) for(float x=c.r+8;x<=w-c.r-8;x+=8) {
                float dx=x-c.x,dy=y-c.y,cost=dx*dx+dy*dy;if(cost<best && free(x,y)) {best=cost;bx=x;by=y;}
            }
            c.x=bx;c.y=by;
        }
        placed[i]=true;
    }
    return controls;
}
inline int HitTest(const std::array<Control,Count> &controls,float x,float y,bool visible) {
    if(!visible) return -1;
    for(size_t i=0;i<Count;++i) {
        const auto &c=controls[i];
        float dx=x-c.x, dy=std::max(0.0f,std::fabs(y-c.y)-Straight(c));
        if(c.enabled && dx*dx+dy*dy<=c.r*c.r) return int(i);
    }
    return -1;
}
}
#endif
