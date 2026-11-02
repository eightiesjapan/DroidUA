#ifdef __ANDROID__
#include "winp.h"
#include "android_pointer_gesture.h"
#include "android_touch_layout.h"
#include "system/android_display.h"

bool UAAndroidMapIsTarget(const TClickBoxInf &input);
bool UAAndroidMapIsOpen();
void UAAndroidPanMap(float x,float y);
#include "system/gfx.h"
#include "system/system.h"
#include <algorithm>
#include <cmath>

namespace {
struct TouchFinger { int control; float x,y; };
std::map<SDL_FingerID,TouchFinger> fingers;
bool touchPlaying=false, touchPointer=false, manualTouchHeld=false, releaseTouchMouse=false;
Uint32 currentButtons=0, touchPressedLatch=0;

float touchPanX=0,touchPanY=0;
std::array<AndroidTouch::Control,5> TouchLayout() {
    auto size=GFX::Engine.GetScreenSize();
    return AndroidTouch::Layout(size.x,size.y,touchPointer,UAAndroidMapIsOpen());
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
    const int mapping[]={-1,-1,SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
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
        } else buttons |= 1u << mapping[c];
    }
}
SDL_GameController *pad = nullptr;
Uint32 previous = 0, lastTick = 0;
std::map<int, bool> previousKeys;
bool wasPlaying = false;
float moveX = 0, moveY = 0, lookX = 0, lookY = 0;
bool leftClick = false, rightClick = false;
AndroidPointerGesture r1Gesture;
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
    for(const auto &key:previousKeys) if(key.second) KeyUp(key.first);
    previousKeys.clear();
    if(leftClick || manualTouchHeld) OnMouseUp(_mPos,SDL_BUTTON_LEFT,1);
    if(rightClick || r1Held) OnMouseUp(_mPos,SDL_BUTTON_RIGHT,1);
    leftClick=rightClick=manualTouchHeld=releaseTouchMouse=false;
    r1Held=r1Pressed=false; r1Map=nullptr; r1Gesture=AndroidPointerGesture();
    moveX=moveY=lookX=lookY=touchPanX=touchPanY=0;
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
        // Triggers are analog axes, not shoulder buttons.
        if (axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.3f) buttons |= 1u << 24;
        if (axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.3f) buttons |= 1u << 25;
    }
    touchPlaying=playing;
    float touchLX=0,touchLY=0,touchRX=0,touchRY=0;
    TouchAxes(touchLX,touchLY,touchRX,touchRY,buttons);
    currentButtons=buttons;
    if(releaseTouchMouse) { OnMouseUp(_mPos,SDL_BUTTON_LEFT,1); manualTouchHeld=false; releaseTouchMouse=false; }
    auto down = [&](int b) { return (buttons & (1u << b)) != 0; };
    auto pressed = [&](int b) { return down(b) && !(previous & (1u << b)); };
    if (playing && !wasPlaying) grabbed = true;
    if (!playing) grabbed = false;

    if (pressed(SDL_CONTROLLER_BUTTON_START)) grabbed = false;

    const bool pointer = !playing || !grabbed;
    const Uint32 now = SDL_GetTicks();
    const float dt = lastTick ? std::min((now - lastTick) / 1000.0f, 0.05f) : 0.0f;
    lastTick = now;
    frameDt = dt;
    r1Held = down(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    r1Pressed = pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    moveX = std::max(-1.0f,std::min(1.0f,axis(SDL_CONTROLLER_AXIS_LEFTX)+touchLX));
    moveY = std::max(-1.0f,std::min(1.0f,axis(SDL_CONTROLLER_AXIS_LEFTY)+touchLY));
    touchPanX=touchPanY=0;
    if (playing && !grabbed && UAAndroidMapIsOpen()) {
        touchPanX=touchRX; touchPanY=touchRY;
        touchRX=touchRY=0; // Touch pan never also turns the vehicle/camera.
    }
    lookX = std::max(-1.0f,std::min(1.0f,axis(SDL_CONTROLLER_AXIS_RIGHTX)+touchRX));
    lookY = std::max(-1.0f,std::min(1.0f,axis(SDL_CONTROLLER_AXIS_RIGHTY)+touchRY));
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
    auto mouse = [&](int button, bool held, bool &old) {
        if (held != old) {
            SDL_Event e = {};
            e.type = held ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            e.button.button = button;
            e.button.clicks = 1;
            e.button.x = Android::WindowPoint(_mPos).x;
            e.button.y = Android::WindowPoint(_mPos).y;
            InputWatch(nullptr, &e);
            old = held;
        }
    };
    mouse(SDL_BUTTON_LEFT, down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) || (!playing && down(SDL_CONTROLLER_BUTTON_A)), leftClick);
    mouse(SDL_BUTTON_RIGHT, down(24), rightClick);
    std::map<int, bool> keys;
    keys[Input::KC_ESCAPE] = down(SDL_CONTROLLER_BUTTON_START) || (!playing && down(SDL_CONTROLLER_BUTTON_B));
    keys[Input::KC_M] = playing && down(SDL_CONTROLLER_BUTTON_BACK);
    keys[Input::KC_F3] = playing && down(SDL_CONTROLLER_BUTTON_X);
    keys[Input::KC_F2] = playing && down(SDL_CONTROLLER_BUTTON_Y);
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
    if (r1Pressed) r1Map = !grabbed && UAAndroidMapIsTarget(input->ClickInf) ? input->ClickInf.selected_btn : nullptr;
    auto gesture = r1Gesture.Update(playing && (pad || Android::TouchVisible()) && !touchPanX && !touchPanY, r1Held,
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
    if (playing && !grabbed && Android::TouchVisible() && (touchPanX || touchPanY))
        UAAndroidPanMap(touchPanX*220.0f*frameDt,touchPanY*220.0f*frameDt);
    if (!playing || (!pad && !Android::TouchVisible())) return;
    // Disable desktop joystick expressions; those assume a flight-stick throttle.
    for (int i = 12; i <= 16; ++i) input->Sliders[i] = 0;
    if (grabbed) {
        input->Sliders[0] += lookX;
        input->Sliders[1] += lookY;
        input->Sliders[2] -= moveY;
        input->Sliders[3] += lookX;
        input->Sliders[4] -= moveY;
        input->Sliders[5] -= lookY * 1.5f;
        // B/A retain the primary/secondary actions used in the first prototype.
        if (currentButtons & ((1u << SDL_CONTROLLER_BUTTON_A) | (1u << 25))) input->Buttons.Set(0);
        if (currentButtons & (1u << SDL_CONTROLLER_BUTTON_B)) input->Buttons.Set(2);
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
            static const int mapping[]={-1,-1,SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,
                SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
            touchPressedLatch |= 1u << mapping[control];
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
            e.type=SDL_MOUSEMOTION; e.motion.which=SDL_TOUCH_MOUSEID-1;
            e.motion.x=window.x; e.motion.y=window.y;
            e.motion.xrel=p.x-int(old.x); e.motion.yrel=p.y-int(old.y);
        } else {
            e.type=event->type==SDL_FINGERDOWN ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            manualTouchHeld=event->type==SDL_FINGERDOWN;
            e.button.which=SDL_TOUCH_MOUSEID-1; e.button.button=SDL_BUTTON_LEFT; e.button.clicks=1;
            e.button.x=window.x; e.button.y=window.y;
            OnMouseMove(p,Common::Point());
        }
        InputWatch(nullptr,&e);
    }
    if(event->type==SDL_FINGERUP) fingers.erase(found);
    return true;
}

