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
//     No navigation: every button acts at once. BACK TO NOW keeps only its
//     own button (A / Escape, or clicked) since UP / DOWN switch lanes.
//       UP / DOWN      two lanes (Andrés, 2026-11-22, "Timelines: two for
//                      now"): PLAY FROM HERE keeps the future it leaves as
//                      the kept lane (a history of its own, as big as the
//                      recorded one), drawn above; UP goes to it, DOWN back
//                      to the recorded lane, at the same frame (or the
//                      lane's newest). PLAY FROM HERE on the kept lane plays
//                      on there and keeps the other one instead.
//       LEFT / RIGHT   SCRUB: plays that way at 1x (back: silent), until
//                      the other way, Y or an end; held, 2x while held.
//                      SNAPSHOTS: a snapshot; held, one a second
//                      (Andrés, 2026-11-22)
//       L / R          5 s (SCRUB) or a snapshot (INTERFACE's PAGE LEFT / RIGHT)
//       B (accept)     PLAY FROM HERE: play on from the cursor
//       Y (menu)       WATCH = RIGHT; while it plays either way, pauses
//                      (keyboard: Space)
//       A (back)       BACK TO NOW (the game as it was), and Escape
//     (The RetroPad's names for INTERFACE's default buttons: the bottom,
//     left and right face buttons and the shoulders.)
//     A move that can't go further (the oldest or newest frame) lands on
//     the end and the selected thumbnail bounces, its border blinking.
//     Each action is logged (what it did to the cursor).
//     The hints name the controls bound to those INTERFACE actions on the
//     gamepad used last (its profile's, layout's or SDL's names: A / X / B
//     LB RB on an Xbox pad, ✕ □ ○ L1 R1 on a PlayStation one), or the
//     keyboard's keys when it was used last (dvr_widget_name_keys).
//   - HUD (no input, never pauses): the PLAY state icon after PLAY FROM
//     HERE (state_widget), or a notification card saying why it didn't open.
//
// Split like every window: dvr_widget_prepare builds the strip's view from
// the history and dvr_widget_state fills its words (no GUI), dvr_widget_draw
// draws it and records what was clicked / tapped (and Space), and
// dvr_widget_apply acts on it (no GUI): the history's seek, play, watch,
// lane, BACK TO NOW.
//
// Kept behind these calls on purpose: the app's UI state is being reworked
// (ui-state) and rewires them without touching this file.

#include <cstdint>
#include <vector>

#include "core_rewind.h"
#include "dvr_view.h"
#include "video_texture.h"

struct AppShell;
struct HudStack;
struct CoreSession;
#include "retro_ui_font.h"
struct InputFrame;
struct InputLayouts;
struct InputSdl;
struct InputSystem;
struct Localization;
struct Settings;

struct DvrWidget
{
    CoreRewind rewind;            // the history (CoreSession::rewind points here)
    bool       open = false;
    // The core's aspect ratio (its geometry's; 0: its pictures' own, pixels
    // square). The thumbnails take it, as the game is shown (Andrés, testing
    // round 3). The caller sets it every frame (SET_GEOMETRY may change it).
    float      core_aspect = 0.0f;

    // The strip.
    DvrView               view;
    std::vector<uint64_t> previews;   // frames with a thumbnail (every 5 s in SCRUB, every snapshot); built at open
    uint64_t              preview_frames = 1;   // frames between two previews
    VideoTexture          thumbs[DVR_VIEW_MAX_THUMBS];
    uint64_t              thumb_frame[DVR_VIEW_MAX_THUMBS] = {};   // the preview each holds
    bool                  thumb_kept[DVR_VIEW_MAX_THUMBS] = {};    // made on the kept lane (frames before the fork: both)
    bool                  thumb_used[DVR_VIEW_MAX_THUMBS] = {};

    // LEFT / RIGHT: the last one pressed and since when (SCRUB: 2x while
    // held; SNAPSHOTS: the next repeat).
    int32_t  held_direction = 0;
    uint64_t held_since_ms  = 0;
    uint64_t next_step_ms   = 0;

    // SCRUB: which way it plays (-1 back, 0 still, 1 forward). Forward at 1x
    // is the history's own playback (heard); back and 2x step through the
    // states, `play_frames` keeping the part of a frame that's due.
    int32_t  play_direction = 0;
    double   play_frames    = 0.0;
    uint64_t play_last_ms   = 0;
    bool     play_stepping  = false;   // forward, stepped at 2x (back to the playback when let go)

