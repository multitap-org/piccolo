#include "guest_window.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "port_indicators.h"

#include "app_shell.h"
#include "clipboard.h"
#include "piccolo_text.h"
#include "retro_ui.h"

void guest_window_open(GuestWindow* window)
{
    window->open = true;
    window->action = GuestWindowAction::none;
}

void guest_window_end(GuestWindow* window, NetAccess* access)
{
    if (access != nullptr && window->live)
    {
        net_access_guest_close(access);
    }
    window->live = false;
    window->open = false;
    window->link_text[0] = '\0';
    net_guest_link_wipe(&window->link);
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        window->told[i] = false;
    }
}

void guest_window_tell_ports(GuestWindow* window, NetAccess* access, const InputNet* input_net,
                             const InputSystem* input)
{
    if (access == nullptr || !window->live)
    {
        return;
    }
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        if (net_access_guest(access, peer) == nullptr)
        {
            window->told[peer] = false;
            continue;
        }
        int32_t port = input_net_peer_port(input_net, input, peer);
        uint8_t value = port == INPUT_NO_PORT ? NET_GUEST_NO_PORT : static_cast<uint8_t>(port);
        if (window->told[peer] && window->told_port[peer] == value)
        {
            continue;
        }
        uint8_t message[2];
        size_t size = net_guest_write_port(message, sizeof(message), value);
        if (net_access_send(access, peer, NetChannel::control, message, size))
        {
            window->told[peer] = true;
            window->told_port[peer] = value;
        }
    }
}

void guest_window_upload(GuestWindow* window, AppShell* shell)
{
    if (!window->preview_pending)
    {
        return;
    }
    if (!video_texture_created(&window->preview) &&
        !video_texture_create(&window->preview, shell, NET_GUEST_CARD_MAX_WIDTH, NET_GUEST_CARD_MAX_HEIGHT,
                              FramePixelFormat::xrgb8888, true))
    {
        window->preview_pending = false;
        return;
    }
    if (!app_shell_acquire(shell))
    {
        return;   // the next frame
    }
    window->has_preview = video_texture_upload(&window->preview, shell, window->preview_pixels.data(),
                                               window->preview_width, window->preview_height);
    window->preview_pending = false;
}

void guest_window_destroy(GuestWindow* window, AppShell* shell)
{
    video_texture_destroy(&window->preview, shell);   // waits for the GPU
}

// The card's picture, as wide as `width` (its own shape), sharp; NO
// SCREENSHOT AVAILABLE when there's none (the card shows white noise then).
static void draw_preview(GuestWindow* window, float width, const Localization* strings)
{
    float aspect = window->preview_height > 0
                       ? static_cast<float>(window->preview_width) / static_cast<float>(window->preview_height)
                       : 4.0f / 3.0f;
    ImVec2 box = ImVec2(std::floor(width), std::floor(width / aspect));
    ImVec2 corner = ImGui::GetCursorScreenPos();
    corner = ImVec2(std::floor(corner.x), std::floor(corner.y));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(corner, ImVec2(corner.x + box.x, corner.y + box.y), IM_COL32(0, 0, 0, 255));
    if (window->has_preview)
    {
        float u = 0.0f;
        float v = 0.0f;
        video_texture_uv_max(&window->preview, &u, &v);
        const ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        draw_list->AddCallback(platform_io.DrawCallback_SetSamplerNearest, nullptr);
        draw_list->AddImage(ImTextureRef(static_cast<ImTextureID>(window->preview.imgui_id)), corner,
                            ImVec2(corner.x + box.x, corner.y + box.y), ImVec2(0.0f, 0.0f), ImVec2(u, v));
        draw_list->AddCallback(platform_io.DrawCallback_SetSamplerLinear, nullptr);
    }
    else
    {
        // Two lines, centered: the box is narrow.
        const char* text = piccolo_text(strings, Str::guests_no_screenshot);
        float line = ImGui::GetTextLineHeight();
        float y = corner.y + (box.y - line * 2.0f) * 0.5f;
        const char* space = std::strchr(text, ' ');
        const char* second = space != nullptr ? std::strchr(space + 1, ' ') : nullptr;   // "NO SCREENSHOT" / "AVAILABLE"
        size_t first_size = second != nullptr ? static_cast<size_t>(second - text) : std::strlen(text);
        ImVec2 first_extent = ImGui::CalcTextSize(text, text + first_size);
        draw_list->AddText(ImVec2(std::floor(corner.x + (box.x - first_extent.x) * 0.5f), std::floor(y)),
                           ImGui::GetColorU32(ImGuiCol_TextDisabled), text, text + first_size);
        if (second != nullptr)
        {
            ImVec2 second_extent = ImGui::CalcTextSize(second + 1);
            draw_list->AddText(ImVec2(std::floor(corner.x + (box.x - second_extent.x) * 0.5f), std::floor(y + line)),
                               ImGui::GetColorU32(ImGuiCol_TextDisabled), second + 1);
        }
    }
    ImGui::Dummy(box);
}

