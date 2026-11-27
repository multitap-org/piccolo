#pragma once

// TOOLS > CORES: the core downloader's window (core_updater.h in
// frontend-libretro does the work).
//
//   CORE         SYSTEMS              VERSION   STATUS
//   SameBoy      Game Boy, Game Bo... v1.0.5    INSTALLED
//   ...                                          (non-free cores marked)
//   ---------------------------------------------------------------
//   LICENSE MIT (FREE)   DOWNLOAD 119 KB   INSTALLED v1.0.5
//   [INSTALL / UPDATE / REPLACE] [REMOVE]        [REFRESH] [UPDATE ALL]
//   status: what the downloader did last, or the download's progress
//
// Opening it gets the list (REFRESH); the list only lives while piccolo
// runs. The running core can't be updated or removed (the downloader
// refuses it and says so).
//
// SHOW NON-FREE CORES (Andrés, 2026-11-20): off by default, so the list has
// only free-licensed cores; turning it on asks for confirmation first,
// explaining what non-free means. Non-free cores already installed are
// always listed (so they can be removed). Store builds never offer them
// (core_updater's free_only).
//
// Core types (Andrés, 2026-11-20): the index's `type` (multitap-org/cores
// chooses it). OLDER VERSIONS (legacy: Snes9x 2005, ...) and EXPERIMENTAL
// are off by default and need no confirmation. Installed cores are always
// listed, whatever the switches say. The three switches are one SHOW row.

//
// Split like every window: cores_window_update keeps UPDATE ALL going and
// cores_window_state fills what the window shows (no GUI), cores_window_draw
// draws it and records what was pressed, and cores_window_apply acts on it
// (no GUI): the downloader's jobs and the SHOW settings.

#include <cstdint>

#include "core_updater.h"
#include "localization.h"
#include "settings.h"

#include "retro_ui_font.h"

struct CoresWindow
{
    bool    open = false;
    int32_t selected = -1;      // index into the updater's index entries
    bool    was_refreshing = false;   // last frame's updater state: a refresh that just ended replaced (and re-sorted) the index
    bool    update_all = false; // UPDATE ALL: install the next outdated core whenever idle
    float   bottom_height = 0.0f;   // what's under the list, measured last frame (the list takes the rest)
    bool    confirm_non_free = false;   // the NON-FREE confirmation is up
};

// The SHOW switches (settings).
struct CoresShow
{
    bool non_free = false;
    bool legacy = false;
    bool experimental = false;
};

struct CoresWindowState
{
    const Localization* strings = nullptr;
    const CoreUpdater*  updater = nullptr;   // the index and what the downloader does; only read
    const char*         running_file = "";   // the running core's file name, "" = none
    bool                available = false;   // this build has a downloader
    CoresShow           show;
    int64_t             received = 0;        // the download's progress (total 0: unknown)
    int64_t             total = 0;
};

enum class CoresWindowPress
{
    none,
    install,      // INSTALL / UPDATE / REPLACE the selected core
    remove,       // REMOVE the selected core
    refresh,
    update_all,
    cancel,
};

struct CoresWindowAction
{
    CoresWindowPress press = CoresWindowPress::none;
    int32_t          entry = -1;           // the selected core, for install and remove
    CoresShow        show;                 // the SHOW switches after this frame
    bool             show_changed = false;
};

// Opens the window and asks for the list (the updater must be set up first).
void cores_window_open(CoresWindow* window, CoreUpdater* updater);

// Before drawing, every frame: UPDATE ALL starts the next install when the
// downloader is idle.
void cores_window_update(CoresWindow* window, CoreUpdater* updater, const Settings* settings, const char* running_file);
// `updater` isn't changed: core_updater_progress only takes it non-const.
void cores_window_state(CoresWindowState* state, CoreUpdater* updater, const Settings* settings,
                        const Localization* strings, const char* running_file);
void cores_window_draw(CoresWindow* window, const CoresWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                       CoresWindowAction* action);
// Starts the job pressed and stores changed SHOW switches. True when the
// settings changed (main.cpp saves them).
bool cores_window_apply(CoresWindow* window, const CoresWindowAction* action, CoreUpdater* updater, Settings* settings,
                        const char* running_file);
