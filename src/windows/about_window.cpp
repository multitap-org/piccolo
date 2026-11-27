#include "about_window.h"

#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

// ---------------------------------------------------------------------------
// State

void about_window_state(AboutWindowState* state, const Localization* strings, const char* version)
{
    state->strings = strings;
    state->version = version;
}

// ---------------------------------------------------------------------------
// Drawing

void about_window_draw(const AboutWindow* window, const AboutWindowState* state, RetroUiFont* pixel_font,
                       int32_t scale, AboutWindowAction* action)
{
    *action = {};
    if (!window->open)
    {
        return;
    }
    const Localization* strings = state->strings;
    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    retro_ui_push_theme(pixel_font, scale);
    // 200 retro pixels wide (never wider than the screen), as tall as its text.
    float width = 200.0f * block;
    width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    RetroUiWindow about = {};
    about.id = "about";
    about.title = piccolo_text(strings, Str::about_title);
    about.width = static_cast<float>(static_cast<int>(width));
    about.centered_on_open = true;
    about.focus_on_open = true;
    bool closed = false;
    if (retro_ui_window_begin(&about, scale, &closed))
    {
        action->closed = closed;
        retro_ui_text("PICCOLO");   // the name, never translated
        retro_ui_text_format("%s %s", piccolo_text(strings, Str::about_version), state->version);
        retro_ui_spacing();
        retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::about_line), 0.0f);
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}

// ---------------------------------------------------------------------------
// Apply

void about_window_open(AboutWindow* window)
{
    window->open = true;
}

void about_window_apply(AboutWindow* window, const AboutWindowAction* action)
{
    if (action->closed)
    {
        window->open = false;
    }
}
