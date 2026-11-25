#include "guest_window.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "port_indicators.h"

#include "app_shell.h"
#include "clipboard.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"

// ---------------------------------------------------------------------------
// State

void guest_window_state(GuestWindowState* state, const NetAccess* access, const InputNet* input_net,
                        const InputSystem* input, bool game, bool internet, const Localization* strings)
{
    state->strings = strings;
    state->network = access != nullptr;
    state->game = game;
    state->internet = internet;
    if (access == nullptr)
    {
        return;
    }
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        GuestWindowGuest* row = &state->guests[peer];
        const NetAccessPeer* guest = net_access_guest(access, peer);
        if (guest == nullptr)
        {
            continue;
        }
        row->present = true;
        std::memcpy(row->key, guest->key, sizeof(row->key));
        std::snprintf(row->name, sizeof(row->name), "%s", guest->guest_name);
        row->port = input_net_peer_port(input_net, input, peer);
        NetLinkStats stats;
        row->ping_known = net_access_peer_stats(access, peer, &stats);
        row->round_trip_ms = row->ping_known ? stats.round_trip_ms : 0;
    }
    state->failures = access->guest_failures;
}

// ---------------------------------------------------------------------------
// Drawing

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

// The grid LINK and GUESTS + ROLE share, right of the preview (window
// pixels): the labels' column, the number field, ROLE's label, and the
// column of the link's button and of PLAYER (SPECTATOR has one as wide).
struct GuestGrid
{
    float left = 0.0f;       // the grid's left edge, from the window's
    float width = 0.0f;      // to the window's right edge
    float label = 0.0f;      // LINK / GUESTS, and the gap after them
    float number = 0.0f;     // GUESTS' field
    float role = 0.0f;       // ROLE's label
    float button = 0.0f;     // the link's button, SPECTATOR, PLAYER
};

static float wider(float a, float b)
{
    return a > b ? a : b;
}

// The preview's width; the grid's columns from the labels (the language),
// never from what the window shows now: its width stays the same.
static float preview_width()
{
    return retro_ui_font_size() * 12.0f;
}

static GuestGrid grid_columns(const Localization* strings)
{
    float font = retro_ui_font_size();
    float gap = retro_ui_item_spacing();
    GuestGrid grid;
    grid.label = wider(retro_ui_text_width(piccolo_text(strings, Str::guests_link)),
                       retro_ui_text_width(piccolo_text(strings, Str::guests_count))) + font;
    grid.number = font * 7.0f;
    grid.role = retro_ui_text_width(piccolo_text(strings, Str::guests_role));
    grid.button = wider(wider(retro_ui_button_width(piccolo_text(strings, Str::guests_make)),
                              retro_ui_button_width(piccolo_text(strings, Str::guests_new))),
                        wider(retro_ui_button_width(piccolo_text(strings, Str::guests_watch)),
                              retro_ui_button_width(piccolo_text(strings, Str::guests_play))));
    float needed = grid.label + grid.number + gap + grid.role + gap + grid.button * 2.0f + gap;
    grid.width = wider(needed, font * 30.0f);
    return grid;
}

// Why CREATE LINK is greyed out (its tooltip), or nullptr when it isn't.
static const char* create_blocked(const GuestWindow* window, const GuestWindowState* state)
{
    const Localization* strings = state->strings;
    if (!state->network)
    {
        return piccolo_text(strings, Str::net_off);
    }
    if (!state->game || window->title[0] == '\0')
    {
        return piccolo_text(strings, Str::guests_no_game);
    }
    if (!state->internet)
    {
        return piccolo_text(strings, Str::guests_no_internet);
    }
    return nullptr;
}

