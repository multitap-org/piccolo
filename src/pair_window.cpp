#include "pair_window.h"

#include <cstdio>
#include <cstring>

#include <SDL3/SDL_log.h>

#include "net_access.h"
#include "net_window.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"

static const Str LEVEL_NAMES[NET_ACCESS_LEVEL_COUNT] = {Str::net_access_gamepad, Str::net_access_stream,
                                                        Str::net_access_share, Str::net_access_full};

// ---------------------------------------------------------------------------
// State

void pair_window_update(PairWindow* window, const NetShareLinkStatus* link)
{
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
        // The outcome, from SHARE's connection.
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
            window->phase = PairWindowPhase::list;   // refused, gone, or cancelled on the other side
            window->failed = true;
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
                       bool can_give_back)
{
    state->strings = strings;
    state->link = link;
    state->can_give_back = can_give_back;
}

// ---------------------------------------------------------------------------
// Drawing

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

// The code in three groups ("482 193 706"), in the section color.
static void draw_code(const char* code)
{
    char grouped[16] = {};
    std::snprintf(grouped, sizeof(grouped), "%.3s %.3s %.3s", code, code + 3, code + 6);
    retro_ui_push_section_text();
    retro_ui_text(grouped);
    retro_ui_pop_section_text();
}

static void draw_list(PairWindow* window, const PairWindowState* state, PairWindowAction* action)
{
    const Localization* strings = state->strings;
    const NetShareLinkStatus* link = state->link;
    retro_ui_dim_text(piccolo_text(strings, Str::pair_on_network), nullptr);
    float width = retro_ui_font_size() * 24.0f;
    retro_ui_begin_list("pair_found", width, retro_ui_text_line_step() * 4.0f);
    for (size_t i = 0; i < link->found.size(); i++)
    {
        const std::string* name = &link->found[i].name;
        const char* note = is_paired(link, name) ? piccolo_text(strings, Str::pair_paired) : nullptr;
        if (retro_ui_list_item(static_cast<uint32_t>(i), name->c_str(), window->selected == static_cast<int32_t>(i),
                               note))
        {
            window->selected = static_cast<int32_t>(i);
        }
    }
    if (link->found.empty())
    {
        retro_ui_dim_text(piccolo_text(strings, Str::share_found_none), nullptr);
    }
    retro_ui_end_list();
    retro_ui_label(piccolo_text(strings, Str::share_code));
    retro_ui_same_line();
    draw_code(window->code);
    if (window->failed)
    {
        retro_ui_dim_text(piccolo_text(strings, Str::pair_failed), nullptr);
    }
    if (retro_ui_button(piccolo_text(strings, Str::pair_internet)))
    {
        action->press = PairWindowPress::pair_internet;
    }
    retro_ui_same_line();
    retro_ui_begin_disabled(window->selected < 0);
    if (retro_ui_button(piccolo_text(strings, Str::pair_button)))
    {
        action->press = PairWindowPress::pair;
    }
    retro_ui_end_disabled();
}

static void draw_waiting(const PairWindow* window, const PairWindowState* state, PairWindowAction* action)
{
    const Localization* strings = state->strings;
    retro_ui_text_wrapped_at(piccolo_text(strings, window->internet ? Str::pair_give_code : Str::pair_type_code),
                             retro_ui_font_size() * 24.0f);
    draw_code(window->code);
    net_window_waiting_leds(piccolo_text(strings, Str::share_waiting));
    if (retro_ui_button(piccolo_text(strings, Str::net_owner_cancel)))
    {
        action->press = PairWindowPress::cancel;
    }
}

static void draw_done(const PairWindow* window, const PairWindowState* state, PairWindowAction* action)
{
    const Localization* strings = state->strings;
    const char* level = piccolo_text(strings, LEVEL_NAMES[static_cast<uint32_t>(window->given)]);
    retro_ui_text_format("%s %s: %s", piccolo_text(strings, Str::pair_done), window->host_name.c_str(), level);
    retro_ui_spacing();
    if (!state->can_give_back)
    {
        retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::pair_back_off), retro_ui_font_size() * 24.0f);
        if (retro_ui_button(piccolo_text(strings, Str::pair_ok)))
        {
            action->press = PairWindowPress::close;
        }
        return;
    }
    retro_ui_text_format("%s %s?", piccolo_text(strings, Str::pair_back_question), level);
    if (retro_ui_button(piccolo_text(strings, Str::pair_yes)))
    {
        action->press = PairWindowPress::give_back;
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::pair_no)))
    {
        action->press = PairWindowPress::close;
    }
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
    RetroUiWindow frame = {};
    frame.id = "pair_window";
    frame.title = piccolo_text(strings, Str::pair_title);
    frame.centered_on_open = true;
    bool closed = false;
    retro_ui_push_theme(pixel_font, scale);
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        if (state->link == nullptr)
        {
            retro_ui_dim_text(piccolo_text(strings, Str::share_network_off), nullptr);
        }
        else if (window->phase == PairWindowPhase::list)
        {
            draw_list(window, state, action);
        }
        else if (window->phase == PairWindowPhase::waiting)
        {
            draw_waiting(window, state, action);
        }
        else
        {
            draw_done(window, state, action);
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
    *window = {};
    window->open = true;
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
        net_share_link_disconnect(link);   // paired: SHARE connects again when it's used
    }
    window->open = false;
}

void pair_window_apply(PairWindow* window, const PairWindowAction* action, NetShareLink* link,
                       const NetShareLinkStatus* status, const char* beacon, NetService* service, bool share_open)
{
    bool started = net_share_link_started(link);
    switch (action->press)
    {
    case PairWindowPress::none:
        break;
    case PairWindowPress::pair:
        window->internet = false;
        window->phase = PairWindowPhase::waiting;
        if (started && window->selected >= 0 && static_cast<size_t>(window->selected) < status->found.size())
        {
            net_share_link_connect(link, status->found[static_cast<size_t>(window->selected)].address, window->code);
        }
        break;
    case PairWindowPress::pair_internet:
        window->internet = true;
        window->phase = PairWindowPhase::waiting;
        if (started)
        {
            net_share_link_set_beacon(link, beacon);   // as SHARE does
            net_share_link_pair_beacon(link, window->code);
        }
        break;
    case PairWindowPress::cancel:
        window->phase = PairWindowPhase::list;
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
    case PairWindowPress::close:
        close_window(window, link, started, share_open);
        break;
    }
}
