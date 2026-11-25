#include "pair_window.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <SDL3/SDL_log.h>

#include "hud_view.h"
#include "net_access.h"
#include "piccolo_text.h"
#include "remap_widget.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"

// Whose ticks the red question is for (NetCapsUnsignedAsk::target).
constexpr uint32_t ASK_REQUEST = 1;   // a request's (GENERATE PAIRING CODE)
constexpr uint32_t ASK_DEVICE = 2;    // the device just paired (PAIR A DEVICE's last screen)

// A step of one mode is in progress: the other mode is greyed out.
static bool device_in_progress(const PairWindow* window)
{
    return window->phase != PairDevicePhase::list;
}

static bool code_in_progress(const PairWindow* window)
{
    return window->request.active || window->code_done;
}

// PAIR A DEVICE back to its list.
static void reset_device(PairWindow* window)
{
    window->phase = PairDevicePhase::list;
    window->selected = -1;
    window->enter_selected = false;
    window->internet = false;
    std::memset(window->typed, 0, sizeof(window->typed));
    window->wrong = false;
    window->host_name.clear();
    window->host_kind = NetAppKind::piccolo;
    window->host_signed = false;
    window->host_uses = 0;
    window->given = 0;
    window->ticks = 0;
    window->device = -1;
    window->newly_paired = false;
    window->seen_connecting = false;
    window->device_since_ms = 0;
    window->preselect.clear();
}

// The window on GENERATE PAIRING CODE, over every other one, for the request.
static void open_on_request(PairWindow* window)
{
    if (!window->open)
    {
        reset_device(window);
    }
    window->open = true;
    window->mode = PairMode::code;
    window->select_mode = true;
    window->raise = true;
    window->code_done = false;
}

static void set_notice(PairWindow* window, PairNotice notice, uint64_t now_ms)
{
    window->notice = notice;
    window->notice_ms = now_ms;
}

// ---------------------------------------------------------------------------
// State

// What a device just paired may use here at first: what it uses that we
// offer, without FILE TRANSFER and SETTINGS for an unsigned build.
static NetCaps default_ticks(NetCaps uses, bool signed_build)
{
    NetCaps ticks = uses & NET_CAPS_ALL;
    if (!signed_build)
    {
        ticks &= ~NET_CAPS_UNSIGNED_OFF;
    }
    return ticks;
}

