#pragma once

// TOOLS > PAIR... (one window since testing round 1; pairing v3, Andrés
// 2026-11-25: docs/mockups/pairing-v3, design notes 11.9-11.14). The window's
// skeleton never changes: its width stays the same at every step; only the
// panel's contents and the buttons change.
//
//   +- PAIR -------------------------------------------------+
//   | [ PAIR A DEVICE           ][ GENERATE PAIRING CODE    ] |
//   | +----------------------------------------------------+ |
//   | | (the step: a panel of fixed height)                | |
//   | +----------------------------------------------------+ |
//   |   WAITING FOR ... (blinking, only while waiting)       |
//   | [ PAIR                    ][ CLOSE                    ] |
//   +--------------------------------------------------------+
//
// The device that receives a pairing request has the code and grants (it
// shows the code with its PERMISSIONS ticks); the device that starts types it
// (Andrés, 2026-11-25). One pairing sets both directions: the side that typed
// ticks what the other may use here on its last screen.
//
// PAIR A DEVICE (this piccolo starts; FILE TRANSFER's connection, NetShareLink):
//   1  DISCOVER DEVICES: the devices on this network that offer something we
//      use (a kind chip each), then ENTER A CODE; PAIR / CLOSE
//   2  TYPE THE CODE SHOWN ON <DEVICE> (the picked device pops its code up), or
//      TYPE THE CODE SHOWN ON THE OTHER DEVICE (ENTER A CODE: across the
//      internet, the other device's GENERATE PAIRING CODE); the boxes typed
//      into; PAIR / CANCEL. A wrong code: WRONG CODE, the device shows a new one.
//   3  PAIRED WITH <DEVICE>, PERMISSIONS GRANTED HERE (ticks), PERMISSIONS
//      GRANTED ON <DEVICE> (what it gave us); DONE sends the ticks.
//
// GENERATE PAIRING CODE (a device comes to this one; the NetAccess):
//   the code    our code in three boxes, PERMISSIONS (whoever types it gets
//               them, kept to what it uses), WAITING FOR THE OTHER DEVICE · 5 MIN;
//               CANCEL. It waits on this network and on the beacon.
//   a request   a device on this network picked us in its PAIR A DEVICE: TYPE
//               THIS CODE ON <DEVICE> (kind and UNSIGNED BUILD chips), our code
//               for it, PERMISSIONS (what it uses; FILE TRANSFER and SETTINGS
//               off for an unsigned build), WAITING FOR <DEVICE>; CANCEL. It
//               opens the window on this mode by itself (in the game, a card
//               first: below), the game paused.
//   paired      PAIRED WITH <DEVICE>, PERMISSIONS GRANTED HERE, PERMISSIONS
//               GRANTED ON <DEVICE> (WAITING FOR <DEVICE> until it says); CLOSE.
//
// Ticking FILE TRANSFER or SETTINGS for an unsigned build asks first, in a red
// window (net_caps_ui.h). While a step of one mode is in progress the other
// mode is greyed out, so a half-done pairing isn't left behind by switching.
//
// A request while the player is in the game: no window and no pause, a
// notification card instead (pair_window_add_hud):
//
//   +--------------------------------+
//   | <NAME> WANTS TO PAIR           |
//   | HOLD [START] TO ACCEPT         |
//   +--------------------------------+
//
// START held (as HOLD START TO REMAP's) opens the window on it, and the game
// pauses as before. With the menu or a window up (or no game) the window
// opens at once.
//
// Every waiting step gives up after PAIR_WINDOW_WAIT_MS (Andrés: 5 minutes at
// most): back to its start, with a PAIRING TIMED OUT card. A pairing that
// fails otherwise gives PAIRING FAILED.
//
// Split like every window: pair_window_update follows FILE TRANSFER's
// connection and the requests (no GUI), pair_window_state fills what the
// window shows, pair_window_draw draws it and records what was pressed, and
// the two applies act on it (no GUI): PAIR A DEVICE on the connection,
// GENERATE PAIRING CODE on the NetAccess.

#include <cstdint>
#include <string>

#include "input_hold.h"
#include "localization.h"
#include "net_access.h"
#include "net_caps_ui.h"
#include "net_service.h"
#include "net_share_link.h"

#include "retro_ui_font.h"

struct HudStack;

constexpr uint64_t PAIR_WINDOW_WAIT_MS = 5 * 60 * 1000;   // a waiting step gives up after this
// GENERATE PAIRING CODE's ticks before anyone typed it (the mockup's): all we
// offer but GAMEPAD (someone across the internet playing with our pads is
// rarely what's meant).
constexpr NetCaps PAIR_WINDOW_SHOWN_DEFAULT = NET_CAP_STREAM | NET_CAP_SETTINGS | NET_CAP_SHARE;

