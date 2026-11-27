#include "input_window.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "button_names.h"
#include "pad_picture.h"
#include "piccolo_text.h"
#include "pixel_font.h"
#include "piccolo_settings.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "settings_ui.h"
#include "ui_toolkit.h"

// SET gives up after this long without a key or button.
// BIND ALL waits this long before taking the next control, so one press
// doesn't land on two.
constexpr uint64_t BIND_ALL_COOL_OFF_MS = 500;
constexpr uint16_t HID_ESCAPE = 41;
constexpr uint32_t YELLOW = 0xFFFFDC50u;     // like the lit pad
constexpr uint32_t CLASH_RED = 0xFFE04F4Fu;   // an action sharing a control with another

// The label of each InputAction, in its order.
static const Str ACTION_LABELS[INPUT_ACTION_COUNT] = {
    Str::input_action_pad_b, Str::input_action_pad_y, Str::input_action_pad_select, Str::input_action_pad_start,
    Str::input_action_pad_up, Str::input_action_pad_down, Str::input_action_pad_left, Str::input_action_pad_right,
    Str::input_action_pad_a, Str::input_action_pad_x, Str::input_action_pad_l, Str::input_action_pad_r,
    Str::input_action_pad_l2, Str::input_action_pad_r2, Str::input_action_pad_l3, Str::input_action_pad_r3,
    Str::input_action_left_x_plus, Str::input_action_left_x_minus, Str::input_action_left_y_plus,
    Str::input_action_left_y_minus, Str::input_action_right_x_plus, Str::input_action_right_x_minus,
    Str::input_action_right_y_plus, Str::input_action_right_y_minus, Str::input_action_pad_turbo,
    Str::input_action_cursor_right, Str::input_action_cursor_left, Str::input_action_cursor_down, Str::input_action_cursor_up,
    Str::input_action_mouse_left, Str::input_action_mouse_right, Str::input_action_mouse_middle,
    Str::input_action_mouse_wheel_up, Str::input_action_mouse_wheel_down, Str::input_action_gun_reload,
    Str::input_action_gun_aux_a, Str::input_action_gun_aux_b, Str::input_action_gun_aux_c, Str::input_action_gun_start,
    Str::input_action_gun_select,
    Str::input_action_ui_up, Str::input_action_ui_down, Str::input_action_ui_left, Str::input_action_ui_right,
    Str::input_action_ui_accept, Str::input_action_ui_back, Str::input_action_ui_page_left,
    Str::input_action_ui_page_right, Str::input_action_ui_open_menu,
    Str::input_action_hotkey_enable, Str::input_action_hotkey_menu, Str::input_action_hotkey_quick_menu,
    Str::input_action_hotkey_save_state, Str::input_action_hotkey_load_state, Str::input_action_hotkey_fast_forward,
    Str::input_action_hotkey_grab_mouse, Str::input_action_hotkey_ports, Str::input_action_hotkey_pause,
    Str::input_action_hotkey_dvr, Str::input_action_hotkey_remap, Str::input_action_hotkey_screenshot,
    Str::input_action_hotkey_reset, Str::input_action_hotkey_undo_save_state,
    Str::input_action_hotkey_undo_load_state,
};

// ---------------------------------------------------------------------------
// State

static ButtonFaceLabel face_label(SDL_Gamepad* gamepad, InputButton button)
{
    switch (SDL_GetGamepadButtonLabel(gamepad, static_cast<SDL_GamepadButton>(button)))
    {
    case SDL_GAMEPAD_BUTTON_LABEL_A:        return ButtonFaceLabel::a;
    case SDL_GAMEPAD_BUTTON_LABEL_B:        return ButtonFaceLabel::b;
    case SDL_GAMEPAD_BUTTON_LABEL_X:        return ButtonFaceLabel::x;
    case SDL_GAMEPAD_BUTTON_LABEL_Y:        return ButtonFaceLabel::y;
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:    return ButtonFaceLabel::cross;
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:   return ButtonFaceLabel::circle;
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:   return ButtonFaceLabel::square;
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return ButtonFaceLabel::triangle;
    default:                                return ButtonFaceLabel::unknown;
    }
}

static ButtonFamily family_of(SDL_Gamepad* gamepad)
{
    switch (SDL_GetGamepadType(gamepad))
    {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return ButtonFamily::xbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return ButtonFamily::playstation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return ButtonFamily::nintendo;
    default:
        return ButtonFamily::other;
    }
}

const InputLayout* input_window_layout(const InputSystem* input, const InputLayouts* layouts, const InputDevice* device)
{
    if (layouts == nullptr || device == nullptr || device->kind != InputDeviceKind::gamepad)
    {
        return nullptr;
    }
    const InputProfile* profile = input_profile(input, device->guid);
    return profile != nullptr ? input_layouts_find(layouts, profile->appearance.c_str()) : nullptr;
}

void input_window_source_label(const InputSdl* sdl, const InputDevice* device, const InputLayout* layout,
                               const InputProfile* profile, InputSource source, char* out, size_t out_size)
{
    ButtonNamesPad pad = {};
    SDL_Gamepad* gamepad = device != nullptr ? input_sdl_gamepad(sdl, device->id) : nullptr;
    if (gamepad != nullptr)
    {
        pad.south = face_label(gamepad, InputButton::south);
        pad.east = face_label(gamepad, InputButton::east);
        pad.west = face_label(gamepad, InputButton::west);
        pad.north = face_label(gamepad, InputButton::north);
        pad.family = family_of(gamepad);
    }
    button_names_source(layout, profile, &pad, source, out, out_size);
}

bool input_window_action_label(const InputSystem* input, const InputSdl* sdl, const InputLayouts* layouts,
                               uint32_t controller, InputAction action, char* out, size_t out_size)
{
    out[0] = '\0';
    if (controller >= input->controllers.size())
    {
        return false;
    }
    const InputController* mapped = &input->controllers[controller];
    InputSource source = {};
    if (!button_names_first_source(&mapped->bindings, action, &source))
    {
        return false;
    }
    const InputDevice* device = input_devices_find_const(&input->devices, mapped->device_id);
    const InputProfile* profile = device != nullptr ? input_profile(input, device->guid) : nullptr;
    input_window_source_label(sdl, device, input_window_layout(input, layouts, device), profile, source, out, out_size);
    return true;
}

void input_window_controller_defaults(const InputSystem* input, uint32_t controller, InputBindingSet* set)
{
    *set = InputBindingSet{};
    if (controller >= input->controllers.size())
    {
        return;
    }
    const InputDevice* device = input_devices_find_const(&input->devices, input->controllers[controller].device_id);
    if (device == nullptr)
    {
        return;
    }
    bool first = input_first_controller(input, device->id) == static_cast<int32_t>(controller);
    if (device->kind == InputDeviceKind::gamepad || first)
    {
        input_default_bindings_for(device, set);   // more keyboard players start with no keys
    }
}

static bool is_keyboard(const InputSystem* input, uint32_t controller)
{
    const InputDevice* device = input_devices_find_const(&input->devices, input->controllers[controller].device_id);
    return device != nullptr && device->kind == InputDeviceKind::keyboard;
}

