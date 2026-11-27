#pragma once

// CORE CONFIGURATION (decided by Andrés, 2026-11-22): the running core's
// settings, split out of QUICK MENU (which keeps the actions). A MAIN MENU
// window, opened from the menu bar or from QUICK MENU's CONFIG:
//
//   +- CORE CONFIGURATION ------------------------------------+
//   | SNES9X - SUPER METROID                                   |
//   | [CORE OPTIONS] [CONTROLS] [SHADER]                       |
//   |  ...the tab...                                            |
//   +----------------------------------------------------------+
//
//   CORE OPTIONS  the running core's options, live (core_options_ui)
//   CONTROLS      who plays on which port, each port's core device, and the
//                 remap of one port's controller (the advanced mode: SET,
//                 BIND ALL, combos, macros, turbo, mouse / lightgun)
//   SHADER        the preset (CHOOSE: piccolo opens the shader picker) and
//                 FILTER, with their scope (GLOBAL / THIS CORE / THIS GAME)
//
// PAGE LEFT / RIGHT switch tabs. Core options and remaps are saved when it
// closes (main.cpp), so an option that crashes the core at once isn't kept.
//
// Split like every window: core_config_window_update runs a SET / BIND ALL
// or a macro recording in progress and picks SHADER's scope (no GUI),
// core_config_window_state fills what the window shows,
// core_config_window_draw draws it and records what was pressed, and
// core_config_window_apply changes the ports, the remaps, the combos and
// macros, the core devices and the shader settings (no GUI).

#include <cstdint>
#include <string>
#include <vector>

#include "core_options_ui.h"
#include "core_session.h"
#include "input_bindings.h"
#include "input_layouts.h"
#include "input_sdl.h"
#include "localization.h"
#include "settings.h"
#include "settings_ui.h"

#include "retro_ui_font.h"

enum class CoreConfigTab
{
    options,
    controls,
    shader,
    count,
};

// What the window keeps between frames (the drawing may change it).
struct CoreConfigWindow
{
    bool          open = false;
    CoreConfigTab tab = CoreConfigTab::options;
    bool          select_tab = false;   // select `tab` on the next draw (opened on it, PAGE LEFT / RIGHT)

    // CONTROLS can show the remap of one port's controller for the running
    // core and that port's device: its controls by the game's names, SET,
    // BIND ALL, DEFAULT ALL, and the core's combos bound to inputs. It is the
    // core's remap, or THIS GAME's when the game has its own (SCOPE). Changes
    // mark input_dirty (piccolo saves input.cfg).
    bool          remap = false;
    uint32_t      remap_port = 0;
    uint32_t      ports_selected = UINT32_MAX;   // CONTROLS: the port open (UINT32_MAX: none, at first)
    int32_t       remap_section = -1;   // the remap's open section (one at a time; -1 none, at first)
    uint32_t      remap_controller = 0;
    bool          capturing = false;
    InputAction   capture_action = InputAction::pad_b;
    uint32_t      capture_row = 0;      // the row (source) being set; its count = a new row (+)
    InputCoreProfile capture_backup;    // the remap as the SET / BIND ALL began (CANCEL)
    uint64_t      capture_started_ms = 0;
    bool          bind_all = false;
    bool          capture_seen = false; // the remap view looked at the capture this frame (else it's dropped)
    std::string   capture_combo;        // SET on a combo's row: its id ("" = on capture_action)
    InputAction   pad_action = InputAction::count;   // the row the drawn pad shows when not capturing (focused or hovered)
    std::string   pad_combo;            // ...or this combo's row (its id)
    uint32_t      pad_row = 0;          // which of its sources
    // COMBOS: making or changing one, in a modal (the core's, any game of it can bind it).
    bool          combo_editing = false;
    std::string   combo_edit_id;        // "" = a new one
    char          combo_edit_name[64] = {};
    uint16_t      combo_edit_buttons = 0;
    // MACROS: the same, in their own modal, and its recorder.
    bool          macro_editing = false;
    std::string   macro_edit_id;        // "" = a new one
    char          macro_edit_name[64] = {};
    std::vector<InputMacroStep> macro_edit_steps;
    bool          macro_edit_repeat = false;
    int32_t       macro_edit_selected = -1;   // the step whose buttons are shown (-1 none)
    int32_t       macro_change_steps = 3;     // CHANGE STEPS TO
    int32_t       macro_change_pauses = 3;    // CHANGE PAUSES TO
    int32_t       macro_record = 0;           // 0 off, 1 getting ready, 2 recording (the router suspended)
    uint64_t      macro_record_ms = 0;        // when getting ready began, then the last change
    bool          macro_record_seen = false;  // the modal looked at the recording this frame (else it's dropped)
    std::vector<uint64_t> macro_record_times;   // a change of the RetroPad buttons: when...
    std::vector<uint16_t> macro_record_buttons; // ...and to what
    bool          input_dirty = false;
    // SHADER: the settings scope it edits (GLOBAL / THIS CORE / THIS GAME,
    // the most specific one in use when the window opens). Its CHOOSE is
    // answered by piccolo (CoreConfigRequest::choose_shader), which opens the
    // shader picker for that scope.
    SettingLayer  shader_scope = SettingLayer::global;
    bool          shader_choose_scope = true;   // pick the scope on the next update (just opened)
};

