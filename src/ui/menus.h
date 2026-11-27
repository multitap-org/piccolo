#pragma once

// What piccolo's menus contain: menus, their items and the action each item
// triggers. Only data; retro_ui.h draws it. What runs decides what shows
// (testing round 1, Andrés 2026-11-25):
//
//   GAME, no game     LOAD GAME, CLOSE GAME (greyed), QUIT
//   GAME, a game      LOAD GAME, CLOSE GAME, ---, PAUSE / RESUME, QUICK SAVE,
//                     QUICK LOAD, UNDO SAVE STATE, UNDO LOAD STATE (greyed with
//                     nothing to undo), TAKE SCREENSHOT, SOFT RESET, HARD RESET,
//                     CORE CONFIGURATION, QUIT
//   SETTINGS          GENERAL, INTERFACE, VIDEO, SOUND, NETWORK (the window's
//                     tabs, in order), INPUT
//   TOOLS             GAME DVR (greyed without a game), CORE MANAGER, CORE
//                     OPTIONS MANAGER, ---, the network tools
//   MISC              DEBUG, ABOUT
//
// RECENT (main.cpp builds it: the games, then CLEAR ALL) comes first.

#include <cstdint>

#include "piccolo_text.h"   // Str

enum class MenuAction
{
    none,
    pause,         // GAME > PAUSE / RESUME (the USER pause): back to the game, everything else closed
    load_game,
    close_game,    // GAME > CLOSE GAME (also QUICK MENU's)
    reset,         // GAME > SOFT RESET: the core's own reset
    hard_reset,    // GAME > HARD RESET: the core and the game loaded again
    quick_save,    // GAME > QUICK SAVE: the quick slot (QUICK MENU's too)
    quick_load,    // GAME > QUICK LOAD
    save_state,    // the selected slot (the save / load hotkeys, a remote device's)
    load_state,
    undo_save_state,   // GAME > UNDO SAVE STATE: the slot saved last back as before (also its hotkey, a device's)
    undo_load_state,   // GAME > UNDO LOAD STATE: the game back as before the last state load
    screenshot,    // GAME > TAKE SCREENSHOT (also the screenshot hotkey)
    quit,
    general,
    interface,
    video,
    sound,
    input,
    about,
    debug_window,  // MISC > DEBUG
    recent_game,   // from the RECENT menu (see retro_ui_menu_bar)
    clear_recents, // RECENT > CLEAR ALL
    game_dvr,      // TOOLS > GAME DVR (the rewind DVR; also QUICK MENU's)
    options_manager,   // TOOLS > CORE OPTIONS MANAGER (stored core options)
    cores,         // TOOLS > CORE MANAGER (the core downloader)
    share,         // TOOLS > FILE TRANSFER (files with another piccolo)
    pair,          // TOOLS > PAIR... (both ways: with a host, with a client)
    guests,        // TOOLS > REMOTE PLAY (a guest link for the running game)
    network,       // CONFIG > NETWORK (listener, pairing, paired devices)
    core_config,   // GAME > CORE CONFIGURATION (the running core's options, controls, shader)
    separator,     // not an action: a line between items (its label isn't shown)
};

// When an item shows, by whether a game runs.
enum class MenuShown
{
    always,
    with_game,       // hidden while no game runs
    greyed_no_game,  // greyed out while no game runs (people see where it is)
};

int32_t     menus_count();
Str         menus_label(int32_t menu);
int32_t     menus_item_count(int32_t menu);
Str         menus_item_label(int32_t menu, int32_t item);
MenuAction  menus_item_action(int32_t menu, int32_t item);
MenuShown   menus_item_shown(int32_t menu, int32_t item);

// "LOAD GAME" etc., for logs (English on purpose).
const char* menu_action_name(MenuAction action);
