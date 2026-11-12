#include "android_widefill.h"
#include <algorithm>
#include <cmath>

namespace AndroidWideFill {
namespace {
constexpr int SW = 640, H = 480;
constexpr float GRID = 20.65f;

struct Img {
    int w = 0, h = 0;
    std::vector<float> p; // RGB floats
    Img(int w_, int h_) : w(w_), h(h_), p(size_t(w_) * h_ * 3, 0.0f) {}
    float *at(int x, int y) { return &p[(size_t(y) * w + x) * 3]; }
};

// Separable box blur, repeated `passes` times (3 passes ~ gaussian).
void BoxBlur(Img &im, int radius, int passes)
{
    if (radius <= 0) return;
    std::vector<float> line(std::max(im.w, im.h) * 3);
    for (int pass = 0; pass < passes; ++pass) {
        for (int y = 0; y < im.h; ++y) {               // horizontal
            for (int x = 0; x < im.w; ++x) std::copy_n(im.at(x, y), 3, &line[x * 3]);
            for (int x = 0; x < im.w; ++x) {
                float s[3] = {0, 0, 0}; int n = 0;
                for (int k = std::max(0, x - radius); k <= std::min(im.w - 1, x + radius); ++k, ++n)
                    for (int c = 0; c < 3; ++c) s[c] += line[k * 3 + c];
                for (int c = 0; c < 3; ++c) im.at(x, y)[c] = s[c] / n;
            }
        }
        for (int x = 0; x < im.w; ++x) {               // vertical
            for (int y = 0; y < im.h; ++y) std::copy_n(im.at(x, y), 3, &line[y * 3]);
            // running sum for speed on the tall axis
            float s[3] = {0, 0, 0};
            int lo = 0, hi = -1;
            for (int y = 0; y < im.h; ++y) {
                int nlo = std::max(0, y - radius), nhi = std::min(im.h - 1, y + radius);
                while (hi < nhi) { ++hi; for (int c = 0; c < 3; ++c) s[c] += line[hi * 3 + c]; }
                while (lo < nlo) { for (int c = 0; c < 3; ++c) s[c] -= line[lo * 3 + c]; ++lo; }
                const float n = float(hi - lo + 1);
                for (int c = 0; c < 3; ++c) im.at(x, y)[c] = s[c] / n;
            }
        }
    }
}

// Bilinear resample of columns [x0,x0+cw) of the source into a w x H image.
Img StretchColumns(const uint8_t *src, int pitch, int x0, int cw, int w)
{
    Img out(w, H);
    for (int y = 0; y < H; ++y) {
        const uint8_t *row = src + size_t(y) * pitch;
        for (int x = 0; x < w; ++x) {
            float fx = (x + 0.5f) * cw / float(w) - 0.5f;
            fx = std::max(0.0f, std::min(float(cw - 1), fx));
            int i0 = int(fx), i1 = std::min(cw - 1, i0 + 1);
            float t = fx - i0;
            const uint8_t *a = row + (x0 + i0) * 4, *b = row + (x0 + i1) * 4;
            for (int c = 0; c < 3; ++c) out.at(x, y)[c] = a[c] * (1 - t) + b[c] * t;
        }
    }
    return out;
}

void Blend(float *px, const float col[3], float alpha)
{
    for (int c = 0; c < 3; ++c) px[c] = px[c] * (1 - alpha) + col[c] * alpha;
}

float Noise(int y)
{
    uint32_t h = uint32_t(y) * 2654435761u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (float(h & 0xffff) / 65535.0f - 0.5f) * 12.0f; // about N(0,3.5) in range
}

void Rail(Img &im, int x0, int rw, bool flip)
{
    const float light[3] = {120, 153, 159}, mid[3] = {70, 99, 110}, dark[3] = {20, 32, 38};
    const int hiCol = int(rw * 0.18f);
    for (int i = 0; i < rw; ++i) {
        int col = flip ? rw - 1 - i : i;
        float t = rw > 1 ? float(i) / float(rw - 1) : 0.0f;
        float prof = t < 0.18f ? t / 0.18f : (t > 0.75f ? (1 - t) / 0.25f : 1.0f);
        float base[3];
        for (int c = 0; c < 3; ++c) base[c] = (i == hiCol) ? light[c] : dark[c] + (mid[c] - dark[c]) * prof;
        int x = x0 + col;
        if (x < 0 || x >= im.w) continue;
        for (int y = 0; y < im.h; ++y) {
            float n = Noise(y);
            for (int c = 0; c < 3; ++c) im.at(x, y)[c] = base[c] + n;
        }
    }
}

void Bolt(Img &im, int cx, int cy, int r = 5)
{
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r * r) continue;
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= im.w || y >= im.h) continue;
            float shade = std::max(0.25f, std::min(1.4f, 1.0f - (dx + dy) / (2.2f * r)));
            float *p = im.at(x, y);
            p[0] = 92 * shade; p[1] = 120 * shade; p[2] = 128 * shade;
        }
}