// The same sources, in any order?
static bool same_sources(const InputSource* a, const InputSource* b)
{
    uint32_t count_a = 0;
    uint32_t count_b = 0;
    for (uint32_t s = 0; s < INPUT_SOURCES_PER_ACTION; s++)
    {
        count_a += a[s].type != InputSourceType::none ? 1u : 0u;
        count_b += b[s].type != InputSourceType::none ? 1u : 0u;
        bool found = a[s].type == InputSourceType::none;
        for (uint32_t t = 0; t < INPUT_SOURCES_PER_ACTION && !found; t++)
        {
            found = b[t].type != InputSourceType::none && input_source_equal(a[s], b[t]);
        }
        if (!found)
        {
            return false;
        }
    }
    return count_a == count_b;
}

static void start_capture(InputWindow* window, InputSystem* input, uint64_t now_ms, uint32_t controller,
                          InputAction action, uint32_t row)
{
    if (!window->capturing)
    {
        window->capture_backup = input->controllers[controller].bindings;   // a new pass: CANCEL goes back to this
    }
    window->capturing = true;
    window->capture_action = action;
    window->capture_row = row;
    window->capture_controller = controller;
    window->capture_started_ms = now_ms;
    input_capture_begin(input, input->controllers[controller].device_id);
}

// The next action of a BIND ALL, or the end of it.
static void next_or_stop(InputWindow* window, InputSystem* input, uint64_t now_ms)
{
    if (window->bind_all && window->capture_action != window->bind_all_last)
    {
        start_capture(window, input, now_ms, window->capture_controller,
                      static_cast<InputAction>(static_cast<uint32_t>(window->capture_action) + 1), 0);
        return;
    }
    window->bind_all = false;
    input_capture_cancel(input);
    window->capturing = false;
}

// A SET / BIND ALL in progress: the next key or button binds; Escape stops;
// 5 s without one skips (BIND ALL) or gives up (SET).
void input_window_update(InputWindow* window, InputSystem* input, uint64_t now_ms)
{
    if (!window->capturing)
    {
        return;
    }
    if (window->capture_controller >= input->controllers.size() || !window->open)
    {
        window->bind_all = false;
        input_capture_cancel(input);
        window->capturing = false;
        return;
    }
    uint64_t since = now_ms - window->capture_started_ms;
    InputSource source = {};
    if ((!window->bind_all || since >= BIND_ALL_COOL_OFF_MS) && input_capture_poll(input, now_ms, &source))
    {
        if (source.type == InputSourceType::key && source.code == HID_ESCAPE)
        {
            window->bind_all = false;
            window->capturing = false;
            return;
        }
        InputBindingSet* bindings = &input->controllers[window->capture_controller].bindings;
        if (window->bind_all)
        {
            input_unbind_all(bindings, window->capture_action);   // BIND ALL: this one, alone
            input_bind(bindings, window->capture_action, source);
        }
        else
        {
            input_sources_set(bindings->sources[static_cast<uint32_t>(window->capture_action)], window->capture_row, source);
        }
        window->dirty = true;
        next_or_stop(window, input, now_ms);
        return;
    }
    InputSource held = {};
    bool holding = input_capture_candidate(input, &held);   // no time-out while a control is being held
    if (!input->capture.active || (!holding && since > input->settings.press_ms + (window->bind_all ? BIND_ALL_COOL_OFF_MS : 0)))
    {
        next_or_stop(window, input, now_ms);
    }
}

void input_window_state(InputWindowState* state, const InputSystem* input, const InputSdl* sdl,
                        const Localization* strings, const Settings* settings, const char* core_name,
                        const char* game_name, const InputLayouts* layouts, uint64_t now_ms)
{
    state->input = input;
    state->sdl = sdl;
    state->strings = strings;
    state->settings = settings;
    state->core_name = core_name;
    state->game_name = game_name;
    state->layouts = layouts;
    state->now_ms = now_ms;
    state->press_ms = input->settings.press_ms;
    state->holding = input_capture_candidate(input, &state->held);
    state->hold_progress = input_capture_hold_progress(input, now_ms);
}

// ---------------------------------------------------------------------------
// Drawing

static void controller_label(const InputWindowState* state, uint32_t controller, char* out, size_t out_size)
{
    const InputSystem* input = state->input;
    const InputDevice* device = input_devices_find_const(&input->devices, input->controllers[controller].device_id);
    const char* status = "";
    if (device == nullptr || !device->connected)
    {
        status = piccolo_text(state->strings, Str::input_device_disconnected);
    }
    else if (input_devices_ignored(device))
    {
        status = piccolo_text(state->strings, Str::input_device_ignored);
    }
    std::snprintf(out, out_size, status[0] != '\0' ? "%s (%s)" : "%s%s", input_controller_label(input, controller).c_str(), status);
}

// The NAME field of a controller (written when the field is left; the
// field keeps the text while it's typed).
static void name_field(const InputWindowState* state, uint32_t controller, float width, InputWindowAction* action)
{
    char edited[INPUT_WINDOW_NAME_MAX] = {};
    retro_ui_next_width(width);
    if (retro_ui_text_field("name", "", state->input->controllers[controller].name.c_str(), edited, sizeof(edited)) &&
        edited[0] != '\0')
    {
        action->rename = true;
        action->rename_controller = controller;
        std::snprintf(action->name, sizeof(action->name), "%s", edited);
    }
}

// GENERAL

// GENERAL (the tab was SETTINGS): one SCOPE for the whole tab (it was drawn
// once per group, two controls with one id), then the general rows, always
// shown (Andrés, testing round 2: no section of their own), and ON-SCREEN
// GAMEPAD, which folds away. The rows hand their edit out
// (input_window_apply).
static void draw_settings(InputWindow* window, const InputWindowState* state, InputWindowAction* action)
{
    settings_ui_scope(state->settings, state->strings, state->core_name, state->game_name, &window->scope,
                      &window->choose_scope);
    SettingsUiContext ui = {};
    ui.settings = state->settings;
    ui.strings = state->strings;
    ui.scope = window->scope;
    ui.label_width = settings_ui_label_width(state->settings, state->strings);
    ui.edit = &action->setting;

    static const char* const KEYS[] = {PICCOLO_INPUT_SHARE_PORT1, PICCOLO_INPUT_PORTS, PICCOLO_INPUT_PRESS_TIME,
                                       PICCOLO_INPUT_HOLD_TIME, PICCOLO_INPUT_OSK};
    for (uint32_t k = 0; k < sizeof(KEYS) / sizeof(KEYS[0]); k++)
    {
        settings_ui_row(&ui, KEYS[k], nullptr);
    }

    // ON-SCREEN GAMEPAD (onscreen_pad.h): its settings and EDIT LAYOUT. Open
    // at first only where there's a touch screen to use it on.
#if defined(SDL_PLATFORM_ANDROID)
    bool touch_open = true;
#else
    bool touch_open = false;
#endif
    if (!settings_ui_section("touch_pad", piccolo_text(state->strings, Str::input_section_touch_pad), nullptr,
                             touch_open))
    {
        return;
    }
    static const char* const TOUCH_KEYS[] = {PICCOLO_TOUCH_PAD, PICCOLO_TOUCH_PAD_OPACITY, PICCOLO_TOUCH_PAD_VIBRATE};
    for (uint32_t k = 0; k < sizeof(TOUCH_KEYS) / sizeof(TOUCH_KEYS[0]); k++)
    {
        settings_ui_row(&ui, TOUCH_KEYS[k], nullptr);
    }
    // STYLE: the controller layouts (layouts/), by name; "" = RETROPAD.
    if (state->layouts != nullptr)
    {
        constexpr uint32_t MAX_STYLES = 64;
        const char* names[MAX_STYLES] = {};
        uint32_t name_count = 0;
        for (size_t i = 0; i < state->layouts->layouts.size() && name_count < MAX_STYLES; i++)
        {
            names[name_count] = state->layouts->layouts[i].name.c_str();
            name_count++;
        }
        settings_ui_text_choice_row(&ui, PICCOLO_TOUCH_PAD_STYLE, names, name_count,
                                    piccolo_text(state->strings, Str::touch_pad_style_retropad),
                                    piccolo_text(state->strings, Str::touch_pad_style_missing));
    }
    if (retro_ui_button(piccolo_text(state->strings, Str::input_touch_pad_edit)))
    {
        action->press = InputWindowPress::edit_touch_layout;
    }
    retro_ui_item_tooltip(piccolo_text(state->strings, Str::input_touch_pad_edit_help));
}

