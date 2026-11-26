#include "settings_error_window.h"

#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

// ---------------------------------------------------------------------------
// State

void settings_error_window_state(SettingsErrorWindowState* state, const Localization* strings)
{
    state->strings = strings;
}

// ---------------------------------------------------------------------------
// Drawing

void settings_error_window_draw(const SettingsErrorWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                                SettingsErrorWindowAction* action)
{
    *action = {};
    const Localization* strings = state->strings;
    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);

    retro_ui_push_theme(pixel_font, scale);
    // The theme's red twin, as the UNSIGNED BUILD question has it (net_caps_ui_unsigned).
    RetroUiPalette red;
    retro_ui_danger_palette(retro_ui_palette(), &red);
    retro_ui_push_palette(&red);
    // 300 retro pixels wide (never wider than the window), as tall as its text.
    float width = 300.0f * block;
    width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    RetroUiWindow window = {};
    window.id = "settings_error";
    window.title = piccolo_text(strings, Str::settings_error_title);
    window.width = width;
    window.centered = true;
    window.closable = false;   // the one way out is OK
    window.focus = true;
    if (retro_ui_window_begin(&window, scale, nullptr))
    {
        retro_ui_text_wrapped(piccolo_text(strings, Str::settings_error_text));
        retro_ui_spacing();
        if (retro_ui_button(piccolo_text(strings, Str::settings_error_ok)))
        {
            action->ok = true;
        }
    }
    retro_ui_window_end();
    retro_ui_pop_palette();
    retro_ui_pop_theme();
}

// ---------------------------------------------------------------------------
// Apply

bool settings_error_window_apply(const SettingsErrorWindowAction* action)
{
    return action->ok;
}