void pair_window_update(PairWindow* window, const NetShareLinkStatus* link, bool in_game, uint64_t now_ms)
{
    // When each waiting step began (pair_window_expire's timeouts).
    if (window->request.active && window->request.since_ms == 0)
    {
        window->request.since_ms = now_ms;   // (a card slides in from then too)
    }
    if (window->shown.active && window->shown.since_ms == 0)
    {
        window->shown.since_ms = now_ms;
    }
    if (window->phase != PairDevicePhase::typing && window->phase != PairDevicePhase::checking)
    {
        window->device_since_ms = 0;
    }
    else if (window->device_since_ms == 0)
    {
        window->device_since_ms = now_ms;
    }
    // A device asks to pair: the window opens on GENERATE PAIRING CODE,
    // unless PAIR A DEVICE is halfway through a step (it waits until that
    // ends), or the player is in the game with the window closed (a card
    // asks: pair_window_hold_accept opens it). A new request also replaces
    // PAIRED WITH, and raises the window even when it waits behind a step.
    bool device_busy = window->open && device_in_progress(window);
    bool as_card = in_game && !window->open;
    bool fresh = window->request.active && window->request.fresh;
    window->request.fresh = false;
    if (window->request.active && !device_busy && !as_card &&
        (!window->open || window->mode != PairMode::code || (fresh && window->code_done)))
    {
        open_on_request(window);
    }
    if (fresh && window->open)
    {
        window->raise = true;
    }
    if (!window->open || link == nullptr)
    {
        return;
    }
    if (window->phase == PairDevicePhase::list)
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
                window->enter_selected = false;
                window->preselect.clear();
            }
        }
        return;
    }
    if (window->phase == PairDevicePhase::done)
    {
        window->given = link->state == NetShareLinkState::ready ? link->caps : window->given;
        return;
    }
    // Typing or checking: the outcome, from FILE TRANSFER's connection.
    if (link->state == NetShareLinkState::connecting)
    {
        window->seen_connecting = true;
        if (link->code_needed && window->phase == PairDevicePhase::checking)
        {
            // The code was wrong: the device shows a new one, typed again.
            window->phase = PairDevicePhase::typing;
            window->wrong = true;
            std::memset(window->typed, 0, sizeof(window->typed));
            window->focus = true;
        }
        if (link->code_needed && !link->host_name.empty())
        {
            window->host_name = link->host_name;   // across the internet: now we know who
        }
    }
    else if (link->state == NetShareLinkState::ready)
    {
        // Ours: connecting set the state under the link's lock before this
        // step began, so a ready now is this connection's. Without
        // newly_paired the device already knew us (paired before).
        window->phase = PairDevicePhase::done;
        window->newly_paired = link->newly_paired;
        window->host_name = link->host_name;
        window->host_kind = link->host_kind;
        window->host_signed = link->host_signed;
        window->host_uses = link->host_uses;
        window->given = link->caps;
        window->device = link->device;
        window->ticks = default_ticks(link->host_uses, link->host_signed);
        if (!link->newly_paired && link->device >= 0 && static_cast<size_t>(link->device) < link->paired_devices.size())
        {
            window->ticks = link->paired_devices[static_cast<size_t>(link->device)].grants;   // as it is
        }
    }
    else if (window->seen_connecting && link->state == NetShareLinkState::idle)
    {
        // Refused, gone, too many wrong codes, or cancelled on the other side.
        reset_device(window);
        set_notice(window, PairNotice::failed, now_ms);
    }
}

void pair_window_state(PairWindowState* state, const Localization* strings, const NetShareLinkStatus* link,
                       bool network)
{
    state->strings = strings;
    state->link = link;
    state->network = network;
}

// ---------------------------------------------------------------------------
// Drawing