// INTERFACE / HOTKEYS

void input_window_draw_sources(const InputSdl* sdl, const InputDevice* device, const InputLayout* layout,
                               const InputProfile* profile, const InputSource* sources, float width, const Localization* strings)
{
    char names[INPUT_SOURCES_PER_ACTION][48] = {};
    uint32_t count = 0;
    for (uint32_t s = 0; s < INPUT_SOURCES_PER_ACTION; s++)
    {
        if (sources[s].type != InputSourceType::none)
        {
            input_window_source_label(sdl, device, layout, profile, sources[s], names[count], sizeof(names[count]));
            count++;
        }
    }
    if (count == 0)
    {
        retro_ui_dim_text(piccolo_text(strings, Str::input_none), nullptr);
        return;
    }
    std::string all;
    for (uint32_t s = 0; s < count; s++)
    {
        all += s == 0 ? "" : ", ";
        all += names[s];
    }
    if (retro_ui_text_width(all.c_str()) <= width)
    {
        retro_ui_text(all.c_str());
        return;
    }
    if (count == 1)
    {
        // One that doesn't fit: cut short with "…" (whole characters), all of it in the tooltip.
        retro_ui_text_fit(all.c_str(), width);
        return;
    }
    // Doesn't fit: the first one and how many more; all of them in the tooltip.
    char shortened[80] = {};
    std::snprintf(shortened, sizeof(shortened), "%s +%u", names[0], static_cast<unsigned>(count - 1));
    retro_ui_text(shortened);
    retro_ui_item_tooltip(all.c_str());
}

void input_window_draw_waiting(bool keyboard, uint64_t seconds_left, float hold_progress, const Localization* strings)
{
    char text[100] = {};
    std::snprintf(text, sizeof(text), piccolo_text(strings, keyboard ? Str::input_press_key : Str::input_press_button),
                  static_cast<int>(seconds_left));
    retro_ui_push_section_text();
    retro_ui_text(text);
    retro_ui_pop_section_text();
    // A gamepad's control is taken once held: a ring closes next to it.
    retro_ui_hold_ring(hold_progress, YELLOW);
}

// Records a SET / + / BIND ALL ... press on one action of a controller.
static void press_on(InputWindowAction* action, InputWindowPress press, uint32_t controller, InputAction input_action,
                     uint32_t row)
{
    action->press = press;
    action->controller = controller;
    action->input_action = input_action;
    action->row = row;
}

// One action: a row per source (its label on the first only), each with
// SET and CLEAR; the first also has + (one more source for it: a new row)
// and RESET when it differs from the defaults.
static void binding_row(InputWindow* window, const InputWindowState* state, uint32_t controller_index,
                        const InputBindingSet* defaults, InputAction input_action, float label_width,
                        InputWindowAction* action)
{
    const InputSystem* input = state->input;
    const InputController* controller = &input->controllers[controller_index];
    const InputDevice* device = input_devices_find_const(&input->devices, controller->device_id);
    const InputLayout* layout = input_window_layout(input, state->layouts, device);
    const Localization* strings = state->strings;
    uint32_t index = static_cast<uint32_t>(input_action);
    const InputSource* sources = controller->bindings.sources[index];
    bool changed = !same_sources(sources, defaults->sources[index]);
    InputAction clash = input_binding_conflict_with(&controller->bindings, input_action);
    bool waiting = window->capturing && window->capture_action == input_action && window->capture_controller == controller_index;
    uint32_t count = input_sources_count(sources);
    uint32_t rows = count > 0 ? count : 1;   // an action with nothing still has its row
    if (waiting && window->capture_row >= rows)
    {
        rows = window->capture_row + 1;      // + : the new row, waiting
    }

    const char* set = piccolo_text(strings, Str::input_set);
    const char* clear = piccolo_text(strings, Str::input_clear);
    const char* reset = piccolo_text(strings, Str::input_reset);
    const char* add = "+";
    float spacing = retro_ui_item_spacing();
    float row_buttons = retro_ui_button_width(set) + retro_ui_button_width(clear) + spacing;
    float first_buttons = row_buttons + retro_ui_button_width(add) + spacing +
                          (changed ? retro_ui_button_width(reset) + spacing : 0.0f);

    retro_ui_push_index(index);
    for (uint32_t r = 0; r < rows; r++)
    {
        retro_ui_push_index(r);
        if (r == 0)
        {
            if (waiting)
            {
                retro_ui_push_section_text();   // the one being bound stands out
                retro_ui_label(piccolo_text(strings, ACTION_LABELS[index]));
                retro_ui_pop_section_text();
            }
            else if (clash != InputAction::count)
            {
                // Shares a control with an action that fires with it
                // (input_binding_conflicts): red, and the other one named.
                retro_ui_label_colored(CLASH_RED, piccolo_text(strings, ACTION_LABELS[index]));
                char also[160] = {};
                std::snprintf(also, sizeof(also), piccolo_text(strings, Str::input_conflict_also),
                              piccolo_text(strings, ACTION_LABELS[static_cast<uint32_t>(clash)]));
                retro_ui_item_tooltip(also);
            }
            else if (changed)
            {
                // Changed from the defaults: highlighted, like an override.
                retro_ui_label_colored(retro_ui_palette()->highlight, piccolo_text(strings, ACTION_LABELS[index]));
            }
            else
            {
                retro_ui_label(piccolo_text(strings, ACTION_LABELS[index]));
            }
        }
        else
        {
            retro_ui_label("");   // more sources of the same action: no label
        }

        // The buttons at the right end; the binding gets the room before them.
        float buttons_x = retro_ui_right_edge() - (r == 0 ? first_buttons : row_buttons);
        retro_ui_same_line_at(label_width);
        if (waiting && window->capture_row == r)
        {
            uint64_t elapsed = state->now_ms - window->capture_started_ms;
            uint64_t total = state->press_ms + (window->bind_all ? BIND_ALL_COOL_OFF_MS : 0);
            uint64_t left = elapsed < total ? (total - elapsed + 999) / 1000 : 0;
            input_window_draw_waiting(device != nullptr && device->kind == InputDeviceKind::keyboard, left,
                                      state->hold_progress, strings);
            retro_ui_scroll_here();   // BIND ALL: keep it in view
        }
        else
        {
            InputSource one[INPUT_SOURCES_PER_ACTION] = {};
            one[0] = r < count ? sources[r] : InputSource{};
            input_window_draw_sources(state->sdl, device, layout,
                                      input_profile(input, input_controller_profile_id(input, controller_index)), one,
                                      buttons_x - retro_ui_cursor_x() - spacing, strings);
        }
        retro_ui_same_line_at(buttons_x > retro_ui_cursor_x() ? buttons_x : 0.0f);
        bool usable = !window->capturing && device != nullptr && device->connected;
        if (r == 0)
        {
            if (changed)
            {
                retro_ui_begin_disabled(window->capturing);
                if (retro_ui_button(reset))
                {
                    press_on(action, InputWindowPress::reset, controller_index, input_action, 0);
                }
                retro_ui_end_disabled();
                retro_ui_same_line();
            }
            retro_ui_begin_disabled(!usable || count == 0 || count >= INPUT_SOURCES_PER_ACTION);
            if (retro_ui_button(add))
            {
                press_on(action, InputWindowPress::set, controller_index, input_action, count);   // a new row
            }
            retro_ui_item_tooltip(piccolo_text(strings, Str::input_add_help));
            retro_ui_end_disabled();
            retro_ui_same_line();
        }
        retro_ui_begin_disabled(!usable);
        if (retro_ui_button(set))
        {
            press_on(action, InputWindowPress::set, controller_index, input_action, r < count ? r : count);
        }
        // The floating pad shows the row under the mouse, or the one in focus
        // while navigating with a gamepad or keys.
        if (retro_ui_item_pointed())
        {
            window->pad_action = input_action;   // shown on the next frame
            window->pad_row = r;
        }
        retro_ui_same_line();
        retro_ui_begin_disabled(r >= count);
        if (retro_ui_button(clear))
        {
            press_on(action, InputWindowPress::clear, controller_index, input_action, r);
        }
        retro_ui_end_disabled();
        retro_ui_end_disabled();
        retro_ui_pop_id();
    }
    retro_ui_pop_id();
}

