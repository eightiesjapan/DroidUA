#ifdef __ANDROID__
#include "winp.h"
#include "system/gfx.h"
#include "system/system.h"
#include <algorithm>
#include <cmath>

namespace {
SDL_GameController *pad = nullptr;
Uint32 previous = 0, lastTick = 0;
std::map<int, bool> previousKeys;
bool wasPlaying = false;
float moveX = 0, moveY = 0, lookX = 0, lookY = 0;
bool leftClick = false, rightClick = false;
float axis(SDL_GameControllerAxis a) {
    float v = SDL_GameControllerGetAxis(pad, a) / 32767.0f;
    const float dead = 0.18f;
    return std::fabs(v) <= dead ? 0.0f : std::copysign((std::min(std::fabs(v), 1.0f) - dead) / (1.0f - dead), v);
}
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
    Uint32 buttons = 0;
    if (pad) {
        SDL_GameControllerUpdate();
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
            if (SDL_GameControllerGetButton(pad, static_cast<SDL_GameControllerButton>(b))) buttons |= 1u << b;
        // Triggers are analog axes, not shoulder buttons.
        if (axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.3f) buttons |= 1u << 24;
        if (axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.3f) buttons |= 1u << 25;
    }
    auto down = [&](int b) { return (buttons & (1u << b)) != 0; };
    auto pressed = [&](int b) { return down(b) && !(previous & (1u << b)); };
    if (playing && !wasPlaying) grabbed = true;
    if (!playing) grabbed = false;
    if (playing && pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
        grabbed = !grabbed;
        SDL_Log("OpenUA controller mode: %s", grabbed ? "vehicle" : "pointer");
    }
    if (pressed(SDL_CONTROLLER_BUTTON_START)) grabbed = false;
    const bool pointer = !playing || !grabbed;
    const Uint32 now = SDL_GetTicks();
    const float dt = lastTick ? std::min((now - lastTick) / 1000.0f, 0.05f) : 0.0f;
    lastTick = now;
    moveX = pad ? axis(SDL_CONTROLLER_AXIS_LEFTX) : 0;
    moveY = pad ? axis(SDL_CONTROLLER_AXIS_LEFTY) : 0;
    lookX = pad ? axis(SDL_CONTROLLER_AXIS_RIGHTX) : 0;
    lookY = pad ? axis(SDL_CONTROLLER_AXIS_RIGHTY) : 0;
    if (pointer && (moveX || moveY)) {
        static float x = 320, y = 240;
        static Common::Point last;
        if (_mPos != last) { x = _mPos.x; y = _mPos.y; }
        SDL_Surface *screen = GFX::Engine.Screen();
        x = std::max(0.0f, std::min(float(screen->w - 1), x + moveX * 420 * dt));
        y = std::max(0.0f, std::min(float(screen->h - 1), y + moveY * 420 * dt));
        const Common::Point real = System::GetResolution();
        SDL_Event e = {};
        e.type = SDL_MOUSEMOTION;
        e.motion.x = int(x * real.x / screen->w);
        e.motion.y = int(y * real.y / screen->h);
        InputWatch(nullptr, &e);
        last = _mPos;
    }
    auto mouse = [&](int button, bool held, bool &old) {
        if (held != old) {
            SDL_Surface *screen = GFX::Engine.Screen();
            const Common::Point real = System::GetResolution();
            SDL_Event e = {};
            e.type = held ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            e.button.button = button;
            e.button.clicks = 1;
            e.button.x = _mPos.x * real.x / screen->w;
            e.button.y = _mPos.y * real.y / screen->h;
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

void NC_STACK_winp::AndroidApply(TInputState *input, bool playing, bool grabbed)
{
    if (!playing || !pad) return;
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
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) || axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.3f) input->Buttons.Set(0);
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) input->Buttons.Set(2);
    }
}
#endif
