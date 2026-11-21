#include "web_pad.h"

#include <cstdint>

#include <SDL3/SDL_log.h>
#include <emscripten/emscripten.h>

#include "input_bindings.h"

// The browser: the core page's HTML gamepad (see web_pad.h). The page calls
// in from its events on the main thread, where the loop runs between frames.

struct WebPad
{
    InputSystem* input   = nullptr;
    bool         plugged = false;
    uint32_t     device  = 0;   // in the router while plugged
};

static WebPad g_pad = {};

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
    const int16_t centered[2][2] = {};
    input_devices_set_retropad(&g_pad.input->devices, g_pad.device, static_cast<uint16_t>(buttons), centered);
}
