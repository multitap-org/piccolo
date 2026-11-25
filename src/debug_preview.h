#pragma once

// The debug window's previews (debug_window.h; Andrés: "anytime a new dialog
// is added, or a pop-up it needs to be added to the debug window so I can
// see it live"). The real notification cards and windows, drawn by their own
// functions from fake state kept here, never the App's: the real windows
// don't know, and nothing reaches the network, the settings or the game.
//
// One window preview at a time (DebugPreview). What's pressed in it is
// ignored, except closing it (its X, CLOSE, NO / YES, Escape: main.cpp's
// back), which ends it. The real window opening (a real pairing request,
// say) ends its preview too: both would be the toolkit's same window.
//
// Split like every window: debug_preview_show / _update / _state fill and
// follow the fake state (no GUI), debug_preview_draw draws the window shown
// with its own draw function, and debug_preview_apply ends the preview when
// it was closed (no GUI). The cards go into the HUD with the windows' own
// *_add_hud.

#include <cstdint>

#include "debug_window.h"
#include "guest_window.h"
#include "hud_view.h"
#include "localization.h"
#include "net_share_link.h"
#include "pair_window.h"

#include "retro_ui_font.h"

// The cards the debug window brings up for a few seconds.
enum class DebugCard
{
    link_copied,      // REMOTE PLAY's LINK COPIED TO THE CLIPBOARD
    pair_timed_out,   // PAIRING TIMED OUT
    pair_failed,      // PAIRING FAILED
};

struct DebugPreviews
{
    // The cards, each from a window's own state that only they use.
    GuestWindow card_guest;            // LINK COPIED: its copied_ms
    PairWindow  card_pair;             // PAIRING TIMED OUT / FAILED, and a request in the game
    bool        request_card = false;  // the fake request's card is up

    // The window shown, and the fake state its draw function gets.
    DebugPreview       shown = DebugPreview::none;
    PairWindow         pair;
    NetShareLinkStatus pair_link;      // the devices PAIR WITH A HOST finds
};

// What the preview shows this frame (UiView).
struct DebugPreviewView
{
    DebugPreview    shown = DebugPreview::none;
    PairWindowState pair = {};
};

// What was pressed in it (UiActions): only closing it counts.
struct DebugPreviewActions
{
    PairWindowAction pair = {};
};

// Shows `which` (another one shown ends), from its first step's fake state.
void debug_preview_show(DebugPreviews* previews, DebugPreview which);
void debug_preview_end(DebugPreviews* previews);
bool debug_preview_shown(const DebugPreviews* previews);
// Before the view, every frame: the real window open (`pair_open`) ends its
// preview.
void debug_preview_update(DebugPreviews* previews, bool pair_open);
void debug_preview_state(DebugPreviewView* view, const DebugPreviews* previews, const Localization* strings);
// Draws the window shown, if any.
void debug_preview_draw(DebugPreviews* previews, const DebugPreviewView* view, RetroUiFont* pixel_font,
                        int32_t scale, DebugPreviewActions* actions);
// Ends the preview when it was closed; everything else pressed is ignored.
void debug_preview_apply(DebugPreviews* previews, const DebugPreviewActions* actions);

void debug_preview_show_card(DebugPreviews* previews, DebugCard card, uint64_t now_ms);
// The fake pairing request's card ("<NAME> WANTS TO PAIR / HOLD START TO
// ACCEPT"): up until taken down, or until START held on it.
void debug_preview_request_card(DebugPreviews* previews, bool shown, uint64_t now_ms);
// Every frame: START held (`held`, pair_window_hold_accept's) while the
// player is in the game (`in_game`) fills the fake card's key as on the real
// one; once full the card goes and nothing else happens (no request is
// there to accept: no window opens).
void debug_preview_hold_request(DebugPreviews* previews, bool in_game, bool held, uint64_t now_ms);
// The fake cards, with the real ones (the request's shows with the menu up
// too, so it can be seen from the debug window).
void debug_preview_add_hud(const DebugPreviews* previews, const Localization* strings, HudStack* stack,
                           uint64_t now_ms);