// The buttons under the panel (they split the window's full width), and the
// blinking line above them (WAITING FOR ...), if any.
constexpr uint32_t PAIR_BUTTONS_MAX = 2;
struct PairButtons
{
    RetroUiRowButton buttons[PAIR_BUTTONS_MAX];
    PairWindowPress  presses[PAIR_BUTTONS_MAX] = {};
    uint32_t         count = 0;
    char             waiting[256] = {};   // "" = no waiting line
    PairWindowPress  enter = PairWindowPress::none;   // Enter in the code boxes
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

// "<PREFIX> <NAME>", cut to the panel's width with "..." (all of it in the tooltip).
static void draw_prefixed(const char* prefix, const char* name)
{
    char line[512];
    std::snprintf(line, sizeof(line), "%s %s", prefix, name);
    retro_ui_text_fit(line, retro_ui_width_left());
}

// A label line (section-colored), as the mockups' PERMISSIONS headings.
static void draw_heading(const char* text)
{
    retro_ui_push_section_text();
    retro_ui_text_fit(text, retro_ui_width_left());
    retro_ui_pop_section_text();
}

// The device's chips after its name line: its kind, UNSIGNED BUILD.
static void draw_chips(const Localization* strings, NetAppKind kind, bool signed_build)
{
    retro_ui_dim_text(net_caps_ui_kind(strings, kind), nullptr);
    if (!signed_build)
    {
        retro_ui_same_line();
        retro_ui_text_colored(0xFFF0A8B4u, piccolo_text(strings, Str::unsigned_build));   // the red chip's text
    }
}

// PERMISSIONS GRANTED ON <NAME>: a list (or WAITING FOR <NAME> until it says).
static void draw_granted_there(const Localization* strings, const char* name, NetCaps caps, bool known)
{
    draw_prefixed(piccolo_text(strings, Str::pair_granted_on), name);
    if (!known)
    {
        draw_prefixed(piccolo_text(strings, Str::pair_waiting_for), name);
        return;
    }
    char list[256];
    net_caps_ui_list(strings, caps, list, sizeof(list));
    retro_ui_text_fit(list, retro_ui_width_left());
}

// PAIR A DEVICE's panel, and its buttons.
static void draw_device(PairWindow* window, const PairWindowState* state, PairButtons* buttons)
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
    if (window->phase == PairDevicePhase::list)
    {
        draw_heading(piccolo_text(strings, Str::pair_discover));
        uint32_t row = 0;
        for (size_t i = 0; i < link->found.size(); i++)
        {
            const NetShareLinkFound* found = &link->found[i];
            // Only devices with something we use (an old one, offers unknown, shows).
            if (found->offers_known && (found->offers & state->uses) == 0)
            {
                continue;
            }
            const std::string* name = &found->name;
            const char* note = is_paired(link, name) ? piccolo_text(strings, Str::pair_paired)
                                                     : net_caps_ui_kind(strings, found->kind);
            bool selected = !window->enter_selected && window->selected == static_cast<int32_t>(i);
            if (retro_ui_list_item(row, name->c_str(), selected, note))
            {
                window->selected = static_cast<int32_t>(i);
                window->enter_selected = false;
            }
            row++;
        }
        // Last: ENTER A CODE (a device elsewhere shows one: GENERATE PAIRING CODE).
        if (retro_ui_list_item(row, piccolo_text(strings, Str::pair_enter_code), window->enter_selected, nullptr))
        {
            window->enter_selected = true;
            window->selected = -1;
        }
        bool picked = window->enter_selected || window->selected >= 0;
        add_button(buttons, "pair", piccolo_text(strings, Str::pair_button), PairWindowPress::pair, picked,
                   picked ? nullptr : piccolo_text(strings, Str::pair_select_help));
        add_button(buttons, "close", piccolo_text(strings, Str::pair_close), PairWindowPress::close, true, nullptr);
        return;
    }
    if (window->phase == PairDevicePhase::typing || window->phase == PairDevicePhase::checking)
    {
        const char* who = window->internet && window->host_name.empty() ? piccolo_text(strings, Str::pair_other_device)
                                                                         : window->host_name.c_str();
        draw_prefixed(piccolo_text(strings, Str::pair_type_shown_on), who);
        if (window->wrong)
        {
            retro_ui_dim_text(piccolo_text(strings, Str::pair_wrong_code), nullptr);
        }
        RetroUiCodeBoxes boxes = {};
        boxes.editable = window->phase == PairDevicePhase::typing;
        boxes.focus = window->focus;
        window->focus = false;
        bool enter = retro_ui_code_boxes("typed", window->typed, sizeof(window->typed), &boxes);
        // On this network the device must have popped its code up first.
        bool ready = window->internet || link->code_needed;
        bool complete = net_pair_code_valid(window->typed) && ready && window->phase == PairDevicePhase::typing;
        if (enter && complete)
        {
            buttons->enter = PairWindowPress::submit;
        }
        add_button(buttons, "submit", piccolo_text(strings, Str::pair_button), PairWindowPress::submit, complete,
                   nullptr);
        add_button(buttons, "cancel", piccolo_text(strings, Str::pair_cancel), PairWindowPress::cancel, true, nullptr);
        if (window->phase == PairDevicePhase::checking || !ready)
        {
            std::snprintf(buttons->waiting, sizeof(buttons->waiting), "%s %s", piccolo_text(strings, Str::pair_waiting_for),
                          who);
        }
        return;
    }
    // Paired: the last screen's ticks (the side that typed ticks last).
    draw_prefixed(piccolo_text(strings, Str::pair_done), window->host_name.c_str());
    draw_chips(strings, window->host_kind, window->host_signed);
    draw_heading(piccolo_text(strings, Str::pair_granted_here));
    NetCaps choices = window->host_uses & state->offers;
    if (choices == 0)
    {
        retro_ui_text(piccolo_text(strings, Str::cap_none));
    }
    else
    {
        NetCaps asked = 0;
        net_caps_ui_checkboxes("device_ticks", strings, choices, window->host_signed, &window->ticks, &asked);
        if (asked != 0)
        {
            window->ask.cap = asked;
            window->ask.name = window->host_name;
            window->ask.target = ASK_DEVICE;
        }
    }
    draw_granted_there(strings, window->host_name.c_str(), window->given, true);
    add_button(buttons, "done", piccolo_text(strings, Str::pair_done_button), PairWindowPress::done, true, nullptr);
}

