#pragma once

// The card a guest link shows in chat apps (design-notes "Guest links"): the
// game's name and one screenshot, uploaded to the beacon, which deletes it
// when the session ends.
//
// The picture is the core's own frame (pixel art at its own size), scaled up
// by a whole number with nearest filtering to at most 1280 x 960, as PNG:
// sharp, and small enough (a few dozen KiB) for the beacon's 256 KiB limit.
// The upload (net_http_post) runs on its own thread; the UI polls it. That
// thread also scales and encodes the picture: the main thread only copies the
// frame (guest_card_frame_copy, one memcpy, under the network lock) and hands
// the copy over in guest_card_send. It's
// tried again for a few seconds while the beacon answers 404 or nothing: the
// beacon takes a card only once the host has joined the link's room, which
// happens just after MAKE LINK, on the network thread.
//
// While the link is open the picture is taken again and sent every
// GUEST_CARD_REFRESH_MS (the beacon allows 4 a minute). Chat apps keep their
// own copy of a card once a message is posted: a newer one shows in new
// shares and on the join page.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "net_guest.h"

// A frame in one of the libretro pixel formats piccolo shows.
enum class GuestCardPixels
{
    xrgb8888,
    rgb565,
    xrgb1555,
};

constexpr uint64_t GUEST_CARD_REFRESH_MS = 60000;
constexpr uint64_t GUEST_PREPARE_MS = 15000;       // a new link waits this long for its room at most
constexpr uint32_t GUEST_CARD_TRIES = 8;           // 404 / no answer: tried again, a second apart

// The frame as XRGB8888 (the window's preview). False if it can't be converted.
bool guest_card_frame_xrgb(const void* pixels, uint32_t width, uint32_t height, uint32_t pitch, GuestCardPixels format,
                           std::vector<uint32_t>* out);

// A copy of the core's frame, so the PNG can be made later and on another
// thread: the core's own buffer changes with the next frame.
struct GuestCardFrame
{
    std::vector<uint8_t> pixels;      // `pitch` x `height` bytes; empty: no frame
    uint32_t             width = 0;
    uint32_t             height = 0;
    uint32_t             pitch = 0;
    GuestCardPixels      format = GuestCardPixels::xrgb8888;
};

// Copies the frame into `frame` (its buffer is reused). False, `frame` empty,
// when there is none or it is bigger than the card allows (as guest_card_png).
bool guest_card_frame_copy(GuestCardFrame* frame, const void* pixels, uint32_t width, uint32_t height, uint32_t pitch,
                           GuestCardPixels format);

// The PNG for `pixels` (width x height, `pitch` bytes per row). False if
// it can't be made within the beacon's limits.
bool guest_card_png(const void* pixels, uint32_t width, uint32_t height, uint32_t pitch, GuestCardPixels format,
                    std::vector<uint8_t>* png);

enum class GuestCardState
{
    none,
    sending,
    sent,       // the beacon has it (204)
    failed,     // no answer, or another status (`status`, `error`)
};

struct GuestCardUpload
{
    SDL_Thread*  thread = nullptr;
    SDL_AtomicInt state = {};         // GuestCardState
    std::string  url;
    // The thread's own copies, filled by guest_card_send before it starts and
    // not touched by the main thread while it runs (state `sending`): the
    // frame to encode, the card key and title for the body, and the body.
    GuestCardFrame frame;
    uint8_t      card_key[NET_GUEST_KEY_SIZE] = {};
    char         title[NET_GUEST_MAX_TITLE + 1] = {};
    std::vector<uint8_t> body;
    uint32_t     status = 0;
    char         error[256] = {};
};

// The card's URL on the beacon: https://<beacon>/join/<room>/card.
std::string guest_card_url(const char* beacon, const NetGuestLink* link);

// Starts sending (after waiting for a send still running). `frame` goes to
// the thread (its buffer is swapped with the upload's: afterwards it is empty,
// ready for the next copy); the thread makes the PNG (none: a card with only
// its title, as when the frame is too big) and the body. False if the title
// is out of bounds or the thread doesn't start (then `failed`).
bool guest_card_send(GuestCardUpload* upload, const char* beacon, const NetGuestLink* link, const char* title,
                     GuestCardFrame* frame);
GuestCardState guest_card_state(GuestCardUpload* upload);
// Waits for the thread (at the end of a session, or quitting).
void guest_card_finish(GuestCardUpload* upload);
