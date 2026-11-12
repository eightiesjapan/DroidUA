#include "android_touchsprites.h"
#include "system.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace AndroidTouchSprites {
namespace {
struct Canvas {
    int w, h;
    std::vector<float> p; // premultiplied RGBA floats 0..1
    Canvas(int w_, int h_) : w(w_), h(h_), p(size_t(w_) * h_ * 4, 0.0f) {}
    // "over" composite of a straight colour with coverage a
    void Over(int x, int y, float r, float g, float b, float a) {
        if (a <= 0.0f || x < 0 || y < 0 || x >= w || y >= h) return;
        float *d = &p[(size_t(y) * w + x) * 4];
        d[0] = r * a + d[0] * (1 - a);
        d[1] = g * a + d[1] * (1 - a);
        d[2] = b * a + d[2] * (1 - a);
        d[3] = a + d[3] * (1 - a);
    }
    Sprite Finish() const {
        Sprite s; s.w = w; s.h = h; s.rgba.resize(p.size());
        for (size_t i = 0; i < p.size(); ++i)
            s.rgba[i] = uint8_t(std::max(0.0f, std::min(1.0f, p[i])) * 255.0f + 0.5f);
        return s;
    }
};

inline float Cover(float edge, float d) { return std::max(0.0f, std::min(1.0f, edge - d + 0.5f)); }
inline float C(int v) { return v / 255.0f; }

template <class ColorFn>
void Disc(Canvas &c, float cx, float cy, float radius, ColorFn color)
{
    if (radius <= 0) return;
    const int x0 = std::max(0, int(cx - radius - 2)), x1 = std::min(c.w - 1, int(cx + radius + 2));
    const int y0 = std::max(0, int(cy - radius - 2)), y1 = std::min(c.h - 1, int(cy + radius + 2));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float a = Cover(radius, std::sqrt(dx * dx + dy * dy));
            if (a > 0) { float r, g, b; color(y + 0.5f, r, g, b); c.Over(x, y, r, g, b, a); }
        }
}

auto Solid(int r, int g, int b) { return [=](float, float &R, float &G, float &B) { R = C(r); G = C(g); B = C(b); }; }

// Bevel gradient top->bottom over the disc's vertical extent.
auto Bevel(float cy, float radius, bool held)
{
    const int top[3] = {held ? 105 : 78, held ? 150 : 112, held ? 162 : 120};
    const int bot[3] = {held ? 43 : 28, held ? 74 : 42, held ? 82 : 48};
    return [=](float y, float &R, float &G, float &B) {
        float t = std::max(0.0f, std::min(1.0f, (y - (cy - radius)) / std::max(1.0f, 2 * radius)));
        R = C(int(top[0] + (bot[0] - top[0]) * t));
        G = C(int(top[1] + (bot[1] - top[1]) * t));
        B = C(int(top[2] + (bot[2] - top[2]) * t));
    };
}

// Cyan glow arc along the bottom (30..150 degrees, y pointing down), like the editor.
void GlowArc(Canvas &c, float cx, float cy, float radius, float width)
{
    const float pi = 3.14159265f;
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            float a = Cover(width * 0.5f, std::fabs(d - radius));
            if (a <= 0) continue;
            const float ang = std::atan2(dy, dx) * 180.0f / pi; // 90 = straight down
            const float fade = std::max(0.0f, std::min(1.0f, std::min(ang - 30.0f, 150.0f - ang) / 4.0f));
            c.Over(x, y, C(60), C(150), C(165), a * fade);
        }
}

// Same as Disc but for a vertical capsule (straight part of half-length `st`).
template <class ColorFn>
void Capsule(Canvas &c, float cx, float cy, float radius, float st, ColorFn color)
{
    if (radius <= 0) return;
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) {
            const float dx = x + 0.5f - cx, dy = std::max(0.0f, std::fabs(y + 0.5f - cy) - st);
            const float a = Cover(radius, std::sqrt(dx * dx + dy * dy));
            if (a > 0) { float r, g, b; color(y + 0.5f, r, g, b); c.Over(x, y, r, g, b, a); }
        }
}

struct LabelFont { TTF_Font *fill = nullptr, *outline = nullptr; int outlinePx = 0; };
LabelFont &FontFor(int size, int outlinePx)
{
    static std::map<std::pair<int, int>, LabelFont> fonts;
    LabelFont &f = fonts[{size, outlinePx}];
    if (!f.fill) {
        f.fill = System::LoadFont("Xolonium Bold", size);
        if (!f.fill) f.fill = System::LoadFont("Liberation Sans", size);
        f.outline = System::LoadFont("Xolonium Bold", size);
        if (!f.outline) f.outline = System::LoadFont("Liberation Sans", size);
        if (f.outline) TTF_SetFontOutline(f.outline, outlinePx);
        if (f.fill) TTF_SetFontHinting(f.fill, TTF_HINTING_LIGHT);
        if (f.outline) TTF_SetFontHinting(f.outline, TTF_HINTING_LIGHT);
        f.outlinePx = outlinePx;
    }
    return f;
}

// Composite a blended TTF surface (straight alpha) tinted with `rgb` at (ox,oy).
void Stamp(Canvas &c, SDL_Surface *src, int ox, int oy, float r, float g, float b, float alpha)
{
    SDL_Surface *s = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
    if (!s) return;
    SDL_LockSurface(s);
    for (int y = 0; y < s->h; ++y) {
        const uint8_t *row = static_cast<const uint8_t *>(s->pixels) + size_t(y) * s->pitch;
        for (int x = 0; x < s->w; ++x)
            c.Over(ox + x, oy + y, r, g, b, row[x * 4 + 3] / 255.0f * alpha);
    }
    SDL_UnlockSurface(s);
    SDL_FreeSurface(s);
}
} // namespace

