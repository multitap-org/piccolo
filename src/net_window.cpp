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

static NetWindowDevice* device_of(NetWindowState* state, const uint8_t* key)
{
    for (size_t i = 0; i < state->devices.size(); i++)
    {
        if (std::memcmp(state->devices[i].record.key, key, NET_KEY_SIZE) == 0)
        {
            return &state->devices[i];
        }
    }
    return nullptr;
}

void net_window_state(NetWindowState* state, const Settings* settings, const Localization* strings,
                      const NetAccess* access, const NetShareLinkStatus* link, bool listen_failed,
                      const char* this_device, const std::vector<NetSpeedSummary>* speed, bool internet,
                      NetCaps offers, NetCaps uses)
{
    state->settings = settings;
    state->strings = strings;
    state->network = access != nullptr;
    state->listen_failed = listen_failed;
    state->this_device = this_device;
    state->internet = internet;
    state->beacon = NetBeaconStatus::off;
    state->offers = offers;
    state->uses = uses;
    state->link_known = link != nullptr;
    state->devices_known = access != nullptr || link != nullptr;
    state->devices.clear();
    state->link_connected = false;
    state->link_name.clear();
    state->link_caps = 0;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        state->peers[peer] = {};
    }
    // The paired devices: the NetAccess's copy while it runs (pairings land
    // there first), else FILE TRANSFER's (the same file).
    const std::vector<NetTrustedDevice>* records = nullptr;
    if (access != nullptr)
    {
        records = &access->trust.devices;
    }
    else if (link != nullptr)
    {
        records = &link->paired_devices;
    }
    for (size_t i = 0; records != nullptr && i < records->size(); i++)
    {
        NetWindowDevice device;
        device.record = (*records)[i];
        state->devices.push_back(device);
    }
    if (link != nullptr)
    {
        for (size_t i = 0; i < link->paired_devices.size() && i < link->paired_online.size(); i++)
        {
            NetWindowDevice* device = device_of(state, link->paired_devices[i].key);
            if (device != nullptr)
            {
                device->online = link->paired_online[i] != 0 ? 1 : 0;
            }
        }
        if (link->state == NetShareLinkState::ready)
        {
            state->link_connected = true;
            state->link_name = link->host_name;
            state->link_caps = link->caps;
            if (link->device >= 0 && static_cast<size_t>(link->device) < link->paired_devices.size())
            {
                NetWindowDevice* device = device_of(state, link->paired_devices[static_cast<size_t>(link->device)].key);
                if (device != nullptr)
                {
                    device->connected = true;
                    device->record.granted = link->caps;   // what it says now
                }
            }
        }
    }
    if (access == nullptr)
    {
        return;
    }
    // INTERNET STATUS: what the beacon says, UDP and signaling together
    // (net_access_internet_status; a browser has only signaling).
    state->beacon = net_access_internet_status(access);
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        const NetAccessPeer* entry = &access->peers[peer];
        if (entry->state != NetAccessPeerState::authorized || entry->device < 0 ||
            static_cast<size_t>(entry->device) >= access->trust.devices.size())
        {
            continue;
        }
        const NetTrustedDevice* record = &access->trust.devices[static_cast<size_t>(entry->device)];
        NetWindowPeer* row = &state->peers[peer];
        row->connected = true;
        std::memcpy(row->key, record->key, NET_KEY_SIZE);
        row->name = record->name;
        row->caps = net_access_peer_caps(access, peer);
        NetAddress address;
        if (net_access_peer_address(access, peer, &address))
        {
            row->has_address = true;
            net_address_to_text(address, row->address, sizeof(row->address));
        }
        row->has_stats = net_access_peer_stats(access, peer, &row->stats);
        row->speed = speed_of(speed, record->key);
        NetWindowDevice* device = device_of(state, record->key);
        if (device != nullptr)
        {
            device->connected = true;
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing

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

// The lights of STATUS and of the devices' cards (0xAARRGGBB): our own, the
// same in every theme, as the settings' markers.
constexpr uint32_t LIGHT_GOOD = 0xFF50D060u;   // green: ready, connected, online
constexpr uint32_t LIGHT_WAIT = 0xFFE0B040u;   // amber: connecting, or ready with a doubt
constexpr uint32_t LIGHT_BAD  = 0xFFE05050u;   // red: the beacon doesn't answer
constexpr uint32_t LIGHT_OFF  = 0xFF707070u;   // grey: off, offline

// A light: a square two thirds of the font tall, in the middle of a line
// `height` tall, then the next item on the same line.
static void draw_light(uint32_t argb, float height)
{
    float size = std::floor(retro_ui_font_size() * 2.0f / 3.0f);
    size = size < 2.0f ? 2.0f : size;
    retro_ui_canvas(size, height);
    retro_ui_canvas_rect(0.0f, std::floor((height - size) * 0.5f), size, size, argb);
    retro_ui_same_line();
}

// DEVICE ID: this device's key in short (net_key_fingerprint), the same the
// other device shows when it pairs with this one. Read only.
static void draw_device_id(const NetWindowState* state, float label_width)
{
    const Localization* strings = state->strings;
    RetroUiRow row = {};
    row.id = "this_device";
    row.label = piccolo_text(strings, Str::net_this_device);
    row.help = piccolo_text(strings, Str::net_this_device_help);
    row.label_width = label_width;
    retro_ui_row_begin(&row);
    char shown[64] = {};
    std::snprintf(shown, sizeof(shown), "%s", state->this_device[0] != '\0' ? state->this_device : "-");
    RetroUiEditField field = {};
    field.read_only = true;
    retro_ui_edit_field("value", shown, sizeof(shown), &field);
    retro_ui_row_end(&row);
}

// STATUS: what the beacon says (net_window_state), with a light. SYMMETRIC
// NAT, the why of "this network may block connections", is its tooltip.
static void draw_beacon_status(const NetWindowState* state, float label_width)
{
    const Localization* strings = state->strings;
    Str text = Str::net_beacon_off;
    uint32_t light = LIGHT_OFF;
    const char* help = nullptr;
    switch (state->beacon)
    {
    case NetBeaconStatus::off:
        text = Str::net_beacon_off;
        light = LIGHT_OFF;
        break;
    case NetBeaconStatus::waiting:
        text = Str::net_beacon_waiting;
        light = LIGHT_WAIT;
        break;
    case NetBeaconStatus::registered:
        text = Str::net_beacon_registered;
        light = LIGHT_GOOD;
        break;
    case NetBeaconStatus::symmetric_nat:
        text = Str::net_beacon_symmetric;
        light = LIGHT_WAIT;
        help = piccolo_text(strings, Str::net_beacon_symmetric_help);
        break;
    case NetBeaconStatus::unreachable:
        text = Str::net_beacon_unreachable;
        light = LIGHT_BAD;
        break;
    }
    RetroUiRow row = {};
    row.id = "beacon_status";
    row.label = piccolo_text(strings, Str::net_beacon_status);
    row.help = piccolo_text(strings, Str::net_beacon_status_help);
    row.label_width = label_width;
    retro_ui_row_begin(&row);
    draw_light(light, retro_ui_line_height());
    if (state->beacon == NetBeaconStatus::off)
    {
        retro_ui_dim_label(piccolo_text(strings, text), help);
    }
    else
    {
        retro_ui_label(piccolo_text(strings, text));
        if (help != nullptr)
        {
            retro_ui_disabled_item_tooltip(help);
        }
    }
    retro_ui_row_end(&row);
}

// INTERNET: being reachable from outside this network through the beacon.
// Open at first (STATUS is worth seeing). The rows under the checkbox mean
// nothing while it's off: greyed then (Andrés, testing round 1).
static void draw_internet(const NetWindowState* state, const SettingsUiContext* rows)
{
    const Localization* strings = state->strings;
    if (!settings_ui_section("internet", piccolo_text(strings, Str::net_section_internet), nullptr, true))
    {
        return;
    }
    // The browser: the beacon is its only way (no UDP), the checkbox shows checked and greyed.
    retro_ui_begin_disabled(PICCOLO_BROWSER);
    settings_ui_row(rows, PICCOLO_NET_INTERNET, nullptr);
    retro_ui_end_disabled();
    retro_ui_begin_disabled(!state->internet);
    settings_ui_row(rows, PICCOLO_NET_BEACON, nullptr);
    draw_device_id(state, rows->label_width);
    draw_beacon_status(state, rows->label_width);
    retro_ui_end_disabled();
}

// The geometry DEVICES' two columns share, in window pixels: the
// backgrounds are drawn before the cards over them, so every card of a
// column is as tall.
struct DeviceColumns
{
    float top = 0.0f;       // the columns' top, down from the window's (area's) top
    float left = 0.0f;      // the first column's left edge
    float width[2] = {};    // each column's
    float gap = 0.0f;       // between the two columns
    float pad = 0.0f;       // inside a column and a card
    float header = 0.0f;    // the column's title line
    float card[2] = {};     // a card's height, per column
    float card_gap = 0.0f;  // between two cards
    float height = 0.0f;    // the columns' height (the taller one's)
};

static float column_x(const DeviceColumns* columns, uint32_t column)
{
    return column == 0 ? columns->left : columns->left + columns->width[0] + columns->gap;
}

// Where card `index` of `column` starts, down from the window's top.
static float card_y(const DeviceColumns* columns, uint32_t column, uint32_t index)
{
    return columns->top + columns->pad + columns->header +
           static_cast<float>(index) * (columns->card[column] + columns->card_gap);
}

// A column's background and its `count` cards' (one canvas each column, so
// the line holds them side by side).
static void draw_column_background(const DeviceColumns* columns, uint32_t column, uint32_t count)
{
    const RetroUiPalette* palette = retro_ui_palette();
    if (column > 0)
    {
        retro_ui_same_line_at(column_x(columns, column));
    }
    float width = columns->width[column];
    retro_ui_canvas(width, columns->height);
    retro_ui_canvas_rect(0.0f, 0.0f, width, columns->height, palette->field_face);
    for (uint32_t i = 0; i < count; i++)
    {
        retro_ui_canvas_rect(columns->pad, card_y(columns, column, i) - columns->top, width - 2.0f * columns->pad,
                             columns->card[column], palette->panel_face);
    }
}

// A column's title, or `empty` (dimmed, where the first card would be).
static void draw_column_title(const DeviceColumns* columns, uint32_t column, const char* title, uint32_t count,
                              const char* empty)
{
    float x = column_x(columns, column) + columns->pad;
    retro_ui_down_to(columns->top + columns->pad);
    retro_ui_set_cursor_x(x);
    retro_ui_push_section_text();
    retro_ui_text_fit(title, columns->width[column] - 2.0f * columns->pad);
    retro_ui_pop_section_text();
    if (count == 0)
    {
        retro_ui_down_to(card_y(columns, column, 0));
        retro_ui_set_cursor_x(x);
        retro_ui_dim_text_wrapped_at(empty, column_x(columns, column) + columns->width[column] - columns->pad);
    }
}

// Card `index` of a column: the cursor at its first line, inside its
// padding. Returns its inner width.
static float begin_card(const DeviceColumns* columns, uint32_t column, uint32_t index)
{
    float x = column_x(columns, column) + 2.0f * columns->pad;
    retro_ui_down_to(card_y(columns, column, index) + columns->pad);
    retro_ui_set_cursor_x(x);
    return columns->width[column] - 4.0f * columns->pad;
}

// A card's name line: a light (none: 0), then the name, `width` in all.
static void draw_card_name(uint32_t light, const char* name, float width)
{
    float x = retro_ui_cursor_x();
    if (light != 0)
    {
        draw_light(light, retro_ui_font_size());
        width -= retro_ui_cursor_x() - x;
    }
    retro_ui_text_fit(name[0] != '\0' ? name : "?", width);
    retro_ui_set_cursor_x(x);
}

// A section-colored line (the card's PERMISSIONS headings).
static void draw_card_heading(const char* text, float width)
{
    float x = retro_ui_cursor_x();
    retro_ui_push_section_text();
    retro_ui_text_fit(text, width);
    retro_ui_pop_section_text();
    retro_ui_set_cursor_x(x);
}

// The tooltip of a connection's detail: where it is, the link and the last
// speed test.
static void connected_help(const NetWindowState* state, const NetWindowPeer* entry, char* out, size_t size)
{
    const Localization* strings = state->strings;
    size_t used = 0;
    out[0] = '\0';
    if (entry->has_address)
    {
        used += static_cast<size_t>(std::snprintf(out + used, size - used, "%s", entry->address));
    }
    if (entry->has_stats && used < size)
    {
        const NetLinkStats* stats = &entry->stats;
        used += static_cast<size_t>(std::snprintf(out + used, size - used, "%s%u MS, %s %u MS, %s %.0f%%",
                                                  used > 0 ? "\n" : "", stats->round_trip_ms,
                                                  piccolo_text(strings, Str::net_jitter), stats->jitter_ms,
                                                  piccolo_text(strings, Str::net_loss),
                                                  static_cast<double>(stats->loss) * 100.0));
    }
    const NetSpeedSummary* speed = entry->speed;
    if (speed != nullptr && (speed->down_tested || speed->up_tested) && used < size)
    {
        char down[32];
        char up[32];
        speed_text(speed->down_tested, speed->down_kbps, down, sizeof(down));
        speed_text(speed->up_tested, speed->up_kbps, up, sizeof(up));
        std::snprintf(out + used, size - used, "%s%s: %s %s, %s %s", used > 0 ? "\n" : "",
                      piccolo_text(strings, Str::net_speed_test), piccolo_text(strings, Str::net_speed_down), down,
                      piccolo_text(strings, Str::net_speed_up), up);
    }
}

// One CONNECTED NOW card: name, what it uses (and the round trip), DISCONNECT.
static bool draw_connection_card(const DeviceColumns* columns, const NetWindowState* state, uint32_t index,
                                 const char* name, NetCaps caps, const NetWindowPeer* entry)
{
    const Localization* strings = state->strings;
    float width = begin_card(columns, 0, index);
    draw_card_name(LIGHT_GOOD, name, width);
    char list[200];
    net_caps_ui_list(strings, caps, list, sizeof(list));
    char detail[256] = {};
    char help[300] = {};
    if (entry != nullptr && entry->has_stats)
    {
        std::snprintf(detail, sizeof(detail), "%s · %u MS", list, entry->stats.round_trip_ms);
    }
    else
    {
        std::snprintf(detail, sizeof(detail), "%s", list);
    }
    if (entry != nullptr)
    {
        connected_help(state, entry, help, sizeof(help));
    }
    float x = retro_ui_cursor_x();
    retro_ui_dim_text(detail, help[0] != '\0' ? help : nullptr);
    retro_ui_set_cursor_x(x);
    RetroUiRowButton disconnect = {};
    disconnect.id = "disconnect";
    disconnect.label = piccolo_text(strings, Str::net_device_disconnect);
    disconnect.help = piccolo_text(strings, Str::net_device_disconnect_help);
    return retro_ui_equal_buttons(&disconnect, 1, width) == 0;
}

// CONNECTED NOW: our own connection (FILE TRANSFER's), then theirs to us.
// DISCONNECT closes it (net_window_apply checks it's still the same device).
static void draw_connected(const DeviceColumns* columns, const NetWindowState* state, uint32_t count,
                           NetWindowAction* action)
{
    const Localization* strings = state->strings;
    retro_ui_push_id("connected");
    draw_column_title(columns, 0, piccolo_text(strings, Str::net_section_connected), count,
                      piccolo_text(strings, Str::net_no_connected));
    uint32_t index = 0;
    if (state->link_connected)
    {
        retro_ui_push_id("link");
        if (draw_connection_card(columns, state, index, state->link_name.c_str(), state->link_caps, nullptr))
        {
            action->disconnect_link = true;
        }
        retro_ui_pop_id();
        index++;
    }
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        const NetWindowPeer* entry = &state->peers[peer];
        if (!entry->connected)
        {
            continue;
        }
        retro_ui_push_index(peer);
        if (draw_connection_card(columns, state, index, entry->name.c_str(), entry->caps, entry))
        {
            action->disconnect = true;
            action->disconnect_peer = peer;
            std::memcpy(action->disconnect_key, entry->key, NET_KEY_SIZE);
        }
        retro_ui_pop_id();
        index++;
    }
    retro_ui_pop_id();
}

