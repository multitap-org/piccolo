#pragma once

// The notice shown at start when piccolo.cfg exists but could not be read
// (a sync client or an antivirus holding it, permissions, an I/O error;
// review 2026-11-26, H3; Andrés: "show a red dialog with a message"):
//
//   +- SETTINGS COULD NOT BE READ -----------------------------+
//   | PICCOLO.CFG IS THERE BUT COULD NOT BE READ. SETTINGS ARE  |
//   | AT THEIR DEFAULTS FOR NOW, AND NOTHING YOU CHANGE WILL BE |
//   | SAVED, SO THE FILE IS NOT REPLACED. CLOSE PICCOLO, CHECK  |
//   | THE FILE, AND START AGAIN.                                |
//   |                                                [OK]       |
//   +----------------------------------------------------------+
//
// It only informs: no game start waits for it, and nothing is saved while
// the settings are unreadable (main.cpp's save_settings), answered or not.
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
