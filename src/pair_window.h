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
// The connection is SHARE's (NetShareLink): main.cpp acts on the requests
// below (connect / pair across the internet / cancel / give access back).

#include <cstdint>
#include <string>

#include "localization.h"
#include "net_share_link.h"

struct ImFont;

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
    // Requests for main.cpp, cleared by it.
    bool            want_pair = false;           // LAN: found[selected] with `code`
    bool            want_pair_internet = false;  // through the beacon with `code`
    bool            want_cancel = false;         // drop the connection (CANCEL, close while waiting)
    bool            want_give_back = false;      // YES: give `given` back to host_key
    bool            want_close = false;          // closed after pairing: main drops the link unless SHARE uses it
};

struct PairWindowContext
{
    const Localization*       strings = nullptr;
    const NetShareLinkStatus* link = nullptr;   // nullptr: networking couldn't start
    bool                      can_give_back = false;   // our listener runs (ALLOW CONNECTIONS)
};

// `preselect`: a name in the LAN list to pick (SHARE's PAIR), or nullptr.
void pair_window_open(PairWindow* window, const char* preselect);
void pair_window_draw(PairWindow* window, const PairWindowContext* context, ImFont* pixel_font, int32_t scale);
