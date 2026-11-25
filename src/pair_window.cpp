#include "pair_window.h"

#include <cstdio>
#include <cstring>

#include "net_window.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"

static const Str LEVEL_NAMES[NET_ACCESS_LEVEL_COUNT] = {Str::net_access_gamepad, Str::net_access_stream,
                                                        Str::net_access_share, Str::net_access_full};

static std::string preselect_name;   // pair_window_open's, until the list shows it

void pair_window_open(PairWindow* window, const char* preselect)
{
    *window = {};
    window->open = true;
    net_pair_code_generate(window->code);
    preselect_name = preselect != nullptr ? preselect : "";
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

// The code in three groups ("482 193 706"), in the section color.
static void draw_code(const char* code)
{
    char grouped[16] = {};
    std::snprintf(grouped, sizeof(grouped), "%.3s %.3s %.3s", code, code + 3, code + 6);
    retro_ui_push_section_text();
    retro_ui_text(grouped);
    retro_ui_pop_section_text();
}

static void draw_list(PairWindow* window, const PairWindowContext* context)
{
    const Localization* strings = context->strings;
    const NetShareLinkStatus* link = context->link;
    retro_ui_dim_text(piccolo_text(strings, Str::pair_on_network), nullptr);
    if (window->selected >= static_cast<int32_t>(link->found.size()))
    {
        window->selected = -1;
    }
    float width = retro_ui_font_size() * 24.0f;
    retro_ui_begin_list("pair_found", width, retro_ui_text_line_step() * 4.0f);
    for (size_t i = 0; i < link->found.size(); i++)
    {
        const std::string* name = &link->found[i].name;
        if (!preselect_name.empty() && *name == preselect_name)
        {
            window->selected = static_cast<int32_t>(i);
            preselect_name.clear();
        }
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
        window->want_pair_internet = true;
        window->internet = true;
        window->phase = PairWindowPhase::waiting;
    }
    retro_ui_same_line();
    retro_ui_begin_disabled(window->selected < 0);
    if (retro_ui_button(piccolo_text(strings, Str::pair_button)))
    {
        window->want_pair = true;
        window->internet = false;
        window->phase = PairWindowPhase::waiting;
    }
    retro_ui_end_disabled();
}

static void draw_waiting(PairWindow* window, const PairWindowContext* context)
{
    const Localization* strings = context->strings;
    const NetShareLinkStatus* link = context->link;
    if (!link->code.empty())
    {
        std::snprintf(window->code, sizeof(window->code), "%s", link->code.c_str());   // a new one after a wrong try
    }
    retro_ui_text_wrapped_at(piccolo_text(strings, window->internet ? Str::pair_give_code : Str::pair_type_code),
                             retro_ui_font_size() * 24.0f);
    draw_code(window->code);
    net_window_waiting_leds(piccolo_text(strings, Str::share_waiting));
    if (retro_ui_button(piccolo_text(strings, Str::net_owner_cancel)))
    {
        window->want_cancel = true;
        window->phase = PairWindowPhase::list;
        net_pair_code_generate(window->code);
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
    else if (window->seen_connecting && link->state != NetShareLinkState::ready &&
             !window->want_pair && !window->want_pair_internet)
    {
        window->phase = PairWindowPhase::list;   // refused, gone, or cancelled on the other side
        window->failed = true;
        window->seen_connecting = false;
        net_pair_code_generate(window->code);
    }
}

static void draw_done(PairWindow* window, const PairWindowContext* context, bool* keep)
{
    const Localization* strings = context->strings;
    const char* level = piccolo_text(strings, LEVEL_NAMES[static_cast<uint32_t>(window->given)]);
    retro_ui_text_format("%s %s: %s", piccolo_text(strings, Str::pair_done), window->host_name.c_str(), level);
    if (context->link->access != window->given)
    {
        window->given = context->link->access;   // the host's welcome came after ready
    }
    retro_ui_spacing();
    if (!context->can_give_back)
    {
        retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::pair_back_off), retro_ui_font_size() * 24.0f);
        if (retro_ui_button(piccolo_text(strings, Str::pair_ok)))
        {
            *keep = false;
        }
        return;
    }
    retro_ui_text_format("%s %s?", piccolo_text(strings, Str::pair_back_question), level);
    if (retro_ui_button(piccolo_text(strings, Str::pair_yes)))
    {
        window->want_give_back = true;
        *keep = false;
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::pair_no)))
    {
        *keep = false;
    }
}

void pair_window_draw(PairWindow* window, const PairWindowContext* context, ImFont* pixel_font, int32_t scale)
{
    if (!window->open)
    {
        return;
    }
    const Localization* strings = context->strings;
    RetroUiWindow frame = {};
    frame.id = "pair_window";
    frame.title = piccolo_text(strings, Str::pair_title);
    frame.centered_on_open = true;
    bool keep = true;
    bool closed = false;
    retro_ui_push_theme(pixel_font, scale);
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        if (closed)
        {
            keep = false;
        }
        if (context->link == nullptr)
        {
            retro_ui_dim_text(piccolo_text(strings, Str::share_network_off), nullptr);
        }
        else if (window->phase == PairWindowPhase::list)
        {
            draw_list(window, context);
        }
        else if (window->phase == PairWindowPhase::waiting)
        {
            draw_waiting(window, context);
        }
        else
        {
            draw_done(window, context, &keep);
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
    if (!keep)
    {
        if (window->phase == PairWindowPhase::waiting)
        {
            window->want_cancel = true;   // closing kills the code
        }
        window->want_close = window->phase == PairWindowPhase::done;
        window->open = false;
    }
}
