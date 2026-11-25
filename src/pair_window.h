#pragma once

// TOOLS > PAIR... (Andrés, 2026-11-22; docs/pairing.md; one window since
// testing round 1, item 5): pairing both ways, in one window whose skeleton
// never changes. Its width stays the same at every step; only the panel's
// contents and the buttons change.
//
//   +- PAIR -------------------------------------------------+
//   | [ PAIR WITH A HOST        ][ PAIR WITH A CLIENT       ] |
//   | +----------------------------------------------------+ |
//   | | (the step: a panel of fixed height)                | |
//   | +----------------------------------------------------+ |
//   |   WAITING FOR DEVICE  (blinking, only while waiting)   |
//   | [ PAIR                    ][ CLOSE                    ] |
//   +--------------------------------------------------------+
//
// PAIR WITH A HOST (was PAIR): this piccolo asks another one to let it in.
//   1  DETECTED DEVICES, the list, then PAIR OVER THE INTERNET...
//   2  TYPE THIS CODE ON <HOST> (or GIVE THIS CODE TO THE OTHER USER), our
//      code in three boxes, WAITING FOR DEVICE, CANCEL
//   3  PAIRED WITH <HOST>, ROLE, LET <HOST> CONNECT TO THIS DEVICE, WITH
//      THE SAME ROLE?  YES / NO
//   The connection is FILE TRANSFER's (NetShareLink).
//
// PAIR WITH A CLIENT (was INVITE and the pairing request dialog): a device
// comes to this one.
//   1  TYPE THE CODE DISPLAYED ON THE OTHER DEVICE, the boxes typed into,
//      ROLE; ALLOW (once the nine digits are in) waits for it in the code's
//      pairing room on the beacon
//   request  a device asking on the network (the game pauses meanwhile):
//      <NAME> WANTS TO PAIR, its code typed, ROLE, ALLOW / DENY. It opens
//      the window on this mode by itself.
//   2  the code and the role, WAITING FOR DEVICE, CANCEL
//   3  PAIRED WITH <NAME>, ROLE, CLOSE
//   These answer on the NetAccess (under the network lock).
//
// While a step of one mode is in progress the other mode is greyed out, so
// a half-done pairing isn't left behind by switching.
//
// Split like every window: pair_window_update follows FILE TRANSFER's
// connection and the requests (no GUI), pair_window_state fills what the
// window shows, pair_window_draw draws it and records what was pressed, and
// the two applies act on it (no GUI): the host side on the connection, the
// client side on the NetAccess.

#include <cstdint>
#include <string>

#include "localization.h"
#include "net_access.h"
#include "net_service.h"
#include "net_share_link.h"

#include "retro_ui_font.h"

enum class PairMode : uint8_t
{
    host,     // PAIR WITH A HOST
    client,   // PAIR WITH A CLIENT
};

enum class PairWindowPhase : uint8_t
{
    list,      // pick a piccolo (or PAIR OVER THE INTERNET...)
    waiting,   // our code shown; the other side's user types it
    done,      // paired: access back?
};

// A device asking to pair on the network (NetAccessEventType::pairing_requested):
// the code its screen shows is typed here, with the role to give it.
struct PairRequest
{
    bool        active = false;
    bool        waiting = false;   // ALLOW pressed: until paired, or a new request (wrong code)
    bool        retry = false;     // the last code typed was wrong
    bool        focus = false;     // the code boxes take the keyboard next frame
    bool        fresh = false;     // just came (or came back with a new code): the window is raised for it
    uint32_t    peer = 0;
    char        name[NET_DEVICE_NAME_MAX + 1] = {};
    char        code[NET_PAIR_CODE_DIGITS + 1] = {};
    uint32_t    level = 0;         // NetAccessLevel
};

// PAIR WITH A CLIENT's own code (was TOOLS > INVITE): a device across the
// internet shows a code; typed here with the role, ALLOW waits in that
// code's pairing room on the beacon (net_access_invite) and answers the
// request that comes from there.
struct PairInvite
{
    bool     waiting = false;      // ALLOW pressed: in the pairing room
    bool     answered = false;     // a request came and was answered with our code
    uint32_t peer = 0;             // the one answered
    char     code[NET_PAIR_CODE_DIGITS + 1] = {};
    uint32_t level = 0;            // NetAccessLevel
    bool     focus = false;
};

struct PairWindow
{
    bool            open = false;
    PairMode        mode = PairMode::host;
    bool            select_mode = false;   // the drawing switches the tabs to `mode` (a request came)
    bool            raise = false;         // the drawing brings the window over every other one (a request came)