enum class PairMode : uint8_t
{
    device,   // PAIR A DEVICE
    code,     // GENERATE PAIRING CODE
};

enum class PairDevicePhase : uint8_t
{
    list,       // DISCOVER DEVICES
    typing,     // TYPE THE CODE SHOWN ON ...
    checking,   // the code typed is being checked
    done,       // PAIRED WITH: the ticks for it
};

// GENERATE PAIRING CODE's own code, shown before anyone typed it.
struct PairShown
{
    bool     active = false;
    char     code[NET_PAIR_CODE_DIGITS + 1] = {};
    NetCaps  grants = PAIR_WINDOW_SHOWN_DEFAULT;
    uint64_t since_ms = 0;
};

// A device asking to pair on this network (NetAccessEventType::pairing_requested).
struct PairRequest
{
    bool        active = false;
    bool        fresh = false;     // just came (or came back with a new code): the window is raised for it
    bool        retry = false;     // the last code typed there was wrong (a new one now)
    uint32_t    peer = 0;
    char        name[NET_DEVICE_NAME_MAX + 1] = {};
    NetAppKind  kind = NetAppKind::piccolo;
    bool        signed_build = false;
    NetCaps     uses = 0;
    char        code[NET_PAIR_CODE_DIGITS + 1] = {};
    NetCaps     grants = 0;        // the ticks
    uint64_t    since_ms = 0;      // when it came (pair_window_update stamps it): its card, its timeout
};

// Why the last pairing step went back to its start, as a notification card.
enum class PairNotice : uint8_t
{
    none,
    timed_out,   // PAIRING TIMED OUT
    failed,      // PAIRING FAILED
};

struct PairWindow
{
    bool            open = false;
    PairMode        mode = PairMode::device;
    bool            select_mode = false;   // the drawing switches the tabs to `mode` (a request came)
    bool            raise = false;         // the drawing brings the window over every other one

    // PAIR A DEVICE.
    PairDevicePhase phase = PairDevicePhase::list;
    int32_t         selected = -1;          // in the status' `found`
    bool            enter_selected = false; // ENTER A CODE is the one picked
    bool            internet = false;       // typing / checking across the internet (no device picked)
    char            typed[NET_PAIR_CODE_DIGITS + 1] = {};
    bool            focus = false;          // the code boxes take the keyboard next frame
    bool            wrong = false;          // the last code was wrong: WRONG CODE...
    std::string     host_name;              // typing / checking / done: who
    NetAppKind      host_kind = NetAppKind::piccolo;
    bool            host_signed = false;
    NetCaps         host_uses = 0;
    NetCaps         given = 0;              // done: what it lets us use
    NetCaps         ticks = 0;              // done: what it may use here
    int32_t         device = -1;            // done: its index in the status' `paired` when it connected (>= 0: a device was paired)
    uint8_t         device_key[NET_KEY_SIZE] = {};   // done: its key; DONE looks the index up again (the list may have changed)
    bool            newly_paired = false;
    bool            seen_connecting = false;   // the link took our connect
    uint64_t        device_since_ms = 0;    // typing / checking since (pair_window_update stamps it)
    std::string     preselect;              // pair_window_open's name to pick, until the list shows it

    // GENERATE PAIRING CODE.
    PairShown       shown;
    PairRequest     request;
    bool            code_done = false;      // PAIRED WITH: who, and the two directions
    std::string     done_name;
    NetAppKind      done_kind = NetAppKind::piccolo;
    NetCaps         done_here = 0;
    NetCaps         done_there = 0;
    bool            done_there_known = false;
    uint32_t        done_peer = 0;

    // The red question (FILE TRANSFER / SETTINGS for an unsigned build).
    NetCapsUnsignedAsk ask;
    uint32_t        ask_peer = 0;           // ask.target == request: the peer it was asked about (with ask.name)

    // A request shown as a card in the game: START held on it.
    InputHold       accept_hold;

    PairNotice      notice = PairNotice::none;
    uint64_t        notice_ms = 0;          // when it came: its card shows for a few seconds
};

struct PairWindowState
{
    const Localization*       strings = nullptr;
    const NetShareLinkStatus* link = nullptr;   // nullptr: FILE TRANSFER's connection couldn't start
    bool                      network = false;  // the NetAccess runs (GENERATE PAIRING CODE works)
    NetCaps                   offers = NET_CAPS_ALL;   // what this piccolo offers
    NetCaps                   uses = NET_CAP_SHARE | NET_CAP_SETTINGS;   // ...and uses
};

