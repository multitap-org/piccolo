#include "map_window.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "input_sdl.h"
#include "pad_picture.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

constexpr uint64_t OFFER_MS = 15000;      // NOT NOW by itself after this long
constexpr uint64_t START_MS = 10000;      // a step's first page STARTs by itself after this long (untouched)
constexpr uint64_t QUESTION_MS = 15000;   // a STEP 4 question: NO by itself after this long

// The window's size and where the controller drawing sits, in UI blocks:
// always the same place, whatever the text above it.
constexpr float WINDOW_WIDTH = 320.0f;
constexpr float WINDOW_HEIGHT = 252.0f;
constexpr float PAD_TOP = 112.0f;
constexpr float PAD_UNIT = 1.25f;

// The names of the controls a layout doesn't name: the d-pad and the
// sticks (and any button a layout leaves without a label).
static const Str DEFAULT_LABELS[INPUT_MAP_CONTROL_COUNT] = {
    Str::map_bottom, Str::map_right, Str::map_left, Str::map_top, Str::map_dpad_up, Str::map_dpad_down,
    Str::map_dpad_left, Str::map_dpad_right, Str::map_title, Str::map_title, Str::map_title, Str::map_title,
    Str::map_title, Str::map_title, Str::map_title, Str::map_title, Str::map_guide, Str::map_left_x, Str::map_left_y,
    Str::map_right_x, Str::map_right_y, Str::map_paddle_1, Str::map_paddle_2, Str::map_paddle_3, Str::map_paddle_4,
    Str::map_other_button, Str::map_other_button, Str::map_other_button, Str::map_other_button, Str::map_l4, Str::map_r4,
};

// STEP 3: the actions each role binds, in the order asked.
struct MapRole
{
    Str         label;
    InputAction action;
    InputAction also;       // the same button goes here too (count = nothing more)
    bool        optional;   // a pad may not have it (shoulders for the pages)
};

static const MapRole ROLES[] = {
    {Str::map_role_up, InputAction::ui_up, InputAction::count, false},
    {Str::map_role_down, InputAction::ui_down, InputAction::count, false},
    {Str::map_role_left, InputAction::ui_left, InputAction::count, false},
    {Str::map_role_right, InputAction::ui_right, InputAction::count, false},
    {Str::map_role_page_left, InputAction::ui_page_left, InputAction::count, true},
    {Str::map_role_page_right, InputAction::ui_page_right, InputAction::count, true},
    {Str::map_role_accept, InputAction::ui_accept, InputAction::count, false},
    {Str::map_role_back, InputAction::ui_back, InputAction::count, false},
    // The main menu: opens it in the interface, and with HOTKEY ENABLE in a game.
    {Str::map_role_menu, InputAction::ui_open_menu, InputAction::hotkey_menu, false},
    {Str::map_role_modifier, InputAction::hotkey_enable, InputAction::count, false},
    {Str::map_role_quick_menu, InputAction::hotkey_quick_menu, InputAction::count, false},
};
constexpr uint32_t ROLE_COUNT = sizeof(ROLES) / sizeof(ROLES[0]);
static_assert(ROLE_COUNT <= MAP_WINDOW_ROLES_MAX, "MAP_WINDOW_ROLES_MAX too small");

static const Str MODE_NAMES[] = {Str::hotkey_mode_hold, Str::hotkey_mode_one, Str::hotkey_mode_stays};
static const InputHotkeyMode MODES[] = {InputHotkeyMode::hold, InputHotkeyMode::one_hotkey, InputHotkeyMode::stays_on};

constexpr uint32_t YELLOW = 0xFFFFDC50u;   // like the lit pad

static const InputLayout* picked_layout(const MapWindow* window, const InputLayouts* layouts)
{
    const InputLayout* layout = input_layouts_find(layouts, window->layout.c_str());
    return layout != nullptr ? layout : input_layouts_find(layouts, "Generic/generic");
}

// ---------------------------------------------------------------------------
// Opening, SDL's mapping, closing
// ---------------------------------------------------------------------------

// Everything a run of the wizard decides, back to nothing (opening, RESTART).
static void forget_choices(MapWindow* window)
{
    window->skip_type = false;
    window->skip_buttons = false;
    window->roles_done = false;
    for (uint32_t r = 0; r < MAP_WINDOW_ROLES_MAX; r++)
    {
        window->role_sources[r] = InputSource{};
    }
    window->pending_mapping.clear();
    window->interface_intro_done = false;
    window->extras_part = MapStage::closed;
    window->extras_mapped = 0;
    window->shoulders_mapped = false;
    window->others_total = 0;
    window->layout = window->initial_layout;
    window->hotkey_mode = window->initial_hotkey_mode;
}

// SDL's mapping as it was when the wizard opened (nothing made is kept).
static void restore_mapping(MapWindow* window)
{
    if (window->mapping_changed)
    {
        SDL_SetGamepadMapping(static_cast<SDL_JoystickID>(window->joystick_id),
                              window->original_mapping.empty() ? nullptr : window->original_mapping.c_str());
        window->mapping_changed = false;
    }
    window->pending_mapping.clear();
}

// The mapping made, given to SDL now (not kept until FINISH).
static void use_mapping(MapWindow* window, const std::string& line)
{
    if (SDL_SetGamepadMapping(static_cast<SDL_JoystickID>(window->joystick_id), line.c_str()))
    {
        window->pending_mapping = line;
        window->mapping_changed = true;
    }
    else
    {
        SDL_Log("Mapping refused (%s): %s", SDL_GetError(), line.c_str());
    }
}

// ---------------------------------------------------------------------------
// What FINISH keeps
// ---------------------------------------------------------------------------

