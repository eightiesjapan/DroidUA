#ifdef __ANDROID__
#include "android_display.h"
#include "android_display_math.h"
#include "system.h"
#include "gfx.h"
#include "inpt.h"
#include "../button.h"
#include "../winp.h"
#include <SDL2/SDL_system.h>
#include <jni.h>
#include <fstream>
#include <cstdio>
#include <memory>

bool UAAndroidUnitMenuIsOpen();
bool UAAndroidModalIsOpen();

namespace Android {
static Settings settings, draft;
static bool mission=false, controller=false;
static int safe[4]={0,0,0,0};
static float measuredFPS=0;
static void ReadInsets() {
    static Uint32 next=0;
    Uint32 now=SDL_GetTicks();
    if (now < next) return;
    next=now+1000;
    JNIEnv *env=static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!activity) return;
    jclass cls=env->GetObjectClass(activity);
    jmethodID method=env->GetMethodID(cls,"getContentInsets","()[I");
    if (method) {
        jintArray values=static_cast<jintArray>(env->CallObjectMethod(activity,method));
        if (values && env->GetArrayLength(values)==4) env->GetIntArrayRegion(values,0,4,safe);
        if (values) env->DeleteLocalRef(values);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}
void LoadSettings() {
    std::ifstream in("android-settings.cfg");
    std::string key; int value;
    while (in >> key >> value) {
        if(key=="layout" && value>=0 && value<=1) settings.layout=value;
        if(key=="large" && value>=0 && value<=1) settings.large=value;
        if(key=="fps" && (value==30 || value==60)) settings.fps=value;
        if(key=="touch" && value>=0 && value<=2) settings.touch=value;
        if(key=="touchOpacity" && value>=10 && value<=100) settings.touchOpacity=value;
        if(key=="showFPS" && value>=0 && value<=1) settings.showFPS=value;
    }
    draft=settings;
}
static void RefreshMenu(NC_STACK_button *menu) {
    const char *touch[]={"Automatic","Show","Hide"};
    menu->SetText(6000, draft.layout ? "Original 4:3" : "Automatic");
    menu->SetText(6001, draft.large ? "Large" : "Standard");
    menu->SetText(6002, draft.fps==30 ? "30 FPS" : "60 FPS");
    menu->SetText(6003, touch[draft.touch]);
    menu->SetText(6004, draft.showFPS ? "On" : "Off");
    menu->SetText(6065,"Touch opacity " + std::to_string(draft.touchOpacity) + "%");
}
void BeginSettings(NC_STACK_button *menu) {
    draft=settings;
    if(auto slider=menu->GetSliderData(6005)) {
        slider->value=draft.touchOpacity; menu->Refresh(6005);
    }
    RefreshMenu(menu);
}
bool CycleSetting(int event, NC_STACK_button *menu) {
    switch(event) {
    case 6100: draft.layout=!draft.layout; break;
    case 6101: draft.large=!draft.large; break;
    case 6102: draft.fps=draft.fps==60 ? 30 : 60; break;
    case 6103: draft.touch=(draft.touch+1)%3; break;
    case 6104: draft.showFPS=!draft.showFPS; break;
    case 6105: case 6106: case 6107:
        if(auto slider=menu->GetSliderData(6005)) draft.touchOpacity=slider->value;
        break;
    default: return false;
    }
    RefreshMenu(menu); return true;
}
bool ApplySettings() {
    bool resize=settings.layout!=draft.layout || settings.large!=draft.large;
    settings=draft;
    std::ofstream out("android-settings.cfg.tmp",std::ios::trunc);
    out << "layout "<<settings.layout<<"\nlarge "<<settings.large<<"\nfps "<<settings.fps
        <<"\ntouch "<<settings.touch<<"\nshowFPS "<<settings.showFPS
        <<"\ntouchOpacity "<<settings.touchOpacity<<"\n";
    out.close();
    if (!out.fail()) std::rename("android-settings.cfg.tmp","android-settings.cfg");
    else SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Could not save Android display settings");
    return resize;
}
void ConfigureMenu(NC_STACK_button *menu) {
    int contentWidth=GFX::Engine.GetScreenW()-2*menu->getBTN_x();
    int step=20, musicColumn=contentWidth/2;
    for(const auto &b:menu->field_d8) if(b.button_id==1164) musicColumn=b.xpos;
    for (const auto &b: menu->field_d8) if(b.button_id==1156) step=b.ypos/5;
    // Remove old explanatory text, desktop resolution/device selectors and the
    // unsupported window/cursor/16-bit choices; keep original audio/game controls.
    for(int i=int(menu->field_d8.size())-1;i>=0;--i) {
        const auto &b=menu->field_d8[i];
        int row=(b.ypos+step/2)/step;
        bool remove=row<=6 || row==8 || (row==9 && b.xpos<musicColumn);
        if(remove) {
            if(b.field_41C) delete b.field_41C;
            menu->field_d8.erase(menu->field_d8.begin()+i);
            menu->buttons.erase(menu->buttons.begin()+i);
            --menu->idd;
        }
    }
    auto add=[&](int id,int row,const std::string &text,bool button) {
        NC_STACK_button::button_64_arg b={};
        b.button_type=button ? NC_STACK_button::TYPE_BUTTON : NC_STACK_button::TYPE_CAPTION;
        b.button_id=id; b.xpos=button ? contentWidth/2 : 0;
        b.ypos=row*step; b.width=button ? contentWidth/2 : contentWidth/2-6;
        b.caption=text; b.caption2=text;
        b.upCode=button ? id+100 : 0;
        b.flags=NC_STACK_button::FLAG_TEXT | (button ? NC_STACK_button::FLAG_BORDER|NC_STACK_button::FLAG_CENTER : 0);
        b.tileset_up=button ? 18 : 16; b.tileset_down=button ? 19 : 16; b.field_3A=button ? 30 : 16;
        b.txt_r=button ? 220 : 80; b.txt_g=button ? 240 : 190; b.txt_b=button ? 240 : 190;
        menu->Add(&b);
    };
    add(6050,0,"ANDROID DISPLAY",false);
    const char *labels[]={"Screen layout","Interface size","Frame rate","Touch controls","Show FPS"};
    for(int i=0;i<5;++i) { add(6060+i,i+1,labels[i],false); add(6000+i,i+1,"...",true); }
    add(6065,6,"Touch opacity",false);
    NC_STACK_button::Slider opacity={};
    opacity.min=10; opacity.max=100; opacity.value=draft.touchOpacity;
    NC_STACK_button::button_64_arg b={};
    b.button_type=NC_STACK_button::TYPE_SLIDER; b.button_id=6005;
    b.xpos=contentWidth/2; b.ypos=6*step; b.width=contentWidth/2;
    b.caption=" "; b.tileset_up=18; b.tileset_down=18; b.field_3A=30;
    b.downCode=6105; b.pressedCode=6106; b.upCode=6107; b.field_34=&opacity;
    menu->Add(&b);
    BeginSettings(menu);
}
void SetMission(bool value) { mission=value; }
bool InMission() { return mission; }
int FrameRate() { return settings.fps; }
void SetControllerPresent(bool present) { controller=present; }
int TouchOpacity() { return settings.touchOpacity; }
bool TouchEnabled() { return mission && (settings.touch==1 || (settings.touch==0 && !controller)); }
bool TouchVisible() { return TouchEnabled() && !UAAndroidUnitMenuIsOpen() && !(mission && UAAndroidModalIsOpen()); }
SDL_Rect Viewport() {
    ReadInsets();
    auto size=System::GetResolution();
    auto r=AndroidDisplay::Fit(size.x,size.y,safe[0],safe[1],safe[2],safe[3],mission && !settings.layout);
    return SDL_Rect{r.x,r.y,r.w,r.h};
}
Common::Point LogicalSize() {
    SDL_Rect r=Viewport();
    int height=settings.large ? 400 : 480;
    return Common::Point(AndroidDisplay::LogicalWidth({r.x,r.y,r.w,r.h},height),height);
}
Common::Point WindowPoint(Common::Point point) {
    SDL_Rect r=Viewport(); auto size=GFX::Engine.GetScreenSize();
    return Common::Point(r.x+point.x*r.w/std::max(size.x,1),r.y+point.y*r.h/std::max(size.y,1));
}
Common::Point LogicalPoint(Common::Point point) {
    SDL_Rect r=Viewport(); auto size=GFX::Engine.GetScreenSize();
    return Common::Point(AndroidDisplay::MapAxis(point.x,r.x,r.w,size.x),AndroidDisplay::MapAxis(point.y,r.y,r.h,size.y));
}
bool InsideViewport(int x,int y) { auto r=Viewport(); return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h; }
void SetViewport() { auto r=Viewport(); auto size=System::GetResolution(); glViewport(r.x,size.y-r.y-r.h,r.w,r.h); }
void ReportFPS(float fps) { measuredFPS=fps; }
void DrawOverlay(SDL_Surface *screen) {
    NC_STACK_winp::AndroidDrawTouchControls(screen);
    if(!settings.showFPS) return;
    static std::unique_ptr<TTF_Font,decltype(&TTF_CloseFont)> font(System::LoadFont("Liberation Sans",14),TTF_CloseFont);
    static std::shared_ptr<SDL_Surface> label;
    static int old=-1;
    int fps=int(measuredFPS+0.5f);
    if(font && fps!=old) {
        char text[32]; if(fps) std::snprintf(text,sizeof(text),"%d FPS",fps); else std::snprintf(text,sizeof(text),"-- FPS");
        label.reset(TTF_RenderUTF8_Blended(font.get(),text,SDL_Color{255,255,255,255}),SDL_FreeSurface);
        old=fps;
    }
    if(label) { SDL_Rect rect{screen->w-label->w-8,8,label->w,label->h}; SDL_FillRect(screen,&rect,SDL_MapRGBA(screen->format,0,0,0,210)); SDL_BlitSurface(label.get(),nullptr,screen,&rect); }
}
}
#endif