    // PAIR WITH A HOST.
    PairWindowPhase phase = PairWindowPhase::list;
    int32_t         selected = -1;      // in the status' `found`
    bool            internet_selected = false;   // PAIR OVER THE INTERNET... is the one picked
    bool            internet = false;   // waiting across the internet (a client's code on the other side)
    char            code[NET_PAIR_CODE_DIGITS + 1] = {};
    std::string     host_name;          // waiting on the network / done: who
    NetAccessLevel  given = NetAccessLevel::gamepad;   // done: what it gave us
    uint8_t         host_key[NET_KEY_SIZE] = {};
    bool            seen_connecting = false;   // waiting: the link took our request
    std::string     preselect;          // pair_window_open's name to pick, until the list shows it

    // PAIR WITH A CLIENT.
    PairRequest     request;
    PairInvite      invite;
    bool            client_done = false;     // paired: who, and the role given
    std::string     client_name;
    NetAccessLevel  client_level = NetAccessLevel::gamepad;
};

struct PairWindowState
{
    const Localization*       strings = nullptr;
    const NetShareLinkStatus* link = nullptr;   // nullptr: FILE TRANSFER's connection couldn't start
    bool                      network = false;  // the NetAccess runs (PAIR WITH A CLIENT works)
    bool                      can_give_back = false;   // our listener runs (ALLOW CONNECTIONS)
};

enum class PairWindowPress
{
    none,
    pair,            // PAIR: found[selected] with our code, on the LAN
    pair_internet,   // PAIR with PAIR OVER THE INTERNET...: through the beacon with our code
    cancel,          // CANCEL while waiting (host)
    give_back,       // YES: give `given` back to the host (and close)
    allow,           // ALLOW (or Enter with all nine digits): the request's or our own code
    deny,            // DENY a request, or CANCEL while a client waits
    close,           // closed (the title bar, CLOSE, NO)
};

struct PairWindowAction
{
    PairWindowPress press = PairWindowPress::none;
    PairMode        mode = PairMode::host;   // the mode it was pressed in
    bool            on_request = false;      // allow / deny / close on a request...
    uint32_t        request_peer = 0;        // ...drawn for this device
};

// `preselect`: a name in the LAN list to pick (FILE TRANSFER's PAIR), or
// nullptr. Opens on PAIR WITH A HOST; already open, it stays as it is.
void pair_window_open(PairWindow* window, const char* preselect);
// Before drawing, every frame: follows FILE TRANSFER's connection (`link`
// nullptr: it doesn't run) and opens the window on a request.
void pair_window_update(PairWindow* window, const NetShareLinkStatus* link);
void pair_window_state(PairWindowState* state, const Localization* strings, const NetShareLinkStatus* link,
                       bool network, bool can_give_back);
void pair_window_draw(PairWindow* window, const PairWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                      PairWindowAction* action);
// PAIR WITH A HOST's presses, on FILE TRANSFER's connection (`link` not
// started: only the window changes). `status`: the connection's last status
// (the LAN list), `beacon`: BEACON SERVER, `service`: our listener, nullptr
// when it doesn't run (no access back), `share_open`: FILE TRANSFER uses the
// connection (closing after pairing keeps it).
void pair_window_apply_host(PairWindow* window, const PairWindowAction* action, NetShareLink* link,
                            const NetShareLinkStatus* status, const char* beacon, NetService* service,
                            bool share_open);
// PAIR WITH A CLIENT's presses, on the NetAccess (under the network lock;
// nullptr: networking is off, only the window changes).
void pair_window_apply_client(PairWindow* window, const PairWindowAction* action, NetAccess* access);

// The NetAccess's events for PAIR WITH A CLIENT (under the network lock).
// A device asks: answered with our own code when it's the one we wait for
// (true: nothing to show), else shown as a request (one at a time: another
// device asks again on its own once this one is done).
bool pair_window_take_request(PairWindow* window, NetAccess* access, const NetAccessEvent* event);
// A device paired (`name`: as the list calls it now): PAIRED WITH, if it was ours.
void pair_window_paired(PairWindow* window, NetAccess* access, const NetAccessEvent* event, const char* name);
// A request ended without pairing (it cancelled, left, or failed).
void pair_window_request_gone(PairWindow* window, uint32_t peer);
// Networking stopped (turned off, or the listener restarted): the request
// and our own code's wait belonged to the NetAccess that's gone (its peer
// numbers mean nothing to the next one).
void pair_window_network_stopped(PairWindow* window);
