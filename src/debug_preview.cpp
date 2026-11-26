// The debug window's previews (debug_preview.h): fake state only. No toolkit
// calls in the state and apply parts.

#include "debug_preview.h"

#include "net_caps_ui.h"
#include "piccolo_text.h"

#include <cstdio>

#include <SDL3/SDL_time.h>

#include "imgui.h"      // the display's size, for the phone previews' scale
#include "touch_ui.h"

// The fake devices (the pixel font draws them in capitals) and code.
static const char PREVIEW_DEVICE[] = "LIVING ROOM PC";
static const char PREVIEW_DEVICE_PAIRED[] = "STEAM DECK";
static const char PREVIEW_CODE[] = "482193706";

static const char PREVIEW_GAME[] = "SUPER METROID";
static const char PREVIEW_CORE[] = "SNES9X";
static const char PREVIEW_LINK[] = "https://beacon.get-piccolo.app/join/k3x9q2#preview";

static bool is_pair(DebugPreview preview)
{
    return preview >= DebugPreview::pair_device_list && preview <= DebugPreview::pair_code_done;
}

// The phone's HOME: a few recent games (fake).
static const MobileGameRow PREVIEW_GAMES[] = {
    {"SUPER METROID (JAPAN, USA)", "snes9x"},
    {"POKEMON - CRYSTAL VERSION (USA, EUROPE)", "sameboy"},
    {"SONIC THE HEDGEHOG 2 (WORLD)", "genesis_plus_gx"},
    {"METROID FUSION (USA)", "mgba"},
};

static bool is_phone(DebugPreview preview)
{
    return preview >= DebugPreview::phone_home && preview <= DebugPreview::phone_menu;
}

// DEVICES: nobody OUT, the touch pad (the viewer's) and a gamepad on port 1,
// a phone over the network on port 2, nobody on 3 and 4.
static void show_devices(DebugPreviews* previews)
{
    PortsWidget* widget = &previews->devices;
    *widget = PortsWidget{};
    PortsView* view = &widget->view;
    view->port_count = 4;
    view->chip_height = 18.0f;   // the touch size
    PortsViewChip touch;
    touch.key = 1;
    touch.label = "TOUCH PAD";
    touch.yours = true;
    PortsViewChip pad;
    pad.key = 2;
    pad.label = "XBOX PAD 1";
    PortsViewChip phone;
    phone.key = 3;
    phone.label = "PIXEL 8";
    phone.has_ping = true;
    phone.ping_known = true;
    phone.ping_ms = 24;
    view->ports[0].chips.push_back(touch);
    view->ports[0].chips.push_back(pad);
    view->ports[1].chips.push_back(phone);
    widget->open = true;
    ports_view_open_board(view);
}

