#pragma once

// The rewind DVR in piccolo (Andrés, 2026-11-20 / 21; docs/design-notes.md
// "Rewind / DVR" and "UI modes"):
//
//   - The history: frontend-libretro's core_rewind, recorded by the core
//     session around every frame. Started once the game's settings are
//     known (dvr_widget_game_started): GENERAL > REWIND, and AUTO's earlier
//     verdict for the game (REWIND KNOWN, in the game's layer).
//   - The GUI is a window widget: it takes the input (one window at a time,
//     opening it closes the QUICK MENU) and the game stops while it's open
//     (its own logic, not a pause cause). Drawn with retroui's dvr_view: the
//     game shrinks into the space above the strip.
//     No navigation but one (Andrés, 2026-11-21): UP focuses the exit
//     button (BACK TO NOW), DOWN or any move leaves it. Every other button
//     acts at once.
//       LEFT / RIGHT   a frame (SCRUB) or a snapshot; held, faster
//       L / R          5 s (SCRUB) or a snapshot
//       B (accept)     PLAY FROM HERE: play on from the cursor (the exit
//                      button focused: BACK TO NOW)
//       Y              WATCH: the recording plays / pauses (keyboard: Space)
//       A (back)       BACK TO NOW (the game as it was), and Escape
//     Each action is logged (what it did to the cursor).
//     The hints name the keyboard's keys when it was used last.
//   - HUD (no input, never pauses): the PLAY state icon after PLAY FROM
//     HERE (state_widget), or a notification card saying why it didn't open.
//
// Kept behind these calls on purpose: main.cpp's UI state is being reworked
// (ui-state) and rewires them without touching this file.

#include <cstdint>
#include <vector>

#include "core_rewind.h"
#include "dvr_view.h"
#include "video_texture.h"

struct AppShell;
struct HudStack;
struct CoreSession;
struct ImFont;
struct InputFrame;
struct Localization;
struct Settings;

struct DvrWidget
{
    CoreRewind rewind;            // the history (CoreSession::rewind points here)
    bool       open = false;

    // The strip.
    DvrView               view;
    std::vector<uint64_t> previews;   // frames with a thumbnail (every 5 s in SCRUB, every snapshot); built at open
    uint64_t              preview_frames = 1;   // frames between two previews
    VideoTexture          thumbs[DVR_VIEW_MAX_THUMBS];
    uint64_t              thumb_frame[DVR_VIEW_MAX_THUMBS] = {};   // the preview each holds
    bool                  thumb_used[DVR_VIEW_MAX_THUMBS] = {};

    // LEFT / RIGHT held: steps repeat, then faster.
    int32_t  held_direction = 0;
    uint64_t held_since_ms  = 0;
    uint64_t next_step_ms   = 0;

    // The keyboard was used last (the hints show its keys, Space is WATCH).
    bool keyboard = false;
    // UP went to the exit button: ACCEPT is BACK TO NOW.
    bool exit_focused = false;

    // Thumbnails cost a load and a frame, twice: with slow loads (bsnes,
    // 20 ms) they're spaced out so stepping stays smooth.
    uint64_t next_thumb_ms = 0;

    // Why it didn't open: a notification card for a few seconds (HUD).
    char     hud[96] = {};
    uint64_t hud_since_ms = 0;
    uint64_t played_from_ms = 0;   // the last PLAY FROM HERE: the PLAY state icon (state_widget)

    char readout[16] = {};
    char kept[32]    = {};
    char mode[48]    = {};
};

// The game just started (the session runs it, the settings' context is the
// game's): starts recording as GENERAL > REWIND says.
void dvr_widget_game_started(DvrWidget* widget, CoreSession* session, const Settings* settings);
// AUTO decided this frame: the mode to keep as the game's REWIND KNOWN
// (scrub / snapshots). False otherwise.
bool dvr_widget_take_verdict(DvrWidget* widget, RewindMode* known);

// Opens the GUI. False (and the HUD says why) if there's nothing to go back to.
bool dvr_widget_open(DvrWidget* widget, const Localization* strings, uint64_t now_ms);
// BACK TO NOW: closes, the game as it was.
void dvr_widget_close(DvrWidget* widget);

// While open, after input_update: the gamepads' and keyboard's actions.
void dvr_widget_update(DvrWidget* widget, const InputFrame* frame, uint64_t now_ms);

// While open, in a frame (after app_shell_acquire, before the session's own
// upload): makes the next missing thumbnail.
void dvr_widget_upload(DvrWidget* widget, CoreSession* session, AppShell* shell);

// Window pixels the strip takes at the bottom (0 when closed): the game is
// drawn above it.
float dvr_widget_strip_height(const DvrWidget* widget, int32_t scale);

// The strip (above every window) and what a click / tap on it did.
void dvr_widget_draw(DvrWidget* widget, ImFont* pixel_font, int32_t scale, const Localization* strings, bool touch,
                     uint64_t now_ms);
// Why it didn't open, as a notification card for a few seconds (hud_view).
void dvr_widget_add_hud(const DvrWidget* widget, HudStack* stack, uint64_t now_ms);

// For SETTINGS > GENERAL: what AUTO chose for the running game ("" = nothing).
void dvr_widget_note(const DvrWidget* widget, const Localization* strings, char* out, size_t out_size);

// Frees the thumbnails (the history goes with the game: core_session_stop).
void dvr_widget_destroy(DvrWidget* widget, AppShell* shell);
