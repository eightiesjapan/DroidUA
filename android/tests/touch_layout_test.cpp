#include "../app/jni/android_touch_layout.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
int main() {
    for(auto size: {std::array<float,2>{640,480}, {533,400}, {853,480}, {1067,480}, {898,400}, {1077,480}}) {
        auto controls=AndroidTouch::Layout(size[0],size[1],true,true);
        for(const auto &c:controls) {
            assert(c.x-c.r>=0 && c.x+c.r<=size[0]);
            assert(c.y-c.r>=0 && c.y+c.r<=size[1]-40);
        }
        for(unsigned i=0;i<controls.size();++i) for(unsigned j=i+1;j<controls.size();++j) {
            float x=controls[i].x-controls[j].x,y=controls[i].y-controls[j].y;
            assert(std::sqrt(x*x+y*y)>controls[i].r+controls[j].r);
        }
        // Hidden controls must not steal a tap from a chooser underneath them.
        for(unsigned i=0;i<controls.size();++i) {
            assert(AndroidTouch::HitTest(controls,controls[i].x,controls[i].y,true)==int(i));
            assert(AndroidTouch::HitTest(controls,controls[i].x,controls[i].y,false)==-1);
        }
        assert(std::strcmp(controls[4].label,"VIEW")==0);
        assert(std::strcmp(AndroidTouch::Layout(size[0],size[1],false,true)[4].label,"SELECT")==0);
        assert(std::strcmp(controls[1].label,"Pan")==0);
        assert(std::strcmp(AndroidTouch::Layout(size[0],size[1],false,true)[1].label,"Look")==0);
        assert(std::strcmp(AndroidTouch::Layout(size[0],size[1],true,false)[1].label,"Look")==0);
    }
    std::cout << "Touch HUD clearance, hit-target separation and Pan label tests passed\n";
}