// STEP 3's buttons as changes to the profile's bindings (and to the
// controller's own, if it is in use). Each role gets the button pressed for
// it and nothing else; a skipped one keeps its default.
static void apply_roles(MapWindow* window, InputSystem* input)
{
    InputProfile* profile = input_profile_for(input, window->guid);
    for (uint32_t r = 0; r < ROLE_COUNT; r++)
    {
        InputSource source = window->role_sources[r];
        InputBindingOverride change = {};
        uint32_t count = 0;
        if (source.type != InputSourceType::none)
        {
            change.sources[count++] = source;
        }
        else if (!ROLES[r].optional)
        {
            continue;   // skipped: the default stays
        }
        InputAction targets[2] = {ROLES[r].action, ROLES[r].also};
        for (uint32_t t = 0; t < 2 && targets[t] != InputAction::count; t++)
        {
            change.action = targets[t];
            bool found = false;
            for (size_t o = 0; o < profile->overrides.size() && !found; o++)
            {
                if (profile->overrides[o].action == targets[t])
                {
                    profile->overrides[o] = change;
                    found = true;
                }
            }
            if (!found)
            {
                profile->overrides.push_back(change);
            }
            for (size_t c = 0; c < input->controllers.size(); c++)
            {
                if (input_controller_profile_id(input, static_cast<uint32_t>(c)) != window->guid)
                {
                    continue;
                }
                input_unbind_all(&input->controllers[c].bindings, targets[t]);
                for (uint32_t s = 0; s < count; s++)
                {
                    input_bind(&input->controllers[c].bindings, targets[t], change.sources[s]);
                }
            }
        }
    }
}

// FINISH: the profile gets what the wizard made; SDL keeps the mapping.
static void finish(MapWindow* window, InputSystem* input, const Localization* strings)
{
    InputProfile* profile = input_profile_for(input, window->guid);
    if (!window->pending_mapping.empty())
    {
        profile->sdl_mapping = window->pending_mapping;
        window->mapping_changed = false;   // stays in SDL
    }
    if (!window->skip_type)
    {
        profile->appearance = window->layout;
        profile->hotkey_mode = window->hotkey_mode;
        for (size_t c = 0; c < input->controllers.size(); c++)
        {
            if (input_controller_profile_id(input, static_cast<uint32_t>(c)) == window->guid)
            {
                input->controllers[c].hotkey_mode = window->hotkey_mode;
            }
        }
    }
    if (window->roles_done)
    {
        apply_roles(window, input);
    }
    if (window->shoulders_mapped)
    {
        // misc5 / misc6 as the extra shoulders: named so (unless named already).
        std::string* l4 = &profile->button_labels[static_cast<uint32_t>(InputButton::misc5)];
        std::string* r4 = &profile->button_labels[static_cast<uint32_t>(InputButton::misc6)];
        *l4 = l4->empty() ? std::string(piccolo_text(strings, Str::map_l4)) : *l4;
        *r4 = r4->empty() ? std::string(piccolo_text(strings, Str::map_r4)) : *r4;
    }
    window->profiles_dirty = true;
    char text[300] = {};
    std::snprintf(text, sizeof(text), piccolo_text(strings, Str::map_done), window->name.c_str());
    window->result = text;
    map_window_close(window, input);
}

static void not_now(MapWindow* window, InputSystem* input)
{
    InputProfile* profile = input_profile_for(input, window->guid);
    if (profile->appearance.empty())
    {
        profile->appearance = "none";   // asked: not again
        window->profiles_dirty = true;
    }
    map_window_close(window, input);
}

// ---------------------------------------------------------------------------
// The mapper
// ---------------------------------------------------------------------------

static void go(MapWindow* window, uint64_t now_ms, MapStage stage)
{
    window->stage = stage;
    window->page_started_ms = now_ms;
}

// A press of any button on the controller being mapped, for pages it can
// answer that way: one SDL doesn't know can't drive the interface. Each page
// starts from everything let go (the press that opened it doesn't count).
static bool raw_press(MapWindow* window)
{
    if (window->press_stage != window->stage)
    {
        window->press_stage = window->stage;
        input_sdl_press_wait_release(&window->press);
    }
    return input_sdl_press_poll(&window->press, window->joystick_id);
}

// Starts the mapper on STEP 2's positions (the layout's), or on none (STEP 2
// skipped, or STEP 3 again: SDL's mapping now is loaded, so STEP 3 finds its
// buttons there), then STEP 3's roles. The router is suspended from then on.
static void start_mapper(MapWindow* window, InputSystem* input, const InputLayouts* layouts, uint64_t now_ms,
                         bool positions)
{
    const InputLayout* layout = picked_layout(window, layouts);
    bool wanted[INPUT_MAP_CONTROL_COUNT] = {};
    for (uint32_t c = 0; positions && c < static_cast<uint32_t>(INPUT_MAP_FIRST_EXTRA); c++)
    {
        wanted[c] = layout == nullptr || input_layout_has(layout, static_cast<InputMapControl>(c));
    }
    wanted[static_cast<uint32_t>(InputMapControl::guide)] = false;   // GUIDE / HOME: STEP 4's
    bool optional[ROLE_COUNT] = {};
    for (uint32_t r = 0; r < ROLE_COUNT; r++)
    {
        optional[r] = ROLES[r].optional;
    }
    if (!input_sdl_mapper_begin(&window->mapper, window->joystick_id, ROLE_COUNT, optional, wanted, now_ms))
    {
        map_window_close(window, input);   // went away
        return;
    }
    window->mapper.hold_ms = input->settings.hold_ms;   // SETTINGS > INPUT
    window->mapper.optional_wait_ms = input->settings.press_ms;
    if (!positions)
    {
        char* mapping = SDL_GetGamepadMappingForID(static_cast<SDL_JoystickID>(window->joystick_id));
        input_sdl_mapper_load(&window->mapper, mapping);
        SDL_free(mapping);
    }
    window->interface_intro_done = false;
    go(window, now_ms, MapStage::mapping);
    input->suspended = true;   // its presses are the mapping's alone
}

// After STEP 3: STEP 4 from SETTINGS, else the summary.
static void after_interface(MapWindow* window, InputSystem* input, uint64_t now_ms)
{
    input_sdl_mapper_cancel(&window->mapper);
    input->suspended = false;
    go(window, now_ms, window->offered ? MapStage::summary : MapStage::extras_guide);
}

// STEP 3 done: its buttons kept for FINISH.
static void roles_done(MapWindow* window, InputSystem* input, uint64_t now_ms)
{
    for (uint32_t r = 0; r < ROLE_COUNT; r++)
    {
        window->role_sources[r] = input_sdl_mapper_role_source(&window->mapper, r);
    }
    window->roles_done = true;
    after_interface(window, input, now_ms);
}

