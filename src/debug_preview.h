#pragma once

// The debug window's previews (debug_window.h; Andrés: "anytime a new dialog
// is added, or a pop-up it needs to be added to the debug window so I can
// see it live"). The real notification cards, drawn from fake state kept
// here, never the App's: the real windows don't know, and nothing reaches
// the network, the settings or the game.
//
// Split like every window: the functions below fill and follow the fake
// state (no GUI); the cards go into the HUD with the windows' own
// *_add_hud.

#include <cstdint>

#include "guest_window.h"
#include "hud_view.h"
#include "localization.h"
#include "pair_window.h"

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
};

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
