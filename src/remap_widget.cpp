#include "remap_widget.h"

#include <cstdio>
#include <string>

#include "hud_view.h"
#include "input_window.h"
#include "piccolo_text.h"

constexpr size_t   MAX_CARDS = INPUT_PORT_COUNT * 4;
constexpr uint64_t MAX_STEP_MS = 100;   // a long frame (a load) doesn't eat the countdown

void remap_widget_create(RemapWidget* widget)
{
    widget->cards.reserve(MAX_CARDS);
    widget->answered.reserve(32);
}

void remap_widget_game_started(RemapWidget* widget)
{
    widget->cards.clear();
    widget->answered.clear();
    widget->scan = true;
}

void remap_widget_rescan(RemapWidget* widget)
{
    widget->scan = true;
}

// Who plays where (and each port's device): a number that changes when they do.
static uint64_t ports_summary(const InputSystem* input)
{
    uint64_t hash = 1469598103934665603ull;   // FNV-1a
    for (uint32_t p = 0; p < INPUT_PORT_COUNT; p++)
    {
        hash = (hash ^ (static_cast<uint64_t>(input->port_devices[p]) + 0x100u)) * 1099511628211ull;
        for (size_t m = 0; m < input->ports[p].size(); m++)
        {
            uint32_t controller = input->ports[p][m];
            uint64_t device = controller < input->controllers.size() ? input->controllers[controller].device_id : 0;
            hash = (hash ^ (static_cast<uint64_t>(controller) << 32 | device)) * 1099511628211ull;
        }
    }
    return hash;
}

static uint64_t answer_key(uint32_t controller, uint32_t port)
{
    return static_cast<uint64_t>(controller) * INPUT_PORT_COUNT + port;
}

static bool was_answered(const RemapWidget* widget, uint32_t controller, uint32_t port)
{
    uint64_t key = answer_key(controller, port);
    for (size_t i = 0; i < widget->answered.size(); i++)
    {
        if (widget->answered[i] == key)
        {
            return true;
        }
    }
    return false;
}

// REMAP's first button on this controller, as INPUT names it ("" = unbound).
static void remap_key_label(const InputSystem* input, const InputSdl* sdl, const InputLayouts* layouts,
                            uint32_t controller, char* out, size_t out_size)
{
    out[0] = '\0';
    const InputController* mapped = &input->controllers[controller];
    const InputDevice* device = input_devices_find_const(&input->devices, mapped->device_id);
    const InputSource* sources = mapped->bindings.sources[static_cast<uint32_t>(InputAction::hotkey_remap)];
    for (uint32_t s = 0; s < INPUT_SOURCES_PER_ACTION; s++)
    {
        if (sources[s].type != InputSourceType::none)
        {
            const InputProfile* profile = device != nullptr ? input_profile(input, device->guid) : nullptr;
            input_window_source_label(sdl, device, input_window_layout(input, layouts, device), profile, sources[s], out,
                                      out_size);
            return;
        }
    }
}

// A controller this widget asks about: local, connected, not a mouse, with
// REMAP bound.
static bool askable(const InputSystem* input, uint32_t controller)
{
    if (controller >= input->controllers.size())
    {
        return false;
    }
    const InputDevice* device = input_devices_find_const(&input->devices, input->controllers[controller].device_id);
    return device != nullptr && device->connected && !device->game_only && !device->retropad &&
           device->kind != InputDeviceKind::mouse;
}

