#ifndef OPENUA_ANDROID_TOUCH_LAYOUT_H
#define OPENUA_ANDROID_TOUCH_LAYOUT_H
#include <array>
namespace AndroidTouch {
struct Control { float x,y,r; const char *label; };
inline std::array<Control,10> Layout(float w,float h,bool pointer,bool mapOpen) {
    // Keep every overlay hit target above the original HUD and its tooltips.
    const float row=h-65;
    return {{{70,h-90,48,pointer ? "Cursor" : "Move"},
        {w-70,h-90,48,pointer && mapOpen ? "Pan" : "Look"},
        {w-55,h-180,25,"Fire"},{w-120,h-155,25,"Alt"},
        {w/2-90,row,25,"Mode"},{w/2-30,row,25,"Map"},
        {w/2+90,row,25,"Menu"},{w/2+30,row,25,"Click"},
        {140,h-165,25,"Next"},{195,h-130,25,"Host"}}};
}
inline int HitTest(const std::array<Control,10> &controls,float x,float y,bool visible) {
    if (!visible) return -1;
    for (unsigned i=0;i<controls.size();++i) {
        const float dx=x-controls[i].x,dy=y-controls[i].y;
        if (dx*dx+dy*dy <= controls[i].r*controls[i].r) return int(i);
    }
    return -1;
}
}
#endif