// A wizard BACK into STEP 2: what it made goes (SDL's mapping as it was).
static void back_to_buttons(MapWindow* window, InputSystem* input, uint64_t now_ms)
{
    input_sdl_mapper_cancel(&window->mapper);
    input->suspended = false;
    restore_mapping(window);
    window->skip_buttons = false;
    go(window, now_ms, MapStage::intro_buttons);
}

// The STEP 4 part after this one (its question's stage), the summary after the last.
static MapStage next_part(MapStage part)
{
    switch (part)
    {
        case MapStage::extras_guide:     return MapStage::extras_paddles;
        case MapStage::extras_paddles:   return MapStage::extras_shoulders;
        case MapStage::extras_shoulders: return MapStage::extras_others;
        default:                         return MapStage::summary;
    }
}

static bool mapping_has(const char* mapping, const char* field)
{
    return mapping != nullptr && std::strstr(mapping, field) != nullptr;
}

// A STEP 4 part answered YES: its inputs, added to the mapping SDL has now.
static void start_extras(MapWindow* window, InputSystem* input, uint64_t now_ms, MapStage part)
{
    char* mapping = SDL_GetGamepadMappingForID(static_cast<SDL_JoystickID>(window->joystick_id));
    bool wanted[INPUT_MAP_CONTROL_COUNT] = {};
    InputMapControl first = InputMapControl::count;
    window->others_total = 0;
    switch (part)
    {
        case MapStage::extras_guide:
            wanted[static_cast<uint32_t>(InputMapControl::guide)] = true;
            break;
        case MapStage::extras_paddles:
            for (uint32_t c = static_cast<uint32_t>(InputMapControl::left_paddle1);
                 c <= static_cast<uint32_t>(InputMapControl::right_paddle2); c++)
            {
                wanted[c] = true;
            }
            first = InputMapControl::left_paddle1;   // a pad with two paddles: ends when the third doesn't come
            break;
        case MapStage::extras_shoulders:
            wanted[static_cast<uint32_t>(InputMapControl::l4)] = true;
            wanted[static_cast<uint32_t>(InputMapControl::r4)] = true;
            break;
        default:
        {
            // Other buttons: misc1..4, and misc5 / misc6 (L4 / R4's) when nothing uses them.
            bool shoulders_free = !window->shoulders_mapped && !mapping_has(mapping, "misc5:") && !mapping_has(mapping, "misc6:");
            for (uint32_t c = static_cast<uint32_t>(InputMapControl::misc1); c <= static_cast<uint32_t>(InputMapControl::r4); c++)
            {
                bool shoulder = c >= static_cast<uint32_t>(InputMapControl::l4);
                wanted[c] = !shoulder || shoulders_free;
                window->others_total += wanted[c] ? 1u : 0u;
            }
            first = InputMapControl::misc1;   // until one doesn't come
            break;
        }
    }
    if (!input_sdl_mapper_begin(&window->mapper, window->joystick_id, 0, nullptr, wanted, now_ms))
    {
        SDL_free(mapping);
        map_window_close(window, input);   // went away
        return;
    }
    window->mapper.hold_ms = input->settings.hold_ms;
    window->mapper.optional_wait_ms = input->settings.press_ms;
    window->mapper.end_on_skip_from = first;
    input_sdl_mapper_load(&window->mapper, mapping);
    SDL_free(mapping);
    window->extras_part = part;
    go(window, now_ms, MapStage::extras_mapping);
    input->suspended = true;
}

// A STEP 4 part's inputs done: into the mapping SDL uses now; the next part.
static void extras_done(MapWindow* window, InputSystem* input, uint64_t now_ms)
{
    uint32_t mapped = 0;
    for (uint32_t c = 0; c < INPUT_MAP_CONTROL_COUNT; c++)
    {
        if (window->mapper.wanted[c] && !window->mapper.elements[c].empty())
        {
            mapped++;
        }
    }
    std::string line;
    if (mapped > 0 && input_sdl_mapper_line(&window->mapper, &line))
    {
        use_mapping(window, line);
        window->extras_mapped += mapped;
        if (window->extras_part == MapStage::extras_shoulders)
        {
            window->shoulders_mapped = true;
        }
    }
    input_sdl_mapper_cancel(&window->mapper);
    input->suspended = false;
    go(window, now_ms, next_part(window->extras_part));
}

// ---------------------------------------------------------------------------
// State

bool map_window_open_now(const MapWindow* window)
{
    return window->stage != MapStage::closed;
}

void map_window_update(MapWindow* window, InputSystem* input, uint64_t now_ms)
{
    window->pressed = false;
    if (window->stage == MapStage::mapping)
    {
        // STEP 2's inputs, then STEP 3's.
        if (input_sdl_mapper_update(&window->mapper, now_ms))
        {
            roles_done(window, input, now_ms);
        }
        else if (!window->mapper.active)
        {
            map_window_close(window, input);   // went away
        }
        else if (input_sdl_mapper_role(&window->mapper) >= 0 && !window->interface_intro_done)
        {
            // STEP 2 is done: its mapping, given to SDL now (kept on FINISH).
            std::string line;
            if (!window->skip_buttons && input_sdl_mapper_line(&window->mapper, &line))
            {
                use_mapping(window, line);
            }
            go(window, now_ms, MapStage::intro_interface);
        }
    }
    else if (window->stage == MapStage::extras_mapping)
    {
        // A STEP 4 part's inputs.
        if (input_sdl_mapper_update(&window->mapper, now_ms))
        {
            extras_done(window, input, now_ms);
        }
        else if (!window->mapper.active)
        {
            map_window_close(window, input);   // went away
        }
    }
    // Any button on the controller is the page's main button where it can
    // answer that way (STEP 1 only for one SDL doesn't know).
    bool answers = window->stage == MapStage::offer || (window->stage == MapStage::type && window->needed) ||
                   window->stage == MapStage::intro_buttons || window->stage == MapStage::intro_interface;
    if (answers)
    {
        window->pressed = raw_press(window);
    }
}

