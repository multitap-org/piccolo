#include "pair_window.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <SDL3/SDL_log.h>

#include "net_access.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"

// ROLE's values: today's access levels.
static const Str LEVEL_NAMES[NET_ACCESS_LEVEL_COUNT] = {Str::net_access_gamepad, Str::net_access_stream,
                                                        Str::net_access_share, Str::net_access_full};

static const char* level_name(const Localization* strings, uint32_t level)
{
    return piccolo_text(strings, LEVEL_NAMES[level < NET_ACCESS_LEVEL_COUNT ? level : 0]);
}

// A step of one mode is in progress: the other mode is greyed out.
static bool host_in_progress(const PairWindow* window)
{
    return window->phase != PairWindowPhase::list;
}

static bool client_in_progress(const PairWindow* window)
{
    return window->request.active || window->invite.waiting || window->client_done;
}

// PAIR WITH A HOST back to its list, with a new code.
static void reset_host(PairWindow* window)
{
    window->phase = PairWindowPhase::list;
    window->selected = -1;
    window->internet_selected = false;
    window->internet = false;
    window->host_name.clear();
    window->given = NetAccessLevel::gamepad;
    std::memset(window->host_key, 0, sizeof(window->host_key));
    window->seen_connecting = false;
    window->preselect.clear();
    net_pair_code_generate(window->code);
}

// ---------------------------------------------------------------------------
// State

void pair_window_update(PairWindow* window, const NetShareLinkStatus* link)
{
    // A device asks to pair: the window opens on PAIR WITH A CLIENT, unless
    // PAIR WITH A HOST is halfway through a step (it waits until that ends).
    // A new request also replaces PAIRED WITH (the last pairing is over), and
    // raises the window even when it waits behind a host step, so it's never
    // left hidden behind another window.
    bool host_busy = window->open && host_in_progress(window);
    bool fresh = window->request.active && window->request.fresh;
    window->request.fresh = false;
    if (window->request.active && !host_busy &&
        (!window->open || window->mode != PairMode::client || (fresh && window->client_done)))
    {
        if (!window->open)
        {
            reset_host(window);
        }
        window->open = true;
        window->mode = PairMode::client;
        window->select_mode = true;
        window->raise = true;
        window->request.focus = true;
        window->client_done = false;
        window->client_name.clear();
    }
    if (fresh && window->open)
    {
        window->raise = true;
    }
    if (!window->open || link == nullptr)
    {
        return;
    }
    if (window->phase == PairWindowPhase::list)
    {
        if (window->selected >= static_cast<int32_t>(link->found.size()))
        {
            window->selected = -1;
        }
        for (size_t i = 0; i < link->found.size() && !window->preselect.empty(); i++)
        {
            if (link->found[i].name == window->preselect)
            {
                window->selected = static_cast<int32_t>(i);
                window->internet_selected = false;
                window->preselect.clear();
            }
        }
    }
    else if (window->phase == PairWindowPhase::waiting)
    {
        if (!link->code.empty())
        {
            // A new one after a wrong try.
            std::snprintf(window->code, sizeof(window->code), "%s", link->code.c_str());
        }
        // The outcome, from FILE TRANSFER's connection.
        if (link->state == NetShareLinkState::connecting)
        {
            window->seen_connecting = true;
        }
        else if (link->state == NetShareLinkState::ready && link->newly_paired)
        {
            window->phase = PairWindowPhase::done;
            window->host_name = link->host_name;
            window->given = link->access;
            std::memcpy(window->host_key, link->host_key, NET_KEY_SIZE);
        }
        else if (window->seen_connecting && link->state != NetShareLinkState::ready)
        {
            // Refused, gone, or cancelled on the other side: back to the
            // list with a new code (no line says so: testing round 1 took
            // the explanations out).
            window->phase = PairWindowPhase::list;
            window->seen_connecting = false;
            net_pair_code_generate(window->code);
        }
    }
    else if (link->access != window->given)
    {
        window->given = link->access;   // the host's welcome came after ready
    }
}

void pair_window_state(PairWindowState* state, const Localization* strings, const NetShareLinkStatus* link,
                       bool network, bool can_give_back)
{
    state->strings = strings;
    state->link = link;
    state->network = network;
    state->can_give_back = can_give_back;
}

// ---------------------------------------------------------------------------
// Drawing

