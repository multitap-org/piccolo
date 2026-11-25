#pragma once

// piccolo's core options UI (decided by Andrés, 2026-11-14): the running
// core's options live in the quick menu (F1); the CORE OPTIONS MANAGER
// window (CONFIG menu) lists and deletes what is stored for any core. Both
// drawn from here. The manager:
//
//   CORE     [ SNES9X (RUNNING)                v ]
//   v STORED VALUES                                       any core, even one that won't start
//     FOR THE CORE                           [DELETE SECTION]
//       CONSOLE REGION = PAL                 [DELETE]
//     [DELETE ALL FOR THIS CORE]
//
// The running core's options are edited live, at the chosen layer (core,
// content folder, game; core_options_store.h), with C / D / G markers like
// the settings' C / G. STORED VALUES lists what core_options.cfg keeps for
// the chosen core, labelled from the definitions cache
// (core_options_cache.h), so a core whose options make it crash on start can
// be put right without loading it. Keys the core no longer declares are
// marked.
//
// Split like every window: core_options_ui_state fills what both show (no
// GUI; the store, the cache and the running core's options as read-only
// pointers), the drawing records what was done in an action (the panel's
// CoreOptionsUiAction, the manager's CoreOptionsManagerAction), and
// core_options_ui_apply / core_options_manager_apply act on it (no GUI).

#include <string>

#include "core_options_cache.h"
#include "core_options_store.h"
#include "localization.h"

struct CoreSession;
struct LibretroOptions;
#include "retro_ui_font.h"

// What the panel and the manager keep between frames (one for both).
struct CoreOptionsUi
{
    std::string     selected_core;          // "" = the running one (or the first cached)
    CoreOptionLayer layer = CoreOptionLayer::core;
    bool            choose_layer = true;    // pick the most specific layer in use on the next draw
    std::string     section;                // category key shown; "" = the options without a category
};

struct CoreOptionsUiState
{
    const Localization*     strings = nullptr;
    // Read only, under the drawing: what's stored at each layer, the markers,
    // STORED VALUES (big lists, read where they're drawn).
    const CoreOptionsStore* store   = nullptr;
    const CoreOptionsCache* cache   = nullptr;
    const LibretroOptions*  options = nullptr;   // the running core's; nullptr = no core runs
    const char*             running_core = "";   // short name, "" = none
    const char*             folder = "";         // the running content's folder name ("" = none)
    const char*             game = "";           // the running content's name ("" = none)
    bool                    has_folder = false;  // SAVE TO offers the folder (a content runs)
    bool                    has_game = false;    // SAVE TO offers the game
    bool                    folder_has_values = false;   // what SAVE TO picks first: the most specific
    bool                    game_has_values = false;     // layer in use
};

// The running core's options: one change a frame.
struct CoreOptionsUiAction
{
    bool        set = false;      // `key` = `value` at the edited layer
    bool        reset = false;    // RESET: `key` leaves the edited layer
    const char* key = nullptr;    // the running core's (LibretroOption)
    const char* value = nullptr;
};

enum class CoreOptionsManagerPress
{
    none,
    delete_section,   // DELETE SECTION: `section`
    delete_entry,     // DELETE: `key` in `section`
    delete_all,       // DELETE ALL FOR THIS CORE (the selected one)
};

struct CoreOptionsManagerAction
{
    CoreOptionsManagerPress press = CoreOptionsManagerPress::none;
    std::string section;
    std::string key;
};

// `session`: live editing while its core runs (`running_core` not ""). The
// rest as CoreOptionsUiState says.
void core_options_ui_state(CoreOptionsUiState* state, const Localization* strings, const CoreSession* session,
                           const CoreOptionsStore* store, const CoreOptionsCache* cache, const char* running_core,
                           const char* folder, const char* game);

// Only the running core's options (SAVE TO and the options), e.g. for the
// quick menu. Nothing when no core runs. `label_width`: SAVE TO / SECTION's
// labels (the options size their own column).
void core_options_ui_draw_running(CoreOptionsUi* ui, const CoreOptionsUiState* state, float label_width,
                                  CoreOptionsUiAction* action);
// The running core's option changed or reset, live.
void core_options_ui_apply(const CoreOptionsUi* ui, const CoreOptionsUiAction* action, CoreSession* session);

// The manager's contents: CORE (any core the cache or the store knows) and
// its STORED VALUES, labels `label_width` wide. Inside a themed window.
void core_options_ui_draw(CoreOptionsUi* ui, const CoreOptionsUiState* state, float label_width,
                          CoreOptionsManagerAction* action);

// The CORE OPTIONS MANAGER window. Opening it starts on the running core.
void core_options_manager_open(bool* open, CoreOptionsUi* ui);
void core_options_manager_draw(bool* open, CoreOptionsUi* ui, const CoreOptionsUiState* state,
                               RetroUiFont* pixel_font, int32_t scale, CoreOptionsManagerAction* action);
// Deletes what was asked from the store; a running core whose values were
// deleted gets what applies now. `session`: nullptr when no core runs.
void core_options_manager_apply(const CoreOptionsUi* ui, const CoreOptionsManagerAction* action,
                                CoreOptionsStore* store, CoreSession* session, const char* running_core);
