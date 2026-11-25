#pragma once

// SETTINGS > INPUT (milestone 3, step 4), one level of tabs:
//
//   SETTINGS   the input settings, with the SCOPE selector (GLOBAL / THIS
//              CORE / THIS GAME): keyboard shares port 1. (HOTKEYS has the
//              hotkey mode the same way, above its bindings.)
//   RETROPAD   the bindings of one controller (MAPPING: the one on port 1
//   INTERFACE  unless another is picked), no scope (they're the
//   HOTKEYS    controller's): GAME / GAMEPAD (or KEY) columns, SET / CLEAR,
//              RESET on changed rows (shown highlighted), BIND ALL asks for
//              each in turn (5 s each skips one, Esc stops).
//   DEVICES    every device: state, NAME, DEADZONE, IGNORE; keyboard players.
//
// Nothing about the running game here: a core's own devices are asked for
// in the game (step 5). Binding changes mark the window dirty (piccolo
// saves settings/input.cfg); the settings go to piccolo.cfg.
//
// Split like every window: input_window_update runs a SET / BIND ALL in
// progress (no GUI), input_window_state fills what the window shows,
// input_window_draw draws it and records what was pressed, and
// input_window_apply changes the bindings, devices and settings (no GUI).

#include <cstdint>
#include <string>
#include <vector>

#include "input_bindings.h"
#include "input_layouts.h"
#include "input_sdl.h"
#include "localization.h"
#include "settings.h"
#include "settings_ui.h"

#include "retro_ui_font.h"

// What the window keeps between frames (the drawing may change it).
struct InputWindow
{
    bool         open = false;
    int          tab = 0;                    // 0 SETTINGS .. 3 DEVICES (for PAGE LEFT / RIGHT)
    SettingLayer scope = SettingLayer::global;
    bool         choose_scope = false;       // pick the scope on the next draw (just opened)
    uint32_t     mapping = UINT32_MAX;       // the controller mapped in RETROPAD / INTERFACE / HOTKEYS (UINT32_MAX: port 1's)
    bool         mapping_picked = false;     // picked in its list: stays; else it follows port 1 (who joins it)

    bool        capturing = false;           // SET: waiting for a key or button
    InputAction capture_action = InputAction::pad_b;
    uint32_t    capture_row = 0;             // the row (source) being set; its count = a new row (+)
    uint32_t    capture_controller = 0;
    InputBindingSet capture_backup;          // the controller's bindings as the SET / BIND ALL began (CANCEL)
    InputAction pad_action = InputAction::count;   // the row the floating pad shows (focused or hovered)
    uint32_t    pad_row = 0;
    uint64_t    capture_started_ms = 0;
    bool        bind_all = false;            // BIND ALL: go on to the next action...
    InputAction bind_all_last = InputAction::pad_b;   // ...up to this one

    // DEVICES > MAP...: this joystick. Kept here: piccolo opens MAP CONTROLLER
    // from its input step (with the offers), on the next frame.
    uint32_t     map_request = 0;
    bool dirty = false;                      // bindings or devices changed: save settings/input.cfg
};

// What the window shows from the rest of piccolo, read only. The drawing
// reads the controllers, devices and profiles through `input`, and SDL's
// gamepads (labels, extra buttons, mapping) through `sdl`.
struct InputWindowState
{
    const InputSystem*  input = nullptr;
    const InputSdl*     sdl = nullptr;
    const Localization* strings = nullptr;
    const Settings*     settings = nullptr;  // the rows read it (settings_ui)
    const char*         core_name = "";      // "" = no core running (SCOPE)
    const char*         game_name = "";
    const InputLayouts* layouts = nullptr;   // the controllers' button names
    uint64_t            now_ms = 0;
    uint64_t            press_ms = 0;        // TIME TO PRESS (SET's countdown)
    // A SET / BIND ALL in progress: the control being held, and how far.
    bool                holding = false;
    InputSource         held = {};
    float               hold_progress = 0.0f;
};

