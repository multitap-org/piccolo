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

// ACCESS: GAMEPAD / STREAM / SHARE / FULL for one device, as a row of buttons
// (applied and saved at once; SHARE and FULL, which write files here, only
// after their confirmation).
static void draw_access(const NetWindowContext* context, uint32_t device)
{
    NetAccess* access = context->access;
    const Localization* strings = context->strings;
    const char* labels[NET_ACCESS_LEVEL_COUNT];
    for (uint32_t level = 0; level < NET_ACCESS_LEVEL_COUNT; level++)
    {
        labels[level] = piccolo_text(strings, ACCESS_LABELS[level]);
    }
    uint32_t current = static_cast<uint32_t>(access->trust.devices[device].access);
    if (retro_ui_choice_buttons("##access", labels, NET_ACCESS_LEVEL_COUNT, &current))
    {
        NetAccessLevel chosen = static_cast<NetAccessLevel>(current);
        if (net_access_allows(chosen, NetAccessLevel::share) &&
            !net_access_allows(access->trust.devices[device].access, NetAccessLevel::share))
        {
            // Writing files on this machine: asked first (draw_owner_confirm).
            std::memcpy(context->state->owner_key, access->trust.devices[device].key, NET_KEY_SIZE);
            context->state->confirm_owner = true;
            context->state->confirm_level = chosen;
        }
        else
        {
            net_access_set_level(access, device, chosen);
        }
    }
    retro_ui_item_tooltip(piccolo_text(strings, Str::net_device_access_help));
}

// The confirmation OWNER asks for: the device can read and write files in
// the shared folders. Given to the device with the key kept when OWNER was
// clicked, if it's still in the list.
static void draw_owner_confirm(const NetWindowContext* context)
{
    NetWindowState* state = context->state;
    const Localization* strings = context->strings;
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
    RetroUiAnswer answer = retro_ui_question(&question, state->confirm_owner);
    if (answer == RetroUiAnswer::yes)
    {
        NetAccess* access = context->access;
        for (size_t i = 0; i < access->trust.devices.size(); i++)
        {
            if (std::memcmp(access->trust.devices[i].key, state->owner_key, NET_KEY_SIZE) == 0)
            {
                net_access_set_level(access, static_cast<uint32_t>(i), state->confirm_level);
            }
        }
    }
    if (answer != RetroUiAnswer::none)
    {
        state->confirm_owner = false;
    }
}

