#pragma once

// The card a guest link shows in chat apps (design-notes "Guest links"): the
// game's name and one screenshot, uploaded to the beacon, which deletes it
// when the session ends.
//
// The picture is the core's own frame (pixel art at its own size), scaled up
// by a whole number with nearest filtering to at most 1280 x 960, as PNG:
// sharp, and small enough (a few dozen KiB) for the beacon's 256 KiB limit.
// The upload (net_http_post) runs on its own thread; the UI polls it.

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
    std::vector<uint8_t> body;
    uint32_t     status = 0;
    char         error[256] = {};
};

// The card's URL on the beacon: https://<beacon>/join/<room>/card.
std::string guest_card_url(const char* beacon, const NetGuestLink* link);

// Starts sending (after waiting for a send still running). False if the body
// can't be made or the thread doesn't start (then `failed`).
bool guest_card_send(GuestCardUpload* upload, const char* beacon, const NetGuestLink* link, const char* title,
                     const std::vector<uint8_t>* png);
GuestCardState guest_card_state(GuestCardUpload* upload);
// Waits for the thread (at the end of a session, or quitting).
void guest_card_finish(GuestCardUpload* upload);