// The buttons under the panel (they split the window's full width), and
// whether WAITING FOR DEVICE shows above them.
constexpr uint32_t PAIR_BUTTONS_MAX = 2;
struct PairButtons
{
    RetroUiRowButton buttons[PAIR_BUTTONS_MAX];
    PairWindowPress  presses[PAIR_BUTTONS_MAX] = {};
    uint32_t         count = 0;
    bool             waiting = false;
};

static void add_button(PairButtons* row, const char* id, const char* label, PairWindowPress press, bool enabled,
                       const char* help)
{
    if (row->count >= PAIR_BUTTONS_MAX)
    {
        return;
    }
    RetroUiRowButton* button = &row->buttons[row->count];
    button->id = id;
    button->label = label;
    button->enabled = enabled;
    button->help = help;
    row->presses[row->count] = press;
    row->count++;
}

static bool is_paired(const NetShareLinkStatus* link, const std::string* name)
{
    for (size_t i = 0; i < link->paired.size(); i++)
    {
        if (link->paired[i] == *name)
        {
            return true;
        }
    }
    return false;
}

// ROLE's column: the values (or the role buttons) start there.
static float role_column(const Localization* strings)
{
    return retro_ui_text_width(piccolo_text(strings, Str::pair_role)) + retro_ui_font_size() * 2.0f;
}

// ROLE and the role given, as text.
static void draw_role_value(const Localization* strings, uint32_t level)
{
    float left = retro_ui_cursor_x();
    retro_ui_label(piccolo_text(strings, Str::pair_role));
    retro_ui_same_line_at(left + role_column(strings));
    retro_ui_text(level_name(strings, level));
}

// ROLE and the four levels to pick from, splitting the rest of the width.
static void draw_role_choice(const Localization* strings, const char* id, uint32_t* level)
{
    float left = retro_ui_cursor_x();
    retro_ui_label(piccolo_text(strings, Str::pair_role));
    retro_ui_same_line_at(left + role_column(strings));
    const char* labels[NET_ACCESS_LEVEL_COUNT];
    for (uint32_t i = 0; i < NET_ACCESS_LEVEL_COUNT; i++)
    {
        labels[i] = level_name(strings, i);
    }
    retro_ui_choice_buttons_equal(id, labels, NET_ACCESS_LEVEL_COUNT, level, 0.0f);
}

static void draw_paired_with(const Localization* strings, const char* name)
{
    char line[512];
    std::snprintf(line, sizeof(line), "%s %s", piccolo_text(strings, Str::pair_done), name);
    retro_ui_text_fit(line, retro_ui_width_left());   // a long name ends in "..." at the panel's edge
}

// PAIR WITH A HOST's panel, and its buttons.
static void draw_host(PairWindow* window, const PairWindowState* state, PairButtons* buttons)
{
    const Localization* strings = state->strings;
    const NetShareLinkStatus* link = state->link;
    if (link == nullptr)
    {
        retro_ui_dim_text(piccolo_text(strings, Str::share_network_off), nullptr);
        add_button(buttons, "pair", piccolo_text(strings, Str::pair_button), PairWindowPress::pair, false, nullptr);
        add_button(buttons, "close", piccolo_text(strings, Str::pair_close), PairWindowPress::close, true, nullptr);
        return;
    }
    if (window->phase == PairWindowPhase::list)
    {
        retro_ui_push_section_text();
        retro_ui_text(piccolo_text(strings, Str::pair_detected));
        retro_ui_pop_section_text();
        for (size_t i = 0; i < link->found.size(); i++)
        {
            const std::string* name = &link->found[i].name;
            const char* note = is_paired(link, name) ? piccolo_text(strings, Str::pair_paired) : nullptr;
            bool selected = !window->internet_selected && window->selected == static_cast<int32_t>(i);
            if (retro_ui_list_item(static_cast<uint32_t>(i), name->c_str(), selected, note))
            {
                window->selected = static_cast<int32_t>(i);
                window->internet_selected = false;
            }
        }
        // Last: a piccolo on another network, through the beacon.
        if (retro_ui_list_item(static_cast<uint32_t>(link->found.size()), piccolo_text(strings, Str::pair_internet),
                               window->internet_selected, nullptr))
        {
            window->internet_selected = true;
            window->selected = -1;
        }
        bool picked = window->internet_selected || window->selected >= 0;
        add_button(buttons, "pair", piccolo_text(strings, Str::pair_button),
                   window->internet_selected ? PairWindowPress::pair_internet : PairWindowPress::pair, picked,
                   picked ? nullptr : piccolo_text(strings, Str::pair_select_help));
        add_button(buttons, "close", piccolo_text(strings, Str::pair_close), PairWindowPress::close, true, nullptr);
        return;
    }
    if (window->phase == PairWindowPhase::waiting)
    {
        if (window->internet)
        {
            retro_ui_text(piccolo_text(strings, Str::pair_give_code));
        }
        else
        {
            // A long device name ends in "..." at the panel's edge (all of it in the tooltip).
            char line[512];
            std::snprintf(line, sizeof(line), "%s %s", piccolo_text(strings, Str::pair_type_code),
                          window->host_name.c_str());
            retro_ui_text_fit(line, retro_ui_width_left());
        }
        RetroUiCodeBoxes boxes = {};
        retro_ui_code_boxes("host_code", window->code, sizeof(window->code), &boxes);
        add_button(buttons, "cancel", piccolo_text(strings, Str::net_owner_cancel), PairWindowPress::cancel, true,
                   nullptr);
        buttons->waiting = true;
        return;
    }
    draw_paired_with(strings, window->host_name.c_str());
    draw_role_value(strings, static_cast<uint32_t>(window->given));
    if (!state->can_give_back)
    {
        // Our listener doesn't run: nothing to give back.
        add_button(buttons, "close", piccolo_text(strings, Str::pair_close), PairWindowPress::close, true, nullptr);
        return;
    }
    retro_ui_spacing();
    char question[512];
    std::snprintf(question, sizeof(question), "%s %s %s", piccolo_text(strings, Str::pair_back_before),
                  window->host_name.c_str(), piccolo_text(strings, Str::pair_back_after));
    retro_ui_text_wrapped(question);
    add_button(buttons, "yes", piccolo_text(strings, Str::pair_yes), PairWindowPress::give_back, true, nullptr);
    add_button(buttons, "no", piccolo_text(strings, Str::pair_no), PairWindowPress::close, true, nullptr);
}

