#ifdef __ANDROID__
#include "winp.h"
#include "android_pointer_gesture.h"
#include "android_click_gesture.h"
#include "android_touch_layout.h"
#include "system/android_display.h"

bool UAAndroidMapIsTarget(const TClickBoxInf &input);
bool UAAndroidMapIsOpen();
bool UAAndroidModalIsOpen();
void UAAndroidPanMap(float x,float y);
#include "system/gfx.h"
#include "system/system.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>

namespace {
struct TouchFinger { int control; float x,y; };
std::map<SDL_FingerID,TouchFinger> fingers;
bool touchPlaying=false, touchPointer=false, manualTouchHeld=false, releaseTouchMouse=false;
Uint32 currentButtons=0, touchPressedLatch=0;
Uint32 currentActions=0, previousActions=0, pendingHotkeys=0;
bool selectPressed=false;

float mapPanX=0,mapPanY=0;
std::array<AndroidTouch::Control,AndroidTouch::Count> TouchLayout() {
    auto size=GFX::Engine.GetScreenSize();
    return AndroidTouch::Layout(size.x,size.y,touchPointer,UAAndroidMapIsOpen(),&Android::TouchConfiguration());
}
void TouchAxes(float &lx,float &ly,float &rx,float &ry,Uint32 &buttons) {
    if(!touchPlaying) {
        fingers.clear(); touchPressedLatch=0; releaseTouchMouse=manualTouchHeld; return;
    }
    if(!Android::TouchVisible()) {
        // Stop held overlay pads/buttons, but retain direct menu touches through
        // their release. Changing visibility must not turn a held pad into a click.
        for(auto it=fingers.begin();it!=fingers.end();) {
            if(it->second.control>=0) it=fingers.erase(it); else ++it;
        }
        touchPressedLatch=0;

        return;
    }
    auto layout=TouchLayout();
    buttons |= touchPressedLatch;
    touchPressedLatch=0;
    for(const auto &entry:fingers) {
        int c=entry.second.control;
        if(c<0) continue;
        if(c<2) {
            float x=(entry.second.x-layout[c].x)/layout[c].r;
            float y=(entry.second.y-layout[c].y)/layout[c].r;
            float length=std::sqrt(x*x+y*y);
            if(length>1) {x/=length; y/=length;}
            if(length<.12f) x=y=0;
            if(c==0) {lx=x; ly=y;} else {rx=x; ry=y;}
        } else buttons |= AndroidControls::Bit(AndroidTouch::Action(c));
    }
}
SDL_GameController *pad = nullptr;
Uint32 previous = 0, lastTick = 0;
std::map<int, bool> previousKeys;
bool wasPlaying = false;
float moveX = 0, moveY = 0, lookX = 0, lookY = 0;
bool leftClick = false, rightClick = false;
AndroidPointerGesture r1Gesture;
AndroidClickGesture touchClicks, padClicks(4);
bool r1Held = false, r1Pressed = false;
ClickBox *r1Map = nullptr;
float frameDt = 0;
float axis(SDL_GameControllerAxis a) {
    if (!pad) return 0;
    float v = SDL_GameControllerGetAxis(pad, a) / 32767.0f;
    const float dead = 0.18f;
    return std::fabs(v) <= dead ? 0.0f : std::copysign((std::min(std::fabs(v), 1.0f) - dead) / (1.0f - dead), v);
}
}

void NC_STACK_winp::AndroidReset()
{
    fingers.clear(); touchPressedLatch=0; currentButtons=previous=0;
    currentActions=previousActions=pendingHotkeys=0;
    selectPressed=false;
    for(const auto &key:previousKeys) if(key.second) KeyUp(key.first);
    previousKeys.clear();
    if(leftClick || manualTouchHeld) OnMouseUp(_mPos,SDL_BUTTON_LEFT,1);
    if(rightClick || r1Held) OnMouseUp(_mPos,SDL_BUTTON_RIGHT,1);
    leftClick=rightClick=manualTouchHeld=releaseTouchMouse=false;
    touchClicks=AndroidClickGesture(); padClicks=AndroidClickGesture(4);
    r1Held=r1Pressed=false; r1Map=nullptr; r1Gesture=AndroidPointerGesture();
    moveX=moveY=lookX=lookY=mapPanX=mapPanY=0;
    lastTick=0;
}

