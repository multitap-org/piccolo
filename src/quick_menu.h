#pragma once

// QUICK MENU (F1 while a game runs): the actions on the running core, a
// window widget in the middle of the screen (decided by Andrés, 2026-11-22;
// before that a slide-out that also held the core's settings, now in CORE
// CONFIGURATION: core_config_window.h).
//
//   +- QUICK MENU ------------------------------------ [X] +
//   | SUPER METROID - SNES9X                               |
//   | [PAUSE]        [DVR]          [CONFIG]               |
//   | [RESET]                [CLOSE GAME]                  |
//   |------------------------------------------------------|
//   | [1] [2] [3] [4] [5] [6] [7] [8] [9]                   |   slots, used ones lit
//   | +--------------+  SLOT 3                              |
//   | |  thumbnail   |  2026-11-14 15:32                    |
//   | +--------------+  [SAVE] [LOAD]                       |
//   +------------------------------------------------------+
//
// Slots 1..9: arrows or 1-9 pick one (also the slot F2 / F4 use), S saves, L
// loads; Enter does nothing by itself, so a state is never loaded or
// overwritten by accident. Each slot shows its thumbnail (the PNG saved next
// to the state, at the aspect in use: core_session.h) and date. PAUSE IN
// MENU pauses the game while it's open. F1 or Esc closes it.

#include <cstdint>
#include <vector>

#include "core_session.h"
#include "localization.h"
#include "video_texture.h"

struct AppShell;
struct ImFont;

enum class QuickMenuAction
{
    none,
    save,         // save to `slot`
    load,         // load `slot`
    reset,
    close_game,
    pause,        // PAUSE / RESUME: the player's own pause (main.cpp toggles it)
    dvr,          // open the rewind DVR (a window widget: it replaces QUICK MENU)
    config,       // open CORE CONFIGURATION (a MAIN MENU window: QUICK MENU closes)
};

struct QuickMenu
{
    bool          open = false;
    uint32_t      slot = LIBRETRO_STATE_SLOT_FIRST;   // the selected slot
    StateSlotInfo slots[LIBRETRO_STATE_SLOT_LAST + 1] = {};   // [1..9]
    bool          refresh = true;   // read the slots again on the next draw
    bool          focus_slot = false;   // move the keyboard focus to `slot` on the next draw (picked by 1-9)

    // The selected slot's thumbnail: loaded from its PNG, uploaded on the next frame.
    VideoTexture          texture;   // texture.imgui_id: what ImGui draws
    std::vector<uint32_t> pixels;
    uint32_t              width = 0;
    uint32_t              height = 0;
    bool                  upload_pending = false;
    bool                  has_picture = false;   // the texture shows shown_slot
    uint32_t              shown_slot = 0;
    int64_t               shown_modified = 0;
};

struct QuickMenuContext
{
    const Localization* strings = nullptr;
    const CoreSession*  session = nullptr;
    const char*         core_name = "";
    const char*         game_name = "";
    bool                user_paused = false;   // the player paused: PAUSE reads RESUME
};

// Opens on `slot` (the session's current slot).
void quick_menu_open(QuickMenu* menu, uint32_t slot);

// The slots changed on disk (a state was saved): read them again.
void quick_menu_refresh(QuickMenu* menu);

// Draws the menu while open (its X closes it: open = false); returns what
// was picked this frame.
QuickMenuAction quick_menu_draw(QuickMenu* menu, const QuickMenuContext* context, ImFont* pixel_font, int32_t scale);

// Uploads a newly loaded thumbnail. Call after app_shell_acquire, outside
// rendering (where the game's video is uploaded).
void quick_menu_upload(QuickMenu* menu, AppShell* shell);

// Frees the texture (waits for the GPU).
void quick_menu_destroy(QuickMenu* menu, AppShell* shell);