// PAIR WITH A CLIENT's panel, and its buttons.
static void draw_client(PairWindow* window, const PairWindowState* state, PairButtons* buttons,
                        PairWindowAction* action)
{
    const Localization* strings = state->strings;
    PairRequest* request = &window->request;
    if (!state->network)
    {
        // A request left from before networking stopped: CLOSE still drops it
        // (pair_window_network_stopped clears it too, this is the safe side).
        action->on_request = request->active;
        action->request_peer = request->peer;
        retro_ui_dim_text(piccolo_text(strings, Str::net_off), nullptr);
        add_button(buttons, "allow", piccolo_text(strings, Str::pair_allow), PairWindowPress::allow, false, nullptr);
        add_button(buttons, "close", piccolo_text(strings, Str::pair_close), PairWindowPress::close, true, nullptr);
        return;
    }
    // A request showing wins over PAIRED WITH (pair_window_update drops that
    // when a new one comes).
    if (window->client_done && !request->active)
    {
        draw_paired_with(strings, window->client_name.c_str());
        draw_role_value(strings, static_cast<uint32_t>(window->client_level));
        add_button(buttons, "close", piccolo_text(strings, Str::pair_close), PairWindowPress::close, true, nullptr);
        return;
    }
    if (request->active && !request->waiting)
    {
        action->on_request = true;
        action->request_peer = request->peer;
        char line[512];
        std::snprintf(line, sizeof(line), "%s %s", request->name, piccolo_text(strings, Str::pair_wants));
        retro_ui_text_fit(line, retro_ui_width_left());   // a long name ends in "..." at the panel's edge
        // A wrong code: the other device shows a new one (the line says so
        // in place of the usual one).
        retro_ui_dim_text(piccolo_text(strings, request->retry ? Str::pair_wrong_code : Str::pair_type_other), nullptr);
        RetroUiCodeBoxes boxes = {};
        boxes.editable = true;
        boxes.focus = request->focus;
        request->focus = false;
        bool enter = retro_ui_code_boxes("request_code", request->code, sizeof(request->code), &boxes);
        draw_role_choice(strings, "##request_role", &request->level);
        bool complete = net_pair_code_valid(request->code);
        if (enter && complete)
        {
            action->press = PairWindowPress::allow;
        }
        add_button(buttons, "allow", piccolo_text(strings, Str::pair_allow), PairWindowPress::allow, complete, nullptr);
        add_button(buttons, "deny", piccolo_text(strings, Str::pair_deny), PairWindowPress::deny, true, nullptr);
        return;
    }
    if (request->active || window->invite.waiting)
    {
        // ALLOW pressed: until it pairs (or comes back with a new code).
        action->on_request = request->active;
        action->request_peer = request->peer;
        char* code = request->active ? request->code : window->invite.code;
        uint32_t level = request->active ? request->level : window->invite.level;
        retro_ui_text(piccolo_text(strings, Str::pair_code));
        RetroUiCodeBoxes boxes = {};
        retro_ui_code_boxes("waiting_code", code, NET_PAIR_CODE_DIGITS + 1, &boxes);
        draw_role_value(strings, level);
        add_button(buttons, "cancel", piccolo_text(strings, Str::net_owner_cancel), PairWindowPress::deny, true,
                   nullptr);
        buttons->waiting = true;
        return;
    }
    PairInvite* invite = &window->invite;
    retro_ui_text(piccolo_text(strings, Str::pair_type_other));
    RetroUiCodeBoxes boxes = {};
    boxes.editable = true;
    boxes.focus = invite->focus;
    invite->focus = false;
    bool enter = retro_ui_code_boxes("invite_code", invite->code, sizeof(invite->code), &boxes);
    draw_role_choice(strings, "##invite_role", &invite->level);
    bool complete = net_pair_code_valid(invite->code);
    if (enter && complete)
    {
        action->press = PairWindowPress::allow;
    }
    add_button(buttons, "allow", piccolo_text(strings, Str::pair_allow), PairWindowPress::allow, complete, nullptr);
    add_button(buttons, "close", piccolo_text(strings, Str::pair_close), PairWindowPress::close, true, nullptr);
}