// The controller mapped: picked by the user (it stays), else port 1's (the
// last that joined it that isn't the keyboard or the mouse put there: a
// gamepad pressing START takes over at once), else the keyboard.
static uint32_t mapped_controller(InputWindow* window, const InputSystem* input)
{
    if (window->mapping_picked && window->mapping < input->controllers.size())
    {
        return window->mapping;
    }
    const std::vector<uint32_t>* port1 = &input->ports[0];
    for (size_t m = port1->size(); m > 0; m--)
    {
        uint32_t controller = (*port1)[m - 1];
        const InputDevice* device =
            controller < input->controllers.size() ? input_devices_find_const(&input->devices, input->controllers[controller].device_id) : nullptr;
        if (device != nullptr && device->kind == InputDeviceKind::gamepad)
        {
            window->mapping = controller;
            return window->mapping;
        }
    }
    window->mapping = 0;
    for (uint32_t c = 0; c < input->controllers.size(); c++)
    {
        if (is_keyboard(input, c))   // the keyboard is never ignored
        {
            window->mapping = c;
            break;
        }
    }
    return window->mapping;
}

static void draw_bindings(InputWindow* window, const InputWindowState* state, InputAction first, InputAction last,
                          Str left_title, float label_width, InputWindowAction* action)
{
    const InputSystem* input = state->input;
    const Localization* strings = state->strings;
    if (input->controllers.empty())
    {
        return;
    }
    uint32_t controller = mapped_controller(window, input);

    // MAPPING [ controller v ]
    char label[200] = {};
    retro_ui_label(piccolo_text(strings, Str::input_mapping));
    retro_ui_same_line_at(label_width);
    controller_label(state, controller, label, sizeof(label));
    retro_ui_begin_disabled(window->capturing);
    if (retro_ui_begin_choice("mapping", label))
    {
        for (uint32_t c = 0; c < input->controllers.size(); c++)
        {
            const InputDevice* candidate = input_devices_find_const(&input->devices, input->controllers[c].device_id);
            if (candidate != nullptr && input_devices_ignored(candidate))
            {
                continue;   // ignored devices aren't mapped
            }
            controller_label(state, c, label, sizeof(label));
            if (retro_ui_choice_item_repick(c, label, c == controller))
            {
                window->mapping = c;
                window->mapping_picked = true;
            }
        }
        retro_ui_end_choice();
    }
    controller = mapped_controller(window, input);
    InputBindingSet defaults = {};
    input_window_controller_defaults(input, controller, &defaults);

    // HOTKEYS: how this controller's modifier works.
    if (first == InputAction::hotkey_enable)
    {
        static const Str MODE_NAMES[] = {Str::hotkey_mode_hold, Str::hotkey_mode_one, Str::hotkey_mode_stays};
        static const InputHotkeyMode MODES[] = {InputHotkeyMode::hold, InputHotkeyMode::one_hotkey, InputHotkeyMode::stays_on};
        const InputController* mapped = &input->controllers[controller];
        retro_ui_label(piccolo_text(strings, Str::input_hotkey_mode));
        retro_ui_item_tooltip(piccolo_text(strings, Str::input_hotkey_mode_help));
        retro_ui_same_line_at(label_width);
        if (retro_ui_begin_choice("hotkey_mode", piccolo_text(strings, MODE_NAMES[static_cast<int>(mapped->hotkey_mode)])))
        {
            for (uint32_t m = 0; m < 3; m++)
            {
                if (retro_ui_choice_item_repick(m, piccolo_text(strings, MODE_NAMES[m]), mapped->hotkey_mode == MODES[m]))
                {
                    action->press = InputWindowPress::hotkey_mode;
                    action->controller = controller;
                    action->hotkey_mode = MODES[m];
                }
            }
            retro_ui_end_choice();
        }
    }

    // BIND ALL, DEFAULTS
    const InputDevice* device = input_devices_find_const(&input->devices, input->controllers[controller].device_id);
    retro_ui_begin_disabled(device == nullptr || !device->connected);
    if (retro_ui_button(piccolo_text(strings, Str::input_bind_all)))
    {
        press_on(action, InputWindowPress::bind_all, controller, first, 0);
        action->last = last;
    }
    retro_ui_item_tooltip(piccolo_text(strings, Str::input_bind_all_help));
    retro_ui_end_disabled();
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::input_defaults)))
    {
        press_on(action, InputWindowPress::defaults, controller, first, 0);
        action->last = last;
    }
    retro_ui_item_tooltip(piccolo_text(strings, Str::input_defaults_help));
    retro_ui_end_disabled();
    // CANCEL: stops a SET / BIND ALL and puts back what it changed.
    retro_ui_same_line();
    retro_ui_begin_disabled(!window->capturing || window->capture_controller != controller);
    if (retro_ui_button(piccolo_text(strings, Str::input_cancel_binding)))
    {
        action->press = InputWindowPress::cancel;
        action->controller = controller;
    }
    retro_ui_item_tooltip(piccolo_text(strings, Str::input_cancel_binding_help));
    retro_ui_end_disabled();

    // GAME / GAMEPAD (or KEY), then the rows, which scroll under the titles.
    retro_ui_dim_text(piccolo_text(strings, left_title), nullptr);
    retro_ui_same_line_at(label_width);
    retro_ui_dim_text(piccolo_text(strings, is_keyboard(input, controller) ? Str::input_column_key : Str::input_column_pad),
                      nullptr);
    // A gamepad's pad floats in the screen's corner: the control being bound
    // while it is held, else where the row in focus is now.
    InputMapControl lit = InputMapControl::count;
    bool minus = false;
    if (window->capturing && window->capture_controller == controller && state->holding)
    {
        lit = input_map_control_for_source(state->held);
        minus = state->held.type == InputSourceType::axis_minus;
    }
    else
    {
        InputAction shown = window->capturing ? window->capture_action : window->pad_action;
        uint32_t row = window->capturing ? window->capture_row : window->pad_row;
        if (static_cast<uint32_t>(shown) < INPUT_ACTION_COUNT && row < INPUT_SOURCES_PER_ACTION)
        {
            InputSource source = input->controllers[controller].bindings.sources[static_cast<uint32_t>(shown)][row];
            lit = input_map_control_for_source(source);
            minus = source.type == InputSourceType::axis_minus;
        }
    }
    window->pad_action = InputAction::count;
    retro_ui_begin_scroll_area("rows", 0.0f, 0.0f);
    for (uint32_t a = static_cast<uint32_t>(first); a <= static_cast<uint32_t>(last); a++)
    {
        binding_row(window, state, controller, &defaults, static_cast<InputAction>(a), label_width, action);
    }
    retro_ui_end_scroll_area();
    if (device != nullptr && device->kind == InputDeviceKind::gamepad)
    {
        float block = retro_ui_font_size() / static_cast<float>(PIXEL_FONT_LINE_HEIGHT);   // the font is a line tall per block
        pad_picture_draw_floating(input_window_layout(input, state->layouts, device), lit, minus, 1.25f * block);
    }
}