void NC_STACK_winp::AndroidPoll(bool playing, bool &grabbed)
{
    if (!(SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER))
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    if (pad && !SDL_GameControllerGetAttached(pad)) {
        SDL_GameControllerClose(pad);
        pad = nullptr;
    }
    if (!pad) {
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_IsGameController(i)) {
                pad = SDL_GameControllerOpen(i);
                if (pad) {
                    char *mapping = SDL_GameControllerMapping(pad);
                    SDL_Log("OpenUA controller: %s mapping: %s", SDL_GameControllerName(pad), mapping ? mapping : "unknown");
                    SDL_free(mapping);
                    break;
                }
            }
        }
    }
    Android::SetControllerPresent(pad != nullptr);
    Uint32 buttons = 0;
    if (pad) {
        SDL_GameControllerUpdate();
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
            if (SDL_GameControllerGetButton(pad, static_cast<SDL_GameControllerButton>(b))) buttons |= 1u << b;
        // The Anbernic driver advertises trigger axes but also sends digital
        // Android BUTTON_L2/R2 events. SDL's Android backend exposes those as
        // raw joystick buttons 15/16; its generated controller map ignores them.
        // Limit this fallback to the built-in pad: HID pads may use these raw
        // button numbers for unrelated actions.
        SDL_Joystick *joystick=SDL_GameControllerGetJoystick(pad);
        const char *name=SDL_GameControllerName(pad);
        const bool digitalTriggers=name && std::string(name)=="retrogame_joypad";
        if (axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.3f ||
            (digitalTriggers && SDL_JoystickGetButton(joystick,15))) buttons |= 1u << 24;
        if (axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.3f ||
            (digitalTriggers && SDL_JoystickGetButton(joystick,16))) buttons |= 1u << 25;
        if ((buttons ^ previous) & ((1u << 24) | (1u << 25)))
            SDL_Log("OpenUA weapon triggers: L2=%d R2=%d",bool(buttons & (1u << 24)),bool(buttons & (1u << 25)));
    }
    touchPlaying=playing;
    float touchLX=0,touchLY=0,touchRX=0,touchRY=0;
    Uint32 touchActions=0;
    TouchAxes(touchLX,touchLY,touchRX,touchRY,touchActions);
    currentButtons=buttons;
    currentActions=AndroidControls::Actions(Android::GamepadConfiguration(),buttons)|touchActions;
    const Uint32 actionPressed=playing ? currentActions&~previousActions:0;
    if(!playing) pendingHotkeys=0;
    for(int action=1;action<AndroidControls::ActionCount;++action)
        if(AndroidControls::Hotkey(action)>=0) pendingHotkeys|=actionPressed&AndroidControls::Bit(action);
    previousActions=currentActions;
    if(releaseTouchMouse) { OnMouseUp(_mPos,SDL_BUTTON_LEFT,1); manualTouchHeld=false; releaseTouchMouse=false; }
    auto down = [&](int b) { return (buttons & (1u << b)) != 0; };
    auto pressed = [&](int b) { return down(b) && !(previous & (1u << b)); };
    selectPressed=playing && pressed(SDL_CONTROLLER_BUTTON_BACK);
    if (playing && !wasPlaying) grabbed = true;
    const bool modal = playing && UAAndroidModalIsOpen();
    if (!playing || modal) grabbed = false;

    if (pressed(SDL_CONTROLLER_BUTTON_START)) grabbed = false;

    const bool pointer = !playing || !grabbed;
    const Uint32 now = SDL_GetTicks();
    const float dt = lastTick ? std::min((now - lastTick) / 1000.0f, 0.05f) : 0.0f;
    lastTick = now;
    frameDt = dt;
    r1Held = (currentActions&AndroidControls::Bit(AndroidControls::Mode))!=0;
    r1Pressed = (actionPressed&AndroidControls::Bit(AndroidControls::Mode))!=0;
    moveX = std::max(-1.0f,std::min(1.0f,axis(SDL_CONTROLLER_AXIS_LEFTX)+touchLX));
    moveY = std::max(-1.0f,std::min(1.0f,axis(SDL_CONTROLLER_AXIS_LEFTY)+touchLY));
    mapPanX=mapPanY=0;
    lookX = std::max(-1.0f,std::min(1.0f,axis(SDL_CONTROLLER_AXIS_RIGHTX)+touchRX));
    lookY = std::max(-1.0f,std::min(1.0f,axis(SDL_CONTROLLER_AXIS_RIGHTY)+touchRY));
    if (playing && !modal && UAAndroidMapIsOpen()) {
        // Physical right stick pans anywhere, in either interaction mode.
        // The touch Look pad retains its existing pointer-mode Pan behaviour.
        mapPanX=std::max(-1.0f,std::min(1.0f,axis(SDL_CONTROLLER_AXIS_RIGHTX)+(!grabbed?touchRX:0)));
        mapPanY=std::max(-1.0f,std::min(1.0f,axis(SDL_CONTROLLER_AXIS_RIGHTY)+(!grabbed?touchRY:0)));
        lookX=grabbed?touchRX:0; lookY=grabbed?touchRY:0;
    }
    if (pointer && (moveX || moveY)) {
        static float x = 320, y = 240;
        static Common::Point last;
        if (_mPos != last) { x = _mPos.x; y = _mPos.y; }
        SDL_Surface *screen = GFX::Engine.Screen();
        x = std::max(0.0f, std::min(float(screen->w - 1), x + moveX * 420 * dt));
        y = std::max(0.0f, std::min(float(screen->h - 1), y + moveY * 420 * dt));

        SDL_Event e = {};
        e.type = SDL_MOUSEMOTION;
        e.motion.x = Android::WindowPoint(Common::Point(int(x),int(y))).x;
        e.motion.y = Android::WindowPoint(Common::Point(int(x),int(y))).y;
        InputWatch(nullptr, &e);
        last = _mPos;
    }
    padClicks.Move(_mPos.x,_mPos.y);
    auto mouse = [&](int button, bool held, bool &old) {
        if (held != old) {
            SDL_Event e = {};
            e.type = held ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            e.button.button = button;
            e.button.clicks = button==SDL_BUTTON_LEFT
                ? (held?padClicks.Down(now,_mPos.x,_mPos.y):padClicks.Up(now,_mPos.x,_mPos.y)):1;
            e.button.x = Android::WindowPoint(_mPos).x;
            e.button.y = Android::WindowPoint(_mPos).y;
            InputWatch(nullptr, &e);
            old = held;
        }
    };
    mouse(SDL_BUTTON_LEFT, playing ? bool(currentActions&AndroidControls::Bit(AndroidControls::Click)):
        down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)||down(SDL_CONTROLLER_BUTTON_A),leftClick);
    std::map<int, bool> keys;
    keys[Input::KC_ESCAPE] = down(SDL_CONTROLLER_BUTTON_START) || (!playing && down(SDL_CONTROLLER_BUTTON_B));
    keys[Input::KC_Q] = playing && down(SDL_CONTROLLER_BUTTON_DPAD_UP);
    keys[Input::KC_W] = playing && down(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    keys[Input::KC_F1] = playing && down(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    keys[Input::KC_F4] = playing && down(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    for (const auto &key : keys) {
        if (key.second && !previousKeys[key.first]) KeyDown(key.first);
        if (!key.second && previousKeys[key.first]) KeyUp(key.first);
    }
    previousKeys = keys;
    previous = buttons;
    wasPlaying = playing;
}

void NC_STACK_winp::AndroidApply(TInputState *input, bool playing, bool &grabbed)
{
    const bool modal = playing && UAAndroidModalIsOpen();
    if (modal) grabbed=false;
    if (r1Pressed) r1Map = !grabbed && UAAndroidMapIsTarget(input->ClickInf) ? input->ClickInf.selected_btn : nullptr;
    auto gesture = r1Gesture.Update(playing && !modal && (pad || Android::TouchVisible()) && !mapPanX && !mapPanY, r1Held,
        !grabbed && UAAndroidMapIsTarget(input->ClickInf), UAAndroidMapIsOpen(),
        _mPos.x, _mPos.y, lookX, lookY, frameDt, SDL_GetTicks());
    if (gesture.toggle) {
        grabbed = !grabbed;
        SDL_Log("OpenUA controller mode on R1 release: %s", grabbed ? "vehicle" : "pointer");
    }
    if (gesture.down || gesture.hold || gesture.up) {
        // Preserve the native map drag, including its zoom scaling and clamping.
        // A virtual drag coordinate lets the right stick pan beyond screen edges
        // while the visible pointer remains at its actual position.
        input->ClickInf.move.ScreenPos = Common::Point(gesture.x, gesture.y);
        if (r1Map) {
            input->ClickInf.selected_btn = r1Map;
            input->ClickInf.selected_btnID = 17;
        }
        if (gesture.down) input->ClickInf.flag |= TClickBoxInf::FLAG_RM_DOWN;
        if (gesture.hold) input->ClickInf.flag |= TClickBoxInf::FLAG_RM_HOLD;
        if (gesture.up) input->ClickInf.flag |= TClickBoxInf::FLAG_RM_UP;
    }
    touchPointer=!grabbed;
    if (playing && !modal && (mapPanX || mapPanY))
        UAAndroidPanMap(mapPanX*220.0f*frameDt,mapPanY*220.0f*frameDt);
    if (!playing || (!pad && !Android::TouchVisible())) return;
    // Semantic actions bypass profile keyboard bindings. Fixed Start/Select and
    // desktop/mouse keyboard behavior remain separate from remappable buttons.
    using namespace AndroidControls;
    if(!modal && selectPressed) input->HotKeyID=8;
    else if(!modal) for(int action=1;action<ActionCount;++action) if(pendingHotkeys&Bit(action)) {
        input->HotKeyID=Hotkey(action);pendingHotkeys&=~Bit(action);break;
    }
    if(modal) pendingHotkeys=0;
    if(!modal && (currentActions&Bit(Brake))) input->Buttons.Set(3);
    // Disable desktop joystick expressions; those assume a flight-stick throttle.
    for (int i = 12; i <= 16; ++i) input->Sliders[i] = 0;
    if (grabbed && !modal) {
        input->Sliders[0] += lookX;
        input->Sliders[1] += lookY;
        input->Sliders[2] -= moveY;
        input->Sliders[3] += lookX;
        input->Sliders[4] -= moveY;
        input->Sliders[5] -= lookY * 1.5f;
        // Physical triggers and touch Fire/Alt are independent of A/B hotkeys.
        if(currentActions&Bit(Fire)) input->Buttons.Set(0);
        if(currentActions&Bit(Alt)) input->Buttons.Set(2);
    }
}

bool NC_STACK_winp::AndroidTouchEvent(SDL_Event *event)
{
    // Direct fingers must use absolute positions even with Touch Hide/Auto on a
    // handheld. SDL's synthetic mouse may be relative/warped while aiming.
    if(!touchPlaying) return false;
    if((event->type==SDL_MOUSEMOTION && event->motion.which==SDL_TOUCH_MOUSEID) ||
       ((event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP) && event->button.which==SDL_TOUCH_MOUSEID))
        return true; // Finger events below replace SDL's single-touch mouse.
    if(event->type!=SDL_FINGERDOWN && event->type!=SDL_FINGERMOTION && event->type!=SDL_FINGERUP) return false;
    auto real=System::GetResolution();
    Common::Point p=Android::LogicalPoint(Common::Point(int(event->tfinger.x*real.x),int(event->tfinger.y*real.y)));
    auto id=event->tfinger.fingerId;
    if(event->type==SDL_FINGERDOWN) {
        if(!Android::InsideViewport(int(event->tfinger.x*real.x),int(event->tfinger.y*real.y))) return true;
        int control=AndroidTouch::HitTest(TouchLayout(),p.x,p.y,Android::TouchVisible());
        fingers[id]={control,float(p.x),float(p.y)};

        if(control>=2) {
            touchPressedLatch |= AndroidControls::Bit(AndroidTouch::Action(control));
        }
    }
    auto found=fingers.find(id);
    if(found==fingers.end()) return true;
    auto old=found->second;
    found->second.x=p.x; found->second.y=p.y;
    if(old.control<0) {
        SDL_Event e={};
        auto window=Android::WindowPoint(p);
        if(event->type==SDL_FINGERMOTION) {
            touchClicks.Move(p.x,p.y);
            e.type=SDL_MOUSEMOTION; e.motion.which=SDL_TOUCH_MOUSEID-1;
            e.motion.x=window.x; e.motion.y=window.y;
            e.motion.xrel=p.x-int(old.x); e.motion.yrel=p.y-int(old.y);
        } else {
            e.type=event->type==SDL_FINGERDOWN ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            manualTouchHeld=event->type==SDL_FINGERDOWN;
            e.button.which=SDL_TOUCH_MOUSEID-1; e.button.button=SDL_BUTTON_LEFT;
            e.button.clicks=manualTouchHeld?touchClicks.Down(SDL_GetTicks(),p.x,p.y):touchClicks.Up(SDL_GetTicks(),p.x,p.y);
            e.button.x=window.x; e.button.y=window.y;
            OnMouseMove(p,Common::Point());
        }
        InputWatch(nullptr,&e);
    }
    if(event->type==SDL_FINGERUP) fingers.erase(found);
    return true;
}

void NC_STACK_winp::AndroidCollectTouchControls(std::vector<AndroidTouchItem> &items)
{
    items.clear();
    if(!Android::TouchVisible() || !touchPlaying) return;
    auto layout=TouchLayout();
    for(int i=0;i<int(layout.size());++i) {
        if(!layout[i].enabled) continue;
        const auto &c=layout[i];
        bool held=false; float fx=c.x,fy=c.y;
        for(const auto &f:fingers) if(f.second.control==i) {held=true;fx=f.second.x;fy=f.second.y;}
        AndroidTouchItem it;
        it.x=c.x; it.y=c.y; it.r=c.r; it.pad=i<2; it.held=held; it.knobX=c.x; it.knobY=c.y;
        if(it.pad && held) {
            float dx=fx-c.x,dy=fy-c.y,length=std::sqrt(dx*dx+dy*dy);
            if(length>c.r*.65f) {dx*=c.r*.65f/length;dy*=c.r*.65f/length;}
            it.knobX=c.x+dx; it.knobY=c.y+dy;
        }
        it.label=c.label;
        for(char &ch:it.label) ch=char(std::toupper((unsigned char)ch));
        items.push_back(it);
    }
}
#endif
