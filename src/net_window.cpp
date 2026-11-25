#include "net_window.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "piccolo_settings.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "settings_ui.h"
#include "ui_toolkit.h"

static const Str ACCESS_LABELS[NET_ACCESS_LEVEL_COUNT] = {Str::net_access_gamepad, Str::net_access_stream,
                                                          Str::net_access_share, Str::net_access_full};

// ---------------------------------------------------------------------------
// State

static const NetSpeedSummary* speed_of(const std::vector<NetSpeedSummary>* speed, const uint8_t* key)
{
    for (size_t i = 0; speed != nullptr && i < speed->size(); i++)
    {
        if (std::memcmp((*speed)[i].key, key, NET_KEY_SIZE) == 0)
        {
            return &(*speed)[i];
        }
    }
    return nullptr;
}

void net_window_state(NetWindowState* state, const Settings* settings, const Localization* strings,
                      const NetAccess* access, bool listen_failed, const char* this_device,
                      const std::vector<NetSpeedSummary>* speed, bool internet,
                      const std::vector<std::string>* hosts, const std::vector<uint8_t>* hosts_online)
{
    state->settings = settings;
    state->strings = strings;
    state->network = access != nullptr;
    state->listen_failed = listen_failed;
    state->this_device = this_device;
    state->internet = internet;
    state->hosts = hosts;
    state->hosts_online = hosts_online;
    if (access == nullptr)
    {
        return;
    }
    // INTERNET STATUS: what the beacon says, UDP and signaling together
    // (net_access_internet_status; a browser has only signaling).
    state->beacon = net_access_internet_status(access);
    state->devices = &access->trust.devices;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        const NetAccessPeer* entry = &access->peers[peer];
        if (entry->state != NetAccessPeerState::authorized || entry->device < 0 ||
            static_cast<size_t>(entry->device) >= access->trust.devices.size())
        {
            continue;
        }
        NetWindowPeer* row = &state->peers[peer];
        row->connected = true;
        row->device = static_cast<uint32_t>(entry->device);
        NetAddress address;
        if (net_access_peer_address(access, peer, &address))
        {
            row->has_address = true;
            net_address_to_text(address, row->address, sizeof(row->address));
        }
        row->has_stats = net_access_peer_stats(access, peer, &row->stats);
        row->speed = speed_of(speed, access->trust.devices[row->device].key);
    }
}

void net_pairing_request_state(NetPairingRequestState* state, const Localization* strings)
{
    state->strings = strings;
}

void net_invite_state(NetInviteState* state, const Localization* strings, const NetAccess* access)
{
    state->strings = strings;
    state->network = access != nullptr;
}

// ---------------------------------------------------------------------------
// Drawing

// ACCESS: GAMEPAD / STREAM / SHARE / FULL for one device, as a row of buttons
// (applied and saved at once; SHARE and FULL, which write files here, only
// after their confirmation).
static void draw_access(NetWindow* window, const NetWindowState* state, uint32_t device, NetWindowAction* action)
{
    const Localization* strings = state->strings;
    const NetTrustedDevice* entry = &(*state->devices)[device];
    const char* labels[NET_ACCESS_LEVEL_COUNT];
    for (uint32_t level = 0; level < NET_ACCESS_LEVEL_COUNT; level++)
    {
        labels[level] = piccolo_text(strings, ACCESS_LABELS[level]);
    }
    uint32_t current = static_cast<uint32_t>(entry->access);
    if (retro_ui_choice_buttons("##access", labels, NET_ACCESS_LEVEL_COUNT, &current))
    {
        NetAccessLevel chosen = static_cast<NetAccessLevel>(current);
        if (net_access_allows(chosen, NetAccessLevel::share) && !net_access_allows(entry->access, NetAccessLevel::share))
        {
            // Writing files on this machine: asked first (draw_owner_confirm).
            std::memcpy(window->owner_key, entry->key, NET_KEY_SIZE);
            window->confirm_owner = true;
            window->confirm_level = chosen;
        }
        else
        {
            action->set_level = true;
            action->level_device = device;
            action->level = chosen;
        }
    }
    retro_ui_item_tooltip(piccolo_text(strings, Str::net_device_access_help));
}

