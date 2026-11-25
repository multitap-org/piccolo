// The debug window's previews (debug_preview.h): fake state only. No toolkit
// calls in the state and apply parts.

#include "debug_preview.h"

#include <cstdio>

#include <SDL3/SDL_time.h>

// The fake devices (the pixel font draws them in capitals) and code.
static const char PREVIEW_DEVICE[] = "LIVING ROOM PC";
static const char PREVIEW_DEVICE_PAIRED[] = "STEAM DECK";
static const char PREVIEW_CODE[] = "482193706";

static const char PREVIEW_GAME[] = "SUPER METROID";
static const char PREVIEW_CORE[] = "SNES9X";
static const char PREVIEW_LINK[] = "https://beacon.get-piccolo.app/join/k3x9q2#preview";

static bool is_pair(DebugPreview preview)
{
    return preview >= DebugPreview::pair_host_list && preview <= DebugPreview::pair_client_paired;
}

static bool is_guest(DebugPreview preview)
{
    return preview >= DebugPreview::remote_no_link && preview <= DebugPreview::remote_guests;
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

// REMOTE PLAY for a game, PLAYER the role, two seats; the link (or not).
static void show_guest(DebugPreviews* previews, DebugPreview which)
{
    GuestWindow* window = &previews->guest;
    window->open = true;
    std::snprintf(window->title, sizeof(window->title), "%s (JAPAN, USA)", PREVIEW_GAME);
    window->seats = 2;
    window->level = static_cast<uint32_t>(NetGuestLevel::play);
    window->live = which != DebugPreview::remote_no_link;
    if (window->live)
    {
        std::snprintf(window->link_text, sizeof(window->link_text), "%s", PREVIEW_LINK);
    }
}

// QUICK MENU on slot 1, SAVE STATES open: the quick slot and two others
// used (today and an older day), no pictures.
static void show_quick(DebugPreviews* previews)
{
    QuickMenu* menu = &previews->quick;
    menu->open = true;
    menu->refresh = false;   // nothing on disk to read: these slots stay
    menu->states_open = true;
    menu->slot = 1;
    menu->focus_slot = true;
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    const SDL_Time day = static_cast<SDL_Time>(24) * 3600 * 1000000000;   // SDL_Time: nanoseconds
    const uint32_t used[3] = {QUICK_SLOT, 1, 3};
    const SDL_Time when[3] = {now, now, now - 40 * day};
    for (uint32_t i = 0; i < 3; i++)
    {
        menu->slots[used[i]].used = true;
        menu->slots[used[i]].modified = when[i];
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
    else if (is_guest(which))
    {
        show_guest(previews, which);
    }
    else if (which == DebugPreview::about)
    {
        previews->about.open = true;
    }
    else if (which == DebugPreview::quick_menu)
    {
        show_quick(previews);
    }
}

void debug_preview_end(DebugPreviews* previews)
{
    previews->shown = DebugPreview::none;
    previews->pair = {};
    previews->about = {};
    previews->guest = {};
    previews->quick = {};
}

bool debug_preview_shown(const DebugPreviews* previews)
{
    return previews->shown != DebugPreview::none;
}

void debug_preview_update(DebugPreviews* previews, bool pair_open, bool about_open, bool guest_open, bool quick_open)
{
    DebugPreview shown = previews->shown;
    if ((pair_open && is_pair(shown)) || (about_open && shown == DebugPreview::about) ||
        (guest_open && is_guest(shown)) || (quick_open && shown == DebugPreview::quick_menu))
    {
        debug_preview_end(previews);
    }
}

// REMOTE PLAY's guests: one plays on port 2, one watches (no port).
static void guest_state(GuestWindowState* state, const Localization* strings, DebugPreview shown)
{
    *state = {};
    state->strings = strings;
    state->network = true;
    state->game = true;
    state->internet = true;
    if (shown != DebugPreview::remote_guests)
    {
        return;
    }
    GuestWindowGuest* player = &state->guests[0];
    player->present = true;
    std::snprintf(player->name, sizeof(player->name), "%s", "KITCHEN PC");
    player->port = 1;
    player->ping_known = true;
    player->round_trip_ms = 24;
    player->key[0] = 1;
    GuestWindowGuest* spectator = &state->guests[1];
    spectator->present = true;
    std::snprintf(spectator->name, sizeof(spectator->name), "%s", "PHONE");
    spectator->port = INPUT_NO_PORT;
    spectator->ping_known = true;
    spectator->round_trip_ms = 61;
    spectator->key[0] = 2;
}

void debug_preview_state(DebugPreviewView* view, const DebugPreviews* previews, const Localization* strings,
                         const char* version)
{
    view->shown = previews->shown;
    if (is_pair(previews->shown))
    {
        // The network runs, and our listener too (YES / NO is asked).
        pair_window_state(&view->pair, strings, &previews->pair_link, true, true);
    }
    else if (is_guest(previews->shown))
    {
        guest_state(&view->guest, strings, previews->shown);
    }
    else if (previews->shown == DebugPreview::about)
    {
        about_window_state(&view->about, strings, version);
    }
    else if (previews->shown == DebugPreview::quick_menu)
    {
        quick_menu_state(&view->quick, &previews->quick, strings, PREVIEW_CORE, PREVIEW_GAME, false);
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
    else if (is_guest(view->shown))
    {
        guest_window_draw(&previews->guest, &view->guest, pixel_font, scale, &actions->guest);
    }
    else if (view->shown == DebugPreview::about)
    {
        about_window_draw(&previews->about, &view->about, pixel_font, scale, &actions->about);
    }
    else if (view->shown == DebugPreview::quick_menu)
    {
        quick_menu_draw(&previews->quick, &view->quick, pixel_font, scale, &actions->quick);
    }
}

// ---------------------------------------------------------------------------
// Apply

void debug_preview_apply(DebugPreviews* previews, const DebugPreviewActions* actions)
{
    // PAIR closes on its X, CLOSE, NO, and YES (which gives access back first).
    PairWindowPress pair = actions->pair.press;
    bool closed = is_pair(previews->shown) && (pair == PairWindowPress::close || pair == PairWindowPress::give_back);
    // REMOTE PLAY closes itself (its X, CLOSE); STOP REMOTE PLAY is ignored.
    closed = closed || (is_guest(previews->shown) && !previews->guest.open);
    closed = closed || (previews->shown == DebugPreview::about && actions->about.closed);
    closed = closed || (previews->shown == DebugPreview::quick_menu && actions->quick.closed);
    if (closed)
    {
        debug_preview_end(previews);
    }
}
