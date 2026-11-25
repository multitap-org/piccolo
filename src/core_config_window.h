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

#include "retro_ui_font.h"

enum class CoreConfigTab
{
    options,
    controls,
    shader,
    count,
};

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
    uint32_t      ports_selected = 0;   // CONTROLS: the port open (UINT32_MAX: none)
    int32_t       remap_section = 0;    // the remap's open section (one at a time; -1 none)
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
    // the most specific one in use when the window opens), and CHOOSE, which
    // piccolo answers by opening the shader picker for that scope.
    SettingLayer  shader_scope = SettingLayer::global;
    bool          shader_choose_scope = true;
    bool          choose_shader_request = false;
};

struct CoreConfigContext
{
    const Localization*  strings = nullptr;
    // OPTIONS (core_options_ui.h): its own state, what it shows, what was done.
    CoreOptionsUi*            core_options_ui = nullptr;
    const CoreOptionsUiState* core_options = nullptr;
    CoreOptionsUiAction*      core_options_action = nullptr;
    InputSystem*         input = nullptr;   // CONTROLS: who plays on which port
    LibretroHost*        host = nullptr;    // CONTROLS: the core's devices per port, and the one plugged in
    Settings*            settings = nullptr;   // CONTROLS: the device picked is kept per core
    const InputSdl*      sdl = nullptr;        // CONTROLS: button names
    const InputLayouts*  layouts = nullptr;    // CONTROLS: the controllers' button names
    uint64_t             now_ms = 0;
    const char*          core_name = "";         // the title line and SHADER's scope names
    const char*          game_name = "";
    bool                 shaders_available = false;   // librashader was found
};

// Opens on the tab it was last closed on (the remap view and any SET closed).
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

// Draws the window while open (its X closes it: open = false). Also drops
// a SET or a macro recording nobody looks at any more: call every frame.
void core_config_window_draw(CoreConfigWindow* config, const CoreConfigContext* context, RetroUiFont* pixel_font,
                             int32_t scale);
