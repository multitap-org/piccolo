#pragma once

// The text a core asks for (Multitap's text input extension, TEXT mode;
// libretro_extensions.h in host-libretro). Open while the request is
// pending; the game pauses meanwhile.
//
//   +- ENTER TEXT ----------------------+
//   | NAME                              |   <- the core's prompt
//   | [LINK____________]                |
//   | [OK] [CANCEL]                     |
//   +-----------------------------------+
//
// The field brings up the on-screen keyboard like any of piccolo's text fields
// (INPUT > ON-SCREEN KEYBOARD; text_input in frontends-common); main.cpp asks
// for the number keypad when the core wants a NUMBER.
//
// Split like every window: text_input_window_state / _update read the request
// (no GUI), text_input_window_draw draws and records OK / CANCEL, and
// text_input_window_apply answers the core (no GUI).

#include <cstdint>

#include "libretro_host.h"
#include "localization.h"

#include "retro_ui_font.h"

struct TextInputWindow
{
    uint32_t serial = 0;                         // the request `buffer` belongs to
    char     buffer[LIBRETRO_TEXT_INPUT_MAX] = {};
    bool     focus_field = false;                // put the cursor in the field on the next draw
};

// What the box shows of the core's request.
struct TextInputWindowState
{
    const Localization* strings = nullptr;
    const char* prompt = "";        // the core's prompt; "" for none
    const char* initial = "";       // the text a new request starts with
    uint32_t serial = 0;            // a new request has another one
    uint32_t max_characters = 0;    // as the core counts them; 0: as many as the buffer holds
    bool digits_only = false;       // the core wants a NUMBER
};

enum class TextInputChoice
{
    none,     // still open
    ok,       // answer with window->buffer
    cancel,   // answer with nothing
};

struct TextInputWindowAction
{
    TextInputChoice choice = TextInputChoice::none;
};

// `request`: the host's pending request (mode TEXT).
void text_input_window_state(TextInputWindowState* state, const LibretroTextInput* request, const Localization* strings);
// A new request (another serial) starts again from its initial text.
void text_input_window_update(TextInputWindow* window, const TextInputWindowState* state);
// Draws the box, modal and centered.
void text_input_window_draw(TextInputWindow* window, const TextInputWindowState* state, RetroUiFont* pixel_font,
                            int32_t scale, TextInputWindowAction* action);
// OK answers the core with window->buffer, CANCEL with nothing.
void text_input_window_apply(const TextInputWindow* window, const TextInputWindowAction* action, LibretroHost* host);
