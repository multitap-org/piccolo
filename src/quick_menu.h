#pragma once

// QUICK MENU (F1 while a game runs): the actions on the running core, a
// window widget in the middle of the screen (decided by Andrés, 2026-11-22;
// layout B of testing round 1, 2026-11-25).
//
//   +- QUICK MENU ----------------------------------------- [X] +
//   | SUPER METROID - SNES9X                                    |
//   | [PAUSE     ][QUICK SAVE][QUICK LOAD][SCREENSHOT]           |
//   | [SOFT RESET][HARD RESET][CORE CONFIG][CLOSE GAME]          |
//   | v SAVE STATES                                              |   collapsed at first
//   | +--------------------------------------------------------+ |
//   | |            the selected slot's picture, big            | |   as wide as five slots
//   | +--------------------------------------------------------+ |
//   | [ 1 ] [ 2 ] [ 3 ] [ 4 ] [ 5 ]                               |   thumbnails, date or time
//   | [ 6 ] [ 7 ] [ 8 ] [ 9 ]                                     |
//   | [SAVE          ][LOAD          ][GAME DVR      ]            |
//   +-----------------------------------------------------------+
//
// Everything sits on one five-column grid: the picture is as wide as five
// slots, the actions split that width in four equal columns, SAVE / LOAD /
// GAME DVR in three. Pictures are a fixed width (a widescreen game's are
// shorter) and drawn sharp bilinear: the big one scaled up by a whole number
// with nearest here, then bilinear to its size.
//
// QUICK SAVE / QUICK LOAD use the quick slot (QUICK_SLOT), as the GAME menu
// does. The slots: arrows or 1-9 pick one (also the slot F2 / F4 use), S
// saves, L loads; Enter does nothing by itself, so a state is never loaded
// or overwritten by accident. PAUSE IN MENU pauses the game while it's
// open. F1 or Esc closes it.
//
// Split like every window: quick_menu_update reads the slots and
// quick_menu_state fills what the window shows (no GUI), quick_menu_draw
// draws it and records what was pressed, and quick_menu_apply acts on it (no
// GUI): the session's slot, the pictures to load. What a press does to the
// game is main.cpp's (the actions it shares with the GAME menu).

#include <cstdint>
#include <vector>

#include "core_session.h"
#include "localization.h"
#include "video_texture.h"

struct AppShell;
#include "retro_ui_font.h"

// The quick slot: QUICK SAVE / QUICK LOAD (the GAME menu's and QUICK MENU's)
// save and load it whatever slot is selected.
// TODO(round1): slot 0, as planned. host-libretro takes slots 1..9 only
// (LIBRETRO_STATE_SLOT_FIRST / _LAST, checked when saving and loading), so
// until it takes a slot 0 the quick slot is slot 1.
constexpr uint32_t QUICK_SLOT = LIBRETRO_STATE_SLOT_FIRST;

enum class QuickMenuPress
{
    none,
    pause,        // PAUSE / RESUME: the player's own pause (main.cpp toggles it)
    quick_save,   // the quick slot
    quick_load,
    screenshot,
    soft_reset,
    hard_reset,   // the core and the game loaded again
    config,       // open CORE CONFIGURATION (a MAIN MENU window: QUICK MENU closes)
    close_game,
    save,         // save to `slot`
    load,         // load `slot`
    dvr,          // GAME DVR: the rewind DVR (a window widget: it replaces QUICK MENU)
};

constexpr uint32_t QUICK_MENU_SLOTS = LIBRETRO_STATE_SLOT_LAST + 1;   // [FIRST..LAST] used
constexpr size_t   QUICK_MENU_LABEL_MAX = 24;

// A picture loaded from a slot's PNG (B8G8R8A8, opaque) and its texture.
struct QuickMenuPicture
{
    VideoTexture          texture;   // texture.ui_texture: what the toolkit draws; made the picture's size
    std::vector<uint32_t> pixels;
    uint32_t              width = 0;
    uint32_t              height = 0;
    bool                  upload_pending = false;
    bool                  shown = false;      // the texture holds `pixels`
    int64_t               modified = 0;       // the state's date the pixels are for (0: none)
    bool                  replace_wait = false;   // its texture is made again (another size) next frame
};

struct QuickMenu
{
    bool          open = false;
    uint32_t      slot = LIBRETRO_STATE_SLOT_FIRST;   // the selected slot
    StateSlotInfo slots[QUICK_MENU_SLOTS] = {};
    bool          refresh = true;   // read the slots again on the next update
    bool          focus_slot = false;   // move the keyboard focus to `slot` on the next draw (picked by 1-9)
    uint32_t      focus_wait = 0;       // draws left where only `slot`'s tile counts as focused (the focus moves late)

    // Each slot's thumbnail as saved (the picture at x1, at the aspect in
    // use: core_session.h), and the selected one scaled up by a whole number
    // (`preview_scale`) for the big picture.
    QuickMenuPicture thumbs[QUICK_MENU_SLOTS];
    QuickMenuPicture preview;
    uint32_t         preview_slot = 0;
    uint32_t         preview_scale = 0;
    // The big picture's size on screen at the last draw (window pixels):
    // what the whole number is chosen for.
    float            preview_width = 0.0f;
    float            preview_height = 0.0f;
};

struct QuickMenuState
{
    const Localization* strings = nullptr;
    const char*         core_name = "";
    const char*         game_name = "";
    bool                user_paused = false;   // the player paused: PAUSE reads RESUME
    char                labels[QUICK_MENU_SLOTS][QUICK_MENU_LABEL_MAX] = {};   // "3 · 00:30" (today), "1 · 11-24", "2"
};

struct QuickMenuAction
{
    QuickMenuPress press = QuickMenuPress::none;
    bool           closed = false;        // its X
    bool           slot_picked = false;   // `slot` changed this frame (a slot, 1-9)
};

// Opens on `slot` (the session's current slot).
void quick_menu_open(QuickMenu* menu, uint32_t slot);

// The slots changed on disk (a state was saved): read them again.
void quick_menu_refresh(QuickMenu* menu);

// Before drawing, every frame: reads the slots again when asked to.
void quick_menu_update(QuickMenu* menu, const CoreSession* session);
void quick_menu_state(QuickMenuState* state, const QuickMenu* menu, const Localization* strings, const char* core_name,
                      const char* game_name, bool user_paused);
// Draws the menu while open.
void quick_menu_draw(QuickMenu* menu, const QuickMenuState* state, RetroUiFont* pixel_font, int32_t scale,
                     QuickMenuAction* action);
// Closes it (its X), makes the picked slot the session's (F2 / F4 follow it)
// and loads the slots' thumbnails and the big picture. What `press` asks is
// main.cpp's.
void quick_menu_apply(QuickMenu* menu, const QuickMenuAction* action, CoreSession* session);

// Uploads newly loaded pictures. Call after app_shell_acquire, outside
// rendering (where the game's video is uploaded).
void quick_menu_upload(QuickMenu* menu, AppShell* shell);

// Frees the textures (waits for the GPU).
void quick_menu_destroy(QuickMenu* menu, AppShell* shell);
