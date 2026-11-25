// The debug window's previews (debug_preview.h): fake state only. No toolkit
// calls in the state and apply parts.

#include "debug_preview.h"

#include <cstdio>

// The fake device asking to pair (the pixel font draws it in capitals).
static const char PREVIEW_DEVICE[] = "LIVING ROOM PC";

// ---------------------------------------------------------------------------
// State

void debug_preview_show_card(DebugPreviews* previews, DebugCard card, uint64_t now_ms)
{
    switch (card)
    {
    case DebugCard::link_copied:
        previews->card_guest.copied_ms = now_ms;
        break;
    case DebugCard::pair_timed_out:
        previews->card_pair.notice = PairNotice::timed_out;
        previews->card_pair.notice_ms = now_ms;
        break;
    case DebugCard::pair_failed:
        previews->card_pair.notice = PairNotice::failed;
        previews->card_pair.notice_ms = now_ms;
        break;
    }
}

void debug_preview_request_card(DebugPreviews* previews, bool shown, uint64_t now_ms)
{
    if (shown == previews->request_card)
    {
        return;
    }
    previews->request_card = shown;
    PairWindow* window = &previews->card_pair;
    window->open = false;
    window->request = {};
    input_hold_reset(&window->accept_hold);
    if (shown)
    {
        window->request.active = true;
        window->request.since_ms = now_ms;   // the card slides in from now
        std::snprintf(window->request.name, sizeof(window->request.name), "%s", PREVIEW_DEVICE);
    }
}

void debug_preview_hold_request(DebugPreviews* previews, bool in_game, bool held, uint64_t now_ms)
{
    if (!previews->request_card)
    {
        return;
    }
    // The real hold, on the fake window: it "opens" it (only the fake
    // window's flag), which takes the card down.
    if (pair_window_hold_accept(&previews->card_pair, in_game, held, now_ms))
    {
        debug_preview_request_card(previews, false, now_ms);
    }
}

void debug_preview_add_hud(const DebugPreviews* previews, const Localization* strings, HudStack* stack,
                           uint64_t now_ms)
{
    guest_window_add_hud(&previews->card_guest, strings, stack, now_ms);
    pair_window_add_hud(&previews->card_pair, strings, true, stack, now_ms);
}