// One PAIRED DEVICES card (the pairing v3 page):
//
//   KITCHEN PC  PICCOLO  UNSIGNED BUILD
//   PERMISSIONS GRANTED HERE
//   [x] FILE TRANSFER   [x] SETTINGS
//   PERMISSIONS GRANTED ON KITCHEN PC
//   FILE TRANSFER, SETTINGS (LAST KNOWN)
//   [ CONNECT ][ FORGET ]
static void draw_device_card(NetWindow* window, const DeviceColumns* columns, const NetWindowState* state,
                             uint32_t index, NetWindowAction* action)
{
    const Localization* strings = state->strings;
    const NetWindowDevice* device = &state->devices[index];
    const NetTrustedDevice* record = &device->record;
    float width = begin_card(columns, 1, index);
    float x = retro_ui_cursor_x();
    uint32_t light = device->connected ? LIGHT_GOOD : device->online == 1 ? LIGHT_WAIT : 0;
    // The name, then its chips: its kind, UNSIGNED BUILD.
    const char* kind = net_caps_ui_kind(strings, record->kind);
    const char* unsigned_chip = piccolo_text(strings, Str::unsigned_build);
    float chips = retro_ui_text_width(kind) + retro_ui_item_spacing();
    if (!record->signed_build)
    {
        chips += retro_ui_text_width(unsigned_chip) + retro_ui_item_spacing();
    }
    float name_width = width - chips > width * 0.4f ? width - chips : width * 0.4f;
    draw_card_name(light, record->name.c_str(), name_width);
    char fingerprint[32];
    net_key_fingerprint(record->key, fingerprint, sizeof(fingerprint));
    retro_ui_item_tooltip(fingerprint);
    retro_ui_same_line();
    retro_ui_dim_text(kind, nullptr);
    if (!record->signed_build)
    {
        retro_ui_same_line();
        retro_ui_text_colored(0xFFF0A8B4u, unsigned_chip);   // the red chip's text
    }
    retro_ui_set_cursor_x(x);
    // GRANTED HERE: only what the device uses (its kind tells) and we offer.
    draw_card_heading(piccolo_text(strings, Str::pair_granted_here), width);
    retro_ui_item_tooltip(piccolo_text(strings, Str::net_device_grants_help));
    NetCaps choices = net_caps_ui_kind_uses(record->kind) & state->offers;
    if (choices == 0)
    {
        retro_ui_text(piccolo_text(strings, Str::cap_none));
        retro_ui_set_cursor_x(x);
        retro_ui_space(1.0f, retro_ui_line_step());   // the second row's room
    }
    else
    {
        NetCaps value = record->grants & choices;
        NetCaps asked = 0;
        retro_ui_push_index(index);
        if (net_caps_ui_checkboxes("grants", strings, choices, record->signed_build, &value, &asked))
        {
            action->set_grants = true;
            action->grants = value | (record->grants & ~choices);
            std::memcpy(action->key, record->key, NET_KEY_SIZE);
        }
        retro_ui_pop_id();
        if (asked != 0)
        {
            window->ask.cap = asked;
            window->ask.name = record->name;
            std::memcpy(window->ask.key, record->key, NET_KEY_SIZE);
        }
    }
    retro_ui_down_to(card_y(columns, 1, index) + columns->pad + 2.0f * retro_ui_text_line_step() +
                     2.0f * retro_ui_line_step());
    retro_ui_set_cursor_x(x);
    // GRANTED ON <NAME>: read only, the other device decides.
    char heading[256];
    std::snprintf(heading, sizeof(heading), "%s %s", piccolo_text(strings, Str::pair_granted_on), record->name.c_str());
    draw_card_heading(heading, width);
    retro_ui_item_tooltip(piccolo_text(strings, Str::net_device_granted_help));
    char list[200];
    net_caps_ui_list(strings, record->granted, list, sizeof(list));
    char granted[256];
    if (device->connected || record->granted == 0)
    {
        std::snprintf(granted, sizeof(granted), "%s", list);
    }
    else
    {
        std::snprintf(granted, sizeof(granted), "%s %s", list, piccolo_text(strings, Str::net_last_known));
    }
    retro_ui_text_fit(granted, width);
    retro_ui_set_cursor_x(x);
    // CONNECT: FILE TRANSFER with it; greyed when we use nothing there.
    bool can_connect = state->link_known && (net_caps_ui_kind_offers(record->kind) & state->uses) != 0;
    RetroUiRowButton buttons[2] = {};
    buttons[0].id = "connect";
    buttons[0].label = piccolo_text(strings, Str::net_host_connect);
    buttons[0].help = piccolo_text(strings, Str::net_host_connect_help);
    buttons[0].enabled = can_connect;
    buttons[1].id = "forget";
    buttons[1].label = piccolo_text(strings, Str::net_device_forget);
    buttons[1].help = piccolo_text(strings, Str::net_device_forget_help);
    retro_ui_push_index(index);
    int32_t pressed = retro_ui_equal_buttons(buttons, 2, width);
    retro_ui_pop_id();
    if (pressed == 0)
    {
        action->connect = true;
        std::memcpy(action->key, record->key, NET_KEY_SIZE);
    }
    else if (pressed == 1)
    {
        action->forget = true;
        std::memcpy(action->key, record->key, NET_KEY_SIZE);
    }
}