// The confirmation OWNER asks for: the device can read and write files in
// the shared folders. Given to the device with the key kept when OWNER was
// clicked, if it's still in the list (net_window_apply).
static void draw_owner_confirm(NetWindow* window, const NetWindowState* state, NetWindowAction* action)
{
    const Localization* strings = state->strings;
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    RetroUiQuestion question = {};
    question.id = "owner_confirm";
    question.title = piccolo_text(strings, Str::net_owner_confirm_title);
    question.text = piccolo_text(strings, Str::net_owner_confirm_text);
    question.yes = piccolo_text(strings, Str::net_owner_confirm_yes);
    question.no = piccolo_text(strings, Str::net_owner_cancel);
    question.width = static_cast<float>(static_cast<int>(screen_width * 0.4f));
    // Escape (B on a gamepad) answers NO, as CANCEL.
    RetroUiAnswer answer = retro_ui_question(&question, window->confirm_owner);
    if (answer == RetroUiAnswer::yes)
    {
        action->owner_yes = true;
    }
    if (answer != RetroUiAnswer::none)
    {
        window->confirm_owner = false;
    }
}

// "95.0 MBIT/S", or "-" when nothing went through clean.
static void speed_text(bool tested, uint32_t kbps, char* out, size_t size)
{
    if (!tested)
    {
        std::snprintf(out, size, "?");
    }
    else if (kbps == 0)
    {
        std::snprintf(out, size, "-");
    }
    else
    {
        std::snprintf(out, size, "%.1f MBIT/S", static_cast<double>(kbps) / 1000.0);
    }
}

//   ANA'S PHONE  192.168.1.20:41000
//   4 MS  JITTER 2 MS  LOSS 0%   SPEED TEST: DOWN 95.0 MBIT/S, UP 90.0 MBIT/S
static void draw_connected(const NetWindowState* state)
{
    const Localization* strings = state->strings;
    bool any = false;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        const NetWindowPeer* entry = &state->peers[peer];
        if (!entry->connected)
        {
            continue;
        }
        const NetTrustedDevice* device = &(*state->devices)[entry->device];
        retro_ui_push_index(peer);
        if (any)
        {
            retro_ui_spacing();
        }
        any = true;
        retro_ui_text(device->name.empty() ? "?" : device->name.c_str());
        if (entry->has_address)
        {
            retro_ui_same_line();
            retro_ui_dim_text(entry->address, nullptr);
        }
        if (entry->has_stats)
        {
            const NetLinkStats* stats = &entry->stats;
            retro_ui_text_format("%u MS  %s %u MS  %s %.0f%%", stats->round_trip_ms, piccolo_text(strings, Str::net_jitter),
                        stats->jitter_ms, piccolo_text(strings, Str::net_loss), static_cast<double>(stats->loss) * 100.0);
            retro_ui_item_tooltip(piccolo_text(strings, Str::net_device_ping_help));
        }
        const NetSpeedSummary* speed = entry->speed;
        if (speed != nullptr && (speed->down_tested || speed->up_tested))
        {
            char down[32];
            char up[32];
            speed_text(speed->down_tested, speed->down_kbps, down, sizeof(down));
            speed_text(speed->up_tested, speed->up_kbps, up, sizeof(up));
            retro_ui_text_format("%s: %s %s, %s %s", piccolo_text(strings, Str::net_speed_test),
                        piccolo_text(strings, Str::net_speed_down), down, piccolo_text(strings, Str::net_speed_up), up);
            retro_ui_item_tooltip(piccolo_text(strings, Str::net_speed_test_help));
        }
        retro_ui_pop_id();
    }
    if (!any)
    {
        retro_ui_dim_text(piccolo_text(strings, Str::net_no_connected), nullptr);
    }
}

// A connected device's first connection, or nullptr (not connected).
static const NetWindowPeer* device_peer(const NetWindowState* state, uint32_t device)
{
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        if (state->peers[peer].connected && state->peers[peer].device == device)
        {
            return &state->peers[peer];
        }
    }
    return nullptr;
}