void GlowLine(Img &im, int x, int y0, int y1)
{
    const float glow[3] = {60, 150, 165};
    for (int y = std::max(0, y0); y < std::min(im.h, y1); ++y) {
        if (x >= 0 && x < im.w) Blend(im.at(x, y), glow, 0.85f);
        if (x + 1 >= 0 && x + 1 < im.w) Blend(im.at(x + 1, y), glow, 0.30f);
    }
}
} // namespace

std::vector<uint8_t> BuildConsoleRails(SDL_Surface *src, int W, int left)
{
    std::vector<uint8_t> out;
    if (!src || W < SW) return out;
    left = std::max(0, std::min(W - SW, left));
    const int right = W - SW - left;

    // Normalise the source to 640x480 RGBA32 (bytes R,G,B,A).
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
    if (!conv) return out;
    SDL_Surface *norm = conv;
    if (conv->w != SW || conv->h != H) {
        norm = SDL_CreateRGBSurfaceWithFormat(0, SW, H, 32, SDL_PIXELFORMAT_RGBA32);
        if (norm) {
            SDL_SetSurfaceBlendMode(conv, SDL_BLENDMODE_NONE);
            SDL_BlitScaled(conv, nullptr, norm, nullptr);
        }
        SDL_FreeSurface(conv);
        if (!norm) return out;
    }
    SDL_LockSurface(norm);
    const uint8_t *pix = static_cast<const uint8_t *>(norm->pixels);
    const int pitch = norm->pitch;

    Img im(W, H);
    // Centre: original art.
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < SW; ++x) {
            const uint8_t *s = pix + size_t(y) * pitch + x * 4;
            float *d = im.at(left + x, y);
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }

    auto side = [&](int x0, int sw, bool isLeft) {
        if (sw <= 0) return;
        // 1. heavily blurred stretch of the art's own edge colours
        Img e = StretchColumns(pix, pitch, isLeft ? 0 : SW - 48, 48, sw);
        BoxBlur(e, 14, 3);
        // 2. mirrored machinery ghost (outer columns mirrored outward)
        const int mw = std::min(sw, SW);
        for (int x = 0; x < sw; ++x) {
            // brightness ramp 0.25 (outer) .. 0.55 (next to the art)
            float t = sw > 1 ? float(x) / float(sw - 1) : 1.0f;      // 0 at left edge of this strip
            float tin = isLeft ? t : 1.0f - t;                        // 0 outer .. 1 inner
            float amb = 0.25f + 0.30f * std::pow(tin, 1.2f);
            float ghostA = 0.45f * std::pow(tin, 0.7f);
            // distance from the art edge, in columns
            int dist = isLeft ? (sw - 1 - x) : x;
            int sx = isLeft ? dist : SW - 1 - dist;                   // mirrored source column
            bool hasGhost = dist < mw;
            for (int y = 0; y < H; ++y) {
                float *d = im.at(x0 + x, y);
                const float *a = e.at(x, y);
                for (int c = 0; c < 3; ++c) d[c] = a[c] * amb;
                if (hasGhost) {
                    const uint8_t *g = pix + size_t(y) * pitch + sx * 4;
                    for (int c = 0; c < 3; ++c) d[c] = std::max(d[c], g[c] * ghostA);
                }
            }
        }
        // 3. blueprint grid continuing the original pitch
        const float gridCol[3] = {52, 92, 100};
        const float phase = std::fmod(float(left), GRID);
        for (float gx = phase; gx < W; gx += GRID) {
            int xi = int(std::lround(gx));
            if (xi < x0 || xi >= x0 + sw) continue;
            for (int y = 0; y < H; ++y) Blend(im.at(xi, y), gridCol, 0.30f);
        }
        for (float gy = phase; gy < H; gy += GRID) {
            int yi = int(std::lround(gy));
            if (yi < 0 || yi >= H) continue;
            for (int x = x0; x < x0 + sw; ++x) Blend(im.at(x, yi), gridCol, 0.30f);
        }
    };
    side(0, left, true);
    side(left + SW, right, false);

    // 4. steel rails, bolts and glow lines framing the original picture
    const int rw = 14;
    if (left >= rw) {
        Rail(im, left - rw, rw, false);
        for (int y = 18; y < H; y += 92) Bolt(im, left - rw + rw / 2, y);
        if (left >= rw + 4) GlowLine(im, left - rw - 3, 30, H - 30);
    }
    if (right >= rw) {
        Rail(im, left + SW, rw, true);
        for (int y = 18; y < H; y += 92) Bolt(im, left + SW + rw / 2, y);
        if (right >= rw + 3) GlowLine(im, left + SW + rw + 1, 30, H - 30);
    }

    SDL_UnlockSurface(norm);
    SDL_FreeSurface(norm);

    // 5. scanlines + vignette on the side areas only (centre stays untouched)
    out.resize(size_t(W) * H * 4);
    for (int y = 0; y < H; ++y) {
        float ny = (y - H / 2.0f) / (H / 2.0f);
        for (int x = 0; x < W; ++x) {
            float *p = im.at(x, y);
            float k = 1.0f;
            bool centre = x >= left && x < left + SW;
            if (!centre) {
                float nx = (x - W / 2.0f) / (W / 2.0f);
                k = 1.0f - 0.40f * std::max(0.0f, std::min(1.0f, nx * nx * 0.55f + ny * ny * 0.9f - 0.25f));
                if (y & 1) k *= 0.95f;
            }
            uint8_t *o = &out[(size_t(y) * W + x) * 4];
            for (int c = 0; c < 3; ++c) o[c] = uint8_t(std::max(0.0f, std::min(255.0f, p[c] * k)));
            o[3] = 255;
        }
    }
    return out;
}

