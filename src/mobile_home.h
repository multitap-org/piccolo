#pragma once

// piccolo on a phone (PICCOLO_MOBILE, or --mobile on the desktop): HOME and
// the ☰ sidebar, in piccoletto's look (Andrés, 2026-11-25; mockup:
// https://claude.ai/artifact/JEs4eBAEwF6XthZ4PjPHS2).
//
//   HOME     the screen while no game runs, in place of the menu bar: the
//            header (piccolo at the left, ROTATE and ☰ at the right), the
//            recent games as big rows (tap one: it starts), LOAD GAME at the
//            bottom. No recent games: a card that says so, with LOAD GAME.
//   ☰        a sidebar from the right with the menu bar's menus (the same
//            RetroUiMenu data main.cpp builds for the bar, so items show,
//            grey and hide by the same rules), RECENT left out: it is HOME.
//            Over a paused game (MAIN MENU with a game, nothing open) it is
//            always up, RESUME first; a tap outside or › goes back to the game.
//
// The windows the items open are the desktop's own, drawn as full-screen
// pages (retroui's page mode). A pick returns the menu bar's own item id, so
// main.cpp applies it exactly as a pick on the bar.
//
// Split like every window: main.cpp fills MobileHomeState (no GUI),
// mobile_home_draw draws and records what was tapped, mobile_home_apply
// follows it (no GUI); main.cpp acts on the pick.

#include <cstdint>

#include "localization.h"
#include "retro_ui.h"   // RetroUiMenu

#include "retro_ui_font.h"

struct MobileHome
{
    bool  menu_open   = false;   // the ☰ sidebar over HOME
    float scroll      = 0.0f;    // HOME's list, retro pixels
    float menu_scroll = 0.0f;    // the sidebar's
};

// A recent game's two lines.
struct MobileGameRow
{
    const char* name = "";   // "Super Mario World (USA)", or NO GAME for a core alone
    const char* core = "";   // "snes9x"
};

struct MobileHomeState
{
    const Localization* strings = nullptr;
    bool home_shown = false;    // HOME (no game runs)
    bool menu_shown = false;    // the ☰ sidebar
    bool menu_in_game = false;  // ... over a paused game: RESUME first, a tap outside resumes

    const MobileGameRow* games = nullptr;   // most recent first
    int32_t game_count = 0;
    int32_t first_game_id = 0;              // the bar's id of games[0]; games[i] is that + i
    int32_t load_game_id = 0;               // LOAD GAME's id
    int32_t clear_recents_id = 0;           // CLEAR ALL's id

    const RetroUiMenu* menus = nullptr;     // the menu bar's; menus[0] is RECENT (left out)
    int32_t menu_count = 0;
};

struct MobileHomeAction
{
    int32_t picked = RETRO_UI_MENU_NONE;   // a menu item's, a recent game's or LOAD GAME's id
    bool    rotate = false;                // the header's ROTATE
    bool    open_menu = false;             // the header's ☰
    bool    close_menu = false;            // ›, a tap outside, RESUME
};

// Draws HOME and the sidebar, each when shown. `scale`: the touch scale
// (touch_ui_scale_for).
void mobile_home_draw(MobileHome* home, const MobileHomeState* state, RetroUiFont* pixel_font, int32_t scale,
                      MobileHomeAction* action);

// The sidebar opens with ☰ and closes with any pick or close.
void mobile_home_apply(MobileHome* home, const MobileHomeAction* action);
