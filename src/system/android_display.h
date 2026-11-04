#ifndef UA_ANDROID_DISPLAY_H
#define UA_ANDROID_DISPLAY_H
#ifdef __ANDROID__
#include "common/common.h"
#include <SDL2/SDL.h>
class NC_STACK_button;
namespace AndroidTouch { struct Config; }
namespace Android {
struct Settings { int layout=0, large=0, fps=60, touch=0, showFPS=0, touchOpacity=75; };
void LoadSettings();
void ReloadTouchLayout();
const AndroidTouch::Config &TouchConfiguration();
void BeginSettings(NC_STACK_button *menu);
bool CycleSetting(int event, NC_STACK_button *menu);
bool ApplySettings();
void ConfigureMenu(NC_STACK_button *menu);
void SetMission(bool mission);
bool InMission();
int FrameRate();
int TouchOpacity();
bool TouchEnabled();
// Rendering and overlay hit targets are suspended while the unit chooser is open.
bool TouchVisible();
void SetControllerPresent(bool present);
SDL_Rect Viewport();
Common::Point LogicalSize();
Common::Point WindowPoint(Common::Point logical);
Common::Point LogicalPoint(Common::Point window);
bool InsideViewport(int x, int y);
void SetViewport();
void ReportFPS(float fps);
void DrawOverlay(SDL_Surface *screen);
}
#endif
#endif
