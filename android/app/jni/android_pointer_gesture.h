#ifndef OPENUA_ANDROID_POINTER_GESTURE_H
#define OPENUA_ANDROID_POINTER_GESTURE_H
#include <cmath>
#include <cstdint>

// R1 is a click on release, or a captured map drag. Independent of SDL so
// timing, cancellation and map-only capture can be tested without a device.
class AndroidPointerGesture {
public:
    struct Result {
        bool toggle = false, down = false, hold = false, up = false;
        int x = 0, y = 0;
    };
    Result Update(bool enabled, bool held, bool overMap, bool mapOpen,
                  int x, int y, float stickX, float stickY, float dt, uint32_t now) {
        Result out;
        out.x = x;
        out.y = y;
        if (!enabled || (active && captureMap && !mapOpen)) {
            out.up = dragging;
            out.x = dragX;
            out.y = dragY;
            active = dragging = false;
            // A held button cannot start a new gesture after cancellation.
            blocked = held;
            return out;
        }
        if (blocked) {
            if (!held) blocked = false;
            return out;
        }
        if (held && !active) {
            active = true;
            captureMap = overMap;
            startX = x;
            startY = y;
            dragX = x;
            dragY = y;
            offsetX = offsetY = 0;
            started = now;
            moved = false;
        }
        if (!active) return out;
        const bool cursorMoved = std::abs(x - startX) > 3 || std::abs(y - startY) > 3;
        const bool stickMoved = captureMap && (stickX != 0 || stickY != 0);
        moved = moved || cursorMoved || stickMoved;
        if (held && captureMap && moved) {
            out.down = !dragging;
            dragging = true;
            // The first frame anchors the engine's existing drag at the press.
            if (!out.down) {
                offsetX += stickX * 420.0f * dt;
                offsetY += stickY * 420.0f * dt;
                dragX = x + int(offsetX);
                dragY = y + int(offsetY);
            } else {
                dragX = startX;
                dragY = startY;
            }
            out.hold = true;
        }
        if (!held) {
            out.up = dragging;
            out.toggle = !moved && !dragging && uint32_t(now - started) < 300;
            active = false;
            dragging = false;
        }
        if (out.hold || out.up) {
            out.x = dragX;
            out.y = dragY;
        }
        return out;
    }
private:
    bool active = false, dragging = false, captureMap = false, moved = false, blocked = false;
    int startX = 0, startY = 0, dragX = 0, dragY = 0;
    float offsetX = 0, offsetY = 0;
    uint32_t started = 0;
};
#endif
