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

// ---------------------------------------------------------------------------
// Drawing

// The role a paired client has here (its access level: GAMEPAD / STREAM /
// SHARE / FULL), as ROLE's list on its card, `width` wide. Applied and saved
// at once; SHARE and FULL, which write files here, only after their
// confirmation.
static void draw_role(NetWindow* window, const NetWindowState* state, uint32_t device, float width,
                      NetWindowAction* action)
{
    const Localization* strings = state->strings;
    const NetTrustedDevice* entry = &(*state->devices)[device];
    uint32_t current = static_cast<uint32_t>(entry->access);
    int32_t picked = -1;
    retro_ui_next_width(width);
    if (retro_ui_begin_choice("role", piccolo_text(strings, Str::net_device_role)))
    {
        for (uint32_t level = 0; level < NET_ACCESS_LEVEL_COUNT; level++)
        {
            if (retro_ui_choice_item(level, piccolo_text(strings, ACCESS_LABELS[level]), level == current))
            {
                picked = static_cast<int32_t>(level);
            }
        }
        retro_ui_end_choice();
    }
    retro_ui_item_tooltip(piccolo_text(strings, Str::net_device_access_help));
    if (picked < 0 || static_cast<uint32_t>(picked) == current)
    {
        return;
    }
    NetAccessLevel chosen = static_cast<NetAccessLevel>(picked);
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

// One device's card in a column of DEVICES: a light (none: 0), the name, a
// dimmed detail (`detail_help` its tooltip) and the card's buttons, which
// the caller draws at the cursor `buttons_width` wide.
struct DeviceCard
{
    uint32_t    light = 0;
    const char* name = "";
    const char* detail = "";
    const char* detail_help = nullptr;
};

// The geometry DEVICES' three columns share, in window pixels: every card is
// as tall, so the backgrounds are drawn before the cards over them.
struct DeviceColumns
{
    float top = 0.0f;       // the columns' top, down from the window's (area's) top
    float left = 0.0f;      // the first column's left edge
    float width = 0.0f;     // one column
    float gap = 0.0f;       // between two columns
    float pad = 0.0f;       // inside a column and a card
    float header = 0.0f;    // the column's title line
    float card = 0.0f;      // a card's height
    float card_gap = 0.0f;  // between two cards
    float height = 0.0f;    // the columns' height (the tallest one's)
};

static float column_x(const DeviceColumns* columns, uint32_t column)
{
    return columns->left + static_cast<float>(column) * (columns->width + columns->gap);
}

// Where card `index` starts, down from the window's top.
static float card_y(const DeviceColumns* columns, uint32_t index)
{
    return columns->top + columns->pad + columns->header + static_cast<float>(index) * (columns->card + columns->card_gap);
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
    retro_ui_canvas(columns->width, columns->height);
    retro_ui_canvas_rect(0.0f, 0.0f, columns->width, columns->height, palette->field_face);
    for (uint32_t i = 0; i < count; i++)
    {
        retro_ui_canvas_rect(columns->pad, card_y(columns, i) - columns->top, columns->width - 2.0f * columns->pad,
                             columns->card, palette->panel_face);
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
    retro_ui_text_fit(title, columns->width - 2.0f * columns->pad);
    retro_ui_pop_section_text();
    if (count == 0)
    {
        retro_ui_down_to(card_y(columns, 0));
        retro_ui_set_cursor_x(x);
        retro_ui_dim_text_wrapped_at(empty, column_x(columns, column) + columns->width - columns->pad);
    }
}

// Card `index` of a column: light, name, detail; the cursor is left where
// its buttons go. Returns their width (the card's, inside its padding).
static float draw_card(const DeviceColumns* columns, uint32_t column, uint32_t index, const DeviceCard* card)
{
    float x = column_x(columns, column) + 2.0f * columns->pad;
    float inner = columns->width - 4.0f * columns->pad;
    retro_ui_down_to(card_y(columns, index) + columns->pad);
    retro_ui_set_cursor_x(x);
    float name_width = inner;
    if (card->light != 0)
    {
        float before = retro_ui_cursor_x();
        draw_light(card->light, retro_ui_font_size());
        name_width -= retro_ui_cursor_x() - before;
    }
    retro_ui_text_fit(card->name[0] != '\0' ? card->name : "?", name_width);
    retro_ui_set_cursor_x(x);
    retro_ui_dim_text(card->detail, card->detail_help);
    retro_ui_set_cursor_x(x);
    return inner;
}

// The tooltip of a connected client's detail: where it is, the link and the
// last speed test (what CONNECTED DEVICES showed in full before).
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

// CONNECTED CLIENTS: the devices connected now. DISCONNECT closes the
// connection (net_window_apply checks it's still the same device).
static void draw_connected(const DeviceColumns* columns, const NetWindowState* state, uint32_t count,
                           NetWindowAction* action)
{
    const Localization* strings = state->strings;
    retro_ui_push_id("connected");
    draw_column_title(columns, 0, piccolo_text(strings, Str::net_section_connected), count,
                      piccolo_text(strings, Str::net_no_connected));
    uint32_t index = 0;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        const NetWindowPeer* entry = &state->peers[peer];
        if (!entry->connected)
        {
            continue;
        }
        const NetTrustedDevice* device = &(*state->devices)[entry->device];
        retro_ui_push_index(peer);
        char detail[96] = {};
        const char* role = piccolo_text(strings, ACCESS_LABELS[static_cast<uint32_t>(device->access)]);
        if (entry->has_stats)
        {
            std::snprintf(detail, sizeof(detail), "%s - %u MS", role, entry->stats.round_trip_ms);
        }
        else
        {
            std::snprintf(detail, sizeof(detail), "%s", role);
        }
        char help[300] = {};
        connected_help(state, entry, help, sizeof(help));
        DeviceCard card = {};
        card.light = LIGHT_GOOD;
        card.name = device->name.c_str();
        card.detail = detail;
        card.detail_help = help[0] != '\0' ? help : nullptr;
        float width = draw_card(columns, 0, index, &card);
        RetroUiRowButton disconnect = {};
        disconnect.id = "disconnect";
        disconnect.label = piccolo_text(strings, Str::net_device_disconnect);
        disconnect.help = piccolo_text(strings, Str::net_device_disconnect_help);
        if (retro_ui_equal_buttons(&disconnect, 1, width) == 0)
        {
            action->disconnect = true;
            action->disconnect_peer = peer;
            std::memcpy(action->disconnect_key, device->key, NET_KEY_SIZE);
        }
        retro_ui_pop_id();
        index++;
    }
    retro_ui_pop_id();
}

// PAIRED CLIENTS: the devices paired with this one, with their role here.
// ROLE and FORGET change the list: applied after drawing it (net_window_apply,
// main.cpp checks it's still the list drawn).
static void draw_clients(NetWindow* window, const DeviceColumns* columns, const NetWindowState* state,
                         NetWindowAction* action)
{
    const Localization* strings = state->strings;
    const std::vector<NetTrustedDevice>* devices = state->devices;
    retro_ui_push_id("clients");
    draw_column_title(columns, 1, piccolo_text(strings, Str::net_section_devices),
                      static_cast<uint32_t>(devices->size()), piccolo_text(strings, Str::net_no_devices));
    for (size_t i = 0; i < devices->size(); i++)
    {
        const NetTrustedDevice* device = &(*devices)[i];
        retro_ui_push_index(static_cast<uint32_t>(i));
        char detail[96] = {};
        std::snprintf(detail, sizeof(detail), "%s: %s", piccolo_text(strings, Str::net_device_role),
                      piccolo_text(strings, ACCESS_LABELS[static_cast<uint32_t>(device->access)]));
        char fingerprint[32];
        net_key_fingerprint(device->key, fingerprint, sizeof(fingerprint));
        DeviceCard card = {};
        card.name = device->name.c_str();
        card.detail = detail;
        card.detail_help = fingerprint;
        float width = draw_card(columns, 1, static_cast<uint32_t>(i), &card);
        // ROLE's list in the first half, FORGET in the second (one grid).
        float half = std::floor((width - retro_ui_item_spacing()) * 0.5f);
        draw_role(window, state, static_cast<uint32_t>(i), half, action);
        retro_ui_same_line();
        RetroUiRowButton forget = {};
        forget.id = "forget";
        forget.label = piccolo_text(strings, Str::net_device_forget);
        forget.help = piccolo_text(strings, Str::net_device_forget_help);
        if (retro_ui_equal_buttons(&forget, 1, width - half - retro_ui_item_spacing()) == 0)
        {
            action->forget = true;
            action->forget_device = static_cast<uint32_t>(i);
        }
        retro_ui_pop_id();
    }
    retro_ui_pop_id();
}

// PAIRED HOSTS: the piccolos this one connects to (paired with TOOLS > PAIR /
// FILE TRANSFER). CONNECT opens FILE TRANSFER connected to it; FORGET forgets
// it. Their access is the other side's to give (Andrés, 2026-11-22).
static void draw_hosts(const DeviceColumns* columns, const NetWindowState* state, uint32_t count,
                       NetWindowAction* action)
{
    const Localization* strings = state->strings;
    retro_ui_push_id("hosts");
    draw_column_title(columns, 2, piccolo_text(strings, Str::net_section_hosts), count,
                      piccolo_text(strings, Str::net_no_hosts));
    for (uint32_t i = 0; i < count; i++)
    {
        retro_ui_push_index(i);
        // TODO(round1): the mockup's "GAVE YOU: STREAM" (what the host lets
        // us do) isn't kept per host, only while connected (NetShareLinkStatus
        // access): ONLINE / OFFLINE instead, as before.
        DeviceCard card = {};
        card.name = (*state->hosts)[i].c_str();
        if (state->hosts_online != nullptr && i < state->hosts_online->size())
        {
            bool online = (*state->hosts_online)[i] != 0;
            card.light = online ? LIGHT_GOOD : LIGHT_OFF;
            card.detail = piccolo_text(strings, online ? Str::net_online : Str::net_offline);
        }
        float width = draw_card(columns, 2, i, &card);
        RetroUiRowButton buttons[2] = {};
        buttons[0].id = "connect";
        buttons[0].label = piccolo_text(strings, Str::net_host_connect);
        buttons[0].help = piccolo_text(strings, Str::net_host_connect_help);
        buttons[1].id = "forget";
        buttons[1].label = piccolo_text(strings, Str::net_device_forget);
        buttons[1].help = piccolo_text(strings, Str::net_host_forget_help);
        int32_t pressed = retro_ui_equal_buttons(buttons, 2, width);
        if (pressed == 0)
        {
            action->connect_host = true;
            action->connect_host_index = i;
        }
        else if (pressed == 1)
        {
            action->forget_host = true;
            action->forget_host_index = i;
        }
        if (pressed >= 0)
        {
            std::snprintf(action->host_name, sizeof(action->host_name), "%s", card.name);
        }
        retro_ui_pop_id();
    }
    retro_ui_pop_id();
}

// DEVICES: three columns, one per relationship (Andrés, testing round 1):
//
//   CONNECTED CLIENTS    PAIRED CLIENTS        PAIRED HOSTS
//   +---------------+    +---------------+     +---------------+
//   | * KITCHEN PC  |    | KITCHEN PC    |     | * DEN PC      |
//   | FULL - 12 MS  |    | ROLE: GAMEPAD |     | ONLINE        |
//   | [DISCONNECT ] |    | [ROLE][FORGET]|     |[CONNECT][FORG]|
//   +---------------+    +---------------+     +---------------+
//
// A device both connected and paired shows in both. The actions sit on the
// device's card, splitting its width. Open at first.
static void draw_devices(NetWindow* window, const NetWindowState* state, NetWindowAction* action)
{
    const Localization* strings = state->strings;
    if (!settings_ui_section("devices", piccolo_text(strings, Str::net_section_network_devices), nullptr, true))
    {
        return;
    }
    if (!state->network)
    {
        retro_ui_dim_text(piccolo_text(strings, state->listen_failed ? Str::net_listen_failed : Str::net_off), nullptr);
        return;
    }
    uint32_t connected = 0;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        connected += state->peers[peer].connected ? 1u : 0u;
    }
    uint32_t clients = static_cast<uint32_t>(state->devices->size());
    uint32_t hosts = state->hosts != nullptr ? static_cast<uint32_t>(state->hosts->size()) : 0;
    uint32_t most = connected > clients ? connected : clients;
    most = hosts > most ? hosts : most;
    most = most > 0 ? most : 1;   // room for the empty columns' note

    DeviceColumns columns = {};
    columns.top = retro_ui_cursor_y();
    columns.left = retro_ui_cursor_x();
    columns.gap = retro_ui_item_spacing();
    columns.width = std::floor((retro_ui_width_left() - 2.0f * columns.gap) / 3.0f);
    columns.pad = std::floor(columns.gap * 0.5f) > 1.0f ? std::floor(columns.gap * 0.5f) : 1.0f;
    columns.header = retro_ui_text_line_step();
    // Name and detail (text lines), the buttons (a control), padding round them.
    columns.card = 2.0f * retro_ui_text_line_step() + retro_ui_line_height() + 2.0f * columns.pad;
    columns.card_gap = columns.pad;
    columns.height = columns.pad + columns.header + static_cast<float>(most) * (columns.card + columns.card_gap) +
                     columns.pad;

    // The backgrounds first, side by side on one line; then each column's
    // contents over its own, back at the line's top.
    draw_column_background(&columns, 0, connected);
    draw_column_background(&columns, 1, clients);
    draw_column_background(&columns, 2, hosts);
    retro_ui_same_line_at(column_x(&columns, 0));
    retro_ui_begin_group();
    draw_connected(&columns, state, connected, action);
    retro_ui_end_group();
    retro_ui_same_line_at(column_x(&columns, 1));
    retro_ui_begin_group();
    draw_clients(window, &columns, state, action);
    retro_ui_end_group();
    retro_ui_same_line_at(column_x(&columns, 2));
    retro_ui_begin_group();
    draw_hosts(&columns, state, hosts, action);
    retro_ui_end_group();
    draw_owner_confirm(window, state, action);
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
    // DISCONNECT names a connection as drawn: closed only if it's still that
    // device's (the network thread may have given the slot to another since).
    if (action->disconnect && action->disconnect_peer < NET_MAX_PEERS)
    {
        const NetAccessPeer* peer = &access->peers[action->disconnect_peer];
        if (peer->state == NetAccessPeerState::authorized &&
            std::memcmp(peer->key, action->disconnect_key, NET_KEY_SIZE) == 0)
        {
            net_access_close(access, action->disconnect_peer);
        }
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
