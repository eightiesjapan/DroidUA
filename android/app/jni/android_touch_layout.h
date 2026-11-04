#ifndef OPENUA_ANDROID_TOUCH_LAYOUT_H
#define OPENUA_ANDROID_TOUCH_LAYOUT_H
#include <array>
#include <algorithm>
#include <cmath>
#include <istream>
#include <string>
namespace AndroidTouch {
struct Control { float x,y,r; const char *label; };
constexpr size_t Count=8;
struct Placement { int right=0; float edge=0,bottom=0; };
struct Config { bool custom=false; std::array<Placement,Count> positions; };
inline bool ReadConfig(std::istream &in,Config &out) {
    Config parsed; std::string magic,key; int version=0,custom=0;
    if(!(in>>magic>>version>>key>>custom) || magic!="droidua-touch-layout" || version!=1 || key!="custom" || (custom!=0 && custom!=1)) return false;
    parsed.custom=custom;
    if(custom) for(size_t i=0;i<Count;++i) {
        int id=-1; auto &p=parsed.positions[i];
        if(!(in>>id>>p.right>>p.edge>>p.bottom) || id!=int(i) || (p.right!=0 && p.right!=1) ||
           !std::isfinite(p.edge) || !std::isfinite(p.bottom) || p.edge<0 || p.edge>4 || p.bottom<0 || p.bottom>1) return false;
    }
    if(in>>key) return false;
    out=parsed; return true;
}
inline std::array<Control,Count> Layout(float w,float h,bool pointer,bool mapOpen,const Config *config=nullptr) {
    // Keep every overlay hit target above the original HUD and its tooltips.

    std::array<Control,Count> controls={{{70,h-90,48,pointer ? "Cursor" : "Move"},
        {w-70,h-90,48,pointer && mapOpen ? "Pan" : "Look"},
        {55,h-180,25,"Fire"},{120,h-155,25,"Alt"},
        {w-160,h-65,25,pointer ? "VIEW" : "SELECT"},
        {184,h-85,25,"Brake"},{w-175,h-170,25,"Leader"},{w-105,h-190,25,"Lead"}}};
    if(config && config->custom) for(size_t i=0;i<Count;++i) {
        const auto &p=config->positions[i]; auto &c=controls[i];
        c.x=p.right ? w-p.edge*h : p.edge*h; c.y=h-p.bottom*h;
    }
    // Adapt custom layouts to a new aspect ratio, keeping controls on screen,
    // above the original bottom HUD and separate from each other.
    for(size_t i=0;i<Count;++i) {
        auto &c=controls[i];
        c.x=std::max(c.r+8,std::min(w-c.r-8,c.x));
        c.y=std::max(c.r+44,std::min(h-c.r-40,c.y));
        auto free=[&](float x,float y) {
            for(size_t j=0;j<i;++j) {
                float dx=x-controls[j].x,dy=y-controls[j].y,gap=c.r+controls[j].r+6;
                if(dx*dx+dy*dy<gap*gap) return false;
            }
            return true;
        };
        if(!free(c.x,c.y)) {
            float best=1e30f,bx=c.x,by=c.y;
            for(float y=c.r+44;y<=h-c.r-40;y+=8) for(float x=c.r+8;x<=w-c.r-8;x+=8) {
                float dx=x-c.x,dy=y-c.y,cost=dx*dx+dy*dy;
                if(cost<best && free(x,y)) {best=cost;bx=x;by=y;}
            }
            c.x=bx;c.y=by;
        }
    }
    return controls;
}
inline int HitTest(const std::array<Control,Count> &controls,float x,float y,bool visible) {
    if (!visible) return -1;
    for (unsigned i=0;i<controls.size();++i) {
        const float dx=x-controls[i].x,dy=y-controls[i].y;
        if (dx*dx+dy*dy <= controls[i].r*controls[i].r) return int(i);
    }
    return -1;
}
}
#endif