namespace {
// Converts any surface to a locked 640x480 RGBA32 copy; caller frees it.
SDL_Surface *Normalise(SDL_Surface *src)
{
    SDL_Surface *conv = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
    if (!conv) return nullptr;
    if (conv->w == SW && conv->h == H) return conv;
    SDL_Surface *norm = SDL_CreateRGBSurfaceWithFormat(0, SW, H, 32, SDL_PIXELFORMAT_RGBA32);
    if (norm) {
        SDL_SetSurfaceBlendMode(conv, SDL_BLENDMODE_NONE);
        SDL_BlitScaled(conv, nullptr, norm, nullptr);
    }
    SDL_FreeSurface(conv);
    return norm;
}

// Bilinear sample of a source rectangle into a destination rectangle of im.
void Resample(const uint8_t *pix, int pitch, int sx, int sy, int sw, int sh, Img &im, int dx, int dy, int dw, int dh, float gain)
{
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    for (int y = 0; y < dh; ++y) {
        float fy = (y + 0.5f) * sh / float(dh) - 0.5f;
        fy = std::max(0.0f, std::min(float(sh - 1), fy));
        int y0 = int(fy), y1 = std::min(sh - 1, y0 + 1); float ty = fy - y0;
        for (int x = 0; x < dw; ++x) {
            float fx = (x + 0.5f) * sw / float(dw) - 0.5f;
            fx = std::max(0.0f, std::min(float(sw - 1), fx));
            int x0 = int(fx), x1 = std::min(sw - 1, x0 + 1); float tx = fx - x0;
            int ox = dx + x, oy = dy + y;
            if (ox < 0 || oy < 0 || ox >= im.w || oy >= im.h) continue;
            const uint8_t *a = pix + size_t(sy + y0) * pitch + (sx + x0) * 4;
            const uint8_t *b = pix + size_t(sy + y0) * pitch + (sx + x1) * 4;
            const uint8_t *c = pix + size_t(sy + y1) * pitch + (sx + x0) * 4;
            const uint8_t *d = pix + size_t(sy + y1) * pitch + (sx + x1) * 4;
            float *o = im.at(ox, oy);
            for (int k = 0; k < 3; ++k)
                o[k] = gain * ((a[k] * (1 - tx) + b[k] * tx) * (1 - ty) + (c[k] * (1 - tx) + d[k] * tx) * ty);
        }
    }
}
}

