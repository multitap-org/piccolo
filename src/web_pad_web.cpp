#include "web_pad.h"

#include <cmath>
#include <cstdint>

#include <SDL3/SDL_log.h>
#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "input_bindings.h"
#include "retro_ui.h"
#include "retro_ui_theme.h"

// The browser: the core page's HTML gamepad (see web_pad.h). The page calls
// in from its events on the main thread, where the loop runs between frames.

struct WebPad
{
    InputSystem* input   = nullptr;
    bool         plugged = false;
    uint32_t     device  = 0;   // in the router while plugged
    uint32_t     menus_held    = 0;   // the page's burger menu's picks (their bits)
    uint32_t     menus_pressed = 0;   // went down since taken
    bool         told         = false;   // the page has heard once
    bool         told_playing = false;
    double       told_aspect  = 0.0;
    int32_t      told_vibrate = -1;   // -1: not told yet
    std::string  told_theme;          // the JSON the page has (empty: none yet)
};

static WebPad g_pad = {};

// The page's multitap_page_game (frontends-common web/multitap-page.js), on
// the main thread (the loop runs there), if the page has it.
EM_JS(void, page_game, (int32_t playing, double aspect), {
    if (typeof multitap_page_game === 'function') {
        multitap_page_game(playing !== 0, aspect);
    }
});

// The page pad's buzz on a press (frontends-common's web/multitap-pad.js), if the page has it.
EM_JS(void, page_vibrate, (int32_t on), {
    if (typeof multitap_pad_set_vibrate === 'function') {
        multitap_pad_set_vibrate(on !== 0);
    }
});

// The page's piccolo_page_theme (web/page/piccolo-page.js) with the theme's
// JSON ("null": the page's own look). 0 if the page has no such function yet
// (told again next frame).
EM_JS(int32_t, page_theme, (const char* json), {
    if (typeof piccolo_page_theme === 'function') {
        piccolo_page_theme(UTF8ToString(json));
        return 1;
    }
    return 0;
});

EM_JS(void, page_language, (const char* code), {
    if (typeof piccolo_web_set_language === 'function') {
        piccolo_web_set_language(UTF8ToString(code));
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
            // On port 1 from the start, as the touch pad is, without taking it
            // from a gamepad that joins later (InputDevice::shares_port1): the
            // first press plays or moves in the menus instead of only joining.
            device->shares_port1 = true;
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
    uint32_t menus = static_cast<uint32_t>(buttons) & ((1u << WEB_PAD_QUICK_MENU_BIT) | (1u << WEB_PAD_MAIN_MENU_BIT) |
                                                       (1u << WEB_PAD_GAME_DVR_BIT) | (1u << WEB_PAD_CONTROLLERS_BIT));
    g_pad.menus_pressed |= menus & ~g_pad.menus_held;
    g_pad.menus_held = menus;
    const int16_t centered[2][2] = {};
    input_devices_set_retropad(&g_pad.input->devices, g_pad.device, static_cast<uint16_t>(buttons & 0xffff), centered);
}

static bool take(int32_t bit)
{
    uint32_t mask = 1u << bit;
    bool pressed = (g_pad.menus_pressed & mask) != 0;
    g_pad.menus_pressed &= ~mask;
    return pressed;
}

bool web_pad_take_quick_menu()
{
    return take(WEB_PAD_QUICK_MENU_BIT);
}

bool web_pad_take_main_menu()
{
    return take(WEB_PAD_MAIN_MENU_BIT);
}

bool web_pad_take_game_dvr()
{
    return take(WEB_PAD_GAME_DVR_BIT);
}

bool web_pad_take_controllers()
{
    return take(WEB_PAD_CONTROLLERS_BIT);
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

void web_pad_set_vibrate(bool on)
{
    int32_t value = on ? 1 : 0;
    if (value == g_pad.told_vibrate)
    {
        return;
    }
    g_pad.told_vibrate = value;
    page_vibrate(value);
}

// "#RRGGBB" of an ARGB color, appended as a JSON string.
static void append_color(std::string* json, uint32_t argb)
{
    char text[16];
    std::snprintf(text, sizeof(text), "\"#%06X\"", static_cast<unsigned>(argb & 0xFFFFFFu));
    *json += text;
}

static void append_field(std::string* json, const char* name, uint32_t argb, bool* first)
{
    if (!*first)
    {
        *json += ",";
    }
    *first = false;
    *json += "\"";
    *json += name;
    *json += "\":";
    append_color(json, argb);
}

// The page's theme (see piccolo_page_theme): the UI colors it needs by the
// names it uses, every pad color without its "ui_color_pad_" prefix, and the
// pad's opacities.
static std::string theme_json(const RetroUiPalette* palette, const float look[4])
{
    std::string json = "{\"ui\":{";
    bool first = true;
    append_field(&json, "field", palette->field_face, &first);
    append_field(&json, "window", palette->panel_face, &first);
    append_field(&json, "window_dark", palette->panel_dark, &first);
    append_field(&json, "window_light", palette->panel_light, &first);
    append_field(&json, "button", palette->button_face, &first);
    append_field(&json, "highlight", palette->highlight, &first);
    append_field(&json, "text", palette->text, &first);
    append_field(&json, "text_disabled", palette->text_disabled, &first);
    append_field(&json, "text_highlight", palette->text_highlight, &first);
    append_field(&json, "section", palette->section, &first);
    append_field(&json, "bar", palette->bar_face, &first);
    json += "},\"pad\":{";
    first = true;
    const char* prefix = "ui_color_pad_";
    for (uint32_t i = 0; i < retro_ui_palette_color_count(); i++)
    {
        const char* key = retro_ui_palette_color_key(i);
        if (std::strncmp(key, prefix, std::strlen(prefix)) == 0)
        {
            append_field(&json, key + std::strlen(prefix), retro_ui_palette_color_value(palette, i), &first);
        }
    }
    char levels[96];
    std::snprintf(levels, sizeof(levels), "},\"look\":[%.2f,%.2f,%.2f,%.2f]}", static_cast<double>(look[0]),
                  static_cast<double>(look[1]), static_cast<double>(look[2]), static_cast<double>(look[3]));
    json += levels;
    return json;
}

void web_page_set_theme(const RetroUiPalette* palette, const float look[4])
{
    // BLUE as it ships, with its own pad levels, is the page's own look.
    const RetroUiPalette* blue = retro_ui_theme_palette(RetroUiTheme::blue);
    const float blue_look[4] = {static_cast<float>(blue->pad_fill) / 100.0f,
                                static_cast<float>(blue->pad_border_opacity) / 100.0f,
                                static_cast<float>(blue->pad_labels) / 100.0f,
                                static_cast<float>(blue->pad_background_opacity) / 100.0f};
    bool plain = retro_ui_palette_equal(palette, blue);
    for (int32_t i = 0; i < 4; i++)
    {
        plain = plain && std::fabs(look[i] - blue_look[i]) < 0.005f;
    }
    std::string json = plain ? std::string("null") : theme_json(palette, look);
    if (json == g_pad.told_theme)
    {
        return;
    }
    if (page_theme(json.c_str()) != 0)
    {
        g_pad.told_theme = json;
    }
}

void web_page_set_language(const char* code)
{
    page_language(code);
}