// PAIRED DEVICES, then the red question a card's checkbox asked, if any:
// the bit is granted on GRANT ANYWAY, to the device asked about.
static void draw_paired(NetWindow* window, const DeviceColumns* columns, const NetWindowState* state,
                        NetWindowAction* action)
{
    const Localization* strings = state->strings;
    retro_ui_push_id("paired");
    draw_column_title(columns, 1, piccolo_text(strings, Str::net_section_devices),
                      static_cast<uint32_t>(state->devices.size()), piccolo_text(strings, Str::net_no_devices));
    for (size_t i = 0; i < state->devices.size(); i++)
    {
        draw_device_card(window, columns, state, static_cast<uint32_t>(i), action);
    }
    retro_ui_pop_id();
    NetCaps granted = net_caps_ui_unsigned(&window->ask, strings);
    if (granted == 0)
    {
        return;
    }
    for (size_t i = 0; i < state->devices.size(); i++)
    {
        const NetTrustedDevice* record = &state->devices[i].record;
        if (std::memcmp(record->key, window->ask.key, NET_KEY_SIZE) == 0)
        {
            action->set_grants = true;
            action->grants = record->grants | granted;
            std::memcpy(action->key, record->key, NET_KEY_SIZE);
        }
    }
}

// DEVICES: two columns (Andrés, 2026-11-25: one record per device):
//
//   CONNECTED NOW        PAIRED DEVICES
//   +---------------+    +--------------------------------+
//   | * KITCHEN PC  |    | KITCHEN PC  PICCOLO            |
//   | FILE TRANSFER |    | PERMISSIONS GRANTED HERE       |
//   | · 12 MS       |    | [x] FILE TRANSFER [x] SETTINGS |
//   | [DISCONNECT ] |    | PERMISSIONS GRANTED ON ...     |
//   +---------------+    | [ CONNECT ][ FORGET ]          |
//                        +--------------------------------+
//
// A device both connected and paired shows in both. Open at first.
static void draw_devices(NetWindow* window, const NetWindowState* state, NetWindowAction* action)
{
    const Localization* strings = state->strings;
    if (!settings_ui_section("devices", piccolo_text(strings, Str::net_section_network_devices), nullptr, true))
    {
        return;
    }
    if (!state->devices_known)
    {
        retro_ui_dim_text(piccolo_text(strings, state->listen_failed ? Str::net_listen_failed : Str::net_off), nullptr);
        return;
    }
    uint32_t connected = state->link_connected ? 1u : 0u;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        connected += state->peers[peer].connected ? 1u : 0u;
    }
    uint32_t paired = static_cast<uint32_t>(state->devices.size());

    DeviceColumns columns = {};
    columns.top = retro_ui_cursor_y();
    columns.left = retro_ui_cursor_x();
    columns.gap = retro_ui_item_spacing();
    float room = retro_ui_width_left() - columns.gap;
    columns.width[0] = std::floor(room / 3.0f);
    columns.width[1] = room - columns.width[0];
    columns.pad = std::floor(columns.gap * 0.5f) > 1.0f ? std::floor(columns.gap * 0.5f) : 1.0f;
    columns.header = retro_ui_text_line_step();
    float text = retro_ui_text_line_step();
    // A connection: name, detail, the button. A paired device: name,
    // heading, two rows of checkboxes, heading, list, the buttons.
    columns.card[0] = 2.0f * text + retro_ui_line_height() + 2.0f * columns.pad;
    columns.card[1] = 4.0f * text + 2.0f * retro_ui_line_step() + retro_ui_line_height() + 2.0f * columns.pad;
    columns.card_gap = columns.pad;
    float tall[2] = {};
    uint32_t counts[2] = {connected > 0 ? connected : 1u, paired > 0 ? paired : 1u};
    for (uint32_t column = 0; column < 2; column++)
    {
        tall[column] = columns.pad + columns.header +
                       static_cast<float>(counts[column]) * (columns.card[column] + columns.card_gap) + columns.pad;
    }
    columns.height = tall[0] > tall[1] ? tall[0] : tall[1];

    // The backgrounds first, side by side on one line; then each column's
    // contents over its own, back at the line's top.
    draw_column_background(&columns, 0, connected);
    draw_column_background(&columns, 1, paired);
    retro_ui_same_line_at(column_x(&columns, 0));
    retro_ui_begin_group();
    draw_connected(&columns, state, connected, action);
    retro_ui_end_group();
    retro_ui_same_line_at(column_x(&columns, 1));
    retro_ui_begin_group();
    draw_paired(window, &columns, state, action);
    retro_ui_end_group();
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
    // FILE TRANSFER's (TOOLS), here with the network settings; it works
    // with NETWORK off too (copying to another piccolo needs no listening).
    settings_ui_row(&rows, PICCOLO_SHARE_MIRROR_FOLDERS, nullptr);
    draw_internet(state, &rows);
    draw_devices(window, state, action);
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

