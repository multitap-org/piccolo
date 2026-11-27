#pragma once

// MISC > ABOUT (testing round 1, Andrés 2026-11-25): piccolo's name, its
// version and one line about it, in a small window in the middle.
//
//   +- ABOUT ------------------------------------+
//   | PICCOLO                                    |
//   | VERSION 0.3.0-...                          |
//   | A SMALL LIBRETRO FRONTEND FOR PLAYING      |
//   | GAMES, PART OF THE MULTITAP PROJECT.       |
//   +--------------------------------------------+
//
// Split like every window: about_window_state fills what it shows (no GUI),
// about_window_draw draws it and records its X, and about_window_apply acts
// on it (no GUI).

#include <cstdint>

#include "localization.h"

#include "retro_ui_font.h"

struct AboutWindow
{
    bool open = false;
};

struct AboutWindowState
{
    const Localization* strings = nullptr;
    const char*         version = "";   // PICCOLO_VERSION (piccolo_version.h, made by the build)
};

struct AboutWindowAction
{
    bool closed = false;   // its X
};

void about_window_open(AboutWindow* window);

void about_window_state(AboutWindowState* state, const Localization* strings, const char* version);
// Draws the window while open.
void about_window_draw(const AboutWindow* window, const AboutWindowState* state, RetroUiFont* pixel_font,
                       int32_t scale, AboutWindowAction* action);
void about_window_apply(AboutWindow* window, const AboutWindowAction* action);
