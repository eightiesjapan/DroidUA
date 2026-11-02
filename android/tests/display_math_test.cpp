#include "../../src/system/android_display_math.h"
#include <cassert>
#include <iostream>
using namespace AndroidDisplay;
int main() {
    auto original=Fit(1280,960,0,0,0,0,false);
    assert(original.x==0 && original.y==0 && original.w==1280 && original.h==960);
    auto phone=Fit(2400,1080,0,0,0,0,false);
    assert(phone.x==480 && phone.w==1440 && phone.h==1080);
    assert(MapAxis(480,phone.x,phone.w,640)==0);
    assert(MapAxis(1200,phone.x,phone.w,640)==320);
    assert(MapAxis(1919,phone.x,phone.w,640)==639);
    assert(MapAxis(0,phone.x,phone.w,640)==0);
    auto wide=Fit(2400,1080,90,0,0,0,true);
    assert(wide.x==90 && wide.w==2310 && LogicalWidth(wide,480)==1027);
    assert(LogicalWidth(wide,400)==856);
    auto tall=Fit(1080,2400,0,100,0,100,false);
    assert(tall.w==1080 && tall.h==810 && tall.y==795);
    auto invalid=Fit(10,10,50,50,50,50,true);
    assert(invalid.w>0 && invalid.h>0 && invalid.x+invalid.w<=10 && invalid.y+invalid.h<=10);
    std::cout << "Display fit, safe-area and pointer tests passed\n";
}