    // The keyboard was used last (the hints show its keys, Space is WATCH).
    bool keyboard = false;
    // The gamepad used last in the interface (a controller index, -1 none
    // yet: port 1's first gamepad), whose buttons the hints name.
    int32_t gamepad = -1;
    // The hints' control names (dvr_widget_name_keys): PLAY FROM HERE,
    // WATCH, BACK TO NOW and the jumps' pair. "" = the RetroPad's.
    char key_accept[32] = {};
    char key_watch[32]  = {};
    char key_back[32]   = {};
    char key_pages[64]  = {};

    // A move hit an end of the history: the selected thumbnail bounces.
    int32_t  bump_direction = 0;   // -1 the oldest, 1 the newest
    uint64_t bump_ms        = 0;

    // Thumbnails cost a load and a frame, twice: with slow loads (bsnes,
    // 20 ms) they're spaced out so stepping stays smooth.
    uint64_t next_thumb_ms = 0;

    // Why it didn't open: a notification card for a few seconds (HUD).
    char     hud[96] = {};
    uint64_t hud_since_ms = 0;
    uint64_t played_from_ms = 0;   // the last PLAY FROM HERE: the PLAY state icon (state_widget)

    // The phone's strip held by a finger (dvr_view_draw_phone): SCRUB moves
    // faster the further it is from where it went down; SNAPSHOTS moves a
    // snapshot per picture it slides.
    bool     dragging = false;
    uint64_t drag_last_ms = 0;
    double   drag_frames = 0.0;   // SCRUB: the part of a frame due; SNAPSHOTS: of a picture

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

// Every frame, after input_update: the gamepads' and keyboard's actions
// while open (and, open or not, which gamepad was used last).
void dvr_widget_update(DvrWidget* widget, const InputFrame* frame, uint64_t now_ms);

// While open, before dvr_widget_prepare: names the hints' controls from the
// INTERFACE bindings of the gamepad used last, or the keyboard's.
void dvr_widget_name_keys(DvrWidget* widget, const InputSystem* input, const InputSdl* sdl, const InputLayouts* layouts);

// While open, in a frame (after app_shell_acquire, before the session's own
// upload): makes the next missing thumbnail.
void dvr_widget_upload(DvrWidget* widget, CoreSession* session, AppShell* shell);

// Window pixels the strip takes at the bottom (0 when closed): the game is
// drawn above it.
float dvr_widget_strip_height(const DvrWidget* widget, int32_t scale);

// Every frame before dvr_widget_state: the GUI follows the history (closed
// when the game stopped) and, while open, the strip's view is built from the
// cursor. `touch`: the hints are buttons, with arrows for the steps.
void dvr_widget_prepare(DvrWidget* widget, const Localization* strings, bool touch, uint64_t now_ms);

// What the strip shows besides its view (the widget's own, from prepare).
struct DvrWidgetState
{
    DvrViewText text;   // title, mode, kept, readout, BACK TO NOW, the lane, the phone's buttons
    bool  phone = false;          // the phone UI: dvr_view_draw_phone, at the touch scale
    float game_aspect = 4.0f / 3.0f;   // the game's picture (the phone's layout leaves it room)
};

// What a click / tap on the strip (or Space) did this frame.
struct DvrWidgetAction
{
    DvrViewAction press = DvrViewAction::none;
    int32_t       clicked = -1;      // a thumbnail clicked (-1 none): the cursor goes there
    bool          keyboard = false;  // Space was WATCH: the hints name the keyboard's keys
    bool          dragging = false;  // the phone's strip held (dvr_view_draw_phone's)
    float         drag_from = 0.0f;
    float         drag_moved = 0.0f;
};

void dvr_widget_state(DvrWidgetState* state, const DvrWidget* widget, const Localization* strings, bool phone,
                      float game_aspect);

// The phone UI while the DVR is open: the game's room (window pixels) on a
// screen this size, the panel taking the rest (dvr_view_phone_areas).
// False when closed.
bool dvr_widget_phone_game_area(const DvrWidget* widget, float screen_width, float screen_height, int32_t scale,
                                float game_aspect, float* width, float* height);

// The strip, above every window.
void dvr_widget_draw(DvrWidget* widget, const DvrWidgetState* state, RetroUiFont* pixel_font, int32_t scale,
                     DvrWidgetAction* action);

// A click / tap: as the buttons do.
void dvr_widget_apply(DvrWidget* widget, const DvrWidgetAction* action, uint64_t now_ms);

// Why it didn't open, as a notification card for a few seconds (hud_view).
void dvr_widget_add_hud(const DvrWidget* widget, HudStack* stack, uint64_t now_ms);

// For SETTINGS > GENERAL: AUTO measuring, or no savestates ("" = nothing; the
// mode in use is the caller's CURRENT MODE).
void dvr_widget_note(const DvrWidget* widget, const Localization* strings, char* out, size_t out_size);

// Frees the thumbnails (the history goes with the game: core_session_stop).
void dvr_widget_destroy(DvrWidget* widget, AppShell* shell);