// DEVICES

static void draw_device(const InputWindowState* state, const InputDevice* device, float label_width,
                        bool open_by_default, InputWindowAction* action);

static void draw_devices(const InputWindowState* state, float label_width, InputWindowAction* action)
{
    const InputSystem* input = state->input;
    const Localization* strings = state->strings;
    // Controllers SDL doesn't know as gamepads: unusable until mapped.
    if (!state->sdl->unmapped.empty())
    {
        retro_ui_separator_text(piccolo_text(strings, Str::input_map_unmapped));
        for (size_t u = 0; u < state->sdl->unmapped.size(); u++)
        {
            uint32_t joystick = state->sdl->unmapped[u];
            retro_ui_push_index(10000 + joystick);
            retro_ui_label(input_sdl_joystick_name(joystick).c_str());
            const char* map = piccolo_text(strings, Str::input_map);
            float x = retro_ui_right_edge() - retro_ui_button_width(map);
            retro_ui_same_line_at(x > retro_ui_cursor_x() ? x : 0.0f);
            if (retro_ui_button(map))
            {
                action->press = InputWindowPress::map;
                action->joystick = joystick;
            }
            retro_ui_pop_id();
        }
    }
    // ENABLED (connected and used), then DETECTED (ignored, or not
    // connected now); each controller folded at first (testing round 2).
    for (int section = 0; section < 2; section++)
    {
        retro_ui_separator_text(piccolo_text(strings, section == 0 ? Str::input_section_enabled : Str::input_section_detected));
        for (size_t d = 0; d < input->devices.devices.size(); d++)
        {
            const InputDevice* candidate = &input->devices.devices[d];
            bool enabled = candidate->connected && !input_devices_ignored(candidate);
            if (enabled != (section == 0))
            {
                continue;
            }
            draw_device(state, candidate, label_width, false, action);
        }
    }
}

// The extra buttons this gamepad has (MISC, paddles, touchpad: what MAP
// CONTROLLER's STEP 4 adds), each with a name of the user's own ("VOLUME
// UP"), kept in its profile and shown wherever its buttons are listed.
static void extra_button_names(const InputWindowState* state, const InputDevice* device, uint32_t controller,
                               float label_width, InputWindowAction* action)
{
    static const InputButton EXTRAS[] = {
        InputButton::misc1, InputButton::misc2, InputButton::misc3, InputButton::misc4, InputButton::misc5,
        InputButton::misc6, InputButton::right_paddle1, InputButton::left_paddle1, InputButton::right_paddle2,
        InputButton::left_paddle2, InputButton::touchpad,
    };
    const InputSystem* input = state->input;
    SDL_Gamepad* gamepad = input_sdl_gamepad(state->sdl, device->id);
    if (gamepad == nullptr)
    {
        return;
    }
    std::string id = input_controller_profile_id(input, controller);
    const InputLayout* layout = input_window_layout(input, state->layouts, device);
    bool any = false;
    for (size_t e = 0; e < sizeof(EXTRAS) / sizeof(EXTRAS[0]); e++)
    {
        if (!SDL_GamepadHasButton(gamepad, static_cast<SDL_GamepadButton>(EXTRAS[e])))
        {
            continue;
        }
        if (!any)
        {
            retro_ui_text(piccolo_text(state->strings, Str::input_extra_names));
            retro_ui_item_tooltip(piccolo_text(state->strings, Str::input_extra_names_help));
            any = true;
        }
        InputSource source = {};
        source.type = InputSourceType::button;
        source.code = static_cast<uint16_t>(EXTRAS[e]);
        char own[48] = {};
        input_window_source_label(state->sdl, device, layout, nullptr, source, own, sizeof(own));   // its own name
        retro_ui_push_index(static_cast<uint32_t>(EXTRAS[e]));
        retro_ui_label(own);
        retro_ui_same_line_at(label_width);
        const InputProfile* profile = input_profile(input, id);
        char buffer[INPUT_WINDOW_LABEL_MAX] = {};
        std::snprintf(buffer, sizeof(buffer), "%s", profile != nullptr ? profile->button_labels[source.code].c_str() : "");
        char before[INPUT_WINDOW_LABEL_MAX] = {};
        std::snprintf(before, sizeof(before), "%s", buffer);
        RetroUiEditField field = {};
        retro_ui_edit_field("label", buffer, sizeof(buffer), &field);
        if (std::strcmp(buffer, before) != 0)   // written as it's typed
        {
            action->button_label = true;
            action->label_controller = controller;
            action->label_button = source.code;
            std::snprintf(action->label, sizeof(action->label), "%s", buffer);
        }
        retro_ui_pop_id();
    }
}

