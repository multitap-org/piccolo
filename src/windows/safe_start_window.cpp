#include "safe_start_window.h"

#include <cstdio>
#include <string>
#include <vector>

#include <SDL3/SDL_log.h>

#include "core_options_store.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

// ---------------------------------------------------------------------------
// State

void safe_start_window_state(SafeStartWindowState* state, const Localization* strings, const char* core,
                             bool options_changed)
{
    state->strings = strings;
    state->core = core;
    state->options_changed = options_changed;
}

// ---------------------------------------------------------------------------
// Drawing

void safe_start_window_draw(const SafeStartWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                            SafeStartWindowAction* action)
{
    *action = {};
    const Localization* strings = state->strings;
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
            action->choice = SafeStartChoice::cancel;
        }
        char crashed[512] = {};
        std::snprintf(crashed, sizeof(crashed), "%s %s", state->core, piccolo_text(strings, Str::safe_start_crashed));
        retro_ui_text_wrapped(crashed);
        if (state->options_changed)
        {
            retro_ui_text_wrapped(piccolo_text(strings, Str::safe_start_after_options));
        }
        retro_ui_text_wrapped(piccolo_text(strings, Str::safe_start_question));
        retro_ui_spacing();

        if (retro_ui_button(piccolo_text(strings, Str::safe_start_stored)))
        {
            action->choice = SafeStartChoice::stored;
        }
        retro_ui_same_line();
        if (retro_ui_button(piccolo_text(strings, Str::safe_start_defaults)))
        {
            action->choice = SafeStartChoice::defaults;
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::safe_start_defaults_help));
        if (retro_ui_button(piccolo_text(strings, Str::safe_start_delete)))
        {
            action->choice = SafeStartChoice::delete_stored;
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::safe_start_delete_help));
        retro_ui_same_line();
        if (retro_ui_button(piccolo_text(strings, Str::safe_start_cancel)))
        {
            action->choice = SafeStartChoice::cancel;
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}

// ---------------------------------------------------------------------------
// Apply

SafeStartResult safe_start_window_apply(const SafeStartWindowAction* action, CoreOptionsStore* store, const char* core)
{
    SafeStartResult result = {};
    SafeStartChoice answer = action->choice;
    if (answer == SafeStartChoice::delete_stored)
    {
        std::vector<std::string> sections;
        core_options_store_sections_of(store, core, &sections);
        for (size_t i = 0; i < sections.size(); i++)
        {
            core_options_store_remove_section(store, sections[i].c_str());
        }
        result.options_deleted = true;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Safe start: deleted the stored options of %s", core);
    }
    result.skip_stored_options_once = answer == SafeStartChoice::defaults;
    result.start = answer == SafeStartChoice::stored || answer == SafeStartChoice::defaults ||
                   answer == SafeStartChoice::delete_stored;
    result.closed = answer != SafeStartChoice::none;
    return result;
}