// GENERATE PAIRING CODE's panel, and its buttons.
static void draw_code(PairWindow* window, const PairWindowState* state, PairButtons* buttons,
                      PairWindowAction* action)
{
    const Localization* strings = state->strings;
    PairRequest* request = &window->request;
    if (!state->network)
    {
        action->on_request = request->active;
        action->request_peer = request->peer;
        retro_ui_dim_text(piccolo_text(strings, Str::net_off), nullptr);
        add_button(buttons, "close", piccolo_text(strings, Str::pair_close), PairWindowPress::close, true, nullptr);
        return;
    }
    if (request->active)
    {
        action->on_request = true;
        action->request_peer = request->peer;
        draw_prefixed(piccolo_text(strings, Str::pair_type_this_on), request->name);
        draw_chips(strings, request->kind, request->signed_build);
        RetroUiCodeBoxes boxes = {};
        retro_ui_code_boxes("request_code", request->code, sizeof(request->code), &boxes);
        draw_heading(piccolo_text(strings, Str::pair_permissions));
        NetCaps choices = request->uses & state->offers;
        if (choices == 0)
        {
            retro_ui_text(piccolo_text(strings, Str::cap_none));
        }
        else
        {
            NetCaps asked = 0;
            if (net_caps_ui_checkboxes("request_ticks", strings, choices, request->signed_build, &request->grants, &asked))
            {
                action->request_grants = true;
            }
            if (asked != 0)
            {
                window->ask.cap = asked;
                window->ask.name = request->name;
                window->ask.target = ASK_REQUEST;
            }
        }
        add_button(buttons, "cancel", piccolo_text(strings, Str::pair_cancel), PairWindowPress::cancel, true, nullptr);
        std::snprintf(buttons->waiting, sizeof(buttons->waiting), "%s %s", piccolo_text(strings, Str::pair_waiting_for),
                      request->name);
        return;
    }
    if (window->code_done)
    {
        draw_prefixed(piccolo_text(strings, Str::pair_done), window->done_name.c_str());
        draw_chips(strings, window->done_kind, true);
        draw_heading(piccolo_text(strings, Str::pair_granted_here));
        char list[256];
        net_caps_ui_list(strings, window->done_here, list, sizeof(list));
        retro_ui_text_fit(list, retro_ui_width_left());
        draw_granted_there(strings, window->done_name.c_str(), window->done_there, window->done_there_known);
        add_button(buttons, "close", piccolo_text(strings, Str::pair_close), PairWindowPress::close, true, nullptr);
        return;
    }
    if (!window->shown.active)
    {
        action->press = PairWindowPress::show;   // a code for this mode (pair_window_apply_code)
    }
    draw_prefixed(piccolo_text(strings, Str::pair_type_this_on), piccolo_text(strings, Str::pair_other_device));
    RetroUiCodeBoxes boxes = {};
    retro_ui_code_boxes("shown_code", window->shown.code, sizeof(window->shown.code), &boxes);
    draw_heading(piccolo_text(strings, Str::pair_permissions));
    NetCaps asked = 0;
    // Nobody yet, so nothing to warn about: the ticks are kept to what the
    // device uses, and an unsigned one doesn't get FILE TRANSFER or SETTINGS
    // (net_access_show_code).
    if (net_caps_ui_checkboxes("shown_ticks", strings, state->offers, true, &window->shown.grants, &asked))
    {
        action->shown_grants = true;
    }
    add_button(buttons, "cancel", piccolo_text(strings, Str::pair_cancel), PairWindowPress::cancel, true, nullptr);
    std::snprintf(buttons->waiting, sizeof(buttons->waiting), "%s %s · %s", piccolo_text(strings, Str::pair_waiting_for),
                  piccolo_text(strings, Str::pair_other_device), piccolo_text(strings, Str::pair_wait_minutes));
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
    retro_ui_begin_disabled(code_in_progress(window) && window->mode != PairMode::device);
    if (retro_ui_begin_tab("device", piccolo_text(strings, Str::pair_mode_device),
                           select && window->mode == PairMode::device))
    {
        shown = PairMode::device;
        retro_ui_end_tab();
    }
    retro_ui_end_disabled();
    retro_ui_begin_disabled(device_in_progress(window) && window->mode != PairMode::code);
    if (retro_ui_begin_tab("code", piccolo_text(strings, Str::pair_mode_code), select && window->mode == PairMode::code))
    {
        shown = PairMode::code;
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
    window->mode = shown;
}

// The window's width, the same at every step: room for the longest fixed
// line (the language decides), never less than 40 characters.
static float content_width(const Localization* strings)
{
    float font = retro_ui_font_size();
    float width = font * 40.0f;
    const Str lines[] = {Str::pair_wrong_code, Str::pair_granted_here};
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
    {
        float line = retro_ui_text_width(piccolo_text(strings, lines[i])) + font * 2.0f;
        width = line > width ? line : width;
    }
    float device = retro_ui_text_width(piccolo_text(strings, Str::pair_mode_device));
    float code = retro_ui_text_width(piccolo_text(strings, Str::pair_mode_code));
    float tabs = ((device > code ? device : code) + font * 2.0f) * 2.0f;
    return tabs > width ? tabs : width;
}

// The panel's height, the same at every step: the tallest one (a request:
// its line, the chips, the code, PERMISSIONS and two rows of ticks) and its
// padding.
static float panel_height()
{
    return retro_ui_text_line_step() * 3.0f + retro_ui_code_boxes_height() + retro_ui_line_step() * 2.0f +
           retro_ui_font_size();
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
    // A device asking to pair: over every other window.
    bool raise = window->raise;
    window->raise = false;
    PairButtons buttons;
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        if (raise)
        {
            retro_ui_bring_window_to_front();
        }
        draw_modes(window, strings);
        action->mode = window->mode;
        if (retro_ui_begin_list("pair_panel", 0.0f, panel_height()))
        {
            if (window->mode == PairMode::device)
            {
                draw_device(window, state, &buttons);
            }
            else
            {
                draw_code(window, state, &buttons, action);
            }
        }
        retro_ui_end_list();
        // The only status line: WAITING FOR ..., blinking (on 0.6 s, off
        // 0.6 s); its line is kept when it's off or not shown.
        bool lit = std::fmod(retro_ui_seconds(), 1.2) < 0.6;
        if (buttons.waiting[0] != '\0' && lit)
        {
            retro_ui_text_fit(buttons.waiting, retro_ui_width_left());
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
        else if (buttons.enter != PairWindowPress::none)
        {
            action->press = buttons.enter;
        }
    }
    retro_ui_window_end();
    // The red question, over the window (FILE TRANSFER / SETTINGS for an
    // unsigned build): ticked only on GRANT ANYWAY.
    uint32_t target = window->ask.target;
    NetCaps granted = net_caps_ui_unsigned(&window->ask, strings);
    if (granted != 0 && target == ASK_REQUEST && window->request.active)
    {
        window->request.grants |= granted;
        action->request_grants = true;
        action->on_request = true;
        action->request_peer = window->request.peer;
    }
    else if (granted != 0 && target == ASK_DEVICE && window->phase == PairDevicePhase::done)
    {
        window->ticks |= granted;
    }
    retro_ui_pop_theme();
    if (closed)
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
    PairNotice notice = window->notice;
    uint64_t notice_ms = window->notice_ms;
    *window = {};
    window->request = request;
    window->notice = notice;
    window->notice_ms = notice_ms;
    window->open = true;
    window->mode = PairMode::device;
    window->select_mode = true;
    window->preselect = preselect != nullptr ? preselect : "";
}

static void close_device(PairWindow* window, NetShareLink* link, bool started, bool share_open)
{
    bool waiting = window->phase == PairDevicePhase::typing || window->phase == PairDevicePhase::checking;
    if (started && (waiting || (window->phase == PairDevicePhase::done && !share_open)))
    {
        net_share_link_disconnect(link);   // a code given up; or paired: FILE TRANSFER connects again when used
    }
    reset_device(window);
}

void pair_window_apply_device(PairWindow* window, const PairWindowAction* action, NetShareLink* link,
                              const NetShareLinkStatus* status, const char* beacon, bool share_open)
{
    bool started = net_share_link_started(link);
    if (action->press == PairWindowPress::close)
    {
        close_device(window, link, started, share_open);
        window->open = false;
        return;
    }
    if (action->mode != PairMode::device)
    {
        return;
    }
    switch (action->press)
    {
    case PairWindowPress::pair:
        if (window->enter_selected)
        {
            // Across the internet: the code first, then the connection.
            window->internet = true;
            window->phase = PairDevicePhase::typing;
            window->host_name.clear();
            window->focus = true;
        }
        else if (window->selected >= 0 && static_cast<size_t>(window->selected) < status->found.size())
        {
            // On this network: the device pops its code up for us to type.
            const NetShareLinkFound* found = &status->found[static_cast<size_t>(window->selected)];
            window->internet = false;
            window->phase = PairDevicePhase::typing;
            window->host_name = found->name;
            window->host_kind = found->kind;
            window->focus = true;
            if (started)
            {
                net_share_link_connect(link, found->address, true);
            }
        }
        break;
    case PairWindowPress::submit:
        if (!net_pair_code_valid(window->typed) || !started)
        {
            break;
        }
        if (window->internet && !window->seen_connecting)
        {
            net_share_link_set_beacon(link, beacon);   // as FILE TRANSFER does
            if (net_share_link_pair_beacon(link, window->typed))
            {
                window->phase = PairDevicePhase::checking;
                window->wrong = false;
            }
        }
        else if (net_share_link_enter_code(link, window->typed))
        {
            window->phase = PairDevicePhase::checking;
            window->wrong = false;
        }
        break;
    case PairWindowPress::cancel:
        close_device(window, link, started, share_open);
        break;
    case PairWindowPress::done:
        // The last screen's ticks: what the device just paired may use here
        // (told to it at once; Andrés, 2026-11-25: one pairing, both ways).
        if (started && window->device >= 0)
        {
            net_share_link_set_grants(link, static_cast<uint32_t>(window->device), window->ticks);
            SDL_Log("Pair: \"%s\" may use here: %s", window->host_name.c_str(), net_caps_name(window->ticks).c_str());
        }
        close_device(window, link, started, share_open);
        window->open = false;
        break;
    default:
        break;
    }
}

// GENERATE PAIRING CODE's own code goes (its mode left, the window closed).
static void end_shown(PairWindow* window, NetAccess* access)
{
    if (access != nullptr && window->shown.active)
    {
        net_access_end_code(access);
    }
    NetCaps grants = window->shown.grants;
    window->shown = {};
    window->shown.grants = grants;   // the ticks stay for the next code
}

// The request drawn is still the one showing (the network thread may have
// replaced it since).
static bool same_request(const PairWindow* window, const PairWindowAction* action)
{
    return action->on_request && window->request.active && window->request.peer == action->request_peer;
}

void pair_window_apply_code(PairWindow* window, const PairWindowAction* action, NetAccess* access)
{
    // Leaving GENERATE PAIRING CODE (the other tab, or closed): its code goes.
    if (window->shown.active && (action->mode != PairMode::code || action->press == PairWindowPress::close ||
                                 !window->open))
    {
        end_shown(window, access);
    }
    if (access != nullptr && action->request_grants && same_request(window, action))
    {
        net_access_set_pairing_grants(access, window->request.peer, window->request.grants);
    }
    if (access != nullptr && action->shown_grants && window->shown.active)
    {
        net_access_set_shown_grants(access, window->shown.grants);
    }
    if (action->mode != PairMode::code && action->press != PairWindowPress::close)
    {
        return;
    }
    switch (action->press)
    {
    case PairWindowPress::show:
        if (access != nullptr && !window->shown.active &&
            net_access_show_code(access, window->shown.grants, window->shown.code))
        {
            window->shown.active = true;
            window->shown.since_ms = 0;
        }
        break;
    case PairWindowPress::cancel:
        if (same_request(window, action))
        {
            if (access != nullptr)
            {
                net_access_refuse_pairing(access, window->request.peer);
            }
            window->request = {};
        }
        else
        {
            end_shown(window, access);
            window->open = false;
        }
        break;
    case PairWindowPress::close:
        if (same_request(window, action) && access != nullptr)
        {
            net_access_refuse_pairing(access, window->request.peer);   // closing a request refuses it
        }
        if (same_request(window, action))
        {
            window->request = {};
        }
        end_shown(window, access);
        window->code_done = false;
        window->open = false;
        break;
    default:
        break;
    }
}

void pair_window_take_request(PairWindow* window, const NetAccessEvent* event)
{
    PairRequest* request = &window->request;
    if (request->active && request->peer != event->peer)
    {
        return;   // one at a time
    }
    bool same = request->active && request->peer == event->peer;
    request->active = true;
    request->retry = event->retry;
    request->peer = event->peer;
    std::snprintf(request->name, sizeof(request->name), "%s", event->info.name);
    request->kind = event->info.kind;
    request->signed_build = event->signed_build;
    request->uses = event->info.uses;
    std::snprintf(request->code, sizeof(request->code), "%s", event->code);
    if (!same)
    {
        request->grants = event->caps;   // the default ticks, until the user changes them
    }
    request->fresh = true;
}

static bool expired(uint64_t since_ms, uint64_t now_ms)
{
    return since_ms != 0 && now_ms >= since_ms && now_ms - since_ms >= PAIR_WINDOW_WAIT_MS;
}

void pair_window_expire(PairWindow* window, NetShareLink* link, NetAccess* access, uint64_t now_ms)
{
    if ((window->phase == PairDevicePhase::typing || window->phase == PairDevicePhase::checking) &&
        expired(window->device_since_ms, now_ms))
    {
        SDL_Log("Pair: waiting for the code timed out");
        close_device(window, link, net_share_link_started(link), true);
        set_notice(window, PairNotice::timed_out, now_ms);
    }
    if (window->request.active && expired(window->request.since_ms, now_ms))
    {
        // Refused: the device stops asking (it may ask again).
        SDL_Log("Pair: the request from \"%s\" timed out", window->request.name);
        if (access != nullptr)
        {
            net_access_refuse_pairing(access, window->request.peer);
        }
        window->request = {};
        input_hold_reset(&window->accept_hold);
        set_notice(window, PairNotice::timed_out, now_ms);
    }
    if (window->shown.active && expired(window->shown.since_ms, now_ms))
    {
        SDL_Log("Pair: the pairing code timed out");
        end_shown(window, access);
        window->open = false;
        set_notice(window, PairNotice::timed_out, now_ms);
    }
}

bool pair_window_hold_accept(PairWindow* window, bool in_game, bool held, uint64_t now_ms)
{
    if (!window->request.active || window->open || !in_game)
    {
        input_hold_reset(&window->accept_hold);   // no card: nothing to hold for
        return false;
    }
    // As long as HOLD START TO REMAP's hold.
    if (!input_hold_update(&window->accept_hold, held, now_ms, REMAP_WIDGET_HOLD_MS))
    {
        return false;
    }
    input_hold_reset(&window->accept_hold);
    open_on_request(window);
    return true;
}

constexpr uint64_t NOTICE_CARD_MS = 2500;      // PAIRING TIMED OUT / FAILED stays this long (as the DVR's)...
constexpr uint64_t NOTICE_CARD_FADE_MS = 500;  // ...then fades out

static void add_notice_card(const PairWindow* window, const Localization* strings, HudStack* stack, uint64_t now_ms)
{
    uint64_t since = window->notice_ms;
    if (window->notice == PairNotice::none || since == 0 || now_ms < since ||
        now_ms - since >= NOTICE_CARD_MS + NOTICE_CARD_FADE_MS)
    {
        return;
    }
    HudCard card = {};
    hud_view_card_text(&card, piccolo_text(strings, window->notice == PairNotice::timed_out ? Str::pair_timed_out
                                                                                             : Str::pair_failed));
    card.since_ms = since;
    if (now_ms - since > NOTICE_CARD_MS)
    {
        card.alpha = 1.0f - static_cast<float>(now_ms - since - NOTICE_CARD_MS) / static_cast<float>(NOTICE_CARD_FADE_MS);
    }
    hud_view_add_card(stack, &card);
}

void pair_window_add_hud(const PairWindow* window, const Localization* strings, bool in_game, HudStack* stack,
                         uint64_t now_ms)
{
    add_notice_card(window, strings, stack, now_ms);
    const PairRequest* request = &window->request;
    if (!request->active || window->open || !in_game)
    {
        return;
    }
    // <NAME> WANTS TO PAIR / HOLD [START] TO ACCEPT, the key lit as it's held.
    HudCard card = {};
    // (A long name is cut: the card clips its title at its edge anyway.)
    int written = std::snprintf(card.title, sizeof(card.title), "%s %s", request->name,
                                piccolo_text(strings, Str::pair_wants));
    if (written < 0)
    {
        card.title[0] = '\0';
    }
    std::snprintf(card.before, sizeof(card.before), "%s", piccolo_text(strings, Str::remap_card_hold));
    std::snprintf(card.key, sizeof(card.key), "%s", piccolo_text(strings, Str::input_action_pad_start));
    std::snprintf(card.after, sizeof(card.after), "%s", piccolo_text(strings, Str::pair_card_accept));
    const InputHold* hold = &window->accept_hold;
    if (hold->since_ms != 0 && now_ms >= hold->since_ms)
    {
        float fill = static_cast<float>(now_ms - hold->since_ms) / static_cast<float>(REMAP_WIDGET_HOLD_MS);
        card.key_fill = fill > 1.0f ? 1.0f : fill;
    }
    card.since_ms = request->since_ms != 0 ? request->since_ms : now_ms;
    hud_view_add_card(stack, &card);
}

void pair_window_paired(PairWindow* window, const NetAccessEvent* event, const char* name)
{
    bool from_request = window->request.active && window->request.peer == event->peer;
    // Not the request's: the shown code's (it ends once a device paired with it).
    bool from_shown = !from_request && window->shown.active;
    if (!from_request && !from_shown)
    {
        return;
    }
    window->request = {};
    window->shown.active = false;
    window->code_done = true;
    window->done_name = name;
    window->done_kind = event->info.kind;
    window->done_here = event->caps;
    window->done_there = 0;
    window->done_there_known = false;
    window->done_peer = event->peer;
    if (!window->open)
    {
        reset_device(window);
        window->open = true;
    }
    window->mode = PairMode::code;
    window->select_mode = true;
}

void pair_window_granted(PairWindow* window, const NetAccessEvent* event)
{
    if (window->code_done && event->peer == window->done_peer)
    {
        window->done_there = event->caps;
        window->done_there_known = true;
    }
}

void pair_window_code_expired(PairWindow* window, uint64_t now_ms)
{
    if (!window->shown.active)
    {
        return;
    }
    NetCaps grants = window->shown.grants;
    window->shown = {};
    window->shown.grants = grants;
    if (window->mode == PairMode::code && !window->request.active && !window->code_done)
    {
        window->open = false;
    }
    set_notice(window, PairNotice::timed_out, now_ms);
}

void pair_window_network_stopped(PairWindow* window)
{
    window->request = {};
    NetCaps grants = window->shown.grants;
    window->shown = {};
    window->shown.grants = grants;
}

void pair_window_request_gone(PairWindow* window, uint32_t peer)
{
    if (window->request.active && window->request.peer == peer)
    {
        window->request = {};
    }
}
