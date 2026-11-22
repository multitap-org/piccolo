#pragma once

// TOOLS > REMOTE PLAY TOGETHER (guest links, design-notes "Guest links",
// Andrés 2026-11-22): a link friends open in piccoletto to watch the game, and
// play when they have a seat and a port is free. No pairing, nothing saved.
//
// Before the link (the screenshot for the card is taken when the window opens):
//   +- REMOTE PLAY TOGETHER ------------------------------------+
//   |  GAME (ON THE CARD)  [TMNT IV TURTLES IN TIME          ]   |
//   |  SEATS               [- 2 +]                              |
//   |  ACCESS              [WATCH][WATCH + PLAY]                |
//   |  ANYONE WITH THE LINK TAKES A FREE SEAT, ...              |
//   |                                 [MAKE LINK] [CANCEL]      |
//   +-----------------------------------------------------------+
// With the link:
//   +- REMOTE PLAY TOGETHER ------------------------------------+
//   |  LINK [https://beacon.get-piccolo.app/join/...] [COPY][NEW]|
//   |  SEATS [- 3 +]   ACCESS [WATCH][WATCH + PLAY]             |
//   |  | PIXEL 8     PLAYS P2              ping  [KICK] |       |
//   |  | GALAXY S22  WATCHES: NO FREE PORT ping  [KICK] |       |
//   |  SEATS TAKEN: 2 / 3     COULDN'T CONNECT: 1               |
//   |  CARD: ON THE LINK                                        |
//   |  [END]                                       [CLOSE]      |
//   +-----------------------------------------------------------+
// Closing the window keeps the link; END, closing the game, loading another
// one, or quitting ends the session (main.cpp).
//
// The window acts on the running NetAccess (drawn under the network lock):
// seats, level and KICK at once. MAKE LINK, NEW and END need the frame and
// the card upload: they're left in `action` for main.cpp.

#include <cstdint>
#include <vector>

#include "guest_card.h"
#include "input_net.h"
#include "localization.h"
#include "net_access.h"

enum class GuestWindowAction
{
    none,
    make_link,
    new_link,
    end,
};

struct GuestWindow
{
    bool     open = false;
    bool     live = false;                       // a link is open on the NetAccess
    char     title[NET_GUEST_MAX_TITLE + 1] = {};
    int32_t  seats = 2;
    uint32_t level = 1;                          // NetGuestLevel: WATCH + PLAY by default
    char     link_text[NET_GUEST_MAX_LINK] = {};
    NetGuestLink link;
    uint32_t game_starts = 0;                    // the game the session belongs to (App::game_starts)
    std::vector<uint8_t> png;                    // the card's picture, taken when the window opened
    GuestCardUpload card;
    double   copied_at = -1.0;                   // COPY pressed (shows COPIED for a moment)
    // The port each guest was last told (NET_GUEST_PORT_MESSAGE), per peer.
    uint8_t  told_port[NET_MAX_PEERS] = {};
    bool     told[NET_MAX_PEERS] = {};
    GuestWindowAction action = GuestWindowAction::none;
};

// Opens the window (the caller has filled `title` and `png` for the running game).
void guest_window_open(GuestWindow* window);

// Draws it (under the network lock: `access` may be nullptr when the network
// is off). `internet`: the beacon is on (a link needs it). `ports`: the
// router, for who plays where.
void guest_window_draw(GuestWindow* window, NetAccess* access, const InputNet* input_net, const InputSystem* input,
                       bool game, bool internet, const Localization* strings);

// The session is over (END, the game, quitting): guests told, link closed.
void guest_window_end(GuestWindow* window, NetAccess* access);

// Tells each guest its port when it changed (once a frame, under the lock).
void guest_window_tell_ports(GuestWindow* window, NetAccess* access, const InputNet* input_net,
                             const InputSystem* input);