// ---------------------------------------------------------------------------
// Apply

// The device with `key` in FILE TRANSFER's list, or -1.
static int32_t link_device(const NetShareLinkStatus* status, const uint8_t* key)
{
    for (size_t i = 0; status != nullptr && i < status->paired_devices.size(); i++)
    {
        if (std::memcmp(status->paired_devices[i].key, key, NET_KEY_SIZE) == 0)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

bool net_window_apply(const NetWindowAction* action, Settings* settings, NetAccess* access, NetShareLink* link,
                      const NetShareLinkStatus* link_status)
{
    SettingsUiEdit edit = action->setting;
    bool changed = settings_ui_apply_edit(settings, &edit);
    // GRANTED HERE and FORGET: by key, on whichever side runs (both keep the
    // same file: the other reloads it).
    if (action->set_grants || action->forget)
    {
        int32_t device = access != nullptr ? net_trust_find(&access->trust, action->key) : -1;
        int32_t linked = link != nullptr ? link_device(link_status, action->key) : -1;
        if (device >= 0 && action->set_grants)
        {
            net_access_set_grants(access, static_cast<uint32_t>(device), action->grants);
        }
        else if (linked >= 0 && action->set_grants)
        {
            net_share_link_set_grants(link, static_cast<uint32_t>(linked), action->grants);
        }
        if (device >= 0 && action->forget)
        {
            net_access_remove_device(access, static_cast<uint32_t>(device));   // tells it, if connected
        }
        else if (linked >= 0 && action->forget)
        {
            net_share_link_forget_host(link, static_cast<uint32_t>(linked));
        }
    }
    // DISCONNECT names a connection as drawn: closed only if it's still that
    // device's (the network thread may have given the slot to another since).
    if (access != nullptr && action->disconnect && action->disconnect_peer < NET_MAX_PEERS)
    {
        const NetAccessPeer* peer = &access->peers[action->disconnect_peer];
        if (peer->state == NetAccessPeerState::authorized &&
            std::memcmp(peer->key, action->disconnect_key, NET_KEY_SIZE) == 0)
        {
            net_access_close(access, action->disconnect_peer);
        }
    }
    if (link != nullptr && action->disconnect_link)
    {
        net_share_link_disconnect(link);
    }
    return changed;
}
