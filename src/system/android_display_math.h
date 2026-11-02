#ifndef UA_ANDROID_DISPLAY_MATH_H
#define UA_ANDROID_DISPLAY_MATH_H
#include <algorithm>
#include <cmath>
namespace AndroidDisplay {
struct Rect { int x, y, w, h; };
inline Rect Fit(int width, int height, int left, int top, int right, int bottom, bool wide) {
    width=std::max(1,width); height=std::max(1,height);
    left=std::max(0,std::min(width-1,left)); top=std::max(0,std::min(height-1,top));
    right=std::max(0,std::min(width-left-1,right)); bottom=std::max(0,std::min(height-top-1,bottom));
    Rect r{left, top, width-left-right, height-top-bottom};
    if (!wide) {
        if (r.w * 3 > r.h * 4) { int w = r.h * 4 / 3; r.x += (r.w-w)/2; r.w=w; }
        else { int h = r.w * 3 / 4; r.y += (r.h-h)/2; r.h=h; }
    }
    return r;
}
inline int LogicalWidth(const Rect &r, int height) { return std::max(1, int(std::lround(double(r.w)*height/r.h))); }
inline int MapAxis(int point, int origin, int span, int logical) { return std::max(0, std::min(logical-1, (point-origin)*logical/std::max(span,1))); }
}
#endif
