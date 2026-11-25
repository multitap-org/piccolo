#pragma once

// TOOLS > PAIR (Andrés, 2026-11-22; docs/pairing.md, mockups
// docs/mockups/pairing/pairing-2-flow-lan.png): this piccolo asks another
// one to let it in. One small window that stays open and changes content:
//
//   +- PAIR -----------------------------------------+
//   |  PICCOLOS ON THIS NETWORK                       |
//   |  [KITCHEN PC                         ]          |
//   |  [DEN PC                     PAIRED  ]          |
//   |  YOUR CODE  482 193 706                         |
//   |  [INTERNET]                          [PAIR]     |
//   +------------------------------------------------+
//   then: the code, waiting with the port LEDs, [CANCEL]
//   then: PAIRED: KITCHEN PC GAVE YOU SHARE.
//         GIVE IT SHARE ON THIS PICCOLO TOO?  [YES] [NO]
//
// The connection is SHARE's (NetShareLink): pair_window_apply acts on it
// (connect / pair across the internet / cancel / give access back).
//
// Split like every window: pair_window_update follows SHARE's connection
// (the code, the outcome) and pair_window_state fills what the window shows
// (no GUI), pair_window_draw draws it and records what was pressed, and
// pair_window_apply acts on it (no GUI).

#include <cstdint>
#include <string>

#include "localization.h"
#include "net_service.h"
#include "net_share_link.h"

#include "retro_ui_font.h"

enum class PairWindowPhase : uint8_t
{
    list,      // pick a piccolo (or INTERNET)
    waiting,   // our code shown; the other side's user types it
    done,      // paired: access back?
};

struct PairWindow
{
    bool            open = false;
    PairWindowPhase phase = PairWindowPhase::list;
    int32_t         selected = -1;      // in the status' `found`
    bool            internet = false;   // waiting across the internet (INVITE on the other side)
    bool            failed = false;     // the last try didn't pair (shown in the list)
    char            code[NET_PAIR_CODE_DIGITS + 1] = {};
    std::string     host_name;          // done: who
    NetAccessLevel  given = NetAccessLevel::gamepad;   // done: what it gave us
    uint8_t         host_key[NET_KEY_SIZE] = {};
    bool            seen_connecting = false;   // waiting: the link took our request
    std::string     preselect;          // pair_window_open's name to pick, until the list shows it
};

struct PairWindowState
{
    const Localization*       strings = nullptr;
    const NetShareLinkStatus* link = nullptr;   // nullptr: networking couldn't start
    bool                      can_give_back = false;   // our listener runs (ALLOW CONNECTIONS)
};

enum class PairWindowPress
{
    none,
    pair,            // PAIR: found[selected] with our code, on the LAN
    pair_internet,   // INTERNET: through the beacon with our code
    cancel,          // CANCEL while waiting
    give_back,       // YES: give `given` back to the host (and close)
    close,           // closed (the title bar, OK, NO)
};

struct PairWindowAction
{
    PairWindowPress press = PairWindowPress::none;
};

// `preselect`: a name in the LAN list to pick (SHARE's PAIR), or nullptr.
void pair_window_open(PairWindow* window, const char* preselect);
// Before drawing, every frame the window is open: follows SHARE's connection
// (`link` nullptr: networking couldn't start).
void pair_window_update(PairWindow* window, const NetShareLinkStatus* link);
void pair_window_state(PairWindowState* state, const Localization* strings, const NetShareLinkStatus* link,
                       bool can_give_back);
void pair_window_draw(PairWindow* window, const PairWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                      PairWindowAction* action);
// Acts on SHARE's connection (`link` not started: only the window changes).
// `status`: the connection's last status (the LAN list), `beacon`: BEACON
// SERVER, `service`: our listener, nullptr when it doesn't run (no access
// back), `share_open`: SHARE uses the connection (closing after pairing
// keeps it).
void pair_window_apply(PairWindow* window, const PairWindowAction* action, NetShareLink* link,
                       const NetShareLinkStatus* status, const char* beacon, NetService* service, bool share_open);