// What the window shows from the rest of piccolo, read only. The drawing
// reads the controllers, ports, devices, remaps, combos and macros through
// `input`, the core's ports and descriptors through `host`, SDL's gamepads
// through `sdl` and the shader rows through `settings`.
struct CoreConfigWindowState
{
    const Localization*  strings = nullptr;
    const char*          core_name = "";         // the title line and SHADER's scope names
    const char*          game_name = "";
    uint64_t             now_ms = 0;
    bool                 shaders_available = false;   // librashader was found
    CoreOptionsUiState   options;                // OPTIONS (core_options_ui.h)
    const InputSystem*   input = nullptr;        // CONTROLS: who plays on which port (nullptr: no CONTROLS)
    const LibretroHost*  host = nullptr;         // CONTROLS: the core's devices per port, and the one plugged in
    const Settings*      settings = nullptr;     // SHADER's rows (nullptr: no SHADER)
    const InputSdl*      sdl = nullptr;          // CONTROLS: button names
    const InputLayouts*  layouts = nullptr;      // CONTROLS: the controllers' button names
    bool                 game_loaded = false;    // a core with a game: CORE DEVICE, REMAP
    uint32_t             ports = INPUT_PORT_COUNT;   // the ports shown (SETTINGS > INPUT > PORTS)
    uint64_t             press_ms = 0;           // TIME TO PRESS (SET's countdown)
    // A SET / BIND ALL in progress: the control being held, and how far.
    bool                 holding = false;
    InputSource          held = {};
    float                hold_progress = 0.0f;
    // The remap view (when it's up and its controller and port exist): what
    // plays now, the game's own remap if any, the controller and its names.
    bool                    remap_valid = false;
    InputBindingSet         remap_effective;
    const InputCoreProfile* remap_active = nullptr;   // the remap in use (its combos' bindings)
    const InputCoreProfile* remap_game = nullptr;     // THIS GAME's own remap, or nullptr (SCOPE)
    const InputDevice*      remap_pad = nullptr;
    const InputLayout*      remap_layout = nullptr;
    const InputProfile*     remap_profile = nullptr;
};

enum class CoreConfigPress
{
    none,
    port_device,          // CORE DEVICE picked: `port`'s core device is `device`
    unassign,             // UNASSIGN: `controller` leaves `port`
    add,                  // ADD: `controller` joins `port`
    move_port,            // MOVE UP / DOWN: `port` swaps with `other_port`
    remap_back,           // the remap's BACK: kept as it is, and seen (not asked again)
    remap_scope_core,     // SCOPE: THIS CORE (the game's own remap is dropped)
    remap_scope_game,     // SCOPE: THIS GAME (a copy of the core's)
    bind_all,             // BIND ALL: from `input_action` on
    use_retropad,         // DEFAULT ALL: the remap back to the RetroPad's
    cancel_capture,       // CANCEL: the SET / BIND ALL stops, its changes put back
    set,                  // SET or +: `input_action`'s `row` takes the next control
    clear,                // CLEAR: `row` of `input_action` removed
    percent,              // a stick direction's `row` pushes it `percent`
    turbo,                // TURBO box: `row` of `input_action` gets `turbo`
    combo_set,            // SET or + on combo / macro `combo`'s `row`
    combo_clear,          // CLEAR on combo / macro `combo`'s `row`
    combo_save,           // the combo editor's SAVE (what it edits is in the window)
    combo_delete,         // the combo editor's DELETE
    macro_save,           // the macro editor's SAVE
    macro_delete,         // the macro editor's DELETE
    macro_record,         // RECORD: get ready, then record
    macro_stop,           // STOP: a recording ends (getting ready: cancelled)
    macro_cancel_record,  // Escape while recording: cancelled
};