void map_window_state(MapWindowState* state, const MapWindow* window, const Localization* strings,
                      const InputLayouts* layouts, uint64_t now_ms)
{
    state->strings = strings;
    state->layouts = layouts;
    state->now_ms = now_ms;
    state->is_gamepad = window->stage != MapStage::closed && SDL_IsGamepad(static_cast<SDL_JoystickID>(window->joystick_id));
}

// ---------------------------------------------------------------------------
// Drawing

static const char* control_label(const InputLayout* layout, const Localization* strings, InputMapControl control)
{
    uint32_t index = static_cast<uint32_t>(control);
    if (layout != nullptr && !layout->labels[index].empty())
    {
        return layout->labels[index].c_str();
    }
    return piccolo_text(strings, DEFAULT_LABELS[index]);
}

// ---------------------------------------------------------------------------
// Page pieces
// ---------------------------------------------------------------------------

static void wrapped(const char* text)
{
    retro_ui_text_wrapped(text);
}

static void section(const char* text)
{
    retro_ui_push_section_text();
    retro_ui_text(text);
    retro_ui_pop_section_text();
}

// The controller's name, the step's title, what it is about.
static void header(const MapWindow* window, const Localization* strings, Str title, const char* text)
{
    retro_ui_separator_text(window->name.c_str());
    section(piccolo_text(strings, title));
    if (text != nullptr)
    {
        wrapped(text);
    }
}

// The controller drawing, always in the same place.
static void pad_area(const InputLayout* layout, PadView view, InputMapControl lit, bool minus, float block)
{
    retro_ui_down_to(PAD_TOP * block);
    pad_picture_draw_view(layout, view, lit, minus, PAD_UNIT * block);
}

// The wizard's buttons: at the bottom of the window, always.
static void footer()
{
    retro_ui_down_to(retro_ui_bottom_line_y());
}

// The prompt under the drawing while a step takes its inputs: what to hold,
// the ring closing, what holding means.
static void prompt(const MapWindow* window, const MapWindowState* state, const char* text, Str hint)
{
    section(text);
    retro_ui_hold_ring(input_sdl_mapper_hold_progress(&window->mapper, state->now_ms), YELLOW);
    wrapped(piccolo_text(state->strings, hint));
}

// A step's own buttons while it takes its inputs: BACK (the previous input),
// RESET (the step's first input), SKIP (this one; counting down when it
// skips by itself), CANCEL (the whole wizard, nothing kept).
static void step_buttons(const MapWindow* window, const MapWindowState* state, MapWindowAction* action)
{
    const Localization* strings = state->strings;
    footer();
    retro_ui_begin_disabled(input_sdl_mapper_at_phase_start(&window->mapper));
    if (retro_ui_button(piccolo_text(strings, Str::map_back)))
    {
        action->press = MapWindowPress::step_back;
    }
    retro_ui_end_disabled();
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::map_reset)))
    {
        action->press = MapWindowPress::step_reset;
    }
    retro_ui_same_line();
    uint32_t left = input_sdl_mapper_seconds_left(&window->mapper, state->now_ms);
    bool skip = left > 0 ? retro_ui_countdown_button(piccolo_text(strings, Str::map_skip_n), static_cast<int>(left), "skip")
                         : retro_ui_button(piccolo_text(strings, Str::map_skip));
    if (skip)
    {
        action->press = MapWindowPress::step_skip;
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::map_cancel)))
    {
        action->press = MapWindowPress::cancel;   // nothing kept: SDL's mapping as it was
    }
}

// ---------------------------------------------------------------------------
// The pages
// ---------------------------------------------------------------------------

static void draw_offer(const MapWindow* window, const MapWindowState* state, MapWindowAction* action)
{
    const Localization* strings = state->strings;
    retro_ui_separator_text(window->name.c_str());
    wrapped(piccolo_text(strings, Str::map_offer_text));
    if (window->needed)
    {
        retro_ui_spacing();
        wrapped(piccolo_text(strings, Str::map_offer_needed));
    }
    retro_ui_spacing();
    wrapped(piccolo_text(strings, Str::map_offer_any_button));
    footer();
    // Any button on the new controller is MAP too (map_window_apply).
    if (retro_ui_button(piccolo_text(strings, Str::map_map)))
    {
        action->press = MapWindowPress::map;
        return;
    }
    retro_ui_same_line();
    if (retro_ui_countdown_button(piccolo_text(strings, Str::map_not_now),
                                  retro_ui_seconds_left(state->now_ms, window->offer_started_ms, OFFER_MS), "not_now"))
    {
        action->press = MapWindowPress::not_now;
    }
}