static const NetSpeedSummary* speed_of(const NetWindowContext* context, const uint8_t* key)
{
    for (size_t i = 0; context->speed != nullptr && i < context->speed->size(); i++)
    {
        if (std::memcmp((*context->speed)[i].key, key, NET_KEY_SIZE) == 0)
        {
            return &(*context->speed)[i];
        }
    }
    return nullptr;
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
static void draw_connected(const NetWindowContext* context)
{
    const NetAccess* access = context->access;
    const Localization* strings = context->strings;
    bool any = false;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        const NetAccessPeer* entry = &access->peers[peer];
        if (entry->state != NetAccessPeerState::authorized || entry->device < 0 ||
            static_cast<size_t>(entry->device) >= access->trust.devices.size())
        {
            continue;
        }
        const NetTrustedDevice* device = &access->trust.devices[static_cast<size_t>(entry->device)];
        retro_ui_push_index(peer);
        if (any)
        {
            retro_ui_spacing();
        }
        any = true;
        retro_ui_text(device->name.empty() ? "?" : device->name.c_str());
        NetAddress address;
        if (net_access_peer_address(access, peer, &address))
        {
            char text[64];
            net_address_to_text(address, text, sizeof(text));
            retro_ui_same_line();
            retro_ui_dim_text(text, nullptr);
        }
        NetLinkStats stats;
        if (net_access_peer_stats(access, peer, &stats))
        {
            retro_ui_text_format("%u MS  %s %u MS  %s %.0f%%", stats.round_trip_ms, piccolo_text(strings, Str::net_jitter),
                        stats.jitter_ms, piccolo_text(strings, Str::net_loss), static_cast<double>(stats.loss) * 100.0);
            retro_ui_item_tooltip(piccolo_text(strings, Str::net_device_ping_help));
        }
        const NetSpeedSummary* speed = speed_of(context, device->key);
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

// The ping of a connected device's first connection, or false.
static bool device_ping(const NetAccess* access, uint32_t device, NetLinkStats* stats)
{
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        if (access->peers[peer].state == NetAccessPeerState::authorized &&
            access->peers[peer].device == static_cast<int32_t>(device))
        {
            return net_access_peer_stats(access, peer, stats);
        }
    }
    return false;
}

//   ANA'S PHONE  9c77-3332-311c-4017  CONNECTED 4 MS
//   [GAMEPAD][STREAM][FULL]  [FORGET]
static void draw_devices(const NetWindowContext* context)
{
    NetAccess* access = context->access;
    const Localization* strings = context->strings;
    if (access->trust.devices.empty())
    {
        retro_ui_dim_text(piccolo_text(strings, Str::net_no_devices), nullptr);
        return;
    }
    // FORGET changes the list: act after drawing it.
    int32_t forget = -1;
    for (size_t i = 0; i < access->trust.devices.size(); i++)
    {
        const NetTrustedDevice* device = &access->trust.devices[i];
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
        if (net_access_device_connected(access, static_cast<uint32_t>(i)))
        {
            retro_ui_same_line();
            retro_ui_push_section_text();
            retro_ui_text(piccolo_text(strings, Str::net_device_connected));
            retro_ui_pop_section_text();
            NetLinkStats stats;
            if (device_ping(access, static_cast<uint32_t>(i), &stats))
            {
                // The round trip in milliseconds, as ENet measures it.
                char ping[32];
                std::snprintf(ping, sizeof(ping), "%u MS", stats.round_trip_ms);
                retro_ui_same_line();
                retro_ui_dim_text(ping, piccolo_text(strings, Str::net_device_ping_help));
            }
        }
        draw_access(context, static_cast<uint32_t>(i));
        retro_ui_same_line();
        if (retro_ui_button(piccolo_text(strings, Str::net_device_forget)))
        {
            forget = static_cast<int32_t>(i);
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::net_device_forget_help));
        retro_ui_pop_id();
    }
    if (forget >= 0)
    {
        net_access_remove_device(access, static_cast<uint32_t>(forget));
    }
    draw_owner_confirm(context);
}

