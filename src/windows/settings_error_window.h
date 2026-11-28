#pragma once

// The notice shown at start when piccolo.cfg, core_options.cfg, input.cfg,
// recent.cfg or last_used.cfg exists but could not be read
// (a sync client or an antivirus holding it, permissions, an I/O error;
// review 2026-11-26, H3; Andrés: "show a red dialog with a message"):
//
//   +- SETTINGS COULD NOT BE READ -----------------------------+
//   | THESE FILES ARE THERE BUT COULD NOT BE READ:              |
//   | PICCOLO.CFG, INPUT.CFG                                    |
//   | WHAT THEY HOLD IS AT ITS DEFAULTS FOR NOW, AND NOTHING IS |
//   | SAVED OVER THEM. CLOSE PICCOLO, CHECK THE FILES, AND      |
//   | START AGAIN.                                              |
//   |                                                [OK]       |
//   +----------------------------------------------------------+
//
// It only informs: no game start waits for it, and nothing is saved while
// the files are unreadable (app_files.cpp's save functions), answered or not.
// The list of file names is built once at start (App::settings_error_files).
// It is drawn in the theme's red twin, like the UNSIGNED BUILD question.
//
// Split like every window: settings_error_window_state fills what it shows
// (no GUI), settings_error_window_draw draws it and records the answer, and
// settings_error_window_apply says whether it was answered (no GUI). The
// window keeps no state of its own (whether it's up is the App's).

#include <cstdint>

#include "localization.h"

#include "retro_ui_font.h"

struct SettingsErrorWindowState
{
    const Localization* strings = nullptr;
    const char*         files = "";   // the unreadable files' names, "piccolo.cfg, input.cfg" (set by the caller)
};

struct SettingsErrorWindowAction
{
    bool ok = false;   // OK pressed
};

void settings_error_window_state(SettingsErrorWindowState* state, const Localization* strings);
// Draws the dialog (red, centered).
void settings_error_window_draw(const SettingsErrorWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                                SettingsErrorWindowAction* action);
// True when the dialog was answered: it goes away.
bool settings_error_window_apply(const SettingsErrorWindowAction* action);