// STEP 1: the type (LABELS, LAYOUT) and HOTKEY MODE.
static void draw_type(MapWindow* window, const MapWindowState* state, float block, MapWindowAction* action)
{
    const Localization* strings = state->strings;
    header(window, strings, Str::map_step_type, nullptr);
    // LABELS (the folder), then LAYOUT among that folder's layouts.
    const InputLayout* layout = picked_layout(window, state->layouts);
    std::string maker = layout != nullptr ? layout->maker : std::string();
    std::vector<std::string> makers;
    input_layouts_makers(state->layouts, &makers);
    float label_width = 80.0f * block;
    retro_ui_label(piccolo_text(strings, Str::map_maker));
    retro_ui_same_line_at(label_width);
    // Picking the folder already picked goes back to its first layout too.
    if (retro_ui_begin_choice("maker", maker.c_str()))
    {
        for (size_t m = 0; m < makers.size(); m++)
        {
            if (retro_ui_choice_item_repick(static_cast<uint32_t>(m), makers[m].c_str(), makers[m] == maker))
            {
                // The folder's first layout.
                for (size_t l = 0; l < state->layouts->layouts.size(); l++)
                {
                    if (state->layouts->layouts[l].maker == makers[m])
                    {
                        window->layout = state->layouts->layouts[l].id;
                        break;
                    }
                }
            }
        }
        retro_ui_end_choice();
    }
    layout = picked_layout(window, state->layouts);
    retro_ui_label(piccolo_text(strings, Str::map_model));
    retro_ui_same_line_at(label_width);
    if (retro_ui_begin_choice("model", layout != nullptr ? layout->name.c_str() : ""))
    {
        for (size_t l = 0; layout != nullptr && l < state->layouts->layouts.size(); l++)
        {
            const InputLayout* candidate = &state->layouts->layouts[l];
            if (candidate->maker != layout->maker)
            {
                continue;
            }
            if (retro_ui_choice_item(static_cast<uint32_t>(l), candidate->name.c_str(), candidate->id == window->layout))
            {
                window->layout = candidate->id;
            }
        }
        retro_ui_end_choice();
    }
    layout = picked_layout(window, state->layouts);
    // HOTKEY MODE: how its HOTKEY ENABLE (asked in STEP 3) turns hotkeys on.
    retro_ui_label(piccolo_text(strings, Str::input_hotkey_mode));
    retro_ui_item_tooltip(piccolo_text(strings, Str::input_hotkey_mode_help));
    retro_ui_same_line_at(label_width);
    if (retro_ui_begin_choice("hotkey_mode", piccolo_text(strings, MODE_NAMES[static_cast<int>(window->hotkey_mode)])))
    {
        for (uint32_t m = 0; m < 3; m++)
        {
            if (retro_ui_choice_item(m, piccolo_text(strings, MODE_NAMES[m]), window->hotkey_mode == MODES[m]))
            {
                window->hotkey_mode = MODES[m];
            }
        }
        retro_ui_end_choice();
    }
    retro_ui_spacing();
    wrapped(piccolo_text(strings, window->needed ? Str::map_type_goes_on_any : Str::map_type_goes_on));
    pad_area(layout, PadView::front, InputMapControl::count, false, block);
    footer();
    // Choosing in a list (or opening one) starts the countdown again; the
    // buttons below don't (their press is a choice, not a touch).
    if (ui_toolkit_has_popup())
    {
        window->page_started_ms = state->now_ms;
    }
    // START by itself (untouched, or any button on one SDL doesn't know):
    // map_window_apply.
    if (retro_ui_countdown_button(piccolo_text(strings, Str::map_start),
                                  retro_ui_seconds_left(state->now_ms, window->page_started_ms, START_MS), "start"))
    {
        action->press = MapWindowPress::start;
        return;
    }
    // SKIP: keep its type (only for a controller already mapped).
    retro_ui_same_line();
    retro_ui_begin_disabled(!state->is_gamepad);
    bool skip = retro_ui_button(piccolo_text(strings, Str::map_skip));
    retro_ui_end_disabled();
    if (skip)
    {
        action->press = MapWindowPress::skip;
        return;
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::map_cancel)))
    {
        action->press = MapWindowPress::cancel;
    }
}

// STEP 2's first page: START begins its inputs.
static void draw_intro_buttons(const MapWindow* window, const MapWindowState* state, float block,
                               MapWindowAction* action)
{
    const Localization* strings = state->strings;
    header(window, strings, Str::map_step_buttons, piccolo_text(strings, Str::map_step_buttons_text));
    wrapped(piccolo_text(strings, Str::map_starts_any));
    pad_area(picked_layout(window, state->layouts), PadView::front, InputMapControl::count, false, block);
    footer();
    if (retro_ui_button(piccolo_text(strings, Str::map_back)))
    {
        action->press = MapWindowPress::back;
        return;
    }
    retro_ui_same_line();
    if (retro_ui_countdown_button(piccolo_text(strings, Str::map_start),
                                  retro_ui_seconds_left(state->now_ms, window->page_started_ms, START_MS), "start"))
    {
        action->press = MapWindowPress::start;
        return;
    }
    // SKIP: keep SDL's mapping (only one SDL knows: STEP 3 needs its buttons mapped).
    retro_ui_same_line();
    retro_ui_begin_disabled(!state->is_gamepad);
    bool skip = retro_ui_button(piccolo_text(strings, Str::map_skip));
    retro_ui_end_disabled();
    if (skip)
    {
        action->press = MapWindowPress::skip;
        return;
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::map_cancel)))
    {
        action->press = MapWindowPress::cancel;
    }
}

// STEP 3's first page (the mapper waits on the first role).
static void draw_intro_interface(const MapWindow* window, const MapWindowState* state, float block,
                                 MapWindowAction* action)
{
    const Localization* strings = state->strings;
    header(window, strings, Str::map_step_interface, piccolo_text(strings, Str::map_step_interface_text));
    wrapped(piccolo_text(strings, Str::map_starts_any));   // the router is suspended: no controller drives the page
    pad_area(picked_layout(window, state->layouts), PadView::front, InputMapControl::count, false, block);
    footer();
    if (retro_ui_button(piccolo_text(strings, Str::map_back)))
    {
        action->press = MapWindowPress::back;
        return;
    }
    retro_ui_same_line();
    if (retro_ui_countdown_button(piccolo_text(strings, Str::map_start),
                                  retro_ui_seconds_left(state->now_ms, window->page_started_ms, START_MS), "start"))
    {
        action->press = MapWindowPress::start;
        return;
    }
    // SKIP: keep its interface buttons.
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::map_skip)))
    {
        action->press = MapWindowPress::skip;
        return;
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::map_cancel)))
    {
        action->press = MapWindowPress::cancel;
    }
}

// STEP 2's inputs, then STEP 3's.
static void draw_mapping(const MapWindow* window, const MapWindowState* state, float block,
                         MapWindowAction* action)
{
    // (map_window_update has followed the mapper: STEP 3's first page, or
    // done, or gone.)
    const Localization* strings = state->strings;
    int32_t role = input_sdl_mapper_role(&window->mapper);
    const InputLayout* layout = picked_layout(window, state->layouts);
    char text[300] = {};
    header(window, strings, role >= 0 ? Str::map_step_interface : Str::map_step_buttons, nullptr);
    Str hint = Str::map_hold_hint;
    if (role >= 0)
    {
        // The drawing lights the button held now, as STEP 2 mapped it.
        InputSource held = input_sdl_mapper_held_source(&window->mapper);
        pad_area(layout, PadView::front, input_map_control_for_source(held), held.type == InputSourceType::axis_minus, block);
        std::snprintf(text, sizeof(text), piccolo_text(strings, Str::map_press_for), piccolo_text(strings, ROLES[role].label));
    }
    else
    {
        InputMapControl control = input_sdl_mapper_control(&window->mapper);
        pad_area(layout, PadView::front, control, false, block);   // the button to press, lit
        const char* label = control_label(layout, strings, control);
        if (input_map_control_is_stick(control))
        {
            std::snprintf(text, sizeof(text), "%s", label);
        }
        else
        {
            std::snprintf(text, sizeof(text), piccolo_text(strings, Str::map_press), label);
        }
    }
    prompt(window, state, text, hint);
    step_buttons(window, state, action);
}

