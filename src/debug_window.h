#pragma once

// The debug window (MENU > DEBUG): versions, the GPU, timing and audio
// numbers, and WIDGETS, every widget on demand to see and try them without
// the situation that brings them up (Andrés, 2026-11-21). In English only:
// it's for developers. The numbers stay in view; the widget sections fold
// away (testing round 1).
//
// The rule (Andrés): every new dialog, window or pop-up gets a way to show it
// here, live, without the situation that brings it up: a HUD card with fake
// text, or a preview of the window with fake state (debug_preview.h).
//
// The pattern of every window: main.cpp fills a DebugWindowState (plain
// data, no GUI), debug_window_draw draws it with retroui and returns what was
// pressed as a DebugWindowAction, and main.cpp applies it.

#include <cstdint>

#include "retro_ui_font.h"

// The state icons the window can force on (StateIcon order: pause, rewind,
// play, fast forward).
constexpr uint32_t DEBUG_WINDOW_ICON_COUNT = 4;

// The windows the debug window shows as previews (debug_preview.h), one at a
// time: drawn by their own draw functions with fake state, what's pressed in
// them ignored, closing them (X, CLOSE, Escape) ends the preview.
enum class DebugPreview
{
    none,
    // PAIR, each step of both modes (pair_window.h).
    pair_device_list,          // PAIR A DEVICE: the devices found, ENTER A CODE
    pair_device_type,          // the code a device on this network popped up, to type
    pair_device_internet,      // ENTER A CODE: the code from a device elsewhere
    pair_device_done,          // paired: its permissions here (an unsigned build)
    pair_code_shown,           // GENERATE PAIRING CODE: our code, waiting
    pair_code_request,         // a device asking: its code and permissions
    pair_code_unsigned,        // the red UNSIGNED BUILD question (FILE TRANSFER ticked)
    pair_code_done,            // paired: permissions granted here and there
    about,                     // MISC > ABOUT (about_window.h)
    // REMOTE PLAY (guest_window.h).
    remote_no_link,            // before CREATE LINK
    remote_no_guests,          // a link, nobody in
    remote_guests,             // a link, a player on a port and a spectator
    quick_menu,                // QUICK MENU with SAVE STATES open (quick_menu.h): no game needed
    // The phone UI (mobile_home.h), at the phone's touch scale.
    phone_home,                // HOME with recent games
    phone_home_empty,          // HOME, no recent games yet
    phone_menu,                // the ☰ sidebar over a paused game (RESUME first)
    phone_dvr,                 // the DVR's phone panel (dvr_view_draw_phone): a kept lane, no pictures
};

// MENU > DEBUG > SNOW: the menu snow (an easter egg with no setting) forced
// on or off, or left to the season. Kept in memory only, never saved.
enum class DebugSnow
{
    automatic,   // AUTO: December 24 to 28, local date
    on,
    off,
};

struct DebugWindowState
{
    // About
    const char* piccolo_version = "";
    const char* host_version    = "";
    const char* gpu_device      = "";
    const char* gpu_api         = "";
    const char* gpu_swapchain   = "";
    const char* gpu_validation  = "";
    int32_t     ui_scale        = 1;
    double      screen_hz       = 0.0;   // loops presented in the last second
    double      work_ms         = 0.0;   // the loop's own work, on average

    // The game, when one runs
    bool        game_running   = false;
    const char* core_name      = "";
    double      core_fps       = 0.0;
    const char* timing_mode    = "";
    double      audio_ratio    = 1.0;
    uint32_t    audio_queued_ms = 0;
    double      audio_target_ms = 0.0;
    bool        audio_automatic = false;
    uint64_t    audio_underruns = 0;
    uint64_t    missed_refreshes = 0;
    uint64_t    caught_up_frames = 0;
    uint32_t    frame_delay_ms   = 0;

    // WIDGETS
    bool icons_forced[DEBUG_WINDOW_ICON_COUNT] = {};
    bool devices_available = false;   // a port has a controller (DEVICES has something to show)
    bool text_box_allowed  = false;   // no text box is pending already
    bool keep_revert_allowed = false; // KEEP / REVERT isn't asking already
    bool pair_request_card = false;   // the fake pairing request's card is up
    bool pair_open = false;           // the real PAIR is open (no preview of it meanwhile)
    bool about_open = false;          // the real ABOUT
    bool guest_open = false;          // the real REMOTE PLAY

    // EASTER EGGS
    DebugSnow snow = DebugSnow::automatic;
    bool snow_in_season = false;      // today is in the snow's season (local date)
    bool snow_falling = false;        // the snow shows now (the empty background or MAIN MENU)
};

enum class DebugWindowPress
{
    none,
    remap_card,       // a remap card for port 1's first controller
    play_from_here,   // the PLAY icon as after PLAY FROM HERE
    dvr_card_off,
    dvr_card_unsupported,
    dvr_card_nothing,
    ports_rise,       // the ports' tabs rise
    open_devices,
    open_dvr,
    text_box,         // a fake TEXT request from the game
    keep_revert,      // the KEEP / REVERT question
    card_link_copied,     // REMOTE PLAY's LINK COPIED TO THE CLIPBOARD
    card_pair_timed_out,  // PAIRING TIMED OUT
    card_pair_failed,     // PAIRING FAILED
    card_undo_save,       // SAVE STATE UNDONE, SLOT 3
    card_undo_load,       // LOAD STATE UNDONE
    card_undo_nothing,    // NOTHING TO UNDO
    card_resumed,         // RESUMED WHERE YOU LEFT OFF (SAVE STATE ON EXIT)
    card_resume_failed,   // COULD NOT RESUME WHERE YOU LEFT OFF
    effect_snap,          // the REWIND screen effect (SNAP), as after PLAY FROM HERE
    effect_shutter,       // the SCREENSHOT screen effect (SHUTTER), without a screenshot
    sleigh_now,           // the December 25 sleigh crosses now (the snow must be falling)
};

struct DebugWindowAction
{
    DebugWindowPress press = DebugWindowPress::none;
    bool closed = false;
    bool icons_forced[DEBUG_WINDOW_ICON_COUNT] = {};   // the checkboxes after this frame
    bool pair_request_card = false;                    // its checkbox after this frame
    DebugPreview preview = DebugPreview::none;         // a preview to show
    DebugSnow snow = DebugSnow::automatic;             // SNOW after this frame
};

// Draws the window (inside the retro theme's push / pop, done here).
// action->icons_forced, pair_request_card and snow start as the state's.
void debug_window_draw(const DebugWindowState* state, RetroUiFont* pixel_font, int32_t scale, DebugWindowAction* action);
