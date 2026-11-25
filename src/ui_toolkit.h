#pragma once

#include <cstdint>

// What the frame loop asks and tells the GUI toolkit, without knowing which
// one it is. The loop (main.cpp) routes input with these answers; only
// ui_toolkit_imgui.cpp knows they come from ImGui. A second toolkit would
// write its own ui_toolkit_*.cpp with the same functions.

#include "retro_ui.h"   // RetroUiPalette
#include "retro_ui_font.h"

// Starts the toolkit's side of piccolo once the window and GPU are up (after
// app_shell_create): the pixel font, the theme's text shadows. Returns the
// pixel font as the toolkit holds it (for retroui), or nullptr if it failed.
RetroUiFont* ui_toolkit_start();

// Where the toolkit keeps its window layout between runs (imgui.ini);
// nullptr: nowhere.
void ui_toolkit_set_layout_file(const char* path);

// The retro UI's scale this frame (the text shadows follow it).
void ui_toolkit_set_scale(int32_t scale);

// The retro UI's scale (window pixels per retro pixel) for a screen this
// size, in the toolkit's units.
int32_t ui_toolkit_scale_for_screen(float width, float height);

// The interface's colors (the theme), from now on. At the frame's start,
// before the toolkit's frame begins.
void ui_toolkit_set_palette(const RetroUiPalette* palette);

// The screen changed size (a phone turned, the window resized): the open
// windows go back to the middle, or they'd keep their old place, half off
// the screen.
void ui_toolkit_center_windows();

// PAGE LEFT / RIGHT this loop: the focused window's tabs move by `delta`
// (-1, 0, 1) when they're next drawn.
void ui_toolkit_page_request(int delta);

// The interface keys a gamepad drives the menus and dialogs with, as the
// keyboard does: move, activate, and back (closes an open list or popup).
enum class UiNavKey
{
    up,
    down,
    left,
    right,
    accept,
    back,
};

// A list or popup is open: it takes BACK / Escape before any window does.
bool ui_toolkit_has_popup();

// A text field is being typed in: keys are text, not commands.
bool ui_toolkit_wants_text();

// An interface key went down or up (from a gamepad; the keyboard reaches the
// toolkit through its own events).
void ui_toolkit_nav_key(UiNavKey key, bool down);

// Nothing held acts any more: every key the toolkit thinks is down is let go.
void ui_toolkit_clear_keys();

// The size of the screen the toolkit draws on, in its units.
void ui_toolkit_screen_size(float* width, float* height);