// A STEP 4 part's first page: its question. YES enters it (its inputs),
// NO (by itself after a while) goes on to the next part.
static void draw_question(const MapWindow* window, const MapWindowState* state, float block, Str question, PadView view,
                          InputMapControl lit, MapWindowAction* action)
{
    const Localization* strings = state->strings;
    header(window, strings, Str::map_step_extras, piccolo_text(strings, question));
    pad_area(picked_layout(window, state->layouts), view, lit, false, block);
    footer();
    if (retro_ui_button(piccolo_text(strings, Str::map_back)))
    {
        action->press = MapWindowPress::back;
        return;
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::map_yes)))
    {
        action->press = MapWindowPress::yes;
        return;
    }
    retro_ui_same_line();
    // NO by itself after a while: map_window_apply.
    if (retro_ui_countdown_button(piccolo_text(strings, Str::map_no),
                                  retro_ui_seconds_left(state->now_ms, window->page_started_ms, QUESTION_MS), "no"))
    {
        action->press = MapWindowPress::no;
        return;
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::map_cancel)))
    {
        action->press = MapWindowPress::cancel;
    }
}

// Which other button this is, as small squares under the drawing (the
// current one lit), like the page indicator.
static void draw_other_squares(uint32_t current, uint32_t total)
{
    float size = static_cast<float>(static_cast<int>(retro_ui_font_size() * 0.5f));
    float gap = size;
    float width = static_cast<float>(total) * size + static_cast<float>(total - 1) * gap;
    float left = static_cast<float>(static_cast<int>((retro_ui_width_left() - width) * 0.5f));
    float spacing_below = retro_ui_line_step() - retro_ui_line_height();
    // The canvas takes the room at the line's start; the squares are drawn
    // centered on the line.
    retro_ui_canvas(width, size + spacing_below);
    for (uint32_t s = 0; s < total; s++)
    {
        float x = left + static_cast<float>(s) * (size + gap);
        uint32_t color = s == current ? YELLOW : retro_ui_palette()->text_disabled;
        retro_ui_canvas_rect(x, 0.0f, size, size, color);
    }
}

// A STEP 4 part's inputs.
static void draw_extras_mapping(const MapWindow* window, const MapWindowState* state, float block,
                                MapWindowAction* action)
{
    // (map_window_update has followed the mapper: done, or gone.)
    const Localization* strings = state->strings;
    InputMapControl control = input_sdl_mapper_control(&window->mapper);
    const InputLayout* layout = picked_layout(window, state->layouts);
    char text[300] = {};
    header(window, strings, Str::map_step_extras, nullptr);
    bool others = window->extras_part == MapStage::extras_others;
    if (others)
    {
        // Other buttons aren't on the drawing: the squares say which one.
        pad_area(layout, PadView::front, InputMapControl::count, false, block);
        uint32_t current = 0;
        for (uint32_t c = static_cast<uint32_t>(InputMapControl::misc1); c < static_cast<uint32_t>(control); c++)
        {
            current += window->mapper.wanted[c] ? 1u : 0u;
        }
        draw_other_squares(current, window->others_total);
        std::snprintf(text, sizeof(text), piccolo_text(strings, Str::map_other_n), static_cast<int>(current + 1));
    }
    else
    {
        // A paddle shows the pad from behind, L4 / R4 the extra shoulders.
        bool paddle = control == InputMapControl::left_paddle1 || control == InputMapControl::right_paddle1 ||
                      control == InputMapControl::left_paddle2 || control == InputMapControl::right_paddle2;
        bool shoulder = control == InputMapControl::l4 || control == InputMapControl::r4;
        pad_area(layout, paddle ? PadView::back : (shoulder ? PadView::extra_shoulders : PadView::front), control, false, block);
        std::snprintf(text, sizeof(text), piccolo_text(strings, Str::map_press), control_label(layout, strings, control));
    }
    prompt(window, state, text, others ? Str::map_extras_others_text : Str::map_hold_hint);
    step_buttons(window, state, action);
}

// The last page: what FINISH keeps.
static void draw_summary(const MapWindow* window, const MapWindowState* state, float block,
                         MapWindowAction* action)
{
    const Localization* strings = state->strings;
    header(window, strings, Str::map_summary, nullptr);
    float label_width = 120.0f * block;
    const char* changed = piccolo_text(strings, Str::map_summary_changed);
    const char* unchanged = piccolo_text(strings, Str::map_summary_unchanged);
    const InputLayout* layout = picked_layout(window, state->layouts);

    retro_ui_text(piccolo_text(strings, Str::map_summary_type));
    retro_ui_same_line_at(label_width);
    retro_ui_text(window->skip_type || layout == nullptr ? unchanged : layout->name.c_str());
    retro_ui_text(piccolo_text(strings, Str::map_summary_buttons));
    retro_ui_same_line_at(label_width);
    retro_ui_text(window->skip_buttons ? unchanged : changed);
    retro_ui_text(piccolo_text(strings, Str::map_summary_interface));
    retro_ui_same_line_at(label_width);
    retro_ui_text(window->roles_done ? changed : unchanged);
    if (!window->offered)
    {
        char text[64] = {};
        std::snprintf(text, sizeof(text), piccolo_text(strings, Str::map_summary_added), static_cast<int>(window->extras_mapped));
        retro_ui_text(piccolo_text(strings, Str::map_summary_extras));
        retro_ui_same_line_at(label_width);
        retro_ui_text(window->extras_mapped > 0 ? text : unchanged);
    }
    retro_ui_spacing();
    wrapped(piccolo_text(strings, Str::map_summary_text));
    footer();
    if (retro_ui_button(piccolo_text(strings, Str::map_restart)))
    {
        action->press = MapWindowPress::restart;
        return;
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::map_cancel)))
    {
        action->press = MapWindowPress::cancel;
        return;
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::map_finish)))
    {
        action->press = MapWindowPress::finish;
    }
}