// LINK and its button: NO LINK YET and CREATE LINK, PREPARING THE LINK...
// while it's made, then the link (whole in its tooltip) and REFRESH LINK.
static void draw_link_row(GuestWindow* window, const GuestWindowState* state, const GuestGrid* grid,
                          GuestWindowAction* action)
{
    const Localization* strings = state->strings;
    float gap = retro_ui_item_spacing();
    bool shown = window->live && !window->preparing;
    retro_ui_label(piccolo_text(strings, Str::guests_link));
    retro_ui_same_line_at(grid->left + grid->label);
    retro_ui_next_width(grid->width - grid->label - gap - grid->button);
    RetroUiEditField link_field = {};
    link_field.read_only = true;
    if (shown)
    {
        retro_ui_edit_field("guest_link", window->link_text, sizeof(window->link_text), &link_field);
        // A click copies it: the browser allows a copy only right after
        // one, which CREATE LINK's (up to GUEST_PREPARE_MS later) isn't.
        if (retro_ui_item_clicked())
        {
            action->copy_link = true;
        }
        char tip[NET_GUEST_MAX_LINK + 160];
        std::snprintf(tip, sizeof(tip), "%s\n%s", window->link_text, piccolo_text(strings, Str::guests_link_tip));
        retro_ui_item_tooltip(tip);
    }
    else
    {
        char empty[1] = {};
        link_field.hint = piccolo_text(strings, window->preparing ? Str::guests_preparing : Str::guests_no_link);
        retro_ui_edit_field("guest_no_link", empty, sizeof(empty), &link_field);
    }
    retro_ui_same_line_at(grid->left + grid->width - grid->button);
    const char* blocked = create_blocked(window, state);
    retro_ui_begin_disabled(blocked != nullptr || window->preparing);
    char label[160];
    std::snprintf(label, sizeof(label), "%s###guest_link_button",
                  piccolo_text(strings, window->live ? Str::guests_new : Str::guests_make));
    if (retro_ui_button_sized(label, grid->button, 0.0f))
    {
        action->press = window->live ? GuestWindowPress::new_link : GuestWindowPress::make_link;
    }
    if (blocked != nullptr)
    {
        retro_ui_disabled_item_tooltip(blocked);
    }
    else if (window->live)
    {
        retro_ui_disabled_item_tooltip(piccolo_text(strings, Str::guests_new_tip));
    }
    retro_ui_end_disabled();
}

// GUESTS and ROLE: before the link they're kept for CREATE LINK; with it
// they apply at once (guest_window_apply).
static void draw_guests_row(GuestWindow* window, const Localization* strings, const GuestGrid* grid,
                            GuestWindowAction* action)
{
    float gap = retro_ui_item_spacing();
    retro_ui_label(piccolo_text(strings, Str::guests_count));
    retro_ui_same_line_at(grid->left + grid->label);
    retro_ui_next_width(grid->number);
    action->seats_changed = retro_ui_number_field_steps("guest_seats", &window->seats, 1, 1);
    if (window->seats < 1)
    {
        window->seats = 1;
    }
    if (window->seats > static_cast<int32_t>(NET_GUEST_MAX_SEATS))
    {
        window->seats = static_cast<int32_t>(NET_GUEST_MAX_SEATS);
    }
    float choices = grid->button * 2.0f + gap;
    retro_ui_same_line_at(grid->left + grid->width - choices - gap - grid->role);
    retro_ui_label(piccolo_text(strings, Str::guests_role));
    retro_ui_same_line_at(grid->left + grid->width - choices);
    const char* labels[2] = {piccolo_text(strings, Str::guests_watch), piccolo_text(strings, Str::guests_play)};
    uint32_t level = window->level;
    retro_ui_choice_buttons_equal("##guest_role", labels, 2, &window->level, choices);
    if (window->level != level)
    {
        action->level_changed = true;
    }
}

