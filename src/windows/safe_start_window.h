#pragma once

// piccolo's safe start (decided by Andrés, 2026-11-14): when the last session
// with a core didn't end properly (a crash or a hang, often right after an
// option change) and that core has stored options, starting it again asks
// first:
//
//   +- SAFE START ---------------------------------------------+
//   | SNES9X DIDN'T CLOSE PROPERLY LAST TIME (A CRASH OR A HANG).|
//   | ITS OPTIONS HAD BEEN CHANGED IN THAT SESSION.              |
//   | IT HAS STORED OPTIONS. START WITH:                         |
//   | [STORED OPTIONS] [CORE DEFAULTS, THIS TIME]                |
//   | [DELETE STORED OPTIONS] [CANCEL]                           |
//   +----------------------------------------------------------+
//
// How a session that didn't end is noticed: piccolo writes
// settings/running.cfg before loading a game and removes it when the game
// ends properly (see app_game.cpp).
//
// Split like every window: safe_start_window_state fills what it shows (no
// GUI), safe_start_window_draw draws it and records the answer, and
// safe_start_window_apply acts on it (no GUI). The window keeps no state of
// its own (whether it's up is the App's: it waits with the game to start).

#include <cstdint>

#include "localization.h"

#include "retro_ui_font.h"

struct CoreOptionsStore;

enum class SafeStartChoice
{
    none,            // still open
    stored,          // start as usual
    defaults,        // start with the core's own defaults, this time only
    delete_stored,   // delete every stored option of the core, then start
    cancel,          // don't start
};

struct SafeStartWindowState
{
    const Localization* strings = nullptr;
    const char* core = "";          // the core's short name
    bool options_changed = false;   // options were changed in the session that didn't end
};

struct SafeStartWindowAction
{
    SafeStartChoice choice = SafeStartChoice::none;
};

// What app_ui.cpp still does after safe_start_window_apply.
struct SafeStartResult
{
    bool closed = false;                     // answered: the window goes away
    bool start = false;                      // start the game that waited
    bool skip_stored_options_once = false;   // ... with the core's defaults
    bool options_deleted = false;            // the store changed: save it
};

void safe_start_window_state(SafeStartWindowState* state, const Localization* strings, const char* core,
                             bool options_changed);
// Draws the dialog (modal, centered).
void safe_start_window_draw(const SafeStartWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                            SafeStartWindowAction* action);
// DELETE STORED OPTIONS removes the core's sections from `store` here.
SafeStartResult safe_start_window_apply(const SafeStartWindowAction* action, CoreOptionsStore* store, const char* core);
