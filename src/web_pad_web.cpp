#include "web_pad.h"

#include <cmath>
#include <cstdint>

#include <SDL3/SDL_log.h>
#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>

#include "input_bindings.h"

// The browser: the core page's HTML gamepad (see web_pad.h). The page calls
// in from its events on the main thread, where the loop runs between frames.

struct WebPad
{
    InputSystem* input   = nullptr;
    bool         plugged = false;
    uint32_t     device  = 0;   // in the router while plugged
    bool         menu_held    = false;   // the page's MENU button
    bool         menu_pressed = false;   // went down since web_pad_take_menu
    bool         told         = false;   // the page has heard once
    bool         told_playing = false;
    double       told_aspect  = 0.0;
};

static WebPad g_pad = {};

// The page's multitap_page_game (frontends-common web/multitap-page.js), on
// the main thread (the loop runs there), if the page has it.
EM_JS(void, page_game, (int32_t playing, double aspect), {
    if (typeof multitap_page_game === 'function') {
        multitap_page_game(playing !== 0, aspect);
    }
});

void web_pad_attach(InputSystem* input)
{
    g_pad.input = input;
}

bool web_pad_active()
{
    return g_pad.plugged;
}

// 1 when done; 0 before main() attached the router (the page tries again).
extern "C" EMSCRIPTEN_KEEPALIVE int32_t piccolo_web_pad_show(int32_t on)
{
    if (g_pad.input == nullptr)
    {
        return 0;
    }
    if (on != 0 && !g_pad.plugged)
    {
        g_pad.device = input_devices_add(&g_pad.input->devices, InputDeviceKind::gamepad, "screen", "SCREEN PAD");
        InputDevice* device = input_devices_find(&g_pad.input->devices, g_pad.device);
        if (device != nullptr)
        {
            device->retropad = true;            // hands a RetroPad (the page's buttons)
            device->ignore = InputIgnore::no;   // (a finger held down isn't a stuck receiver)
        }
        input_add_controllers_for_device(g_pad.input, g_pad.device);
        g_pad.plugged = true;
        SDL_Log("Screen pad: plugged in");
    }
    else if (on == 0 && g_pad.plugged)
    {
        input_devices_disconnect(&g_pad.input->devices, g_pad.device);   // (shown again later: the same device comes back)
        g_pad.plugged = false;
        SDL_Log("Screen pad: unplugged");
    }
    return 1;
}

extern "C" EMSCRIPTEN_KEEPALIVE void piccolo_web_pad_state(int32_t buttons)
{
    if (!g_pad.plugged)
    {
        return;
    }
    bool menu = (buttons & (1 << WEB_PAD_MENU_BIT)) != 0;
    if (menu && !g_pad.menu_held)
    {
        g_pad.menu_pressed = true;
    }
    g_pad.menu_held = menu;
    const int16_t centered[2][2] = {};
    input_devices_set_retropad(&g_pad.input->devices, g_pad.device, static_cast<uint16_t>(buttons & 0xffff), centered);
}

bool web_pad_take_menu()
{
    bool pressed = g_pad.menu_pressed;
    g_pad.menu_pressed = false;
    return pressed;
}

void web_pad_tell_page(bool playing, double aspect)
{
    // A tiny change of shape (a core's geometry wobbling) isn't worth a new layout.
    bool same_shape = aspect == g_pad.told_aspect ||
                      (aspect > 0.0 && g_pad.told_aspect > 0.0 && std::fabs(aspect - g_pad.told_aspect) < 0.001);
    if (g_pad.told && playing == g_pad.told_playing && same_shape)
    {
        return;
    }
    g_pad.told = true;
    g_pad.told_playing = playing;
    g_pad.told_aspect = aspect;
    page_game(playing ? 1 : 0, aspect);
}