// The guests in, a line each: the name, its role, the ping and KICK.
static void draw_guests(const GuestWindow* window, const GuestWindowState* state, GuestWindowAction* action)
{
    const Localization* strings = state->strings;
    uint32_t rows = window->seats > 3 ? static_cast<uint32_t>(window->seats) : 3u;
    if (!retro_ui_begin_list("##guests", 0.0f, retro_ui_list_height(rows)))
    {
        retro_ui_end_list();
        return;
    }
    uint32_t shown = 0;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        const GuestWindowGuest* guest = &state->guests[peer];
        if (!guest->present)
        {
            continue;
        }
        shown++;
        retro_ui_push_index(peer);
        retro_ui_label(guest->name[0] != '\0' ? guest->name : "?");
        retro_ui_same_line_at(retro_ui_font_size() * 14.0f);
        // PLAYER with its port; SPECTATOR when it watches (the role, or no
        // free port).
        int32_t port = guest->port;
        bool plays = window->level == static_cast<uint32_t>(NetGuestLevel::play) && port != INPUT_NO_PORT;
        if (plays)
        {
            retro_ui_text_format("%s %d", piccolo_text(strings, Str::guests_player_port), port + 1);
        }
        else
        {
            retro_ui_dim_text(piccolo_text(strings, Str::guests_watch), nullptr);
        }
        // Ping bars, then KICK, at the right.
        float block = std::floor(retro_ui_font_size() / 7.0f);
        block = block < 1.0f ? 1.0f : block;
        float kick_width = retro_ui_button_width(piccolo_text(strings, Str::guests_kick));
        float right = retro_ui_right_edge();
        retro_ui_same_line_at(right - kick_width - port_indicators_ping_width(block) - retro_ui_font_size());
        retro_ui_ping_bars(guest->ping_known, guest->round_trip_ms, block);
        retro_ui_same_line_at(right - kick_width);
        if (retro_ui_button(piccolo_text(strings, Str::guests_kick)))
        {
            action->kick = true;
            action->kick_peer = peer;
            std::memcpy(action->kick_key, guest->key, sizeof(action->kick_key));
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::guests_kick_tip));
        retro_ui_pop_id();
    }
    if (shown == 0)
    {
        retro_ui_dim_text(piccolo_text(strings, Str::guests_none), nullptr);
    }
    if (state->failures > 0)
    {
        char line[128];
        std::snprintf(line, sizeof(line), "%s %u", piccolo_text(strings, Str::guests_failures),
                      static_cast<unsigned>(state->failures));
        retro_ui_dim_text(line, piccolo_text(strings, Str::guests_failures_tip));
    }
    retro_ui_end_list();
}