static void wrapped(const char* text, bool dim)
{
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
    if (dim)
    {
        ImGui::TextDisabled("%s", text);
    }
    else
    {
        ImGui::TextUnformatted(text);
    }
    ImGui::PopTextWrapPos();
}

// SEATS and ACCESS: before the link they're kept for MAKE LINK; with it they
// apply at once (fewer seats never drop anyone).
static void draw_seats_and_access(GuestWindow* window, NetAccess* access, const Localization* strings, bool one_row)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(piccolo_text(strings, Str::guests_seats));
    ImGui::SameLine(one_row ? 0.0f : ImGui::GetFontSize() * 12.0f);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    retro_ui_push_field_text();
    bool seats_changed = ImGui::InputInt("##guest_seats", &window->seats, 1, 1);
    retro_ui_pop_field_text();
    if (window->seats < 1)
    {
        window->seats = 1;
    }
    if (window->seats > static_cast<int32_t>(NET_GUEST_MAX_SEATS))
    {
        window->seats = static_cast<int32_t>(NET_GUEST_MAX_SEATS);
    }
    if (seats_changed && window->live && access != nullptr)
    {
        net_access_guest_set_seats(access, static_cast<uint32_t>(window->seats));
    }
    if (one_row)
    {
        ImGui::SameLine(0.0f, ImGui::GetFontSize() * 2.0f);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(piccolo_text(strings, Str::guests_access));
    ImGui::SameLine(one_row ? 0.0f : ImGui::GetFontSize() * 12.0f);
    const char* labels[2] = {piccolo_text(strings, Str::guests_watch), piccolo_text(strings, Str::guests_play)};
    uint32_t level = window->level;
    retro_ui_choice_buttons("##guest_access", labels, 2, &window->level);
    if (window->level != level && window->live && access != nullptr)
    {
        net_access_guest_set_level(access, static_cast<NetGuestLevel>(window->level));
    }
}

static void draw_setup(GuestWindow* window, bool game, bool internet, const Localization* strings)
{
    draw_preview(window, ImGui::GetFontSize() * 12.0f, strings);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(piccolo_text(strings, Str::guests_game));
    ImGui::SameLine(ImGui::GetFontSize() * 12.0f);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
    retro_ui_push_field_text();
    ImGui::InputText("##guest_title", window->title, sizeof(window->title));
    retro_ui_pop_field_text();
    draw_seats_and_access(window, nullptr, strings, false);
    if (window->level == static_cast<uint32_t>(NetGuestLevel::play))
    {
        wrapped(piccolo_text(strings, Str::guests_play_hint), true);
    }
    ImGui::EndGroup();
    ImGui::Spacing();
    wrapped(piccolo_text(strings, Str::guests_text), false);
    if (window->png.empty() && game)
    {
        wrapped(piccolo_text(strings, Str::guests_card_none), true);
    }
    if (!game)
    {
        wrapped(piccolo_text(strings, Str::guests_no_game), true);
    }
    else if (!internet)
    {
        wrapped(piccolo_text(strings, Str::guests_no_internet), true);
    }
    ImGui::Spacing();
    bool ready = game && internet && window->title[0] != '\0';
    ImGui::BeginDisabled(!ready);
    if (retro_ui_button(piccolo_text(strings, Str::guests_make)))
    {
        window->action = GuestWindowAction::make_link;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (retro_ui_button(piccolo_text(strings, Str::net_owner_cancel)))
    {
        window->open = false;
    }
}

static void draw_guests(GuestWindow* window, NetAccess* access, const InputNet* input_net, const InputSystem* input,
                        const Localization* strings)
{
    float line = ImGui::GetFrameHeightWithSpacing();
    float height = line * static_cast<float>(window->seats > 3 ? window->seats : 3) + ImGui::GetStyle().WindowPadding.y * 2.0f;
    if (!retro_ui_begin_list("##guests", ImGui::GetFontSize() * 34.0f, height))
    {
        retro_ui_end_list();
        return;
    }
    uint32_t shown = 0;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        const NetAccessPeer* guest = net_access_guest(access, peer);
        if (guest == nullptr)
        {
            continue;
        }
        shown++;
        ImGui::PushID(static_cast<int>(peer));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(guest->guest_name[0] != '\0' ? guest->guest_name : "?");
        ImGui::SameLine(ImGui::GetFontSize() * 10.0f);
        int32_t port = input_net_peer_port(input_net, input, peer);
        char what[64];
        if (window->level == static_cast<uint32_t>(NetGuestLevel::watch))
        {
            std::snprintf(what, sizeof(what), "%s", piccolo_text(strings, Str::guests_watches));
        }
        else if (port != INPUT_NO_PORT)
        {
            std::snprintf(what, sizeof(what), "%s%d", piccolo_text(strings, Str::guests_plays), port + 1);
        }
        else
        {
            std::snprintf(what, sizeof(what), "%s", piccolo_text(strings, Str::guests_no_port));
        }
        if (port == INPUT_NO_PORT)
        {
            ImGui::TextDisabled("%s", what);
        }
        else
        {
            ImGui::TextUnformatted(what);
        }
        // Ping bars, then KICK, at the right.
        float block = std::floor(ImGui::GetTextLineHeight() / 7.0f);
        block = block < 1.0f ? 1.0f : block;
        float kick_width = ImGui::CalcTextSize(piccolo_text(strings, Str::guests_kick)).x +
                           ImGui::GetStyle().FramePadding.x * 2.0f;
        float right = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(right - kick_width - port_indicators_ping_width(block) - ImGui::GetFontSize());
        NetLinkStats stats;
        bool known = net_access_peer_stats(access, peer, &stats);
        ImVec2 at = ImGui::GetCursorScreenPos();
        port_indicators_draw_ping(at.x, at.y + ImGui::GetFrameHeight() - block * 2.0f, block, known,
                                  known ? stats.round_trip_ms : 0, retro_ui_palette()->panel_dark);
        ImGui::Dummy(ImVec2(port_indicators_ping_width(block), ImGui::GetFrameHeight()));
        ImGui::SameLine(right - kick_width);
        if (retro_ui_button(piccolo_text(strings, Str::guests_kick)))
        {
            net_access_guest_kick(access, peer);
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::guests_kick_tip));
        ImGui::PopID();
    }
    if (shown == 0)
    {
        ImGui::TextDisabled("%s", piccolo_text(strings, Str::guests_none));
    }
    retro_ui_end_list();
}

