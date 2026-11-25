#include "safe_start_window.h"

#include <cstdio>

#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

SafeStartChoice safe_start_window_draw(RetroUiFont* pixel_font, int32_t scale, const Localization* strings, const char* core,
                                       bool options_changed)
{
    SafeStartChoice choice = SafeStartChoice::none;
    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);

    retro_ui_push_theme(pixel_font, scale);
    // 300 retro pixels wide (never wider than the window), as tall as its text.
    float width = 300.0f * block;
    width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    RetroUiWindow window = {};
    window.id = "safe_start";
    window.title = piccolo_text(strings, Str::safe_start_title);
    window.width = width;
    window.centered = true;
    window.focus = true;
    bool closed = false;
    if (retro_ui_window_begin(&window, scale, &closed))
    {
        if (closed)
        {
            choice = SafeStartChoice::cancel;
        }
        char crashed[512] = {};
        std::snprintf(crashed, sizeof(crashed), "%s %s", core, piccolo_text(strings, Str::safe_start_crashed));
        retro_ui_text_wrapped(crashed);
        if (options_changed)
        {
            retro_ui_text_wrapped(piccolo_text(strings, Str::safe_start_after_options));
        }
        retro_ui_text_wrapped(piccolo_text(strings, Str::safe_start_question));
        retro_ui_spacing();

        if (retro_ui_button(piccolo_text(strings, Str::safe_start_stored)))
        {
            choice = SafeStartChoice::stored;
        }
        retro_ui_same_line();
        if (retro_ui_button(piccolo_text(strings, Str::safe_start_defaults)))
        {
            choice = SafeStartChoice::defaults;
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::safe_start_defaults_help));
        if (retro_ui_button(piccolo_text(strings, Str::safe_start_delete)))
        {
            choice = SafeStartChoice::delete_stored;
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::safe_start_delete_help));
        retro_ui_same_line();
        if (retro_ui_button(piccolo_text(strings, Str::safe_start_cancel)))
        {
            choice = SafeStartChoice::cancel;
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
    return choice;
}