//   ANA'S PHONE  9c77-3332-311c-4017  CONNECTED 4 MS
//   [GAMEPAD][STREAM][FULL]  [FORGET]
static void draw_devices(NetWindow* window, const NetWindowState* state, NetWindowAction* action)
{
    const Localization* strings = state->strings;
    const std::vector<NetTrustedDevice>* devices = state->devices;
    if (devices->empty())
    {
        retro_ui_dim_text(piccolo_text(strings, Str::net_no_devices), nullptr);
        return;
    }
    // FORGET changes the list: applied after drawing it (net_window_apply).
    for (size_t i = 0; i < devices->size(); i++)
    {
        const NetTrustedDevice* device = &(*devices)[i];
        retro_ui_push_index(static_cast<uint32_t>(i));
        if (i > 0)
        {
            retro_ui_spacing();
        }
        char fingerprint[32];
        net_key_fingerprint(device->key, fingerprint, sizeof(fingerprint));
        retro_ui_text(device->name.empty() ? "?" : device->name.c_str());
        retro_ui_same_line();
        retro_ui_dim_text(fingerprint, nullptr);
        const NetWindowPeer* connection = device_peer(state, static_cast<uint32_t>(i));
        if (connection != nullptr)
        {
            retro_ui_same_line();
            retro_ui_push_section_text();
            retro_ui_text(piccolo_text(strings, Str::net_device_connected));
            retro_ui_pop_section_text();
            if (connection->has_stats)
            {
                // The round trip in milliseconds, as ENet measures it.
                char ping[32];
                std::snprintf(ping, sizeof(ping), "%u MS", connection->stats.round_trip_ms);
                retro_ui_same_line();
                retro_ui_dim_text(ping, piccolo_text(strings, Str::net_device_ping_help));
            }
        }
        draw_access(window, state, static_cast<uint32_t>(i), action);
        retro_ui_same_line();
        if (retro_ui_button(piccolo_text(strings, Str::net_device_forget)))
        {
            action->forget = true;
            action->forget_device = static_cast<uint32_t>(i);
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::net_device_forget_help));
        retro_ui_pop_id();
    }
    draw_owner_confirm(window, state, action);
}

// INTERNET STATUS: what the beacon says (net_window_state).
static void draw_beacon_status(const NetWindowState* state, float label_width)
{
    const Localization* strings = state->strings;
    Str text = Str::net_beacon_off;
    bool good = false;
    switch (state->beacon)
    {
    case NetBeaconStatus::off:
        text = Str::net_beacon_off;
        break;
    case NetBeaconStatus::waiting:
        text = Str::net_beacon_waiting;
        break;
    case NetBeaconStatus::registered:
        text = Str::net_beacon_registered;
        good = true;
        break;
    case NetBeaconStatus::symmetric_nat:
        text = Str::net_beacon_symmetric;
        break;
    case NetBeaconStatus::unreachable:
        text = Str::net_beacon_unreachable;
        break;
    }
    RetroUiRow row = {};
    row.id = "beacon_status";
    row.label = piccolo_text(strings, Str::net_beacon_status);
    row.help = piccolo_text(strings, Str::net_beacon_status_help);
    row.label_width = label_width;
    retro_ui_row_begin(&row);
    if (good)
    {
        retro_ui_text(piccolo_text(strings, text));
    }
    else
    {
        retro_ui_dim_text(piccolo_text(strings, text), nullptr);
    }
    retro_ui_row_end(&row);
}

// The piccolos we connect to (paired with TOOLS > PAIR / SHARE): FORGET only;
// their access is the other side's to give (Andrés, 2026-11-22).
static void draw_hosts(const NetWindowState* state, NetWindowAction* action)
{
    const Localization* strings = state->strings;
    if (state->hosts == nullptr || state->hosts->empty())
    {
        retro_ui_dim_text(piccolo_text(strings, Str::net_no_hosts), nullptr);
        return;
    }
    for (size_t i = 0; i < state->hosts->size(); i++)
    {
        retro_ui_push_index(static_cast<uint32_t>(5000 + i));
        retro_ui_label((*state->hosts)[i].empty() ? "?" : (*state->hosts)[i].c_str());
        if (state->hosts_online != nullptr && i < state->hosts_online->size())
        {
            retro_ui_same_line();
            if ((*state->hosts_online)[i] != 0)
            {
                retro_ui_push_section_text();
                retro_ui_text(piccolo_text(strings, Str::net_online));
                retro_ui_pop_section_text();
            }
            else
            {
                retro_ui_dim_text(piccolo_text(strings, Str::net_offline), nullptr);
            }
        }
        retro_ui_same_line();
        if (retro_ui_button(piccolo_text(strings, Str::net_device_forget)))
        {
            action->forget_host = true;
            action->forget_host_index = static_cast<uint32_t>(i);
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::net_host_forget_help));
        retro_ui_pop_id();
    }
}