enum class PairWindowPress
{
    none,
    pair,       // PAIR on the list: found[selected] (connect, it pops its code up) or ENTER A CODE
    submit,     // PAIR (or Enter) with nine digits typed
    cancel,     // CANCEL: back to the list, or the code / request given up
    done,       // DONE: the ticks for the device just paired
    show,       // GENERATE PAIRING CODE wants a code (its mode is shown without one)
    close,      // closed (the title bar, CLOSE)
};

struct PairWindowAction
{
    PairWindowPress press = PairWindowPress::none;
    PairMode        mode = PairMode::device;   // the mode it was pressed in
    bool            on_request = false;        // cancel / close on a request...
    uint32_t        request_peer = 0;          // ...drawn for this device
    bool            request_grants = false;    // the request's ticks changed (request.grants)
    bool            shown_grants = false;      // the shown code's ticks changed (shown.grants)
};

// `preselect`: a name in the LAN list to pick (FILE TRANSFER's PAIR), or
// nullptr. Opens on PAIR A DEVICE; already open, it stays as it is.
void pair_window_open(PairWindow* window, const char* preselect);
// Before drawing, every frame: follows FILE TRANSFER's connection (`link`
// nullptr: it doesn't run) and opens the window on a request, unless
// `in_game` (the player is in the game, no menu or window up: a card shows
// it instead). Stamps when each waiting step began.
void pair_window_update(PairWindow* window, const NetShareLinkStatus* link, bool in_game, uint64_t now_ms);
void pair_window_state(PairWindowState* state, const Localization* strings, const NetShareLinkStatus* link,
                       bool network);
void pair_window_draw(PairWindow* window, const PairWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                      PairWindowAction* action);
// PAIR A DEVICE's presses, on FILE TRANSFER's connection (`link` not started:
// only the window changes). `status`: the connection's last status (the LAN
// list), `beacon`: BEACON SERVER, `share_open`: FILE TRANSFER uses the
// connection (closing after pairing keeps it).
void pair_window_apply_device(PairWindow* window, const PairWindowAction* action, NetShareLink* link,
                              const NetShareLinkStatus* status, const char* beacon, bool share_open);
// GENERATE PAIRING CODE's presses and ticks, on the NetAccess (under the
// network lock; nullptr: networking is off, only the window changes).
void pair_window_apply_code(PairWindow* window, const PairWindowAction* action, NetAccess* access);

// Every frame, after the presses: a waiting step past PAIR_WINDOW_WAIT_MS
// goes back to its start with PAIRING TIMED OUT (the link closed, the request
// refused on `access`, under the network lock; nullptr: networking is off).
void pair_window_expire(PairWindow* window, NetShareLink* link, NetAccess* access, uint64_t now_ms);
// The request's card in the game (`in_game` as pair_window_update's): START
// (`held`, on any controller) held as long as HOLD START TO REMAP opens the
// window on the request. True when it did.
bool pair_window_hold_accept(PairWindow* window, bool in_game, bool held, uint64_t now_ms);
// Its notification cards: the request in the game (`in_game`), and PAIRING
// TIMED OUT / PAIRING FAILED for a few seconds.
void pair_window_add_hud(const PairWindow* window, const Localization* strings, bool in_game, HudStack* stack,
                         uint64_t now_ms);

// The NetAccess's events for GENERATE PAIRING CODE (under the network lock).
// A device asks on this network: shown as a request (one at a time: another
// one asks again on its own once this one is done).
void pair_window_take_request(PairWindow* window, const NetAccessEvent* event);
// A device paired (`name`: as the list calls it now): PAIRED WITH, if it was
// the request or our shown code.
void pair_window_paired(PairWindow* window, const NetAccessEvent* event, const char* name);
// The device just paired says what it lets us use there.
void pair_window_granted(PairWindow* window, const NetAccessEvent* event);
// The shown code's time ran out on the NetAccess: PAIRING TIMED OUT.
void pair_window_code_expired(PairWindow* window, uint64_t now_ms);
// A request ended without pairing (it cancelled, left, or failed).
void pair_window_request_gone(PairWindow* window, uint32_t peer);
// A device typed too many wrong codes for the code shown: PAIRING FAILED (the notice).
void pair_window_failed(PairWindow* window, uint64_t now_ms);
// Networking stopped (turned off, or the listener restarted): the request
// and the shown code belonged to the NetAccess that's gone.
void pair_window_network_stopped(PairWindow* window);