static int32_t find_card(const RemapWidget* widget, uint32_t controller, uint32_t port)
{
    for (size_t i = 0; i < widget->cards.size(); i++)
    {
        if (widget->cards[i].controller == controller && widget->cards[i].port == port)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// The cards again: the undecided controllers on ports keep theirs (and its
// countdown), new ones get one, the rest go.
static void scan_ports(RemapWidget* widget, const InputSystem* input, const InputSdl* sdl, const InputLayouts* layouts,
                       uint64_t now_ms)
{
    bool keep[MAX_CARDS] = {};
    size_t old_count = widget->cards.size();
    for (uint32_t port = 0; port < INPUT_PORT_COUNT; port++)
    {
        for (size_t m = 0; m < input->ports[port].size(); m++)
        {
            uint32_t controller = input->ports[port][m];
            if (!askable(input, controller) || was_answered(widget, controller, port))
            {
                continue;
            }
            const InputCoreProfile* profile = input_active_core_profile(input, controller, port);
            if (profile != nullptr && profile->decided)
            {
                continue;
            }
            int32_t found = find_card(widget, controller, port);
            if (found >= 0)
            {
                if (static_cast<size_t>(found) < old_count)
                {
                    keep[found] = true;
                }
                continue;
            }
            RemapCard card = {};
            remap_key_label(input, sdl, layouts, controller, card.key, sizeof(card.key));
            if (card.key[0] == '\0' || widget->cards.size() >= MAX_CARDS)
            {
                continue;   // REMAP unbound on it: nothing to ask it to hold
            }
            std::snprintf(card.label, sizeof(card.label), "%s", input_controller_label(input, controller).c_str());
            card.controller = controller;
            card.port = port;
            card.since_ms = now_ms;
            widget->cards.push_back(card);
        }
    }
    // Drop the old cards nobody kept (from the back: the indexes before stay valid).
    for (size_t i = old_count; i > 0; i--)
    {
        if (!keep[i - 1] && !widget->cards[i - 1].debug)
        {
            widget->cards.erase(widget->cards.begin() + static_cast<long>(i - 1));
        }
    }
}

// 5 s without REMAP: the RetroPad as it is, remembered (the game's remap if
// it has one, else the core's), as the old prompt did.
static void keep_retropad(InputSystem* input, const RemapCard* card)
{
    std::string id = input_controller_profile_id(input, card->controller);
    uint32_t device = input->port_devices[card->port];
    bool game_has = input_core_profile(input, input->core_name.c_str(), input->game_name.c_str(), device, id) != nullptr;
    InputCoreProfile* profile =
        input_core_profile_for(input, input->core_name.c_str(), game_has ? input->game_name.c_str() : "", device, id);
    profile->decided = true;
}

RemapWidgetResult remap_widget_update(RemapWidget* widget, InputSystem* input, const InputSdl* sdl,
                                      const InputLayouts* layouts, bool game_running, bool in_game, bool headless,
                                      uint64_t now_ms)
{
    RemapWidgetResult result = {};
    uint64_t step = widget->last_ms != 0 && now_ms > widget->last_ms ? now_ms - widget->last_ms : 0;
    step = step > MAX_STEP_MS ? MAX_STEP_MS : step;
    widget->last_ms = now_ms;
    if (!game_running || headless || input->core_name.empty())
    {
        widget->cards.clear();
        widget->scan = true;
        return result;
    }
    uint64_t summary = ports_summary(input);
    if (widget->scan || summary != widget->summary)
    {
        widget->scan = false;
        widget->summary = summary;
        scan_ports(widget, input, sdl, layouts, now_ms);
    }
    for (size_t i = widget->cards.size(); i > 0; i--)
    {
        RemapCard* card = &widget->cards[i - 1];
        bool held = in_game && input_controller_action_held(input, card->controller, InputAction::hotkey_remap);
        if (input_hold_update(&card->hold, held, now_ms, REMAP_WIDGET_HOLD_MS) && !result.open_remap)
        {
            result.open_remap = true;
            result.port = card->port;
            result.controller = card->controller;
            if (!card->debug)
            {
                widget->answered.push_back(answer_key(card->controller, card->port));
            }
            widget->cards.erase(widget->cards.begin() + static_cast<long>(i - 1));
            continue;
        }
        if (in_game && !held)
        {
            card->counted_ms += step;
        }
        if (card->counted_ms >= REMAP_WIDGET_ASK_MS && card->debug)
        {
            widget->cards.erase(widget->cards.begin() + static_cast<long>(i - 1));
            continue;
        }
        if (card->counted_ms >= REMAP_WIDGET_ASK_MS)
        {
            keep_retropad(input, card);
            result.save = true;
            widget->answered.push_back(answer_key(card->controller, card->port));
            widget->cards.erase(widget->cards.begin() + static_cast<long>(i - 1));
        }
    }
    return result;
}

bool remap_widget_debug_card(RemapWidget* widget, const InputSystem* input, const InputSdl* sdl,
                             const InputLayouts* layouts, uint64_t now_ms)
{
    if (input->ports[0].empty() || widget->cards.size() >= MAX_CARDS)
    {
        return false;
    }
    RemapCard card = {};
    card.controller = input->ports[0][0];
    card.port = 0;
    card.since_ms = now_ms;
    card.debug = true;
    remap_key_label(input, sdl, layouts, card.controller, card.key, sizeof(card.key));
    if (card.key[0] == '\0')
    {
        std::snprintf(card.key, sizeof(card.key), "%s", "START");   // (the keyboard has no REMAP key)
    }
    std::snprintf(card.label, sizeof(card.label), "%s", input_controller_label(input, card.controller).c_str());
    widget->cards.push_back(card);
    return true;
}

void remap_widget_add_hud(const RemapWidget* widget, const Localization* strings, HudStack* stack, uint64_t now_ms)
{
    for (size_t i = 0; i < widget->cards.size(); i++)
    {
        const RemapCard* card = &widget->cards[i];
        HudCard hud = {};
        std::snprintf(hud.chip, sizeof(hud.chip), piccolo_text(strings, Str::remap_card_port), card->port + 1);
        std::snprintf(hud.title, sizeof(hud.title), "%s", card->label);
        uint64_t left_ms = REMAP_WIDGET_ASK_MS - (card->counted_ms < REMAP_WIDGET_ASK_MS ? card->counted_ms : REMAP_WIDGET_ASK_MS);
        std::snprintf(hud.corner, sizeof(hud.corner), "%u", static_cast<unsigned>((left_ms + 999) / 1000));
        std::snprintf(hud.before, sizeof(hud.before), "%s", piccolo_text(strings, Str::remap_card_hold));
        std::snprintf(hud.key, sizeof(hud.key), "%s", card->key);
        std::snprintf(hud.after, sizeof(hud.after), "%s", piccolo_text(strings, Str::remap_card_to_remap));
        if (card->hold.since_ms != 0 && now_ms >= card->hold.since_ms)
        {
            hud.key_fill = static_cast<float>(now_ms - card->hold.since_ms) / static_cast<float>(REMAP_WIDGET_HOLD_MS);
        }
        hud.bar = static_cast<float>(left_ms) / static_cast<float>(REMAP_WIDGET_ASK_MS);
        hud.since_ms = card->since_ms;
        hud_view_add_card(stack, &hud);
    }
}