std::vector<uint8_t> BuildSideMonitors(SDL_Surface *src, int W, int left)
{
    std::vector<uint8_t> out;
    if (!src || W < SW) return out;
    left = std::max(0, std::min(W - SW, left));
    const int right = W - SW - left;
    SDL_Surface *norm = Normalise(src);
    if (!norm) return out;
    SDL_LockSurface(norm);
    const uint8_t *pix = static_cast<const uint8_t *>(norm->pixels);
    const int pitch = norm->pitch;

    Img im(W, H);
    // Background plate: an empty stretch of the briefing's grid console
    // (bottom-left area, 120x120), tiled across the sides, slightly darker.
    const int tx0 = 30, ty0 = 330, tw = 120, th = 120;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (x >= left && x < left + SW) continue;
            int px = (x % tw), py = (y % th);
            const uint8_t *s = pix + size_t(ty0 + py) * pitch + (tx0 + px) * 4;
            float *d = im.at(x, y);
            for (int k = 0; k < 3; ++k) d[k] = s[k] * 0.8f;
        }
    // Centre: original art.
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < SW; ++x) {
            const uint8_t *s = pix + size_t(y) * pitch + x * 4;
            float *d = im.at(left + x, y);
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    // Side monitors: 9-slice of the right-hand framed panel (x 355..640, y 28..452).
    const int px0 = 355, py0 = 28, pw0 = SW - 355, ph0 = 452 - 28, b = 14;
    auto monitor = [&](int x0, int pw) {
        if (pw <= 60) return;
        const int mh = ph0, my = py0;
        const int xs[4] = {0, b, pw0 - b, pw0}, xd[4] = {0, b, pw - b, pw};
        const int ys[4] = {0, b, ph0 - b, ph0}, yd[4] = {0, b, mh - b, mh};
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) {
                const float gain = (i == 1 && j == 1) ? 0.55f : 1.0f; // idle, dimmed screen
                Resample(pix, pitch, px0 + xs[i], py0 + ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j],
                         im, x0 + xd[i], my + yd[j], xd[i + 1] - xd[i], yd[j + 1] - yd[j], gain);
            }
        // cyan status line across the idle screen
        const float glow[3] = {60, 150, 165};
        for (int y = 240; y < 242; ++y)
            for (int x = x0 + 20; x < x0 + pw - 20; ++x)
                if (x >= 0 && x < W) Blend(im.at(x, y), glow, 0.7f);
    };
    monitor(8, left - 18);
    monitor(left + SW + 10, right - 18);

    SDL_UnlockSurface(norm);
    SDL_FreeSurface(norm);

    // Gentle vignette on the sides only (centre stays untouched).
    out.resize(size_t(W) * H * 4);
    for (int y = 0; y < H; ++y) {
        float ny = (y - H / 2.0f) / (H / 2.0f);
        for (int x = 0; x < W; ++x) {
            float *p = im.at(x, y);
            float k = 1.0f;
            if (!(x >= left && x < left + SW)) {
                float nx = (x - W / 2.0f) / (W / 2.0f);
                k = 1.0f - 0.30f * std::max(0.0f, std::min(1.0f, nx * nx * 0.55f + ny * ny * 0.9f - 0.25f));
            }
            uint8_t *o = &out[(size_t(y) * W + x) * 4];
            for (int c = 0; c < 3; ++c) o[c] = uint8_t(std::max(0.0f, std::min(255.0f, p[c] * k)));
            o[3] = 255;
        }
    }
    return out;
}
}