// The two modes, splitting the full width; the one not in use is greyed out
// while a step of the other is in progress.
static void draw_modes(PairWindow* window, const Localization* strings)
{
    if (!retro_ui_begin_tabs("pair_modes"))
    {
        return;
    }
    bool select = window->select_mode;
    window->select_mode = false;
    PairMode shown = window->mode;
    retro_ui_begin_disabled(client_in_progress(window) && window->mode != PairMode::host);
    if (retro_ui_begin_tab("host", piccolo_text(strings, Str::pair_mode_host), select && window->mode == PairMode::host))
    {
        shown = PairMode::host;
        retro_ui_end_tab();
    }
    retro_ui_end_disabled();
    retro_ui_begin_disabled(host_in_progress(window) && window->mode != PairMode::client);
    if (retro_ui_begin_tab("client", piccolo_text(strings, Str::pair_mode_client),
                           select && window->mode == PairMode::client))
    {
        shown = PairMode::client;
        retro_ui_end_tab();
    }
    retro_ui_end_disabled();
    retro_ui_end_tabs();
    if (select)
    {
        // Switched from code: the tab bar takes it on the next frame, and
        // reports the old tab on this one. The panel follows `mode`, not that.
        return;
    }
    if (shown == PairMode::client && window->mode != PairMode::client)
    {
        window->invite.focus = true;   // the user switched to it: typing goes to the code at once
    }
    window->mode = shown;
}

// The window's width, the same at every step: room for the longest fixed
// line (the language decides), never less than 40 characters.
static float content_width(const Localization* strings)
{
    float font = retro_ui_font_size();
    float width = font * 40.0f;
    const Str lines[] = {Str::pair_type_other, Str::pair_give_code, Str::pair_wrong_code};
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
    {
        float line = retro_ui_text_width(piccolo_text(strings, lines[i])) + font * 2.0f;
        width = line > width ? line : width;
    }
    float host = retro_ui_text_width(piccolo_text(strings, Str::pair_mode_host));
    float client = retro_ui_text_width(piccolo_text(strings, Str::pair_mode_client));
    float tabs = ((host > client ? host : client) + font * 2.0f) * 2.0f;
    return tabs > width ? tabs : width;
}

// The panel's height, the same at every step: the tallest one (a request:
// two lines, the code, the role buttons) and its padding.
static float panel_height()
{
    return retro_ui_text_line_step() * 2.0f + retro_ui_code_boxes_height() + retro_ui_line_step() +
           retro_ui_line_step() + retro_ui_font_size();
}

