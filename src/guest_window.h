#pragma once

// TOOLS > REMOTE PLAY (guest links, design-notes "Guest links", Andrés
// 2026-11-22; one layout since testing round 1, item 5): a link friends open
// in piccoletto to watch the game, and play when they have a seat and a port
// is free. No pairing, nothing saved. One width throughout:
//
//   +- REMOTE PLAY ---------------------------------------------------+
//   |  GAME TEENAGE MUTANT NINJA TURTLES IV: TURTLES IN TIME (USA)     |
//   |       (REV 1)                                                     |
//   |  +---------+  LINK   [https://beacon.get-piccolo.app/j/k3x9][REFRESH LINK]
//   |  | preview |  GUESTS [- 2 +]          ROLE [SPECTATOR ][PLAYER      ]
//   |  +---------+                                                     |
//   |  | KITCHEN PC     PLAYER, PORT 2              ping  [KICK] |      |
//   |  | PHONE          SPECTATOR                   ping  [KICK] |      |
//   |  [ STOP REMOTE PLAY              ][ CLOSE                   ]     |
//   +-----------------------------------------------------------------+
//
// The labels, the link field's right edge and SPECTATOR, and the link's
// button and PLAYER line up (one grid). Before the link the field says NO
// LINK YET, the button is CREATE LINK and STOP REMOTE PLAY is greyed out.
// CREATE LINK makes the link and, once it's ready, copies it: the
// notification cards say LINK COPIED TO THE CLIPBOARD (guest_window_add_hud)
// when the copy worked (a browser refuses one that isn't right after a
// click). Clicking the link field copies it too, with the same card.
// Hovering the link shows it whole, and says a click copies it. GUESTS counts guests only (seats: the
// host is never one of them).
//
// Closing the window keeps the link; STOP REMOTE PLAY, closing the game,
// loading another one, or quitting ends the session (main.cpp).
//
// guest_window_apply acts on the running NetAccess (under the network lock):
// guests, role and KICK at once. CREATE LINK, REFRESH LINK and STOP REMOTE
// PLAY need the frame and the card upload: apply hands them back to main.cpp.
//
// Split like every window: guest_window_state fills what the window shows
// from the NetAccess (no GUI), guest_window_draw draws it and records what
// was pressed, and guest_window_apply acts on it (no GUI).

#include <cstdint>
#include <vector>

#include "guest_card.h"
#include "hud_view.h"
#include "input_net.h"
#include "localization.h"
#include "video_texture.h"
#include "net_access.h"

#include "retro_ui_font.h"

// What main.cpp does itself (it needs the frame and the card upload).
enum class GuestWindowPress
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
    int32_t  seats = 2;                          // GUESTS
    uint32_t level = 1;                          // ROLE (NetGuestLevel): PLAYER by default
    char     link_text[NET_GUEST_MAX_LINK] = {};
    NetGuestLink link;
    uint32_t game_starts = 0;                    // the game the session belongs to (App::game_starts)
    std::vector<uint8_t> png;                    // the card's picture, taken when the window opened
    GuestCardUpload card;
    uint64_t next_refresh_ms = 0;                // a live link takes the picture again then (GUEST_CARD_REFRESH_MS)
    // CREATE LINK / REFRESH LINK: the link shows only once it's ready (Andrés): its room
    // on the beacon joined, and the card sent (or given up on), so a link
    // pasted anywhere already has its card. GUEST_PREPARE_MS at most.
    bool     preparing = false;
    bool     prepare_sent = false;               // the card for this link is on its way
    uint64_t prepare_started_ms = 0;
    // The same picture in the window (drawn like QUICK MENU's thumbnails).
    VideoTexture          preview;
    std::vector<uint32_t> preview_pixels;        // XRGB8888, uploaded on the next frame
    uint32_t              preview_width = 0;
    uint32_t              preview_height = 0;
    bool                  preview_pending = false;
    bool                  has_preview = false;
    bool     copy_when_ready = false;            // CREATE / REFRESH LINK: copied once it shows
    uint64_t copied_ms = 0;                      // when it was copied (the notification card), 0: never
    // The port each guest was last told (NET_GUEST_PORT_MESSAGE), per peer.
    uint8_t  told_port[NET_MAX_PEERS] = {};
    bool     told[NET_MAX_PEERS] = {};
};

// One guest in the list.
struct GuestWindowGuest
{
    bool     present = false;                    // a guest is on this peer
    uint8_t  key[NET_KEY_SIZE] = {};             // which guest (KICK checks it's still the same one)
    char     name[NET_GUEST_MAX_NAME + 1] = {};  // "" : it gave none
    int32_t  port = INPUT_NO_PORT;               // where it plays (the router)
    bool     ping_known = false;
    uint32_t round_trip_ms = 0;
};

struct GuestWindowState
{
    const Localization* strings = nullptr;
    bool             network = false;            // the network runs (else: NETWORK IS OFF)
    bool             game = false;               // a game runs (a link needs one)
    bool             internet = false;           // the beacon is on (a link needs it)
    GuestWindowGuest guests[NET_MAX_PEERS];
    uint32_t         failures = 0;               // COULDN'T CONNECT
};

struct GuestWindowAction
{
    GuestWindowPress press = GuestWindowPress::none;
    bool     seats_changed = false;              // window->seats changed: a live link takes it
    bool     level_changed = false;              // window->level changed: a live link takes it
    bool     copy_link = false;                  // the link field was clicked: it goes to the clipboard
    bool     kick = false;                       // KICK on `kick_peer`, if it's still `kick_key`
    uint32_t kick_peer = 0;
    uint8_t  kick_key[NET_KEY_SIZE] = {};
};

// Opens the window (the caller has filled `title` and `png` for the running game).
void guest_window_open(GuestWindow* window);

// Under the network lock (`access` nullptr: the network is off). `game`: a
// game runs, `internet`: the beacon is on. `input_net` / `input`: the router,
// for who plays where.
void guest_window_state(GuestWindowState* state, const NetAccess* access, const InputNet* input_net,
                        const InputSystem* input, bool game, bool internet, const Localization* strings);
void guest_window_draw(GuestWindow* window, const GuestWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                       GuestWindowAction* action);
// Under the network lock (`access` nullptr: the network is off): guests,
// role, KICK, and the link copied when its field was clicked (the card says
// so from `now_ms`). Returns what main.cpp does itself.
GuestWindowPress guest_window_apply(GuestWindow* window, const GuestWindowAction* action, NetAccess* access,
                                    uint64_t now_ms);

// The link shows (its room joined, its card sent or given up on): after
// CREATE / REFRESH LINK it goes to the clipboard now, and the notification
// card says so from `now_ms` if the copy worked.
void guest_window_link_ready(GuestWindow* window, uint64_t now_ms);

// LINK COPIED TO THE CLIPBOARD, as a notification card for a few seconds
// after the copy (hud_view), like the other HUD widgets' cards.
void guest_window_add_hud(const GuestWindow* window, const Localization* strings, HudStack* stack,
                          uint64_t now_ms);

// The session is over (END, the game, quitting): guests told, link closed.
void guest_window_end(GuestWindow* window, NetAccess* access);

// Tells each guest its port when it changed (once a frame, under the lock).
void guest_window_tell_ports(GuestWindow* window, NetAccess* access, const InputNet* input_net,
                             const InputSystem* input);

// Uploads a new preview. Call after app_shell_acquire, outside rendering
// (where QUICK MENU uploads its thumbnail).
void guest_window_upload(GuestWindow* window, AppShell* shell);
// Frees the preview's texture (waits for the GPU).
void guest_window_destroy(GuestWindow* window, AppShell* shell);