void net_window_draw_tab(NetWindow* window, const NetWindowState* state, float label_width,
                         NetWindowAction* action)
{
    const Localization* strings = state->strings;
    SettingsUiContext rows;
    rows.settings = state->settings;
    rows.edit = &action->setting;
    rows.strings = strings;
    rows.scope = SettingLayer::global;
    rows.label_width = label_width;
    settings_ui_row(&rows, PICCOLO_NET_ENABLED, nullptr);
    settings_ui_row(&rows, PICCOLO_NET_NAME, nullptr);
    settings_ui_row(&rows, PICCOLO_NET_PORT, nullptr);
    // The browser: the beacon is its only way (no UDP), INTERNET shows checked and greyed.
    retro_ui_begin_disabled(PICCOLO_BROWSER);
    settings_ui_row(&rows, PICCOLO_NET_INTERNET, nullptr);
    retro_ui_end_disabled();
    settings_ui_row(&rows, PICCOLO_NET_BEACON, nullptr);

    // As a row: the marker column, then the label column.
    RetroUiRow row = {};
    row.id = "this_device";
    row.label = piccolo_text(strings, Str::net_this_device);
    row.help = piccolo_text(strings, Str::net_this_device_help);
    row.label_width = label_width;
    retro_ui_row_begin(&row);
    retro_ui_text(state->this_device[0] != '\0' ? state->this_device : "-");
    retro_ui_row_end(&row);

    if (state->network && state->internet)
    {
        draw_beacon_status(state, label_width);
    }

    if (!state->network)
    {
        retro_ui_spacing();
        retro_ui_dim_text(piccolo_text(strings, state->listen_failed ? Str::net_listen_failed : Str::net_off), nullptr);
        return;
    }
    retro_ui_separator_text(piccolo_text(strings, Str::net_section_connected));
    draw_connected(state);
    retro_ui_separator_text(piccolo_text(strings, Str::net_section_devices));
    draw_devices(window, state, action);
    retro_ui_separator_text(piccolo_text(strings, Str::net_section_hosts));
    draw_hosts(state, action);
}

// The four port LEDs lit one at a time, 1 2 3 4 3 2 1 ..., while waiting.
void net_window_waiting_leds(const char* label)
{
    static const int32_t CYCLE[6] = {0, 1, 2, 3, 2, 1};
    int32_t lit = CYCLE[static_cast<uint32_t>(retro_ui_seconds() * 6.0) % 6];
    float block = std::floor(retro_ui_font_size() / 7.0f);
    block = block < 1.0f ? 1.0f : block;
    retro_ui_port_leds(4, lit, block);
    retro_ui_same_line();
    retro_ui_text(label);
}

// --- The pairing request (host) ----------------------------------------------