void NC_STACK_winp::AndroidDrawTouchControls(SDL_Surface *screen)
{
    if(!Android::TouchVisible() || !touchPlaying) return;
    static std::shared_ptr<TTF_Font> font(System::LoadFont("Liberation Sans",13),TTF_CloseFont);
    static std::map<std::string,std::shared_ptr<SDL_Surface>> labels;
    auto circle=[&](int cx,int cy,int radius,Uint32 color) {
        for(int y=-radius;y<=radius;++y) {
            int x=int(std::sqrt(float(radius*radius-y*y)));
            SDL_Rect row{cx-x,cy+y,2*x+1,1}; SDL_FillRect(screen,&row,color);
        }
    };
    auto layout=TouchLayout();
    const Uint8 alpha=Uint8(Android::TouchOpacity()*255/100);
    for(int i=0;i<int(layout.size());++i) {
        bool held=false; float x=layout[i].x,y=layout[i].y;
        for(const auto &f:fingers) if(f.second.control==i) {held=true;x=f.second.x;y=f.second.y;}
        auto c=layout[i];
        circle(int(c.x),int(c.y),int(c.r),SDL_MapRGBA(screen->format,140,200,210,alpha));
        circle(int(c.x),int(c.y),int(c.r)-2,SDL_MapRGBA(screen->format,held?45:12,held?90:30,held?100:40,alpha));
        if(i<2 && held) {
            float dx=x-c.x,dy=y-c.y,length=std::sqrt(dx*dx+dy*dy);
            if(length>c.r*.65f) {dx*=c.r*.65f/length;dy*=c.r*.65f/length;}
            circle(int(c.x+dx),int(c.y+dy),12,SDL_MapRGBA(screen->format,100,200,220,alpha));
        }
        auto &text=labels[c.label];
        if(!text && font) text.reset(TTF_RenderUTF8_Blended(font.get(),c.label,SDL_Color{235,250,255,255}),SDL_FreeSurface);
        if(text) {
            SDL_SetSurfaceAlphaMod(text.get(),alpha);
            // Fit SELECT inside the existing compact button without stretching.
            int width=std::min(text->w,int(c.r*1.65f));
            int height=text->h*width/std::max(text->w,1);
            SDL_Rect dst{int(c.x)-width/2,int(c.y)-height/2,width,height};
            SDL_BlitScaled(text.get(),nullptr,screen,&dst);
        }
    }
}
#endif
