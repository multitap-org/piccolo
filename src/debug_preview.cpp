// The debug window's previews (debug_preview.h): fake state only. No toolkit
// calls in the state and apply parts.

#include "debug_preview.h"

#include <cstdio>

// The fake devices (the pixel font draws them in capitals) and code.
static const char PREVIEW_DEVICE[] = "LIVING ROOM PC";
static const char PREVIEW_DEVICE_PAIRED[] = "STEAM DECK";
static const char PREVIEW_CODE[] = "482193706";

static bool is_pair(DebugPreview preview)
{
    return preview >= DebugPreview::pair_host_list && preview <= DebugPreview::pair_client_paired;
}

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

// PAIR at one step: two devices found on this network (one paired before),
// a code, a device asking.
static void show_pair(DebugPreviews* previews, DebugPreview which)
{
    NetShareLinkStatus* link = &previews->pair_link;
    *link = {};
    link->state = NetShareLinkState::idle;
    NetShareLinkFound found;
    found.name = PREVIEW_DEVICE;
    link->found.push_back(found);
    found.name = PREVIEW_DEVICE_PAIRED;
    link->found.push_back(found);
    link->paired.push_back(PREVIEW_DEVICE_PAIRED);

    PairWindow* window = &previews->pair;
    *window = {};
    window->open = true;
    window->select_mode = true;   // the tabs follow `mode`
    window->raise = true;         // over the debug window
    std::snprintf(window->code, sizeof(window->code), "%s", PREVIEW_CODE);
    window->mode = which >= DebugPreview::pair_client_type ? PairMode::client : PairMode::host;
    switch (which)
    {
    case DebugPreview::pair_host_list:
        window->selected = 0;
        break;
    case DebugPreview::pair_host_code_lan:
        window->phase = PairWindowPhase::waiting;
        window->host_name = PREVIEW_DEVICE;
        break;
    case DebugPreview::pair_host_code_internet:
        window->phase = PairWindowPhase::waiting;
        window->internet = true;
        break;
    case DebugPreview::pair_host_paired:
        window->phase = PairWindowPhase::done;
        window->host_name = PREVIEW_DEVICE;
        window->given = NetAccessLevel::stream;
        window->newly_paired = true;   // the YES / NO question
        break;
    case DebugPreview::pair_client_type:
        window->invite.focus = true;
        break;
    case DebugPreview::pair_client_waiting:
        window->invite.waiting = true;
        std::snprintf(window->invite.code, sizeof(window->invite.code), "%s", PREVIEW_CODE);
        window->invite.level = static_cast<uint32_t>(NetAccessLevel::stream);
        break;
    case DebugPreview::pair_client_request:
        window->request.active = true;
        window->request.focus = true;
        std::snprintf(window->request.name, sizeof(window->request.name), "%s", PREVIEW_DEVICE);
        break;
    case DebugPreview::pair_client_paired:
        window->client_done = true;
        window->client_name = PREVIEW_DEVICE;
        window->client_level = NetAccessLevel::stream;
        break;
    default:
        break;
    }
}

void debug_preview_show(DebugPreviews* previews, DebugPreview which)
{
    debug_preview_end(previews);
    previews->shown = which;
    if (is_pair(which))
    {
        show_pair(previews, which);
    }
}

void debug_preview_end(DebugPreviews* previews)
{
    previews->shown = DebugPreview::none;
    previews->pair = {};
}

bool debug_preview_shown(const DebugPreviews* previews)
{
    return previews->shown != DebugPreview::none;
}

void debug_preview_update(DebugPreviews* previews, bool pair_open)
{
    if (pair_open && is_pair(previews->shown))
    {
        debug_preview_end(previews);
    }
}

void debug_preview_state(DebugPreviewView* view, const DebugPreviews* previews, const Localization* strings)
{
    view->shown = previews->shown;
    if (is_pair(previews->shown))
    {
        // The network runs, and our listener too (YES / NO is asked).
        pair_window_state(&view->pair, strings, &previews->pair_link, true, true);
    }
}

void debug_preview_add_hud(const DebugPreviews* previews, const Localization* strings, HudStack* stack,
                           uint64_t now_ms)
{
    guest_window_add_hud(&previews->card_guest, strings, stack, now_ms);
    pair_window_add_hud(&previews->card_pair, strings, true, stack, now_ms);
}

// ---------------------------------------------------------------------------
// Drawing

void debug_preview_draw(DebugPreviews* previews, const DebugPreviewView* view, RetroUiFont* pixel_font,
                        int32_t scale, DebugPreviewActions* actions)
{
    *actions = {};
    if (is_pair(view->shown))
    {
        pair_window_draw(&previews->pair, &view->pair, pixel_font, scale, &actions->pair);
    }
}

// ---------------------------------------------------------------------------
// Apply

void debug_preview_apply(DebugPreviews* previews, const DebugPreviewActions* actions)
{
    // PAIR closes on its X, CLOSE, NO, and YES (which gives access back first).
    PairWindowPress pair = actions->pair.press;
    if (is_pair(previews->shown) && (pair == PairWindowPress::close || pair == PairWindowPress::give_back))
    {
        debug_preview_end(previews);
    }
}