// Which page of the wizard this is, as a row of small squares above the
// buttons (the current one lit): the steps, then the summary. None on OFFER.
static void draw_page_indicator(const MapWindow* window)
{
    int32_t page = -1;
    switch (window->stage)
    {
        case MapStage::type:            page = 0; break;
        case MapStage::intro_buttons:   page = 1; break;
        case MapStage::mapping:         page = input_sdl_mapper_role(&window->mapper) >= 0 ? 2 : 1; break;
        case MapStage::intro_interface: page = 2; break;
        case MapStage::extras_guide:
        case MapStage::extras_paddles:
        case MapStage::extras_shoulders:
        case MapStage::extras_others:
        case MapStage::extras_mapping:  page = 3; break;
        case MapStage::summary:         page = window->offered ? 3 : 4; break;
        default:                        break;
    }
    if (page < 0)
    {
        return;
    }
    const int32_t PAGES = window->offered ? 4 : 5;   // STEP 4 only from SETTINGS
    float size = static_cast<float>(static_cast<int>(retro_ui_font_size() * 0.75f));
    float gap = size;
    float width = static_cast<float>(PAGES) * size + static_cast<float>(PAGES - 1) * gap;
    float window_width = 0.0f;
    float window_height = 0.0f;
    retro_ui_window_size(&window_width, &window_height);
    float x = static_cast<float>(static_cast<int>((window_width - width) * 0.5f));
    // A gap between it and the buttons.
    float spacing_below = retro_ui_line_step() - retro_ui_line_height();
    float y = retro_ui_bottom_line_y() - spacing_below - size * 2.0f;
    for (int32_t p = 0; p < PAGES; p++)
    {
        float px = x + static_cast<float>(p) * (size + gap);
        uint32_t color = p == page ? YELLOW : retro_ui_palette()->text_disabled;
        retro_ui_window_rect(px, y, size, size, color);
    }
}

