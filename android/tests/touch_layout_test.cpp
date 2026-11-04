#include "../app/jni/android_touch_layout.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
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
        assert(std::strcmp(controls[5].label,"Brake")==0);
        assert(std::strcmp(controls[6].label,"Leader")==0);
        assert(std::strcmp(controls[7].label,"Lead")==0);
        AndroidTouch::Config custom;custom.custom=true;
        for(auto& p:custom.positions) {p.right=1;p.edge=0;p.bottom=0;}
        auto fitted=AndroidTouch::Layout(size[0],size[1],true,true,&custom);
        for(unsigned i=0;i<fitted.size();++i) {
            auto c=fitted[i];assert(c.x>=c.r && c.x+c.r<=size[0] && c.y+c.r<=size[1]-40);
            for(unsigned j=0;j<i;++j) {float x=c.x-fitted[j].x,y=c.y-fitted[j].y;assert(x*x+y*y>(c.r+fitted[j].r)*(c.r+fitted[j].r));}
        }
    }
    AndroidTouch::Config parsed;
    std::istringstream valid("droidua-touch-layout 1 custom 0");assert(AndroidTouch::ReadConfig(valid,parsed));
    for(const char* bad:{"","droidua-touch-layout 2 custom 0","droidua-touch-layout 1 custom 1 0 0 nan 0","droidua-touch-layout 1 custom 0 extra"}) {
        std::istringstream stream(bad);assert(!AndroidTouch::ReadConfig(stream,parsed));
    }
    std::cout << "Touch HUD clearance, hit-target separation and Pan label tests passed\n";
}