static void draw_live(GuestWindow* window, NetAccess* access, const InputNet* input_net, const InputSystem* input,
                      const Localization* strings)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(piccolo_text(strings, Str::guests_link));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24.0f);
    retro_ui_push_field_text();
    ImGui::InputText("##guest_link", window->link_text, sizeof(window->link_text), ImGuiInputTextFlags_ReadOnly);
    retro_ui_pop_field_text();
    ImGui::SameLine();
    bool copied = window->copied_at >= 0.0 && ImGui::GetTime() - window->copied_at < 2.0;
    if (retro_ui_button(piccolo_text(strings, copied ? Str::guests_copied : Str::guests_copy)))
    {
        clipboard_set_text(window->link_text);
        window->copied_at = ImGui::GetTime();
    }
    ImGui::SameLine();
    if (retro_ui_button(piccolo_text(strings, Str::guests_new)))
    {
        window->action = GuestWindowAction::new_link;
    }
    retro_ui_item_tooltip(piccolo_text(strings, Str::guests_new_tip));

    draw_seats_and_access(window, access, strings, true);
    draw_guests(window, access, input_net, input, strings);

    draw_preview(window, ImGui::GetFontSize() * 8.0f, strings);
    ImGui::SameLine();
    ImGui::BeginGroup();
    uint32_t taken = net_access_guest_count(access);
    char line[128];
    std::snprintf(line, sizeof(line), "%s %u / %d%s%s", piccolo_text(strings, Str::guests_taken),
                  static_cast<unsigned>(taken), window->seats,
                  taken >= static_cast<uint32_t>(window->seats) ? "  " : "",
                  taken >= static_cast<uint32_t>(window->seats) ? piccolo_text(strings, Str::guests_full) : "");
    ImGui::TextUnformatted(line);
    if (access->guest_failures > 0)
    {
        std::snprintf(line, sizeof(line), "%s %u", piccolo_text(strings, Str::guests_failures),
                      static_cast<unsigned>(access->guest_failures));
        ImGui::TextDisabled("%s", line);
        retro_ui_item_tooltip(piccolo_text(strings, Str::guests_failures_tip));
    }
    Str card = Str::guests_card_none;
    switch (guest_card_state(&window->card))
    {
        case GuestCardState::sending:
            card = Str::guests_card_sending;
            break;
        case GuestCardState::sent:
            card = Str::guests_card_ready;
            break;
        case GuestCardState::failed:
            card = Str::guests_card_failed;
            break;
        default:
            break;
    }
    ImGui::TextDisabled("%s", piccolo_text(strings, card));
    ImGui::EndGroup();
    ImGui::Spacing();
    if (retro_ui_button(piccolo_text(strings, Str::guests_end)))
    {
        window->action = GuestWindowAction::end;
    }
    ImGui::SameLine();
    if (retro_ui_button(piccolo_text(strings, Str::guests_close)))
    {
        window->open = false;   // the link stays
    }
}

void guest_window_draw(GuestWindow* window, NetAccess* access, const InputNet* input_net, const InputSystem* input,
                       bool game, bool internet, const Localization* strings)
{
    if (!window->open)
    {
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    char title[96];
    std::snprintf(title, sizeof(title), "%s###guests", piccolo_text(strings, Str::guests_title));
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings;
    bool keep = true;
    if (!ImGui::Begin(title, &keep, flags))
    {
        ImGui::End();
        window->open = keep;
        return;
    }
    if (access == nullptr)
    {
        ImGui::TextDisabled("%s", piccolo_text(strings, Str::net_off));
    }
    else if (window->live)
    {
        draw_live(window, access, input_net, input, strings);
    }
    else
    {
        draw_setup(window, game, internet, strings);
    }
    ImGui::End();
    if (!keep)
    {
        window->open = false;   // closing keeps a link (END ends it)
    }
}
