#include "../app/jni/android_click_gesture.h"
#include <cassert>
#include <cstdio>
int main() {
    AndroidClickGesture tap;
    assert(tap.Down(0,10,10)==1); assert(tap.Up(50,11,10)==1);
    assert(tap.Down(140,13,12)==2); assert(tap.Up(200,14,13)==2);
    assert(tap.Down(250,13,12)==1); assert(tap.Up(280,13,12)==1);
    assert(tap.Down(700,13,12)==1); assert(tap.Up(730,13,12)==1); // Timeout.
    assert(tap.Down(800,50,50)==1); tap.Move(90,50); tap.Move(50,50);
    assert(tap.Up(850,50,50)==1); // Out-and-back drag is still a drag.
    assert(tap.Down(900,50,50)==1); assert(tap.Up(950,50,50)==1);
    assert(tap.Down(1000,50,50)==2); assert(tap.Up(1500,50,50)==1); // Long hold cancels.
    assert(tap.Down(1550,50,50)==1); assert(tap.Up(1600,50,50)==1);
    tap=AndroidClickGesture(); // Sleep/movie/reset cannot pair with an old click.
    assert(tap.Down(1650,50,50)==1); assert(tap.Up(1680,50,50)==1);
    AndroidClickGesture pad(4);
    assert(pad.Down(0,100,100)==1); assert(pad.Up(10,100,100)==1);
    assert(pad.Down(100,100,100)==2); assert(pad.Up(120,100,100)==2);
    puts("Double-click timing, touch slop, drag, hold and reset tests passed");
}
