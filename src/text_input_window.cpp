#include "text_input_window.h"

#include <cstdio>

#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

TextInputChoice text_input_window_draw(TextInputWindow* window, const LibretroTextInput* request, RetroUiFont* pixel_font,
                                       int32_t scale, const Localization* strings)
{
    if (window->serial != request->serial)
    {
        window->serial = request->serial;
        std::snprintf(window->buffer, sizeof(window->buffer), "%s", request->initial);
        window->focus_field = true;
    }

    TextInputChoice choice = TextInputChoice::none;
    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);

    retro_ui_push_theme(pixel_font, scale);
    // 200 retro pixels wide (never wider than the window), as tall as its text.
    float width = 200.0f * block;
    width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    RetroUiWindow frame = {};
    frame.id = "text_input";
    frame.title = piccolo_text(strings, Str::text_input_title);
    frame.width = width;
    frame.centered = true;
    frame.focus = true;
    bool closed = false;
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        if (closed)
        {
            choice = TextInputChoice::cancel;
        }
        if (request->prompt[0] != '\0')
        {
            retro_ui_text_wrapped(request->prompt);
        }
        // The field counts characters as the core does; numbers take digits only.
        RetroUiEditField field = {};
        field.digits_only = request->type == RETRO_EXT_TEXT_INPUT_TYPE_NUMBER;
        field.max_characters = request->max_length;
        field.focus = window->focus_field;
        window->focus_field = false;
        if (retro_ui_edit_field("text", window->buffer, sizeof(window->buffer), &field))
        {
            choice = TextInputChoice::ok;   // Enter
        }
        retro_ui_spacing();
        if (retro_ui_button(piccolo_text(strings, Str::text_input_ok)))
        {
            choice = TextInputChoice::ok;
        }
        retro_ui_same_line();
        if (retro_ui_button(piccolo_text(strings, Str::text_input_cancel)))
        {
            choice = TextInputChoice::cancel;
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
    return choice;
}