// SDL's mapping fields as the sources they give ("a" = the bottom button...).
struct SdlField
{
    const char*     name;
    InputSourceType type;
    uint16_t        code;
};

static const SdlField SDL_FIELDS[] = {
    {"a", InputSourceType::button, static_cast<uint16_t>(InputButton::south)},
    {"b", InputSourceType::button, static_cast<uint16_t>(InputButton::east)},
    {"x", InputSourceType::button, static_cast<uint16_t>(InputButton::west)},
    {"y", InputSourceType::button, static_cast<uint16_t>(InputButton::north)},
    {"back", InputSourceType::button, static_cast<uint16_t>(InputButton::back)},
    {"guide", InputSourceType::button, static_cast<uint16_t>(InputButton::guide)},
    {"start", InputSourceType::button, static_cast<uint16_t>(InputButton::start)},
    {"leftstick", InputSourceType::button, static_cast<uint16_t>(InputButton::left_stick)},
    {"rightstick", InputSourceType::button, static_cast<uint16_t>(InputButton::right_stick)},
    {"leftshoulder", InputSourceType::button, static_cast<uint16_t>(InputButton::left_shoulder)},
    {"rightshoulder", InputSourceType::button, static_cast<uint16_t>(InputButton::right_shoulder)},
    {"dpup", InputSourceType::button, static_cast<uint16_t>(InputButton::dpad_up)},
    {"dpdown", InputSourceType::button, static_cast<uint16_t>(InputButton::dpad_down)},
    {"dpleft", InputSourceType::button, static_cast<uint16_t>(InputButton::dpad_left)},
    {"dpright", InputSourceType::button, static_cast<uint16_t>(InputButton::dpad_right)},
    {"misc1", InputSourceType::button, static_cast<uint16_t>(InputButton::misc1)},
    {"paddle1", InputSourceType::button, static_cast<uint16_t>(InputButton::right_paddle1)},
    {"paddle2", InputSourceType::button, static_cast<uint16_t>(InputButton::left_paddle1)},
    {"paddle3", InputSourceType::button, static_cast<uint16_t>(InputButton::right_paddle2)},
    {"paddle4", InputSourceType::button, static_cast<uint16_t>(InputButton::left_paddle2)},
    {"touchpad", InputSourceType::button, static_cast<uint16_t>(InputButton::touchpad)},
    {"misc2", InputSourceType::button, static_cast<uint16_t>(InputButton::misc2)},
    {"misc3", InputSourceType::button, static_cast<uint16_t>(InputButton::misc3)},
    {"misc4", InputSourceType::button, static_cast<uint16_t>(InputButton::misc4)},
    {"misc5", InputSourceType::button, static_cast<uint16_t>(InputButton::misc5)},
    {"misc6", InputSourceType::button, static_cast<uint16_t>(InputButton::misc6)},
    {"lefttrigger", InputSourceType::axis_plus, static_cast<uint16_t>(InputAxis::left_trigger)},
    {"righttrigger", InputSourceType::axis_plus, static_cast<uint16_t>(InputAxis::right_trigger)},
};

// A stick axis field's name ("leftx" = LEFT STICK X), or nullptr.
static const char* stick_axis_name(const std::string& field)
{
    if (field == "leftx") return "LEFT STICK X";
    if (field == "lefty") return "LEFT STICK Y";
    if (field == "rightx") return "RIGHT STICK X";
    if (field == "righty") return "RIGHT STICK Y";
    return nullptr;
}

// A raw joystick element in words: "b3" BUTTON 3, "h0.1" HAT 0 UP, "+a2"
// AXIS 2 +, "a5~" AXIS 5 (INVERTED). Anything else as it is.
static std::string element_words(const std::string& element)
{
    char text[64] = {};
    if (element.size() >= 2 && element[0] == 'b')
    {
        std::snprintf(text, sizeof(text), "BUTTON %s", element.c_str() + 1);
        return text;
    }
    if (element.size() >= 4 && element[0] == 'h')
    {
        size_t dot = element.find('.');
        if (dot != std::string::npos)
        {
            int mask = std::atoi(element.c_str() + dot + 1);
            const char* way = mask == 1 ? "UP" : mask == 2 ? "RIGHT" : mask == 4 ? "DOWN" : mask == 8 ? "LEFT" : "?";
            std::snprintf(text, sizeof(text), "HAT %s %s", element.substr(1, dot - 1).c_str(), way);
            return text;
        }
    }
    std::string axis = element;
    const char* half = "";
    if (!axis.empty() && (axis[0] == '+' || axis[0] == '-'))
    {
        half = axis[0] == '+' ? " +" : " -";
        axis = axis.substr(1);
    }
    bool inverted = !axis.empty() && axis[axis.size() - 1] == '~';
    if (inverted)
    {
        axis = axis.substr(0, axis.size() - 1);
    }
    if (axis.size() >= 2 && axis[0] == 'a')
    {
        std::snprintf(text, sizeof(text), "AXIS %s%s%s", axis.c_str() + 1, half, inverted ? " (INVERTED)" : "");
        return text;
    }
    return element;
}

// The SDL mapping this gamepad uses now, read only: each control by the
// name the rest of the window gives it, and the joystick's own input behind
// it. (To check what MAP CONTROLLER made, or that its CANCEL put it back.)
static void sdl_mapping_view(const InputWindowState* state, const InputDevice* device, uint32_t controller, float label_width)
{
    SDL_Gamepad* gamepad = input_sdl_gamepad(state->sdl, device->id);
    if (gamepad == nullptr)
    {
        return;
    }
    char* mapping = SDL_GetGamepadMapping(gamepad);
    if (mapping == nullptr)
    {
        return;
    }
    const InputSystem* input = state->input;
    const InputLayout* layout = input_window_layout(input, state->layouts, device);
    const InputProfile* profile = input_profile(input, input_controller_profile_id(input, controller));
    if (retro_ui_section_header("sdl_mapping", piccolo_text(state->strings, Str::input_sdl_mapping), false))
    {
        // "<guid>,<name>,field:value,...": past the GUID and the name.
        const char* field = mapping;
        for (int commas = 0; commas < 2 && field != nullptr; commas++)
        {
            field = std::strchr(field, ',');
            field = field != nullptr ? field + 1 : nullptr;
        }
        while (field != nullptr && *field != '\0')
        {
            const char* end = std::strchr(field, ',');
            std::string text = end != nullptr ? std::string(field, static_cast<size_t>(end - field)) : std::string(field);
            field = end != nullptr ? end + 1 : nullptr;
            size_t colon = text.find(':');
            if (colon == std::string::npos)
            {
                continue;
            }
            std::string name = text.substr(0, colon);
            std::string value = text.substr(colon + 1);
            if (name == "platform" || name == "crc" || name == "hint" || name == "type")
            {
                continue;   // about the mapping, not a control
            }
            char label[64] = {};
            const char* stick = stick_axis_name(name);
            bool known = stick != nullptr;
            if (stick != nullptr)
            {
                std::snprintf(label, sizeof(label), "%s", stick);
            }
            for (size_t f = 0; !known && f < sizeof(SDL_FIELDS) / sizeof(SDL_FIELDS[0]); f++)
            {
                if (name == SDL_FIELDS[f].name)
                {
                    InputSource source = {};
                    source.type = SDL_FIELDS[f].type;
                    source.code = SDL_FIELDS[f].code;
                    input_window_source_label(state->sdl, device, layout, profile, source, label, sizeof(label));
                    known = true;
                }
            }
            if (!known)
            {
                std::snprintf(label, sizeof(label), "%s", name.c_str());   // a field piccolo doesn't know: as SDL names it
            }
            retro_ui_text(label);
            retro_ui_same_line_at(label_width);
            retro_ui_dim_text(element_words(value).c_str(), nullptr);
        }
    }
    SDL_free(mapping);
}