//   +- PAIRING REQUEST -------------------------------+
//   |  BEDROOM PC (PICCOLO)                           |
//   |  WANTS TO PAIR. TYPE THE CODE ON ITS SCREEN.    |
//   |  [482193706]                                    |
//   |  ACCESS [GAMEPAD][STREAM][SHARE][FULL]          |
//   |                          [ALLOW] [REFUSE]       |
//   +-------------------------------------------------+
// After ALLOW: the same box, waiting (LEDs), until paired / gone.
void net_pairing_request_draw(NetPairingRequest* request, const NetPairingRequestState* state,
                              RetroUiFont* pixel_font, int32_t scale, NetPairingRequestAction* action)
{
    if (!request->active)
    {
        return;
    }
    const Localization* strings = state->strings;
    RetroUiWindow frame = {};
    frame.id = "pairing_request";
    frame.title = piccolo_text(strings, Str::net_request_title);
    frame.centered = true;
    frame.closable = false;
    frame.focus = true;
    // Themed like every other window: the pixel font, the palette and the window's edge.
    retro_ui_push_theme(pixel_font, scale);
    if (retro_ui_window_begin(&frame, scale, nullptr))
    {
        retro_ui_push_section_text();
        retro_ui_text_format("%s (%s)", request->name,
                             piccolo_text(strings, request->kind == NetPairKind::piccoletto ? Str::net_kind_piccoletto
                                                                                           : Str::net_kind_piccolo));
        retro_ui_pop_section_text();
        retro_ui_dim_text(piccolo_text(strings, request->retry ? Str::net_request_wrong : Str::net_request_text), nullptr);
        retro_ui_begin_disabled(request->waiting);
        retro_ui_next_width(retro_ui_text_width("0000000000000"));
        RetroUiEditField code_field = {};
        code_field.decimal = true;
        code_field.focus = request->focus;
        request->focus = false;
        bool enter = retro_ui_edit_field("code", request->code, sizeof(request->code), &code_field);
        retro_ui_label(piccolo_text(strings, Str::net_request_access));
        retro_ui_same_line();
        const char* labels[NET_ACCESS_LEVEL_COUNT];
        for (uint32_t level = 0; level < NET_ACCESS_LEVEL_COUNT; level++)
        {
            labels[level] = piccolo_text(strings, ACCESS_LABELS[level]);
        }
        retro_ui_choice_buttons("##request_access", labels, NET_ACCESS_LEVEL_COUNT, &request->level);
        retro_ui_end_disabled();
        if (request->waiting)
        {
            net_window_waiting_leds(piccolo_text(strings, Str::net_request_waiting));
            if (retro_ui_button(piccolo_text(strings, Str::net_owner_cancel)))
            {
                action->press = NetPairingRequestPress::refuse;
            }
        }
        else
        {
            bool complete = net_pair_code_valid(request->code);
            retro_ui_begin_disabled(!complete);
            if (retro_ui_button(piccolo_text(strings, Str::net_request_allow)) || (enter && complete))
            {
                action->press = NetPairingRequestPress::allow;
            }
            retro_ui_end_disabled();
            retro_ui_same_line();
            if (retro_ui_button(piccolo_text(strings, Str::net_request_refuse)))
            {
                action->press = NetPairingRequestPress::refuse;
            }
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}

// --- TOOLS > INVITE ---------------------------------------------------------------

//   +- INVITE --------------------------------------------------+
//   |  TYPE THE CODE THE OTHER DEVICE SHOWS (TOOLS > PAIR >      |
//   |  INTERNET, OR PICCOLETTO'S PAIR).                          |
//   |  [482193706]                                               |
//   |  ACCESS [GAMEPAD][STREAM][SHARE][FULL]                     |
//   |                                     [ALLOW] [CANCEL]       |
//   +-----------------------------------------------------------+
// After ALLOW: waiting (LEDs) until paired (it closes then).
void net_invite_draw(NetInvite* invite, const NetInviteState* state, RetroUiFont* pixel_font, int32_t scale,
                     NetInviteAction* action)
{
    if (!invite->open)
    {
        return;
    }
    const Localization* strings = state->strings;
    RetroUiWindow frame = {};
    frame.id = "invite";
    frame.title = piccolo_text(strings, Str::net_invite_title);
    frame.centered_on_open = true;
    bool closed = false;
    // Themed like every other window; the close button is the pixel font's X.
    retro_ui_push_theme(pixel_font, scale);
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        if (closed)
        {
            action->close = true;
        }
        if (!state->network)
        {
            retro_ui_dim_text(piccolo_text(strings, Str::net_off), nullptr);
        }
        else
        {
            retro_ui_text_wrapped_at(piccolo_text(strings, Str::net_invite_text), retro_ui_font_size() * 28.0f);
            retro_ui_begin_disabled(invite->waiting);
            retro_ui_next_width(retro_ui_text_width("0000000000000"));
            RetroUiEditField code_field = {};
            code_field.decimal = true;
            code_field.focus = invite->focus;
            invite->focus = false;
            bool enter = retro_ui_edit_field("invite_code", invite->code, sizeof(invite->code), &code_field);
            retro_ui_label(piccolo_text(strings, Str::net_request_access));
            retro_ui_same_line();
            const char* labels[NET_ACCESS_LEVEL_COUNT];
            for (uint32_t level = 0; level < NET_ACCESS_LEVEL_COUNT; level++)
            {
                labels[level] = piccolo_text(strings, ACCESS_LABELS[level]);
            }
            retro_ui_choice_buttons("##invite_access", labels, NET_ACCESS_LEVEL_COUNT, &invite->level);
            retro_ui_end_disabled();
            if (invite->waiting)
            {
                net_window_waiting_leds(piccolo_text(strings, invite->answered ? Str::net_request_waiting
                                                                               : Str::net_invite_waiting));
            }
            else
            {
                bool complete = net_pair_code_valid(invite->code);
                retro_ui_begin_disabled(!complete);
                if (retro_ui_button(piccolo_text(strings, Str::net_request_allow)) || (enter && complete))
                {
                    action->allow = true;
                }
                retro_ui_end_disabled();
                retro_ui_same_line();
            }
            if (retro_ui_button(piccolo_text(strings, Str::net_owner_cancel)))
            {
                action->close = true;
            }
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}

// ---------------------------------------------------------------------------
// Apply

bool net_window_apply(NetWindow* window, const NetWindowAction* action, Settings* settings, NetAccess* access)
{
    SettingsUiEdit edit = action->setting;
    bool changed = settings_ui_apply_edit(settings, &edit);
    if (access == nullptr)
    {
        return changed;   // the lists aren't shown
    }
    if (action->set_level)
    {
        net_access_set_level(access, action->level_device, action->level);
    }
    if (action->forget)
    {
        net_access_remove_device(access, action->forget_device);
    }
    if (action->owner_yes)
    {
        for (size_t i = 0; i < access->trust.devices.size(); i++)
        {
            if (std::memcmp(access->trust.devices[i].key, window->owner_key, NET_KEY_SIZE) == 0)
            {
                net_access_set_level(access, static_cast<uint32_t>(i), window->confirm_level);
            }
        }
    }
    return changed;
}

void net_pairing_request_open(NetPairingRequest* request, const NetAccessEvent* event)
{
    bool same = request->active && request->peer == event->peer;
    request->active = true;
    request->waiting = false;
    request->retry = event->retry;
    request->peer = event->peer;
    request->kind = event->kind;
    std::snprintf(request->name, sizeof(request->name), "%s", event->name);
    if (!same)
    {
        request->level = static_cast<uint32_t>(event->access);   // what it asked for, until the user picks
    }
    std::memset(request->code, 0, sizeof(request->code));
    request->focus = true;
}

void net_pairing_request_close(NetPairingRequest* request)
{
    *request = {};
}

void net_pairing_request_apply(NetPairingRequest* request, const NetPairingRequestAction* action, NetAccess* access)
{
    switch (action->press)
    {
    case NetPairingRequestPress::none:
        break;
    case NetPairingRequestPress::allow:
        if (net_access_answer_pairing(access, request->peer, request->code, static_cast<NetAccessLevel>(request->level)))
        {
            request->waiting = true;
        }
        break;
    case NetPairingRequestPress::refuse:
        net_access_refuse_pairing(access, request->peer);
        net_pairing_request_close(request);
        break;
    }
}

void net_invite_open(NetInvite* invite)
{
    *invite = {};
    invite->open = true;
    invite->level = static_cast<uint32_t>(NetAccessLevel::gamepad);
    invite->focus = true;
}

void net_invite_close(NetInvite* invite, NetAccess* access)
{
    if (access != nullptr && invite->waiting)
    {
        net_access_end_invite(access);
    }
    *invite = {};
}

void net_invite_apply(NetInvite* invite, const NetInviteAction* action, NetAccess* access)
{
    if (action->allow && access != nullptr && net_access_invite(access, invite->code))
    {
        invite->waiting = true;
    }
    if (action->close)
    {
        net_invite_close(invite, access);
    }
}

bool net_invite_take_request(NetInvite* invite, NetAccess* access, const NetAccessEvent* event)
{
    if (!invite->open || !invite->waiting || invite->answered || event->retry)
    {
        return false;   // a wrong code's new request goes to the request dialog: its new code is typed there
    }
    if (!net_access_answer_pairing(access, event->peer, invite->code, static_cast<NetAccessLevel>(invite->level)))
    {
        return false;
    }
    invite->answered = true;
    invite->peer = event->peer;
    return true;
}