enum class InputWindowPress
{
    none,
    set,                // SET or +: `controller`'s `input_action` takes its next control into `row`
    bind_all,           // BIND ALL: `controller`, from `input_action` to `last`
    reset,              // a row's RESET: `input_action` back to `controller`'s defaults
    clear,              // CLEAR: `row` of `input_action` removed
    defaults,           // DEFAULTS: `input_action` to `last` back to the defaults
    cancel,             // CANCEL: the SET / BIND ALL stops, its changes put back
    hotkey_mode,        // HOTKEY MODE picked: `hotkey_mode`
    deadzone,           // DEADZONE moved: `deadzone`
    ignore,             // IGNORE picked: `device_id`'s `ignore`
    map,                // DEVICES > MAP...: `joystick` (piccolo opens MAP CONTROLLER)
    remove_player,      // a keyboard player's REMOVE: `controller`
    add_player,         // the keyboard's ADD: one more player on `device_id`
    edit_touch_layout,  // SETTINGS > EDIT LAYOUT
};

constexpr uint32_t INPUT_WINDOW_NAME_MAX = 64;
constexpr uint32_t INPUT_WINDOW_LABEL_MAX = 48;

// What the user did this frame (zeroed before drawing).
struct InputWindowAction
{
    SettingsUiEdit   setting;                // a settings row changed (SETTINGS)
    InputWindowPress press = InputWindowPress::none;
    uint32_t         controller = 0;
    InputAction      input_action = InputAction::pad_b;
    InputAction      last = InputAction::pad_b;
    uint32_t         row = 0;
    InputHotkeyMode  hotkey_mode = InputHotkeyMode::hold;
    float            deadzone = 0.0f;
    uint32_t         device_id = 0;
    InputIgnore      ignore = InputIgnore::automatic;
    uint32_t         joystick = 0;
    // A NAME field left with a new name (may come with a press: the click
    // that left it).
    bool             rename = false;
    uint32_t         rename_controller = 0;
    char             name[INPUT_WINDOW_NAME_MAX] = {};
    // An extra button's own name typed (DEVICES).
    bool             button_label = false;
    uint32_t         label_controller = 0;
    uint16_t         label_button = 0;
    char             label[INPUT_WINDOW_LABEL_MAX] = {};
};

// What main.cpp does itself after input_window_apply.
enum class InputWindowRequest
{
    none,
    edit_touch_layout,   // close INPUT and edit the on-screen pad
};

void input_window_open(InputWindow* window);

// Every frame, before the state (and while closed: a SET / BIND ALL in
// progress stops): runs a SET / BIND ALL in progress.
void input_window_update(InputWindow* window, InputSystem* input, uint64_t now_ms);

void input_window_state(InputWindowState* state, const InputSystem* input, const InputSdl* sdl,
                        const Localization* strings, const Settings* settings, const char* core_name,
                        const char* game_name, const InputLayouts* layouts, uint64_t now_ms);

// Draws the window while open.
void input_window_draw(InputWindow* window, const InputWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                       InputWindowAction* action);

// The settings row's edit, and the press: bindings, devices, players, a SET
// / BIND ALL started or cancelled. DEVICES > MAP... sets window->map_request.
InputWindowRequest input_window_apply(InputWindow* window, const InputWindowAction* action, InputSystem* input,
                                      Settings* settings, uint64_t now_ms);

// A gamepad's layout (its appearance, as kept in its profile), or nullptr
// when it has none (never mapped, declined, or a layout that's gone).
const InputLayout* input_window_layout(const InputSystem* input, const InputLayouts* layouts, const InputDevice* device);

// A source as the user sees it on that device: "RETURN", "A", "CROSS",
// "D-PAD UP", "LEFT STICK RIGHT", "ZL"... A button the controller's profile
// gave a name to has that name; the rest are named by its layout, without
// one by SDL's labels. `profile` may be nullptr.
void input_window_source_label(const InputSdl* sdl, const InputDevice* device, const InputLayout* layout,
                               const InputProfile* profile, InputSource source, char* out, size_t out_size);

// A binding's sources in `width`: all of them ("D-PAD UP, LEFT STICK UP"),
// or when they don't fit the first and how many more ("D-PAD UP +1") with
// all of them in the tooltip; one alone that doesn't fit is cut short with
// "…" (all of it in the tooltip); NONE when there's none.
void input_window_draw_sources(const InputSdl* sdl, const InputDevice* device, const InputLayout* layout,
                               const InputProfile* profile, const InputSource* sources, float width, const Localization* strings);

// Waiting for a binding: PRESS A BUTTON (4) / PRESS A KEY (4).
void input_window_draw_waiting(bool keyboard, uint64_t seconds_left, float hold_progress, const Localization* strings);

// The defaults a controller goes back to with RESET / DEFAULTS: the
// keyboard's or a gamepad's, none for the keyboard's extra players.
void input_window_controller_defaults(const InputSystem* input, uint32_t controller, InputBindingSet* set);