static void draw_device(const InputWindowState* state, const InputDevice* device, float label_width,
                        bool open_by_default, InputWindowAction* action)
{
    const InputSystem* input = state->input;
    const Localization* strings = state->strings;
    {
        int32_t first = input_first_controller(input, device->id);
        retro_ui_push_index(static_cast<uint32_t>(device->id));
        // A mouse has nothing to set here: a line with its name (and why
        // it's unused), not a section that opens on nothing (Andrés, testing
        // round 2).
        if (device->kind == InputDeviceKind::mouse)
        {
            retro_ui_label(device->name.c_str());
            if (!device->connected || input_devices_ignored(device))
            {
                retro_ui_same_line();
                retro_ui_dim_label(piccolo_text(strings, !device->connected ? Str::input_device_disconnected
                                                                            : Str::input_device_ignored),
                                   nullptr);
            }
            retro_ui_pop_id();
            return;
        }
        if (!retro_ui_section_header("device", device->name.c_str(), open_by_default))
        {
            retro_ui_pop_id();
            return;
        }
        if (!device->connected || input_devices_ignored(device))
        {
            Str status = !device->connected ? Str::input_device_disconnected
                         : device->ignore == InputIgnore::yes ? Str::input_device_ignored : Str::input_device_ignored_auto;
            retro_ui_dim_text(piccolo_text(strings, status), nullptr);
        }

        if (device->kind == InputDeviceKind::gamepad && first >= 0)
        {
            const InputController* controller = &input->controllers[static_cast<size_t>(first)];
            uint32_t joystick = input_sdl_joystick_of(state->sdl, device->id);
            if (joystick != 0)
            {
                // Map it again as it is (its buttons by position, its appearance).
                if (retro_ui_button(piccolo_text(strings, Str::input_map)))
                {
                    action->press = InputWindowPress::map;
                    action->joystick = joystick;
                }
            }
            retro_ui_label(piccolo_text(strings, Str::input_name));
            retro_ui_same_line_at(label_width);
            name_field(state, static_cast<uint32_t>(first), retro_ui_width_left(), action);

            retro_ui_label(piccolo_text(strings, Str::input_deadzone));
            retro_ui_item_tooltip(piccolo_text(strings, Str::input_deadzone_help));
            retro_ui_same_line_at(label_width);
            int32_t percent = static_cast<int32_t>(controller->deadzone * 100.0f + 0.5f);
            retro_ui_push_field_text();
            if (retro_ui_slider_int("deadzone", &percent, 0, 50, "%d%%"))
            {
                action->press = InputWindowPress::deadzone;
                action->controller = static_cast<uint32_t>(first);
                action->deadzone = static_cast<float>(percent) / 100.0f;
            }
            retro_ui_pop_field_text();

            retro_ui_label(piccolo_text(strings, Str::input_ignore));
            retro_ui_item_tooltip(piccolo_text(strings, Str::input_ignore_help));
            retro_ui_same_line_at(label_width);
            static const Str CHOICES[] = {Str::input_ignore_auto, Str::input_ignore_yes, Str::input_ignore_no};
            static const InputIgnore VALUES[] = {InputIgnore::automatic, InputIgnore::yes, InputIgnore::no};
            if (retro_ui_begin_choice("ignore", piccolo_text(strings, CHOICES[static_cast<int>(device->ignore)])))
            {
                for (uint32_t i = 0; i < 3; i++)
                {
                    if (retro_ui_choice_item_repick(i, piccolo_text(strings, CHOICES[i]), device->ignore == VALUES[i]))
                    {
                        action->press = InputWindowPress::ignore;
                        action->device_id = device->id;
                        action->ignore = VALUES[i];
                    }
                }
                retro_ui_end_choice();
            }
            extra_button_names(state, device, static_cast<uint32_t>(first), label_width, action);
            sdl_mapping_view(state, device, static_cast<uint32_t>(first), label_width);
        }

        if (device->kind == InputDeviceKind::keyboard)
        {
            // Its players: a NAME each; REMOVE for all but the first.
            const char* remove = piccolo_text(strings, Str::input_remove_part);
            uint32_t number = 0;
            for (uint32_t c = 0; c < input->controllers.size(); c++)
            {
                if (input->controllers[c].device_id != device->id)
                {
                    continue;
                }
                number++;
                retro_ui_push_index(c);
                char player[96] = {};
                std::snprintf(player, sizeof(player), "%s %u", piccolo_text(strings, Str::input_player), static_cast<unsigned>(number));
                retro_ui_label(player);
                retro_ui_same_line_at(label_width);
                bool removable = static_cast<int32_t>(c) != first;
                float width = retro_ui_width_left() - (removable ? retro_ui_button_width(remove) + retro_ui_item_spacing() : 0.0f);
                name_field(state, c, width, action);
                if (removable)
                {
                    retro_ui_same_line();
                    // Not while a SET waits: removing shifts the controller indexes under it.
                    retro_ui_begin_disabled(input->capture.active);
                    bool removed = retro_ui_button(remove);
                    retro_ui_end_disabled();
                    if (removed)
                    {
                        action->press = InputWindowPress::remove_player;
                        action->controller = c;
                        retro_ui_pop_id();
                        break;   // the players after it move up (input_window_apply)
                    }
                }
                retro_ui_pop_id();
            }
            retro_ui_begin_disabled(input->capture.active);
            bool added = retro_ui_button(piccolo_text(strings, Str::input_add_part));
            retro_ui_end_disabled();
            if (added)
            {
                action->press = InputWindowPress::add_player;
                action->device_id = device->id;
            }
            retro_ui_item_tooltip(piccolo_text(strings, Str::input_add_part_help));
        }
        retro_ui_pop_id();
    }
}

// The tabs' contents scroll under the tab bar (the window itself never does).
static void begin_content()
{
    retro_ui_begin_scroll_area("content", 0.0f, 0.0f);
}

static void end_content()
{
    retro_ui_end_scroll_area();
}

