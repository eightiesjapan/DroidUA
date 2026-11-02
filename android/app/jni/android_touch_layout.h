#ifndef OPENUA_ANDROID_TOUCH_LAYOUT_H
#define OPENUA_ANDROID_TOUCH_LAYOUT_H
#include <array>
namespace AndroidTouch {
struct Control { float x,y,r; const char *label; };
inline std::array<Control,5> Layout(float w,float h,bool pointer,bool mapOpen) {
    // Keep every overlay hit target above the original HUD and its tooltips.

    return {{{70,h-90,48,pointer ? "Cursor" : "Move"},
        {w-70,h-90,48,pointer && mapOpen ? "Pan" : "Look"},
        {55,h-180,25,"Fire"},{120,h-155,25,"Alt"},
        {w-160,h-65,25,pointer ? "VIEW" : "SELECT"}}};
}
inline int HitTest(const std::array<Control,5> &controls,float x,float y,bool visible) {
    if (!visible) return -1;
    for (unsigned i=0;i<controls.size();++i) {
        const float dx=x-controls[i].x,dy=y-controls[i].y;
        if (dx*dx+dy*dy <= controls[i].r*controls[i].r) return int(i);
    }
    return -1;
}
}
#endif