Sprite Control(float radius, float scale, bool pad, bool held)
{
    const int size = int(std::ceil(radius * 2)) + 4;
    Canvas c(size, size);
    const float cx = size / 2.0f, cy = size / 2.0f;
    Disc(c, cx, cy, radius, Solid(8, 14, 18));                       // dark rim
    Disc(c, cx, cy, radius - 1.0f * scale, Solid(120, 153, 159));    // steel edge
    Disc(c, cx, cy, radius - 2.0f * scale, Bevel(cy, radius, held)); // bevel body
    if (pad) Disc(c, cx, cy, radius * 0.55f, Solid(10, 22, 28));     // recessed well
    GlowArc(c, cx, cy, radius - 2.0f * scale, std::max(1.5f, 1.4f * scale));
    return c.Finish();
}

Sprite Slider(float halfW, float halfH, float scale, bool held)
{
    const int w = int(std::ceil(halfW * 2)) + 4, h = int(std::ceil(halfH * 2)) + 4;
    Canvas c(w, h);
    const float cx = w / 2.0f, cy = h / 2.0f, st = std::max(0.0f, halfH - halfW);
    Capsule(c, cx, cy, halfW, st, Solid(8, 14, 18));                       // dark rim
    Capsule(c, cx, cy, halfW - 1.0f * scale, st, Solid(120, 153, 159));    // steel edge
    Capsule(c, cx, cy, halfW - 2.0f * scale, st, Bevel(cy, halfH, held));  // bevel body
    Capsule(c, cx, cy, halfW * 0.32f, st, Solid(10, 22, 28));              // groove
    for (int y = int(cy - 0.7f * scale); y <= int(cy + 0.7f * scale); ++y)  // centre (neutral) mark
        for (int x = int(cx - halfW * 0.55f); x <= int(cx + halfW * 0.55f); ++x)
            c.Over(x, y, C(60), C(150), C(165), 0.9f);
    GlowArc(c, cx, cy + st, halfW - 2.0f * scale, std::max(1.5f, 1.4f * scale));
    return c.Finish();
}

Sprite Grip(float size, float scale)
{
    const int n = int(std::ceil(size)) + 2;
    Canvas c(n, n);
    const float r = 4.0f * scale; // rounded corner
    auto inside = [&](float x, float y, float inset) {
        const float x0 = inset, y0 = inset, x1 = size - inset, y1 = size - inset;
        const float qx = std::max(std::max(x0 + r - x, x - (x1 - r)), 0.0f);
        const float qy = std::max(std::max(y0 + r - y, y - (y1 - r)), 0.0f);
        return Cover(r, std::sqrt(qx * qx + qy * qy));
    };
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            c.Over(x, y, C(8), C(14), C(18), inside(px, py, 0));
            c.Over(x, y, C(120), C(153), C(159), inside(px, py, 1.0f * scale));
            const float t = py / size;
            c.Over(x, y, C(int(78 - 50 * t)), C(int(112 - 70 * t)), C(int(120 - 72 * t)), inside(px, py, 2.0f * scale));
            // three diagonal ridges toward the bottom-right corner
            const float d = (px + py) / size; // 0 top-left .. 2 bottom-right
            for (float k : {1.25f, 1.5f, 1.75f}) {
                const float dist = std::fabs(d - k) * size / 1.4142f;
                const float a = Cover(0.9f * scale, dist) * inside(px, py, 3.0f * scale);
                c.Over(x, y, C(109), C(220), C(230), a);
            }
        }
    return c.Finish();
}

Sprite Knob(float scale)
{
    const float r = 13.0f * scale;
    const int size = int(std::ceil(r * 2)) + 4;
    Canvas c(size, size);
    const float cx = size / 2.0f, cy = size / 2.0f;
    Disc(c, cx, cy, r, Solid(8, 14, 18));
    Disc(c, cx, cy, r - 1.0f * scale, Solid(120, 153, 159));
    Disc(c, cx, cy, r - 2.0f * scale, Bevel(cy, r, true));
    return c.Finish();
}

Sprite Label(const std::string &text, float scale, float maxWidth)
{
    const int outlinePx = std::max(1, int(std::lround(scale * 0.9f)));
    const int shadowPx = std::max(1, int(std::lround(scale)));
    for (int logical = 13; logical >= 7; --logical) {
        const int size = std::max(6, int(std::lround(logical * scale)));
        LabelFont &f = FontFor(size, outlinePx);
        if (!f.fill || !f.outline) continue;
        int w = 0, h = 0;
        TTF_SizeUTF8(f.fill, text.c_str(), &w, &h);
        if (w + 2 * outlinePx > maxWidth && logical > 7) continue;
        SDL_Surface *bg = TTF_RenderUTF8_Blended(f.outline, text.c_str(), SDL_Color{255, 255, 255, 255});
        SDL_Surface *fg = TTF_RenderUTF8_Blended(f.fill, text.c_str(), SDL_Color{255, 255, 255, 255});
        Sprite out;
        if (bg && fg) {
            Canvas c(bg->w + shadowPx, bg->h + shadowPx);
            Stamp(c, bg, shadowPx, shadowPx, 0, 0, 0, 0.55f);                 // soft drop shadow
            Stamp(c, bg, 0, 0, 0, 0, 0, 1.0f);                                // black outline
            Stamp(c, fg, outlinePx, outlinePx, C(236), C(250), C(252), 1.0f); // bright fill
            out = c.Finish();
        }
        if (bg) SDL_FreeSurface(bg);
        if (fg) SDL_FreeSurface(fg);
        return out;
    }
    return Sprite();
}
}