void guest_window_draw(GuestWindow* window, const GuestWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                       GuestWindowAction* action)
{
    const Localization* strings = state->strings;
    if (!window->open)
    {
        return;
    }
    // Themed like every other window; the close button is the pixel font's X.
    retro_ui_push_theme(pixel_font, scale);
    float chrome_width = 0.0f;
    float chrome_height = 0.0f;
    retro_ui_window_chrome(&chrome_width, &chrome_height);
    GuestGrid grid = grid_columns(strings);
    float content = preview_width() + retro_ui_item_spacing() + grid.width;
    RetroUiWindow frame = {};
    frame.id = "guests";
    frame.title = piccolo_text(strings, Str::guests_title);
    frame.centered_on_open = true;
    frame.width = std::floor(content + chrome_width);
    frame.scrolls = false;
    bool closed = false;
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        // GAME, the name right after it, wrapping under itself when long.
        retro_ui_text(piccolo_text(strings, Str::guests_game));
        retro_ui_same_line();
        retro_ui_text_wrapped(window->title);
        retro_ui_spacing();
        draw_preview(window, preview_width(), strings);
        retro_ui_same_line();
        grid.left = retro_ui_cursor_x();
        retro_ui_begin_group();
        draw_link_row(window, state, &grid, action);
        draw_guests_row(window, strings, &grid, action);
        retro_ui_end_group();
        draw_guests(window, state, action);
        // STOP REMOTE PLAY (greyed out with no link; while one is made it
        // stops that) and CLOSE, splitting the full width.
        RetroUiRowButton buttons[2];
        buttons[0].id = "guest_stop";
        buttons[0].label = piccolo_text(strings, Str::guests_end);
        buttons[0].enabled = window->live;
        buttons[1].id = "guest_close";
        buttons[1].label = piccolo_text(strings, Str::guests_close);
        int32_t pressed = retro_ui_equal_buttons(buttons, 2, 0.0f);
        if (pressed == 0)
        {
            action->press = GuestWindowPress::end;
        }
        else if (pressed == 1)
        {
            window->open = false;   // the link stays
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
    if (closed)
    {
        window->open = false;   // closing keeps a link (STOP REMOTE PLAY ends it)
    }
}

// ---------------------------------------------------------------------------
// Apply

void guest_window_open(GuestWindow* window)
{
    window->open = true;
}

// The link to the clipboard; the notification card only when it got there
// (a browser refuses a copy that isn't right after a click).
static void copy_link(GuestWindow* window, uint64_t now_ms)
{
    if (clipboard_set_text(window->link_text))
    {
        window->copied_ms = now_ms > 0 ? now_ms : 1;
    }
}

GuestWindowPress guest_window_apply(GuestWindow* window, const GuestWindowAction* action, NetAccess* access,
                                    uint64_t now_ms)
{
    if (action->copy_link)
    {
        copy_link(window, now_ms);   // the link field clicked (it shows only once the link is ready)
    }
    // GUESTS and ROLE: before the link they're kept for CREATE LINK; with it
    // they apply at once (fewer seats never drop anyone).
    if (action->seats_changed && window->live && access != nullptr)
    {
        net_access_guest_set_seats(access, static_cast<uint32_t>(window->seats));
    }
    if (action->level_changed && window->live && access != nullptr)
    {
        net_access_guest_set_level(access, static_cast<NetGuestLevel>(window->level));
    }
    // The network lock was let go while drawing: the guest on that peer may
    // have left and another taken its place. Kick only the one that was shown.
    if (action->kick && access != nullptr)
    {
        const NetAccessPeer* guest = net_access_guest(access, action->kick_peer);
        if (guest != nullptr && std::memcmp(guest->key, action->kick_key, sizeof(action->kick_key)) == 0)
        {
            net_access_guest_kick(access, action->kick_peer);
        }
    }
    if (access == nullptr)
    {
        return GuestWindowPress::none;   // CREATE LINK, REFRESH LINK and STOP need the network
    }
    if (action->press == GuestWindowPress::make_link || action->press == GuestWindowPress::new_link)
    {
        window->copy_when_ready = true;   // the link goes to the clipboard once it shows
    }
    return action->press;
}

void guest_window_link_ready(GuestWindow* window, uint64_t now_ms)
{
    if (!window->copy_when_ready)
    {
        return;
    }
    window->copy_when_ready = false;
    // TODO(round1): on Android CREATE LINK should open the share sheet with
    // the link; piccolo has no path to it yet, so it's copied there too.
    // In the browser this copy comes too long after the click to be allowed:
    // no card then, and a click on the link field copies it.
    copy_link(window, now_ms);
}

constexpr uint64_t HUD_CARD_MS = 2500;      // the card stays this long (as the DVR's)...
constexpr uint64_t HUD_CARD_FADE_MS = 500;  // ...then fades out

void guest_window_add_hud(const GuestWindow* window, const Localization* strings, HudStack* stack,
                          uint64_t now_ms)
{
    uint64_t since = window->copied_ms;
    if (since == 0 || now_ms < since || now_ms - since >= HUD_CARD_MS + HUD_CARD_FADE_MS)
    {
        return;
    }
    // [LINK] COPIED / TO THE CLIPBOARD
    HudCard card = {};
    std::snprintf(card.chip, sizeof(card.chip), "%s", piccolo_text(strings, Str::guests_link));
    std::snprintf(card.title, sizeof(card.title), "%s", piccolo_text(strings, Str::guests_copied));
    std::snprintf(card.before, sizeof(card.before), "%s", piccolo_text(strings, Str::guests_copied_where));
    card.since_ms = since;
    if (now_ms - since > HUD_CARD_MS)
    {
        card.alpha = 1.0f - static_cast<float>(now_ms - since - HUD_CARD_MS) / static_cast<float>(HUD_CARD_FADE_MS);
    }
    hud_view_add_card(stack, &card);
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
    window->copy_when_ready = false;
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

// The preview's texture (the renderer's, outside the toolkit).
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
