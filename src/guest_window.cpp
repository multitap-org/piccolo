#include "guest_window.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "port_indicators.h"

#include "app_shell.h"
#include "clipboard.h"
#include "net_window.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"

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
    window->preparing = false;
    window->prepare_sent = false;
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
    float box_width = std::floor(width);
    float box_height = std::floor(width / aspect);
    retro_ui_canvas(box_width, box_height);
    retro_ui_canvas_rect(0.0f, 0.0f, box_width, box_height, 0xFF000000u);
    if (window->has_preview)
    {
        float u = 0.0f;
        float v = 0.0f;
        video_texture_uv_max(&window->preview, &u, &v);
        // retro_ui_window_begin() switched this window to nearest sampling: sharp pixels.
        retro_ui_canvas_image(window->preview.ui_texture, 0.0f, 0.0f, box_width, box_height, u, v, 0xFFFFFFFFu);
    }
    else
    {
        // Two lines, centered: the box is narrow.
        const char* text = piccolo_text(strings, Str::guests_no_screenshot);
        uint32_t dim = retro_ui_palette()->text_disabled;
        float line = retro_ui_font_size();
        float y = (box_height - line * 2.0f) * 0.5f;
        const char* space = std::strchr(text, ' ');
        const char* second = space != nullptr ? std::strchr(space + 1, ' ') : nullptr;   // "NO SCREENSHOT" / "AVAILABLE"
        size_t first_size = second != nullptr ? static_cast<size_t>(second - text) : std::strlen(text);
        char first[128] = {};
        std::snprintf(first, sizeof(first), "%.*s", static_cast<int>(first_size), text);
        retro_ui_canvas_text(std::floor((box_width - retro_ui_text_width(first)) * 0.5f), std::floor(y), first, dim);
        if (second != nullptr)
        {
            retro_ui_canvas_text(std::floor((box_width - retro_ui_text_width(second + 1)) * 0.5f), std::floor(y + line),
                                 second + 1, dim);
        }
    }
}

static void wrapped(const char* text, bool dim)
{
    float wrap_x = retro_ui_font_size() * 34.0f;
    if (dim)
    {
        retro_ui_dim_text_wrapped_at(text, wrap_x);
    }
    else
    {
        retro_ui_text_wrapped_at(text, wrap_x);
    }
}

// SEATS and ACCESS: before the link they're kept for MAKE LINK; with it they
// apply at once (fewer seats never drop anyone).
static void draw_seats_and_access(GuestWindow* window, NetAccess* access, const Localization* strings, bool one_row)
{
    float column = retro_ui_font_size() * 12.0f;
    retro_ui_label(piccolo_text(strings, Str::guests_seats));
    if (one_row)
    {
        retro_ui_same_line();
    }
    else
    {
        retro_ui_same_line_at(column);
    }
    retro_ui_next_width(retro_ui_font_size() * 7.0f);
    bool seats_changed = retro_ui_number_field_steps("guest_seats", &window->seats, 1, 1);
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
        retro_ui_same_line_gap(retro_ui_font_size() * 2.0f);
    }
    retro_ui_label(piccolo_text(strings, Str::guests_access));
    if (one_row)
    {
        retro_ui_same_line();
    }
    else
    {
        retro_ui_same_line_at(column);
    }
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
    draw_preview(window, retro_ui_font_size() * 12.0f, strings);
    retro_ui_same_line();
    retro_ui_begin_group();
    retro_ui_label(piccolo_text(strings, Str::guests_game));
    retro_ui_same_line_at(retro_ui_font_size() * 12.0f);
    retro_ui_next_width(retro_ui_font_size() * 22.0f);
    RetroUiEditField title_field = {};
    retro_ui_edit_field("guest_title", window->title, sizeof(window->title), &title_field);
    draw_seats_and_access(window, nullptr, strings, false);
    if (window->level == static_cast<uint32_t>(NetGuestLevel::play))
    {
        wrapped(piccolo_text(strings, Str::guests_play_hint), true);
    }
    retro_ui_end_group();
    retro_ui_spacing();
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
    retro_ui_spacing();
    bool ready = game && internet && window->title[0] != '\0';
    retro_ui_begin_disabled(!ready);
    if (retro_ui_button(piccolo_text(strings, Str::guests_make)))
    {
        window->action = GuestWindowAction::make_link;
    }
    retro_ui_end_disabled();
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::net_owner_cancel)))
    {
        window->open = false;
    }
}

