#ifndef OPENUA_ANDROID_CLICK_GESTURE_H
#define OPENUA_ANDROID_CLICK_GESTURE_H
#include <cstdint>
#include <cstdlib>

// Synthetic SDL events need explicit click counts. Only short, stationary
// clicks can pair; a drag/long hold must never become a vehicle double-click.
class AndroidClickGesture {
public:
    explicit AndroidClickGesture(int tolerance=12): slop(tolerance) {}
    int Down(uint32_t now,int x,int y) {
        clicks=previous && uint32_t(now-released)<=350 && Near(x,y,lastX,lastY)?2:1;
        previous=false; held=true; moved=false; started=now; startX=x; startY=y;
        return clicks;
    }
    void Move(int x,int y) {
        if(held && !Near(x,y,startX,startY)) moved=true;
    }
    int Up(uint32_t now,int x,int y) {
        if(!held) return 1;
        Move(x,y); held=false;
        if(moved || uint32_t(now-started)>350) { previous=false; return 1; }
        previous=clicks==1; released=now; lastX=x; lastY=y;
        return clicks;
    }
private:
    bool Near(int x,int y,int a,int b) const { return std::abs(x-a)<=slop && std::abs(y-b)<=slop; }
    int slop,clicks=1,startX=0,startY=0,lastX=0,lastY=0;
    uint32_t started=0,released=0;
    bool previous=false,held=false,moved=false;
};
#endif