// INTERNET STATUS: what the beacon says, UDP and signaling together
// (net_access_internet_status; a browser has only signaling).
static void draw_beacon_status(const NetWindowContext* context, float label_width)
{
    const Localization* strings = context->strings;
    Str text = Str::net_beacon_off;
    bool good = false;
    switch (net_access_internet_status(context->access))
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
static void draw_hosts(const NetWindowContext* context)
{
    const Localization* strings = context->strings;
    if (context->hosts == nullptr || context->hosts->empty())
    {
        retro_ui_dim_text(piccolo_text(strings, Str::net_no_hosts), nullptr);
        return;
    }
    for (size_t i = 0; i < context->hosts->size(); i++)
    {
        retro_ui_push_index(static_cast<uint32_t>(5000 + i));
        retro_ui_label((*context->hosts)[i].empty() ? "?" : (*context->hosts)[i].c_str());
        if (context->hosts_online != nullptr && i < context->hosts_online->size())
        {
            retro_ui_same_line();
            if ((*context->hosts_online)[i] != 0)
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
        if (retro_ui_button(piccolo_text(strings, Str::net_device_forget)) && context->forget_host != nullptr)
        {
            *context->forget_host = static_cast<int32_t>(i);
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::net_host_forget_help));
        retro_ui_pop_id();
    }
}

void net_window_draw_tab(const NetWindowContext* context, float label_width)
{
    const Localization* strings = context->strings;
    SettingsUiContext rows;
    rows.settings = context->settings;
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
    retro_ui_text(context->this_device[0] != '\0' ? context->this_device : "-");
    retro_ui_row_end(&row);

    if (context->access != nullptr && context->internet)
    {
        draw_beacon_status(context, label_width);
    }

    if (context->access == nullptr)
    {
        retro_ui_spacing();
        retro_ui_dim_text(piccolo_text(strings, context->listen_failed ? Str::net_listen_failed : Str::net_off), nullptr);
        return;
    }
    retro_ui_separator_text(piccolo_text(strings, Str::net_section_connected));
    draw_connected(context);
    retro_ui_separator_text(piccolo_text(strings, Str::net_section_devices));
    draw_devices(context);
    retro_ui_separator_text(piccolo_text(strings, Str::net_section_hosts));
    draw_hosts(context);
}

// --- The pairing request (host) ----------------------------------------------

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

//   +- PAIRING REQUEST -------------------------------+
//   |  BEDROOM PC (PICCOLO)                           |
//   |  WANTS TO PAIR. TYPE THE CODE ON ITS SCREEN.    |
//   |  [482193706]                                    |
//   |  ACCESS [GAMEPAD][STREAM][SHARE][FULL]          |
//   |                          [ALLOW] [REFUSE]       |
//   +-------------------------------------------------+
// After ALLOW: the same box, waiting (LEDs), until paired / gone.
void net_pairing_request_draw(NetPairingRequest* request, NetAccess* access, const Localization* strings,
                             RetroUiFont* pixel_font, int32_t scale)
{
    if (!request->active || access == nullptr)
    {
        return;
    }
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
                net_access_refuse_pairing(access, request->peer);
                net_pairing_request_close(request);
            }
        }
        else
        {
            bool complete = net_pair_code_valid(request->code);
            retro_ui_begin_disabled(!complete);
            if (retro_ui_button(piccolo_text(strings, Str::net_request_allow)) || (enter && complete))
            {
                if (net_access_answer_pairing(access, request->peer, request->code,
                                              static_cast<NetAccessLevel>(request->level)))
                {
                    request->waiting = true;
                }
            }
            retro_ui_end_disabled();
            retro_ui_same_line();
            if (retro_ui_button(piccolo_text(strings, Str::net_request_refuse)))
            {
                net_access_refuse_pairing(access, request->peer);
                net_pairing_request_close(request);
            }
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}

// --- TOOLS > INVITE ---------------------------------------------------------------

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

//   +- INVITE --------------------------------------------------+
//   |  TYPE THE CODE THE OTHER DEVICE SHOWS (TOOLS > PAIR >      |
//   |  INTERNET, OR PICCOLETTO'S PAIR).                          |
//   |  [482193706]                                               |
//   |  ACCESS [GAMEPAD][STREAM][SHARE][FULL]                     |
//   |                                     [ALLOW] [CANCEL]       |
//   +-----------------------------------------------------------+
// After ALLOW: waiting (LEDs) until paired (it closes then).
void net_invite_draw(NetInvite* invite, NetAccess* access, const Localization* strings, RetroUiFont* pixel_font,
                     int32_t scale)
{
    if (!invite->open)
    {
        return;
    }
    RetroUiWindow frame = {};
    frame.id = "invite";
    frame.title = piccolo_text(strings, Str::net_invite_title);
    frame.centered_on_open = true;
    bool keep = true;
    bool closed = false;
    // Themed like every other window; the close button is the pixel font's X.
    retro_ui_push_theme(pixel_font, scale);
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        if (closed)
        {
            keep = false;
        }
        if (access == nullptr)
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
                if ((retro_ui_button(piccolo_text(strings, Str::net_request_allow)) || (enter && complete)) &&
                    net_access_invite(access, invite->code))
                {
                    invite->waiting = true;
                }
                retro_ui_end_disabled();
                retro_ui_same_line();
            }
            if (retro_ui_button(piccolo_text(strings, Str::net_owner_cancel)))
            {
                keep = false;
            }
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
    if (!keep)
    {
        net_invite_close(invite, access);
    }
}