static void draw_guests(GuestWindow* window, NetAccess* access, const InputNet* input_net, const InputSystem* input,
                        const Localization* strings)
{
    uint32_t rows = window->seats > 3 ? static_cast<uint32_t>(window->seats) : 3u;
    if (!retro_ui_begin_list("##guests", retro_ui_font_size() * 34.0f, retro_ui_list_height(rows)))
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
        retro_ui_push_index(peer);
        retro_ui_label(guest->guest_name[0] != '\0' ? guest->guest_name : "?");
        retro_ui_same_line_at(retro_ui_font_size() * 10.0f);
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
            retro_ui_dim_text(what, nullptr);
        }
        else
        {
            retro_ui_text(what);
        }
        // Ping bars, then KICK, at the right.
        float block = std::floor(retro_ui_font_size() / 7.0f);
        block = block < 1.0f ? 1.0f : block;
        float kick_width = retro_ui_button_width(piccolo_text(strings, Str::guests_kick));
        float right = retro_ui_right_edge();
        retro_ui_same_line_at(right - kick_width - port_indicators_ping_width(block) - retro_ui_font_size());
        NetLinkStats stats;
        bool known = net_access_peer_stats(access, peer, &stats);
        retro_ui_ping_bars(known, known ? stats.round_trip_ms : 0, block);
        retro_ui_same_line_at(right - kick_width);
        if (retro_ui_button(piccolo_text(strings, Str::guests_kick)))
        {
            net_access_guest_kick(access, peer);
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::guests_kick_tip));
        retro_ui_pop_id();
    }
    if (shown == 0)
    {
        retro_ui_dim_text(piccolo_text(strings, Str::guests_none), nullptr);
    }
    retro_ui_end_list();
}

static void draw_live(GuestWindow* window, NetAccess* access, const InputNet* input_net, const InputSystem* input,
                      const Localization* strings)
{
    if (window->preparing)
    {
        // Not shown until it works and has its card (main.cpp guest_update_locked).
        net_window_waiting_leds(piccolo_text(strings, Str::guests_preparing));
        retro_ui_spacing();
        if (retro_ui_button(piccolo_text(strings, Str::net_owner_cancel)))
        {
            window->action = GuestWindowAction::end;
        }
        return;
    }
    retro_ui_label(piccolo_text(strings, Str::guests_link));
    retro_ui_same_line();
    retro_ui_next_width(retro_ui_font_size() * 24.0f);
    RetroUiEditField link_field = {};
    link_field.read_only = true;
    retro_ui_edit_field("guest_link", window->link_text, sizeof(window->link_text), &link_field);
    retro_ui_same_line();
    bool copied = window->copied_at >= 0.0 && retro_ui_seconds() - window->copied_at < 2.0;
    if (retro_ui_button(piccolo_text(strings, copied ? Str::guests_copied : Str::guests_copy)))
    {
        clipboard_set_text(window->link_text);
        window->copied_at = retro_ui_seconds();
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::guests_new)))
    {
        window->action = GuestWindowAction::new_link;
    }
    retro_ui_item_tooltip(piccolo_text(strings, Str::guests_new_tip));

    draw_seats_and_access(window, access, strings, true);
    draw_guests(window, access, input_net, input, strings);

    draw_preview(window, retro_ui_font_size() * 8.0f, strings);
    retro_ui_same_line();
    retro_ui_begin_group();
    uint32_t taken = net_access_guest_count(access);
    char line[128];
    std::snprintf(line, sizeof(line), "%s %u / %d%s%s", piccolo_text(strings, Str::guests_taken),
                  static_cast<unsigned>(taken), window->seats,
                  taken >= static_cast<uint32_t>(window->seats) ? "  " : "",
                  taken >= static_cast<uint32_t>(window->seats) ? piccolo_text(strings, Str::guests_full) : "");
    retro_ui_text(line);
    if (access->guest_failures > 0)
    {
        std::snprintf(line, sizeof(line), "%s %u", piccolo_text(strings, Str::guests_failures),
                      static_cast<unsigned>(access->guest_failures));
        retro_ui_dim_text(line, piccolo_text(strings, Str::guests_failures_tip));
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
    retro_ui_dim_text(piccolo_text(strings, card), nullptr);
    if (card == Str::guests_card_failed && window->card.error[0] != '\0')
    {
        // Why, as net_http said it (there's no console on Windows to read it from).
        retro_ui_dim_text_wrapped_at(window->card.error, retro_ui_cursor_x() + retro_ui_font_size() * 22.0f);
    }
    retro_ui_end_group();
    retro_ui_spacing();
    if (retro_ui_button(piccolo_text(strings, Str::guests_end)))
    {
        window->action = GuestWindowAction::end;
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::guests_close)))
    {
        window->open = false;   // the link stays
    }
}

void guest_window_draw(GuestWindow* window, NetAccess* access, const InputNet* input_net, const InputSystem* input,
                       bool game, bool internet, const Localization* strings, RetroUiFont* pixel_font, int32_t scale)
{
    if (!window->open)
    {
        return;
    }
    RetroUiWindow frame = {};
    frame.id = "guests";
    frame.title = piccolo_text(strings, Str::guests_title);
    frame.centered_on_open = true;
    bool closed = false;
    // Themed like every other window; the close button is the pixel font's X.
    retro_ui_push_theme(pixel_font, scale);
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        if (access == nullptr)
        {
            retro_ui_dim_text(piccolo_text(strings, Str::net_off), nullptr);
        }
        else if (window->live)
        {
            draw_live(window, access, input_net, input, strings);
        }
        else
        {
            draw_setup(window, game, internet, strings);
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
    if (closed)
    {
        window->open = false;   // closing keeps a link (END ends it)
    }
}
