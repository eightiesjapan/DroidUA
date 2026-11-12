#ifndef UA_ANDROID_TOUCHSPRITES_H
#define UA_ANDROID_TOUCHSPRITES_H
#include <cstdint>
#include <string>
#include <vector>

namespace AndroidTouchSprites {
// Premultiplied RGBA8 image (bytes R,G,B,A).
struct Sprite { int w = 0, h = 0; std::vector<uint8_t> rgba; };

// Touch control face at native pixel size, matching the layout editor's look:
// dark rim, steel edge, steel-blue bevel gradient (brighter when held), a cyan
// glow arc along the bottom, and for analog pads a recessed centre well.
// `radius` and `scale` are in native pixels / native pixels per logical pixel.
Sprite Control(float radius, float scale, bool pad, bool held);
// Vertical MOVE slider: capsule of half-width `halfW` and half-height
// `halfH` (native px) with a recessed groove and a cyan centre mark.
Sprite Slider(float halfW, float halfH, float scale, bool held);
// Map resize grip: steel plate with diagonal cyan ridges, `size` native px.
Sprite Grip(float size, float scale);
// Draggable knob of an analog pad.
Sprite Knob(float scale);
// Label in Xolonium Bold capitals with a black outline and soft shadow, sized
// to fit `maxWidth` native pixels (largest size up to 13 logical px).
Sprite Label(const std::string &text, float scale, float maxWidth);
}
#endif
