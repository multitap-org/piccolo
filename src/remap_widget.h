#pragma once

// HOLD START TO REMAP, as a HUD widget (Andrés, 2026-11-21; piccolo-agents
// design-notes "HOLD START TO REMAP as a widget", mockups
// docs/mockups/widgets/remap-*.png):
//
//   +-[PORT 1] SN30 PRO -------- 4-+     a notification card, bottom right,
//   | HOLD [START] TO REMAP         |     one per controller whose remap for
//   +=========-------------------- -+     the running core isn't decided
//
//   - Asked for each local controller on a port that plays a core's device
//     with no decided InputCoreProfile (the game's, else the core's): the
//     same rule as the old prompt. Network pads remap on their own device
//     (not designed yet); headless: no cards (nobody sees them).
//   - No pause, input untouched (a HUD): the game sees the button too.
//   - REMAP (a hotkey binding the router never fires, START by default)
//     held 1 s by that controller opens QUICK MENU on CONTROLS' remap of
//     that port and controller.
//   - 5 s without it: the RetroPad is kept and remembered as decided.
//     The countdown runs only in GAME (elsewhere the button is the UI's) and
//     stops while the button is held.
//
// The router's profile lookups allocate, so the ports are scanned again only
// when who plays where (or a port's device) changes, when a game starts and
// when the player is back in GAME; a summary compared every frame tells.

#include <cstdint>
#include <vector>

#include "input_bindings.h"
#include "input_hold.h"

struct HudStack;
struct InputLayouts;
struct InputSdl;
struct Localization;

constexpr uint64_t REMAP_WIDGET_ASK_MS = 5000;    // untouched this long: the RetroPad is kept
constexpr uint64_t REMAP_WIDGET_HOLD_MS = 1000;   // REMAP held this long opens the remap

struct RemapCard
{
    char      label[32] = {};   // the controller's name, taken when the card appeared (HudCard::title)
    char      key[16] = {};     // REMAP's button on it (HudCard::key)
    uint32_t  controller = 0;
    uint32_t  port = 0;
    uint64_t  since_ms = 0;     // when it appeared
    uint64_t  counted_ms = 0;   // of REMAP_WIDGET_ASK_MS, counted in GAME while not held
    InputHold hold;
    bool      debug = false;    // from the debug window: kept by scans, never remembers a RetroPad
};

struct RemapWidget
{
    std::vector<RemapCard> cards;      // reserved at create: never grown past INPUT_PORT_COUNT * 4
    std::vector<uint64_t>  answered;   // controller * INPUT_PORT_COUNT + port already asked this game
    uint64_t summary = 0;              // who plays where, as last scanned
    bool     scan = true;              // scan again on the next update
    uint64_t last_ms = 0;
};

// What the update decided for the frontend to do.
struct RemapWidgetResult
{
    bool     open_remap = false;   // REMAP held: open QUICK MENU on this remap
    uint32_t port = 0;
    uint32_t controller = 0;
    bool     save = false;         // a RetroPad was remembered: save input.cfg
};

void remap_widget_create(RemapWidget* widget);

// A game started (or stopped): nothing asked yet, scan again.
void remap_widget_game_started(RemapWidget* widget);

// The player is back in GAME (a remap may have been decided meanwhile).
void remap_widget_rescan(RemapWidget* widget);

// Every frame after input_update. `in_game`: GAME mode (the countdown and the
// hold run); `headless`: no cards at all.
RemapWidgetResult remap_widget_update(RemapWidget* widget, InputSystem* input, const InputSdl* sdl,
                                      const InputLayouts* layouts, bool game_running, bool in_game, bool headless,
                                      uint64_t now_ms);

// The debug window's WIDGETS: a card for port 1's first controller whatever
// its remap (holding REMAP opens it; the countdown remembers nothing). False
// when no controller is on port 1.
bool remap_widget_debug_card(RemapWidget* widget, const InputSystem* input, const InputSdl* sdl,
                             const InputLayouts* layouts, uint64_t now_ms);

// Its cards, into the frame's HUD stack.
void remap_widget_add_hud(const RemapWidget* widget, const Localization* strings, HudStack* stack, uint64_t now_ms);