void input_window_draw(InputWindow* window, const InputWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                       InputWindowAction* action)
{
    if (!window->open)
    {
        return;
    }
    const Localization* strings = state->strings;
    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    retro_ui_push_theme(pixel_font, scale);
    float width = 360.0f * block;
    float height = 240.0f * block;
    width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    height = height < screen_height - 24.0f * block ? height : screen_height - 24.0f * block;
    RetroUiWindow frame = {};
    frame.id = "input";
    frame.title = piccolo_text(strings, Str::input_title);
    frame.width = width;
    frame.height = height;
    frame.centered_on_open = true;
    frame.scrolls = false;
    bool closed = false;
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        if (closed)
        {
            window->open = false;
        }
        float label_width = 110.0f * block;
        if (retro_ui_begin_tabs("input_tabs"))
        {
            int target = window->capturing ? -1 : retro_ui_tab_switch(window->tab, 4);
            if (retro_ui_begin_tab("general", piccolo_text(strings, Str::input_tab_general), target == 0))
            {
                window->tab = 0;
                begin_content();
                draw_settings(window, state, action);
                end_content();
                retro_ui_end_tab();
            }
            if (retro_ui_begin_tab("interface", piccolo_text(strings, Str::input_section_interface), target == 1))
            {
                window->tab = 1;
                draw_bindings(window, state, InputAction::ui_up, InputAction::ui_open_menu, Str::input_column_action,
                              label_width, action);
                retro_ui_end_tab();
            }
            if (retro_ui_begin_tab("hotkeys", piccolo_text(strings, Str::input_section_hotkeys), target == 2))
            {
                window->tab = 2;
                draw_bindings(window, state, InputAction::hotkey_enable, InputAction::hotkey_undo_load_state,
                              Str::input_column_action, label_width, action);
                retro_ui_end_tab();
            }
            if (retro_ui_begin_tab("devices", piccolo_text(strings, Str::input_tab_devices), target == 3))
            {
                window->tab = 3;
                begin_content();
                draw_devices(state, label_width, action);
                end_content();
                retro_ui_end_tab();
            }
            retro_ui_end_tabs();
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}

// ---------------------------------------------------------------------------
// Apply

void input_window_open(InputWindow* window)
{
    window->open = true;
    window->choose_scope = true;
    window->mapping = UINT32_MAX;   // port 1's controller comes up
    window->mapping_picked = false;
}

// The keyboard's ADD: one more player on it, with no keys yet, named after
// the first ("Keyboard 2"), and picked in MAPPING, ready to map.
static void add_player(InputWindow* window, InputSystem* input, uint32_t device_id)
{
    const InputDevice* device = input_devices_find_const(&input->devices, device_id);
    if (device == nullptr)
    {
        return;
    }
    int32_t first = input_first_controller(input, device_id);
    uint32_t number = 0;
    for (uint32_t c = 0; c < input->controllers.size(); c++)
    {
        number += input->controllers[c].device_id == device_id ? 1u : 0u;
    }
    char name[64] = {};
    const char* base = first >= 0 ? input->controllers[static_cast<size_t>(first)].name.c_str() : device->name.c_str();
    std::snprintf(name, sizeof(name), "%s %u", base, static_cast<unsigned>(number + 1));
    InputBindingSet empty = {};
    window->mapping = input_add_controller(input, device_id, name, &empty);   // ready to map
    window->mapping_picked = true;
}

// Stops a waiting SET / BIND ALL and puts the controller's bindings back as
// they were when it began.
static void cancel_capture(InputWindow* window, InputSystem* input)
{
    if (window->capture_controller < input->controllers.size())
    {
        input->controllers[window->capture_controller].bindings = window->capture_backup;
    }
    input_capture_cancel(input);
    window->capturing = false;
    window->bind_all = false;
}

InputWindowRequest input_window_apply(InputWindow* window, const InputWindowAction* action, InputSystem* input,
                                      Settings* settings, uint64_t now_ms)
{
    SettingsUiEdit edit = action->setting;
    settings_ui_apply_edit(settings, &edit);   // piccolo saves the settings when the window closes
    if (action->rename && action->rename_controller < input->controllers.size())
    {
        input->controllers[action->rename_controller].name = action->name;
        window->dirty = true;
    }
    if (action->button_label)
    {
        std::string id = input_controller_profile_id(input, action->label_controller);
        input_profile_for(input, id)->button_labels[action->label_button] = action->label;
        window->dirty = true;
    }

    uint32_t controller = action->controller;
    uint32_t index = static_cast<uint32_t>(action->input_action);
    switch (action->press)
    {
    case InputWindowPress::none:
        break;
    case InputWindowPress::set:
        window->bind_all = false;
        start_capture(window, input, now_ms, controller, action->input_action, action->row);
        break;
    case InputWindowPress::bind_all:
        window->bind_all = true;
        window->bind_all_last = action->last;
        start_capture(window, input, now_ms, controller, action->input_action, 0);
        break;
    case InputWindowPress::reset:
    {
        InputBindingSet defaults = {};
        input_window_controller_defaults(input, controller, &defaults);
        InputBindingSet* bindings = &input->controllers[controller].bindings;
        input_unbind_all(bindings, action->input_action);
        for (uint32_t s = 0; s < INPUT_SOURCES_PER_ACTION; s++)
        {
            input_bind(bindings, action->input_action, defaults.sources[index][s]);
        }
        window->dirty = true;
        break;
    }
    case InputWindowPress::clear:
        input_sources_remove(input->controllers[controller].bindings.sources[index], action->row);
        window->dirty = true;
        break;
    case InputWindowPress::defaults:
    {
        InputBindingSet defaults = {};
        input_window_controller_defaults(input, controller, &defaults);
        for (uint32_t a = index; a <= static_cast<uint32_t>(action->last); a++)
        {
            for (uint32_t s = 0; s < INPUT_SOURCES_PER_ACTION; s++)
            {
                input->controllers[controller].bindings.sources[a][s] = defaults.sources[a][s];
            }
        }
        window->dirty = true;
        break;
    }
    case InputWindowPress::cancel:
        cancel_capture(window, input);
        break;
    case InputWindowPress::hotkey_mode:
        input->controllers[controller].hotkey_mode = action->hotkey_mode;
        window->dirty = true;
        break;
    case InputWindowPress::deadzone:
        input->controllers[controller].deadzone = action->deadzone;
        window->dirty = true;
        break;
    case InputWindowPress::ignore:
    {
        InputDevice* device = input_devices_find(&input->devices, action->device_id);
        if (device != nullptr)
        {
            device->ignore = action->ignore;
            window->dirty = true;
        }
        break;
    }
    case InputWindowPress::map:
        window->map_request = action->joystick;
        break;
    case InputWindowPress::remove_player:
        if (window->capturing)
        {
            cancel_capture(window, input);   // before the indexes move
        }
        input_remove_controller(input, controller);
        window->mapping = UINT32_MAX;   // indices moved
        window->mapping_picked = false;
        window->dirty = true;
        break;
    case InputWindowPress::add_player:
        add_player(window, input, action->device_id);
        window->dirty = true;
        break;
    case InputWindowPress::edit_touch_layout:
        return InputWindowRequest::edit_touch_layout;
    }
    return InputWindowRequest::none;
}