void pair_window_draw(PairWindow* window, const PairWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                      PairWindowAction* action)
{
    *action = {};
    if (!window->open)
    {
        return;
    }
    const Localization* strings = state->strings;
    retro_ui_push_theme(pixel_font, scale);
    float chrome_width = 0.0f;
    float chrome_height = 0.0f;
    retro_ui_window_chrome(&chrome_width, &chrome_height);
    float width = content_width(strings);
    RetroUiWindow frame = {};
    frame.id = "pair_window";
    frame.title = piccolo_text(strings, Str::pair_title);
    frame.centered_on_open = true;
    frame.width = std::floor(width + chrome_width);
    frame.scrolls = false;
    bool closed = false;
    // A device asking to pair: over every other window, as its dialog was.
    bool raise = window->raise;
    window->raise = false;
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        if (raise)
        {
            retro_ui_bring_window_to_front();
        }
        draw_modes(window, strings);
        action->mode = window->mode;
        PairButtons buttons;
        if (retro_ui_begin_list("pair_panel", 0.0f, panel_height()))
        {
            if (window->mode == PairMode::host)
            {
                draw_host(window, state, &buttons);
            }
            else
            {
                draw_client(window, state, &buttons, action);
            }
        }
        retro_ui_end_list();
        // The only status line: WAITING FOR DEVICE, blinking (on 0.6 s, off
        // 0.6 s); its line is kept when it's off or not shown.
        bool lit = std::fmod(retro_ui_seconds(), 1.2) < 0.6;
        if (buttons.waiting && lit)
        {
            retro_ui_text(piccolo_text(strings, Str::pair_waiting));
        }
        else
        {
            retro_ui_space(1.0f, retro_ui_font_size());
        }
        int32_t pressed = retro_ui_equal_buttons(buttons.buttons, buttons.count, 0.0f);
        if (pressed >= 0)
        {
            action->press = buttons.presses[pressed];
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
    // Closing wins over a button pressed in the same frame (YES still gives
    // access back: it closes too).
    if (closed && action->press != PairWindowPress::give_back)
    {
        action->press = PairWindowPress::close;
    }
}

// ---------------------------------------------------------------------------
// Apply

void pair_window_open(PairWindow* window, const char* preselect)
{
    if (window->open)
    {
        return;   // a step may be going on: it stays
    }
    PairRequest request = window->request;   // (none while closed: a request opens the window)
    *window = {};
    window->request = request;
    window->open = true;
    window->mode = PairMode::host;
    window->select_mode = true;
    net_pair_code_generate(window->code);
    window->preselect = preselect != nullptr ? preselect : "";
}

static void close_window(PairWindow* window, NetShareLink* link, bool started, bool share_open)
{
    if (started && window->phase == PairWindowPhase::waiting)
    {
        net_share_link_disconnect(link);   // closing kills the code
    }
    else if (started && window->phase == PairWindowPhase::done && !share_open)
    {
        net_share_link_disconnect(link);   // paired: FILE TRANSFER connects again when it's used
    }
    reset_host(window);
    window->open = false;
}

void pair_window_apply_host(PairWindow* window, const PairWindowAction* action, NetShareLink* link,
                            const NetShareLinkStatus* status, const char* beacon, NetService* service,
                            bool share_open)
{
    bool started = net_share_link_started(link);
    if (action->press == PairWindowPress::close)
    {
        close_window(window, link, started, share_open);
        return;
    }
    if (action->mode != PairMode::host)
    {
        return;
    }
    switch (action->press)
    {
    case PairWindowPress::pair:
        // The list may have changed since it was drawn: the device picked
        // must still be there.
        if (window->selected >= 0 && static_cast<size_t>(window->selected) < status->found.size())
        {
            window->internet = false;
            window->phase = PairWindowPhase::waiting;
            window->host_name = status->found[static_cast<size_t>(window->selected)].name;
            if (started)
            {
                net_share_link_connect(link, status->found[static_cast<size_t>(window->selected)].address,
                                       window->code);
            }
        }
        break;
    case PairWindowPress::pair_internet:
        window->internet = true;
        window->phase = PairWindowPhase::waiting;
        if (started)
        {
            net_share_link_set_beacon(link, beacon);   // as FILE TRANSFER does
            net_share_link_pair_beacon(link, window->code);
        }
        break;
    case PairWindowPress::cancel:
        window->phase = PairWindowPhase::list;
        window->seen_connecting = false;
        net_pair_code_generate(window->code);
        if (started)
        {
            net_share_link_disconnect(link);
        }
        break;
    case PairWindowPress::give_back:
        // Access back (Andrés, 2026-11-22): the host may come to us at the
        // level it gave us. It's told on this connection, and it goes in our
        // own devices list.
        if (started && service != nullptr && net_share_link_grant_back(link, window->given))
        {
            net_service_lock(service);
            net_access_add_device(&service->access, window->host_key, window->host_name.c_str(), window->given);
            net_service_unlock(service);
            SDL_Log("Network: \"%s\" may come here too (%s)", window->host_name.c_str(),
                    net_access_level_name(window->given));
        }
        close_window(window, link, started, share_open);
        break;
    default:
        break;
    }
}

// PAIR WITH A CLIENT back to typing a code (the role picked stays).
static void end_invite(PairWindow* window, NetAccess* access)
{
    if (access != nullptr && window->invite.waiting)
    {
        net_access_end_invite(access);
    }
    uint32_t level = window->invite.level;
    window->invite = {};
    window->invite.level = level;
    window->invite.focus = true;
}

// The request drawn is still the one showing (the network thread may have
// replaced it since).
static bool same_request(const PairWindow* window, const PairWindowAction* action)
{
    return action->on_request && window->request.active && window->request.peer == action->request_peer;
}

void pair_window_apply_client(PairWindow* window, const PairWindowAction* action, NetAccess* access)
{
    if (action->mode != PairMode::client)
    {
        return;
    }
    switch (action->press)
    {
    case PairWindowPress::allow:
        if (access == nullptr)
        {
            break;
        }
        if (same_request(window, action))
        {
            if (net_access_answer_pairing(access, window->request.peer, window->request.code,
                                          static_cast<NetAccessLevel>(window->request.level)))
            {
                window->request.waiting = true;
            }
        }
        else if (!action->on_request && !window->request.active && !window->invite.waiting &&
                 net_access_invite(access, window->invite.code))
        {
            window->invite.waiting = true;
        }
        break;
    case PairWindowPress::deny:
        if (same_request(window, action))
        {
            if (access != nullptr)
            {
                net_access_refuse_pairing(access, window->request.peer);
            }
            window->request = {};
        }
        else if (!action->on_request && !window->request.active)
        {
            end_invite(window, access);   // CANCEL while waiting for our own code
        }
        break;
    case PairWindowPress::close:
        if (same_request(window, action) && access != nullptr)
        {
            net_access_refuse_pairing(access, window->request.peer);   // closing a request denies it
        }
        if (same_request(window, action))
        {
            window->request = {};
        }
        end_invite(window, access);
        window->invite.level = static_cast<uint32_t>(NetAccessLevel::gamepad);
        window->client_done = false;
        window->client_name.clear();
        window->open = false;
        break;
    default:
        break;
    }
}

bool pair_window_take_request(PairWindow* window, NetAccess* access, const NetAccessEvent* event)
{
    PairInvite* invite = &window->invite;
    // Our own code's device (a wrong code's new request is typed as a
    // request: its new code shows on its screen).
    if (invite->waiting && !invite->answered && !event->retry &&
        net_access_answer_pairing(access, event->peer, invite->code, static_cast<NetAccessLevel>(invite->level)))
    {
        invite->answered = true;
        invite->peer = event->peer;
        return true;
    }
    PairRequest* request = &window->request;
    if (request->active && request->peer != event->peer)
    {
        return false;   // one at a time
    }
    bool same = request->active && request->peer == event->peer;
    request->active = true;
    request->waiting = false;
    request->retry = event->retry;
    request->peer = event->peer;
    std::snprintf(request->name, sizeof(request->name), "%s", event->name);
    if (!same)
    {
        request->level = static_cast<uint32_t>(event->access);   // what it asked for, until the user picks
    }
    std::memset(request->code, 0, sizeof(request->code));
    request->focus = true;
    request->fresh = true;
    return false;
}

void pair_window_paired(PairWindow* window, NetAccess* access, const NetAccessEvent* event, const char* name)
{
    bool ours = false;
    if (window->request.active && window->request.peer == event->peer)
    {
        ours = true;
        window->request = {};
    }
    if (window->invite.answered && window->invite.peer == event->peer)
    {
        ours = true;
        end_invite(window, access);
    }
    if (!ours)
    {
        return;
    }
    window->client_done = true;
    window->client_name = name;
    window->client_level = event->access;
    if (!window->open)
    {
        reset_host(window);
        window->open = true;
    }
    window->mode = PairMode::client;
    window->select_mode = true;
}

void pair_window_network_stopped(PairWindow* window)
{
    window->request = {};
    uint32_t level = window->invite.level;
    window->invite = {};
    window->invite.level = level;
}

void pair_window_request_gone(PairWindow* window, uint32_t peer)
{
    if (window->request.active && window->request.peer == peer)
    {
        window->request = {};
    }
}