void map_window_draw(MapWindow* window, const MapWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                     MapWindowAction* action)
{
    if (window->stage == MapStage::closed)
    {
        return;
    }
    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    retro_ui_push_theme(pixel_font, scale);
    float width = WINDOW_WIDTH * block;
    float height = WINDOW_HEIGHT * block;
    width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    height = height < screen_height - 24.0f * block ? height : screen_height - 24.0f * block;
    // A modal of its own: always in the middle, no X (CANCEL closes it), its
    // pages laid out to fit (it never scrolls).
    RetroUiWindow frame = {};
    frame.id = "map_controller";
    frame.title = piccolo_text(state->strings, window->offered ? Str::map_detected : Str::map_title);
    frame.width = width;
    frame.height = height;
    frame.centered = true;
    frame.closable = false;
    frame.scrolls = false;
    frame.focus = window->take_focus;   // on top of the others when it opens
    window->take_focus = false;
    if (retro_ui_window_begin(&frame, scale, nullptr))
    {
        action->drawn = true;
        if (window->stage != window->drawn_stage)
        {
            if (window->drawn_stage == MapStage::closed || window->drawn_stage == MapStage::offer)
            {
                window->page_started_ms = state->now_ms;   // the first page's countdown starts on showing it
            }
            window->drawn_stage = window->stage;
        }
        draw_page_indicator(window);
        switch (window->stage)
        {
        case MapStage::offer:
            draw_offer(window, state, action);
            break;
        case MapStage::type:
            draw_type(window, state, block, action);
            break;
        case MapStage::intro_buttons:
            draw_intro_buttons(window, state, block, action);
            break;
        case MapStage::mapping:
            draw_mapping(window, state, block, action);
            break;
        case MapStage::intro_interface:
            draw_intro_interface(window, state, block, action);
            break;
        case MapStage::extras_guide:
            draw_question(window, state, block, Str::map_extras_guide, PadView::front, InputMapControl::guide, action);
            break;
        case MapStage::extras_paddles:
            draw_question(window, state, block, Str::map_extras_paddles, PadView::back, InputMapControl::count, action);
            break;
        case MapStage::extras_shoulders:
            draw_question(window, state, block, Str::map_extras_shoulders, PadView::extra_shoulders,
                          InputMapControl::count, action);
            break;
        case MapStage::extras_others:
            draw_question(window, state, block, Str::map_extras_others, PadView::front, InputMapControl::count, action);
            break;
        case MapStage::extras_mapping:
            draw_extras_mapping(window, state, block, action);
            break;
        case MapStage::summary:
            draw_summary(window, state, block, action);
            break;
        case MapStage::closed:
            break;
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}

// ---------------------------------------------------------------------------
// Apply

void map_window_offer(MapWindow* window, uint32_t joystick_id, bool needed, const InputLayouts* layouts,
                      const InputSystem* input, uint64_t now_ms)
{
    map_window_open(window, joystick_id, layouts, input);
    window->stage = MapStage::offer;
    window->offered = true;
    window->needed = needed;
    window->offer_started_ms = now_ms;
}

void map_window_open(MapWindow* window, uint32_t joystick_id, const InputLayouts* layouts, const InputSystem* input)
{
    window->stage = MapStage::type;
    window->offered = false;
    window->take_focus = true;
    window->joystick_id = joystick_id;
    window->name = input_sdl_joystick_name(joystick_id);
    window->guid = input_sdl_joystick_guid(joystick_id);
    window->needed = !SDL_IsGamepad(static_cast<SDL_JoystickID>(joystick_id));
    // Preselected from what SDL knows of it ("ps5", "xboxone"...), or from its profile.
    const char* type = SDL_GetGamepadStringForType(SDL_GetGamepadTypeForID(static_cast<SDL_JoystickID>(joystick_id)));
    const InputLayout* layout = layouts != nullptr ? input_layouts_for_type(layouts, type) : nullptr;
    window->initial_layout = layout != nullptr ? layout->id : std::string("Generic/generic");
    window->initial_hotkey_mode = InputHotkeyMode::hold;
    const InputProfile* profile = input != nullptr ? input_profile(input, window->guid) : nullptr;
    if (profile != nullptr)
    {
        if (layouts != nullptr && input_layouts_find(layouts, profile->appearance.c_str()) != nullptr)
        {
            window->initial_layout = profile->appearance;
        }
        window->initial_hotkey_mode = profile->hotkey_mode;
    }
    // SDL's mapping now, to put back unless FINISH.
    char* mapping = SDL_GetGamepadMappingForID(static_cast<SDL_JoystickID>(joystick_id));
    window->original_mapping = mapping != nullptr ? mapping : "";
    SDL_free(mapping);
    window->mapping_changed = false;
    forget_choices(window);
}

void map_window_close(MapWindow* window, InputSystem* input)
{
    input_sdl_mapper_cancel(&window->mapper);
    input_sdl_press_close(&window->press);
    restore_mapping(window);
    window->press_stage = MapStage::closed;
    window->drawn_stage = MapStage::closed;
    window->stage = MapStage::closed;
    input->suspended = false;
}

// A page's countdown ran out: it answers by itself (START, NO).
static bool page_timed_out(const MapWindow* window, uint64_t now_ms, uint64_t limit_ms)
{
    return now_ms - window->page_started_ms >= limit_ms;
}

// The STEP 4 question before this one (BACK); STEP 3's comes before the first.
static MapStage previous_part(MapStage part)
{
    switch (part)
    {
        case MapStage::extras_paddles:   return MapStage::extras_guide;
        case MapStage::extras_shoulders: return MapStage::extras_paddles;
        case MapStage::extras_others:    return MapStage::extras_shoulders;
        default:                         return MapStage::intro_interface;
    }
}

void map_window_apply(MapWindow* window, const MapWindowAction* action, InputSystem* input,
                      const Localization* strings, const InputLayouts* layouts, uint64_t now_ms)
{
    if (!action->drawn)
    {
        return;   // its countdowns and presses count only while the page shows
    }
    MapWindowPress press = action->press;
    switch (window->stage)
    {
    case MapStage::closed:
        break;
    case MapStage::offer:
        // Any button on the new controller is MAP, whether SDL knows it or not.
        if (press == MapWindowPress::map || window->pressed)
        {
            go(window, now_ms, MapStage::type);
        }
        else if (press == MapWindowPress::not_now || now_ms - window->offer_started_ms >= OFFER_MS)
        {
            not_now(window, input);
        }
        break;
    case MapStage::type:
        // START: also any button on one SDL doesn't know, or untouched for a while.
        if (press == MapWindowPress::start || window->pressed || page_timed_out(window, now_ms, START_MS))
        {
            window->skip_type = false;
            go(window, now_ms, MapStage::intro_buttons);
        }
        else if (press == MapWindowPress::skip)
        {
            window->skip_type = true;
            window->layout = window->initial_layout;
            window->hotkey_mode = window->initial_hotkey_mode;
            go(window, now_ms, MapStage::intro_buttons);
        }
        else if (press == MapWindowPress::cancel)
        {
            map_window_close(window, input);
        }
        break;
    case MapStage::intro_buttons:
        if (press == MapWindowPress::back)
        {
            go(window, now_ms, MapStage::type);
        }
        else if (press == MapWindowPress::start || window->pressed || page_timed_out(window, now_ms, START_MS))
        {
            window->skip_buttons = false;
            start_mapper(window, input, layouts, now_ms, true);
        }
        else if (press == MapWindowPress::skip)
        {
            window->skip_buttons = true;
            start_mapper(window, input, layouts, now_ms, false);
        }
        else if (press == MapWindowPress::cancel)
        {
            map_window_close(window, input);
        }
        break;
    case MapStage::intro_interface:
        if (press == MapWindowPress::back)
        {
            back_to_buttons(window, input, now_ms);
        }
        else if (press == MapWindowPress::start || window->pressed || page_timed_out(window, now_ms, START_MS))
        {
            window->interface_intro_done = true;
            input_sdl_mapper_restart_step(&window->mapper, now_ms);
            window->stage = MapStage::mapping;
        }
        else if (press == MapWindowPress::skip)
        {
            window->roles_done = false;
            after_interface(window, input, now_ms);
        }
        else if (press == MapWindowPress::cancel)
        {
            map_window_close(window, input);
        }
        break;
    case MapStage::mapping:
    case MapStage::extras_mapping:
        if (press == MapWindowPress::step_back)
        {
            input_sdl_mapper_back(&window->mapper, now_ms);
        }
        else if (press == MapWindowPress::step_reset)
        {
            input_sdl_mapper_restart_phase(&window->mapper, now_ms);
        }
        else if (press == MapWindowPress::step_skip)
        {
            input_sdl_mapper_skip(&window->mapper, now_ms);
        }
        else if (press == MapWindowPress::cancel)
        {
            map_window_close(window, input);   // nothing kept: SDL's mapping as it was
        }
        break;
    case MapStage::extras_guide:
    case MapStage::extras_paddles:
    case MapStage::extras_shoulders:
    case MapStage::extras_others:
    {
        // A STEP 4 part's question.
        MapStage part = window->stage;
        if (press == MapWindowPress::back)
        {
            MapStage back = previous_part(part);
            if (back == MapStage::intro_interface)
            {
                window->roles_done = false;
                start_mapper(window, input, layouts, now_ms, false);   // STEP 3 again, on the mapping it has now
            }
            else
            {
                go(window, now_ms, back);
            }
        }
        else if (press == MapWindowPress::yes)
        {
            start_extras(window, input, now_ms, part);
        }
        else if (press == MapWindowPress::no || page_timed_out(window, now_ms, QUESTION_MS))
        {
            go(window, now_ms, next_part(part));
        }
        else if (press == MapWindowPress::cancel)
        {
            map_window_close(window, input);
        }
        break;
    }
    case MapStage::summary:
        if (press == MapWindowPress::restart)
        {
            restore_mapping(window);
            forget_choices(window);
            go(window, now_ms, MapStage::type);
        }
        else if (press == MapWindowPress::cancel)
        {
            map_window_close(window, input);
        }
        else if (press == MapWindowPress::finish)
        {
            finish(window, input, strings);
        }
        break;
    }
}
