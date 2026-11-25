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
#include "video_texture.h"
#include "net_access.h"

#include "retro_ui_font.h"

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
    uint64_t next_refresh_ms = 0;                // a live link takes the picture again then (GUEST_CARD_REFRESH_MS)
    // MAKE LINK / NEW: the link shows only once it's ready (Andrés): its room
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
                       bool game, bool internet, const Localization* strings, RetroUiFont* pixel_font, int32_t scale);

// The session is over (END, the game, quitting): guests told, link closed.
void guest_window_end(GuestWindow* window, NetAccess* access);

// Uploads a new preview. Call after app_shell_acquire, outside rendering
// (where QUICK MENU uploads its thumbnail).
void guest_window_upload(GuestWindow* window, AppShell* shell);
// Frees the preview's texture (waits for the GPU).
void guest_window_destroy(GuestWindow* window, AppShell* shell);

// Tells each guest its port when it changed (once a frame, under the lock).
void guest_window_tell_ports(GuestWindow* window, NetAccess* access, const InputNet* input_net,
                             const InputSystem* input);