// What the user did this frame (zeroed before drawing).
struct CoreConfigWindowAction
{
    CoreOptionsUiAction options;             // OPTIONS: piccolo applies it (core_options_ui_apply)
    SettingsUiEdit      setting;             // SHADER: a settings row changed
    bool                choose_shader = false;   // SHADER's CHOOSE
    CoreConfigPress     press = CoreConfigPress::none;
    uint32_t            port = 0;
    uint32_t            other_port = 0;
    uint32_t            controller = 0;
    unsigned            device = 0;
    InputAction         input_action = InputAction::pad_b;
    uint32_t            row = 0;
    uint8_t             percent = 0;
    bool                turbo = false;
    std::string         combo;               // the combo's or macro's id
};

// What main.cpp does itself after core_config_window_apply.
enum class CoreConfigRequest
{
    none,
    choose_shader,   // open the shader picker for shader_scope
};

// Opens on the tab it was last closed on (the remap view closed; a SET still waiting is cancelled
// by the next core_config_window_apply).
void core_config_window_open(CoreConfigWindow* config);

// Opens on CONTROLS (a controller went away: rearrange the ports).
void core_config_window_open_controls(CoreConfigWindow* config);

// Opens on CONTROLS showing the remap of `controller` on `port` (HOLD START
// TO REMAP was held; later the remap widget's ADVANCED).
void core_config_window_open_remap(CoreConfigWindow* config, uint32_t port, uint32_t controller);

// The remap view is up (not while a SET waits): BACK leaves it first.
bool core_config_window_sub_view(const CoreConfigWindow* config);
// BACK out of the remap view.
void core_config_window_close_sub_view(CoreConfigWindow* config);

// Every frame, before the state: SHADER's scope (just opened: the most
// specific layer in use; the core or game gone: GLOBAL), a remap whose
// controller or core is gone closes, and a SET / BIND ALL or a macro
// recording in progress runs.
void core_config_window_update(CoreConfigWindow* config, InputSystem* input, const LibretroHost* host,
                               const Settings* settings, const Localization* strings, const char* core_name,
                               const char* game_name, uint64_t now_ms);

void core_config_window_state(CoreConfigWindowState* state, const CoreConfigWindow* config,
                              const Localization* strings, const CoreOptionsUiState* options, const InputSystem* input,
                              const LibretroHost* host, const Settings* settings, const InputSdl* sdl,
                              const InputLayouts* layouts, const char* core_name, const char* game_name,
                              bool shaders_available, uint64_t now_ms);

// Draws the window while open (its X closes it: open = false). OPTIONS is
// drawn with `options_ui`, the core options panel's own state.
void core_config_window_draw(CoreConfigWindow* config, CoreOptionsUi* options_ui, const CoreConfigWindowState* state,
                             RetroUiFont* pixel_font, int32_t scale, CoreConfigWindowAction* action);

// The press and SHADER's row edit: ports, core devices, remaps, combos and
// macros, a SET / BIND ALL started or cancelled, a recording started or
// stopped. Also drops a SET or a macro recording nobody looks at any more:
// call every frame. Changes to the input mark input_dirty. (OPTIONS' action
// is applied by piccolo.)
CoreConfigRequest core_config_window_apply(CoreConfigWindow* config, const CoreConfigWindowAction* action,
                                           InputSystem* input, LibretroHost* host, Settings* settings, uint64_t now_ms);
