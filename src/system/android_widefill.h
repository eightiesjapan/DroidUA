#ifndef UA_ANDROID_WIDEFILL_H
#define UA_ANDROID_WIDEFILL_H
#include <cstdint>
#include <vector>
#include <SDL2/SDL.h>

namespace AndroidWideFill {
// "Console rails" widescreen fill (approved DroidUA 0.6 design).
// Builds a W x 480 RGBA8 image (byte order R,G,B,A) whose centre 640 columns,
// starting at column `left`, are the original 640x480 menu background and whose
// sides are composed from that background: blurred edge colours, the blueprint
// grid, a mirrored machinery ghost, bevelled steel rails with bolts, a cyan glow
// line, vignette and scanlines. `src` may be any size/format; it is scaled to 640x480.
std::vector<uint8_t> BuildConsoleRails(SDL_Surface *src, int W, int left);
// Briefing/debriefing design ("Side monitors"): the briefing's empty grid plate
// continues outward and each side gets an extra framed console monitor, 9-slice
// cut from the original right-hand panel (frame, bevels and bolts intact),
// dimmed as an idle screen with a cyan status line.
std::vector<uint8_t> BuildSideMonitors(SDL_Surface *src, int W, int left);
}
#endif
