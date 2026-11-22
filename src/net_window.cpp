#include "net_window.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "port_indicators.h"

#include "piccolo_settings.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "settings_ui.h"

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
    char title[128] = {};
    std::snprintf(title, sizeof(title), "%s###owner_confirm", piccolo_text(strings, Str::net_owner_confirm_title));
    if (state->confirm_owner && !ImGui::IsPopupOpen(title))
    {
        ImGui::OpenPopup(title);
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(static_cast<int>(viewport->Size.x * 0.4f)), 0.0f),
                             ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove))
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(piccolo_text(strings, Str::net_owner_confirm_text));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        bool close = ImGui::IsKeyPressed(ImGuiKey_Escape);   // (B on a gamepad sends Escape while a popup is up)
        if (retro_ui_button(piccolo_text(strings, Str::net_owner_confirm_yes)))
        {
            NetAccess* access = context->access;
            for (size_t i = 0; i < access->trust.devices.size(); i++)
            {
                if (std::memcmp(access->trust.devices[i].key, state->owner_key, NET_KEY_SIZE) == 0)
                {
                    net_access_set_level(access, static_cast<uint32_t>(i), state->confirm_level);
                }
            }
            close = true;
        }
        ImGui::SameLine();
        if (retro_ui_button(piccolo_text(strings, Str::net_owner_cancel)))
        {
            close = true;
        }
        if (close)
        {
            state->confirm_owner = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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
        ImGui::PushID(static_cast<int>(peer));
        if (any)
        {
            ImGui::Spacing();
        }
        any = true;
        ImGui::TextUnformatted(device->name.empty() ? "?" : device->name.c_str());
        NetAddress address;
        if (net_access_peer_address(access, peer, &address))
        {
            char text[64];
            net_address_to_text(address, text, sizeof(text));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", text);
        }
        NetLinkStats stats;
        if (net_access_peer_stats(access, peer, &stats))
        {
            ImGui::Text("%u MS  %s %u MS  %s %.0f%%", stats.round_trip_ms, piccolo_text(strings, Str::net_jitter),
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
            ImGui::Text("%s: %s %s, %s %s", piccolo_text(strings, Str::net_speed_test),
                        piccolo_text(strings, Str::net_speed_down), down, piccolo_text(strings, Str::net_speed_up), up);
            retro_ui_item_tooltip(piccolo_text(strings, Str::net_speed_test_help));
        }
        ImGui::PopID();
    }
    if (!any)
    {
        ImGui::TextDisabled("%s", piccolo_text(strings, Str::net_no_connected));
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
        ImGui::TextDisabled("%s", piccolo_text(strings, Str::net_no_devices));
        return;
    }
    // FORGET changes the list: act after drawing it.
    int32_t forget = -1;
    for (size_t i = 0; i < access->trust.devices.size(); i++)
    {
        const NetTrustedDevice* device = &access->trust.devices[i];
        ImGui::PushID(static_cast<int>(i));
        if (i > 0)
        {
            ImGui::Spacing();
        }
        char fingerprint[32];
        net_key_fingerprint(device->key, fingerprint, sizeof(fingerprint));
        ImGui::TextUnformatted(device->name.empty() ? "?" : device->name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", fingerprint);
        if (net_access_device_connected(access, static_cast<uint32_t>(i)))
        {
            ImGui::SameLine();
            retro_ui_push_section_text();
            ImGui::TextUnformatted(piccolo_text(strings, Str::net_device_connected));
            retro_ui_pop_section_text();
            NetLinkStats stats;
            if (device_ping(access, static_cast<uint32_t>(i), &stats))
            {
                // The round trip in milliseconds, as ENet measures it.
                ImGui::SameLine();
                ImGui::TextDisabled("%u MS", stats.round_trip_ms);
                retro_ui_item_tooltip(piccolo_text(strings, Str::net_device_ping_help));
            }
        }
        draw_access(context, static_cast<uint32_t>(i));
        ImGui::SameLine();
        if (retro_ui_button(piccolo_text(strings, Str::net_device_forget)))
        {
            forget = static_cast<int32_t>(i);
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::net_device_forget_help));
        ImGui::PopID();
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
    ImGui::Dummy(ImGui::CalcTextSize("C"));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    float start = ImGui::GetCursorPosX();
    ImGui::TextUnformatted(piccolo_text(strings, Str::net_beacon_status));
    retro_ui_item_tooltip(piccolo_text(strings, Str::net_beacon_status_help));
    ImGui::SameLine(start + label_width);
    if (good)
    {
        ImGui::TextUnformatted(piccolo_text(strings, text));
    }
    else
    {
        ImGui::TextDisabled("%s", piccolo_text(strings, text));
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
    settings_ui_row(&rows, PICCOLO_NET_INTERNET, nullptr);
    settings_ui_row(&rows, PICCOLO_NET_BEACON, nullptr);

    // As a row: the marker column, then the label column.
    ImGui::Dummy(ImGui::CalcTextSize("C"));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    float start = ImGui::GetCursorPosX();
    ImGui::TextUnformatted(piccolo_text(strings, Str::net_this_device));
    retro_ui_item_tooltip(piccolo_text(strings, Str::net_this_device_help));
    ImGui::SameLine(start + label_width);
    ImGui::TextUnformatted(context->this_device[0] != '\0' ? context->this_device : "-");

    if (context->access != nullptr && context->internet)
    {
        draw_beacon_status(context, label_width);
    }

    if (context->access == nullptr)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", piccolo_text(strings, context->listen_failed ? Str::net_listen_failed : Str::net_off));
        return;
    }
    retro_ui_separator_text(piccolo_text(strings, Str::net_section_connected));
    draw_connected(context);
    retro_ui_separator_text(piccolo_text(strings, Str::net_section_devices));
    draw_devices(context);
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
    int32_t lit = CYCLE[static_cast<uint32_t>(ImGui::GetTime() * 6.0) % 6];
    float block = std::floor(ImGui::GetTextLineHeight() / 7.0f);
    block = block < 1.0f ? 1.0f : block;
    ImVec2 at = ImGui::GetCursorScreenPos();
    float top = at.y + std::floor((ImGui::GetTextLineHeight() - PORT_LED_SIZE * block) * 0.5f);
    float width = port_indicators_draw_leds(at.x, top, block, 4, lit, retro_ui_palette()->panel_dark);
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
}

//   +- PAIRING REQUEST -------------------------------+
//   |  BEDROOM PC (PICCOLO)                           |
//   |  WANTS TO PAIR. TYPE THE CODE ON ITS SCREEN.    |
//   |  [482193706]                                    |
//   |  ACCESS [GAMEPAD][STREAM][SHARE][FULL]          |
//   |                          [ALLOW] [REFUSE]       |
//   +-------------------------------------------------+
// After ALLOW: the same box, waiting (LEDs), until paired / gone.
void net_pairing_request_draw(NetPairingRequest* request, NetAccess* access, const Localization* strings)
{
    if (!request->active || access == nullptr)
    {
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    char title[96];
    std::snprintf(title, sizeof(title), "%s###pairing_request", piccolo_text(strings, Str::net_request_title));
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowFocus();
    if (!ImGui::Begin(title, nullptr, flags))
    {
        ImGui::End();
        return;
    }
    retro_ui_push_section_text();
    ImGui::Text("%s (%s)", request->name,
                piccolo_text(strings, request->kind == NetPairKind::piccoletto ? Str::net_kind_piccoletto
                                                                              : Str::net_kind_piccolo));
    retro_ui_pop_section_text();
    ImGui::TextDisabled("%s", piccolo_text(strings, request->retry ? Str::net_request_wrong : Str::net_request_text));
    ImGui::BeginDisabled(request->waiting);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000000000000").x);
    if (request->focus)
    {
        ImGui::SetKeyboardFocusHere();
        request->focus = false;
    }
    retro_ui_push_field_text();
    bool enter = ImGui::InputText("##code", request->code, sizeof(request->code),
                                  ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    retro_ui_pop_field_text();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(piccolo_text(strings, Str::net_request_access));
    ImGui::SameLine();
    const char* labels[NET_ACCESS_LEVEL_COUNT];
    for (uint32_t level = 0; level < NET_ACCESS_LEVEL_COUNT; level++)
    {
        labels[level] = piccolo_text(strings, ACCESS_LABELS[level]);
    }
    retro_ui_choice_buttons("##request_access", labels, NET_ACCESS_LEVEL_COUNT, &request->level);
    ImGui::EndDisabled();
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
        ImGui::BeginDisabled(!complete);
        if (retro_ui_button(piccolo_text(strings, Str::net_request_allow)) || (enter && complete))
        {
            if (net_access_answer_pairing(access, request->peer, request->code,
                                          static_cast<NetAccessLevel>(request->level)))
            {
                request->waiting = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (retro_ui_button(piccolo_text(strings, Str::net_request_refuse)))
        {
            net_access_refuse_pairing(access, request->peer);
            net_pairing_request_close(request);
        }
    }
    ImGui::End();
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
void net_invite_draw(NetInvite* invite, NetAccess* access, const Localization* strings)
{
    if (!invite->open)
    {
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    char title[96];
    std::snprintf(title, sizeof(title), "%s###invite", piccolo_text(strings, Str::net_invite_title));
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings;
    bool keep = true;
    if (!ImGui::Begin(title, &keep, flags))
    {
        ImGui::End();
        return;
    }
    if (access == nullptr)
    {
        ImGui::TextDisabled("%s", piccolo_text(strings, Str::net_off));
    }
    else
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(piccolo_text(strings, Str::net_invite_text));
        ImGui::PopTextWrapPos();
        ImGui::BeginDisabled(invite->waiting);
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000000000000").x);
        if (invite->focus)
        {
            ImGui::SetKeyboardFocusHere();
            invite->focus = false;
        }
        retro_ui_push_field_text();
        bool enter = ImGui::InputText("##invite_code", invite->code, sizeof(invite->code),
                                      ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
        retro_ui_pop_field_text();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(piccolo_text(strings, Str::net_request_access));
        ImGui::SameLine();
        const char* labels[NET_ACCESS_LEVEL_COUNT];
        for (uint32_t level = 0; level < NET_ACCESS_LEVEL_COUNT; level++)
        {
            labels[level] = piccolo_text(strings, ACCESS_LABELS[level]);
        }
        retro_ui_choice_buttons("##invite_access", labels, NET_ACCESS_LEVEL_COUNT, &invite->level);
        ImGui::EndDisabled();
        if (invite->waiting)
        {
            net_window_waiting_leds(piccolo_text(strings, invite->answered ? Str::net_request_waiting
                                                                           : Str::net_invite_waiting));
        }
        else
        {
            bool complete = net_pair_code_valid(invite->code);
            ImGui::BeginDisabled(!complete);
            if ((retro_ui_button(piccolo_text(strings, Str::net_request_allow)) || (enter && complete)) &&
                net_access_invite(access, invite->code))
            {
                invite->waiting = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (retro_ui_button(piccolo_text(strings, Str::net_owner_cancel)))
        {
            keep = false;
        }
    }
    ImGui::End();
    if (!keep)
    {
        net_invite_close(invite, access);
    }
}