// The DVR's phone panel: SCRUB 27 s back, five pictures (not made: empty
// cards), a kept lane from between the third and fourth.
static void show_phone_dvr(DebugPreviews* previews)
{
    DvrView* view = &previews->phone_dvr;
    *view = DvrView{};
    const char* labels[5] = {"-0:40", "-0:35", "-0:30", "-0:25", "-0:20"};
    view->count = 5;
    view->selected = 2;
    view->between = 0.45f;
    view->touch = true;
    for (uint32_t i = 0; i < view->count; i++)
    {
        std::snprintf(view->thumbs[i].label, sizeof(view->thumbs[i].label), "%s", labels[i]);
        view->thumbs[i].after = i > view->selected;
    }
    view->lanes = true;
    view->fork_at = 2.6f;
    view->upper_end = 4.3f;
    view->lower_end = 4.8f;
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
    found.kind = NetAppKind::piccolo;
    found.offers = NET_CAPS_ALL;
    found.offers_known = true;
    link->found.push_back(found);
    found.name = PREVIEW_DEVICE_PAIRED;
    link->found.push_back(found);
    link->paired.push_back(PREVIEW_DEVICE_PAIRED);

    PairWindow* window = &previews->pair;
    *window = {};
    window->open = true;
    window->select_mode = true;   // the tabs follow `mode`
    window->raise = true;         // over the debug window
    window->mode = which >= DebugPreview::pair_code_shown ? PairMode::code : PairMode::device;
    PairRequest* request = &window->request;
    switch (which)
    {
    case DebugPreview::pair_device_list:
        window->selected = 0;
        break;
    case DebugPreview::pair_device_type:
        window->phase = PairDevicePhase::typing;
        window->host_name = PREVIEW_DEVICE;
        link->state = NetShareLinkState::connecting;
        link->code_needed = true;   // its code is up: PAIR works once nine digits are in
        std::snprintf(window->typed, sizeof(window->typed), "%s", "482");
        break;
    case DebugPreview::pair_device_internet:
        window->phase = PairDevicePhase::typing;
        window->internet = true;
        window->enter_selected = true;
        break;
    case DebugPreview::pair_device_done:
        window->phase = PairDevicePhase::done;
        window->host_name = PREVIEW_DEVICE;
        window->host_kind = NetAppKind::piccolo;
        window->host_signed = false;   // its UNSIGNED BUILD chip; FILE TRANSFER and SETTINGS not ticked
        window->host_uses = NET_CAP_SHARE | NET_CAP_SETTINGS;
        window->ticks = 0;
        window->given = NET_CAP_SHARE;
        window->newly_paired = true;
        break;
    case DebugPreview::pair_code_shown:
        window->shown.active = true;
        window->shown.grants = PAIR_WINDOW_SHOWN_DEFAULT;
        std::snprintf(window->shown.code, sizeof(window->shown.code), "%s", PREVIEW_CODE);
        break;
    case DebugPreview::pair_code_request:
    case DebugPreview::pair_code_unsigned:
        request->active = true;
        std::snprintf(request->name, sizeof(request->name), "%s", PREVIEW_DEVICE_PAIRED);
        request->kind = NetAppKind::piccoletto;
        request->signed_build = false;
        request->uses = net_caps_ui_kind_uses(NetAppKind::piccoletto) | NET_CAP_SHARE;
        request->grants = NET_CAP_GAMEPAD | NET_CAP_STREAM;   // the default ticks for an unsigned build
        std::snprintf(request->code, sizeof(request->code), "%s", PREVIEW_CODE);
        if (which == DebugPreview::pair_code_unsigned)
        {
            window->ask.cap = NET_CAP_SHARE;   // as if FILE TRANSFER was just ticked
            window->ask.name = request->name;
            window->ask.target = 1;            // (pair_window.cpp's ASK_REQUEST)
        }
        break;
    case DebugPreview::pair_code_done:
        window->code_done = true;
        window->done_name = PREVIEW_DEVICE;
        window->done_kind = NetAppKind::piccolo;
        window->done_here = NET_CAP_SHARE | NET_CAP_SETTINGS;
        window->done_there = NET_CAP_SHARE;
        window->done_there_known = true;
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
// used (today and an older day), no pictures. UNDO SAVE: slot 1 has an
// undo, slot 3 none (greyed when picked); UNDO LOAD is on.
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
    menu->undo[1] = true;
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
    else if (which == DebugPreview::phone_dvr)
    {
        show_phone_dvr(previews);
    }
    else if (which == DebugPreview::devices)
    {
        show_devices(previews);
    }
}

void debug_preview_end(DebugPreviews* previews)
{
    previews->shown = DebugPreview::none;
    previews->pair = {};
    previews->about = {};
    previews->guest = {};
    previews->quick = {};
    previews->phone = {};
    previews->devices.open = false;
}

bool debug_preview_shown(const DebugPreviews* previews)
{
    return previews->shown != DebugPreview::none;
}

bool debug_preview_covers_screen(const DebugPreviews* previews)
{
    return is_phone(previews->shown) || previews->shown == DebugPreview::phone_dvr;
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
                         const char* version, const RetroUiMenu* menus, int32_t menu_count, bool phone_upright)
{
    if (previews->shown == DebugPreview::devices)
    {
        ports_widget_state(&view->devices, strings, false, phone_upright, SDL_GetTicks());
    }
    view->shown = previews->shown;
    if (previews->shown == DebugPreview::phone_dvr)
    {
        DvrViewText* text = &view->dvr_text;
        text->title = piccolo_text(strings, Str::dvr_title);
        text->mode = piccolo_text(strings, Str::dvr_mode_scrub);
        text->readout = "-0:27.8";
        text->exit = piccolo_text(strings, Str::dvr_back_to_now);
        text->play_here = piccolo_text(strings, Str::dvr_play_here);
        text->watch = piccolo_text(strings, Str::dvr_watch);
        text->jump_back = piccolo_text(strings, Str::dvr_jump_back);
        text->jump_forward = piccolo_text(strings, Str::dvr_jump_forward);
        text->lane_recorded = piccolo_text(strings, Str::dvr_tab_recorded);
        text->lane_kept = piccolo_text(strings, Str::dvr_tab_kept);
    }
    if (is_phone(previews->shown))
    {
        MobileHomeState* phone = &view->phone;
        phone->strings = strings;
        phone->home_shown = previews->shown != DebugPreview::phone_menu;
        phone->menu_in_game = previews->shown == DebugPreview::phone_menu;
        phone->menu_shown = phone->menu_in_game;
        phone->games = previews->shown == DebugPreview::phone_home ? PREVIEW_GAMES : nullptr;
        phone->game_count = previews->shown == DebugPreview::phone_home
                                ? static_cast<int32_t>(sizeof(PREVIEW_GAMES) / sizeof(PREVIEW_GAMES[0]))
                                : 0;
        phone->menus = menus;
        phone->menu_count = menu_count;
    }
    if (is_pair(previews->shown))
    {
        // The network runs (GENERATE PAIRING CODE works).
        pair_window_state(&view->pair, strings, &previews->pair_link, true);
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
        quick_menu_state(&view->quick, &previews->quick, strings, PREVIEW_CORE, PREVIEW_GAME, false, true);
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
    else if (view->shown == DebugPreview::devices)
    {
        ports_widget_draw_board(&previews->devices, &view->devices, pixel_font, scale, &actions->devices);
    }
    else if (view->shown == DebugPreview::phone_dvr)
    {
        // At the phone's touch scale; the game's room is left empty.
        const ImGuiIO& io = ImGui::GetIO();
        int32_t phone_scale = touch_ui_scale_for(io.DisplaySize.x, io.DisplaySize.y);
        float block = static_cast<float>(phone_scale);
        DvrPhoneAreas areas;
        dvr_view_phone_areas(&previews->phone_dvr, io.DisplaySize.x / block, io.DisplaySize.y / block, 4.0f / 3.0f, &areas);
        dvr_view_draw_phone(&previews->phone_dvr, pixel_font, phone_scale, &view->dvr_text, &areas, &actions->dvr);
    }
    else if (is_phone(view->shown))
    {
        // At the phone's touch scale whatever the screen (the desktop's is smaller).
        const ImGuiIO& io = ImGui::GetIO();
        int32_t phone_scale = touch_ui_scale_for(io.DisplaySize.x, io.DisplaySize.y);
        mobile_home_draw(&previews->phone, &view->phone, pixel_font, phone_scale, &actions->phone);
    }
}

// ---------------------------------------------------------------------------
// Apply

void debug_preview_apply(DebugPreviews* previews, const DebugPreviewActions* actions)
{
    // PAIR closes on its X, CLOSE, CANCEL and DONE.
    PairWindowPress pair = actions->pair.press;
    bool closed = is_pair(previews->shown) && (pair == PairWindowPress::close || pair == PairWindowPress::cancel ||
                                               pair == PairWindowPress::done);
    // REMOTE PLAY closes itself (its X, CLOSE); STOP REMOTE PLAY is ignored.
    closed = closed || (is_guest(previews->shown) && !previews->guest.open);
    closed = closed || (previews->shown == DebugPreview::about && actions->about.closed);
    closed = closed || (previews->shown == DebugPreview::quick_menu && actions->quick.closed);
    // The phone's: ☰ on HOME moves on to the sidebar, a menu's name opens
    // its items, ◀ goes back; any other tap ends it.
    if (is_phone(previews->shown))
    {
        mobile_home_apply(&previews->phone, &actions->phone, previews->shown == DebugPreview::phone_menu);
    }
    if (is_phone(previews->shown) && actions->phone.open_menu)
    {
        previews->shown = DebugPreview::phone_menu;
    }
    // DEVICES: DONE ends it; a move is ignored (the pick still shows).
    closed = closed || (previews->shown == DebugPreview::devices && actions->devices.done);
    // The DVR's: BACK TO NOW or PLAY FROM HERE ends it; the rest is ignored.
    closed = closed || (previews->shown == DebugPreview::phone_dvr && (actions->dvr.action == DvrViewAction::back_to_now ||
                                                                        actions->dvr.action == DvrViewAction::play_here));
    closed = closed || (is_phone(previews->shown) &&
                        (actions->phone.close_menu || actions->phone.picked != RETRO_UI_MENU_NONE || actions->phone.rotate));
    if (closed)
    {
        debug_preview_end(previews);
    }
}
