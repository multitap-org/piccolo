#pragma once

// The whole frame's drawing, in one call. The app (src/app/) fills a UiView
// (what the frame shows, as plain data, no GUI calls), ui_draw draws it with
// retroui and the windows' draw functions and reports what the user did as
// UiActions, and the app applies them afterwards (no GUI calls either).
// Another GUI toolkit would rewrite this file and the windows' drawing;
// main.cpp's loop would stay as it is (Andrés: "no gui code in the main
// loop").
//
// The windows' own state (FooWindow: open, selection, scroll, text being
// typed) is owned by the App and handed over in UiWindows: the drawing may
// change it. Everything else reaches the drawing only through UiView.

#include <cstdint>
#include <string>

#include "about_window.h"
#include "core_config_window.h"
#include "continue_panel.h"
#include "core_options_ui.h"
#include "cores_window.h"
#include "debug_preview.h"
#include "debug_window.h"
#include "dvr_widget.h"
#include "file_picker.h"
#include "fps_meter.h"
#include "guest_window.h"
#include "hud_view.h"
#include "input_window.h"
#include "map_window.h"
#include "mobile_home.h"
#include "net_share_link.h"
#include "net_window.h"
#include "onscreen_pad.h"
#include "pair_window.h"
#include "ports_widget.h"
#include "quick_menu.h"
#include "retro_ui.h"
#include "retro_ui_font.h"
#include "safe_start_window.h"
#include "settings_error_window.h"
#include "settings_window.h"
#include "shader_check_view.h"
#include "share_window.h"
#include "snow_view.h"
#include "shown_image.h"
#include "text_input_window.h"

constexpr size_t UI_NOTE_MAX = 200;

// SETTINGS' notes as text (SettingsWindowNotes points into these).
struct UiSettingsText
{
    char window_size[UI_NOTE_MAX] = {};
    char audio_latency[UI_NOTE_MAX] = {};
    char frame_delay[UI_NOTE_MAX] = {};
    char run_ahead[UI_NOTE_MAX] = {};
};

// Everything the frame shows, in drawing order. Filled by app_ui.cpp
// (app_ui_view) after the frame's updates; read only by ui_draw.
struct UiView
{
    float screen_width = 0.0f;   // the toolkit's units (ui_toolkit_screen_size)
    float screen_height = 0.0f;

    // The game's picture, behind everything (none while no game shows).
    bool       game_shown = false;
    ShownImage game_image = {};

    // The menu snow over it (behind every window and the menu bar), when it falls.
    const SnowView* snow = nullptr;

    // The touch gamepad over the game, the ports' tabs, the HUD.
    OnscreenPadState pad = {};
    PortsWidgetState ports_tabs = {};
    const HudStack*  hud = nullptr;     // the App's, built this frame
    HudArea          hud_area = {};     // retro pixels
    uint64_t         hud_now_ms = 0;
    ContinuePanelView continue_card;  // the CONTINUE card on the empty screen (or the debug window's fake one)
    uint32_t         backdrop = 0;   // ARGB behind the windows: BACKGROUND, at 100 - BACKGROUND TRANSPARENCY (alpha 0: none)

    // The menu bar (messages are HUD cards: message_widget.h).
    bool               menu_shown = false;   // the MAIN MENU's bar (not during EDIT LAYOUT)
    const RetroUiMenu* menus = nullptr;
    int32_t            menu_count = 0;
    bool               open_first_menu = false;

    // The phone UI (PICCOLO_MOBILE, --mobile): HOME and the ☰ sidebar in
    // place of the menu bar (mobile_home.h); the windows are pages.
    bool            phone = false;
    MobileHomeState mobile_home = {};

    // The windows, in drawing order.
    UiSettingsText          settings_text;
    NetWindowState          network = {};
    NetShareLinkStatus      settings_hosts;   // the NETWORK tab's hosts (SHARE's connection), when known
    SettingsWindowState     settings = {};
    FilePickerState         picker = {};
    QuickMenuState          quick = {};
    CoreOptionsUiState      config_options = {};
    CoreConfigWindowState   config = {};
    FilePickerState         shader_picker = {};
    PortsWidgetState        ports_board = {};
    DvrWidgetState          dvr = {};
    CoreOptionsUiState      manager = {};
    CoresWindowState        cores = {};
    bool                    share_shown = false;
    ShareTransferLine       share_line;
    ShareWindowState        share = {};
    bool                    pair_shown = false;
    PairWindowState         pair = {};
    bool                    guest_shown = false;
    GuestWindowState        guest = {};
    InputWindowState        input = {};
    MapWindowState          map = {};
    bool                    safe_start_shown = false;
    SafeStartWindowState    safe_start = {};
    bool                    settings_error_shown = false;   // piccolo.cfg could not be read, not answered yet
    SettingsErrorWindowState settings_error = {};
    bool                    shader_check_shown = false;
    ShaderCheckText         shader_check_text;
    std::string             shader_check_preset;   // the preset's file name, no folder or extension
    double                  shader_check_measured_fps = 0.0;
    double                  shader_check_target_fps = 0.0;
    uint64_t                shader_check_asked_ms = 0;
    uint64_t                shader_check_now_ms = 0;
    bool                    text_input_shown = false;
    TextInputWindowState    text_input = {};
    bool                    debug_shown = false;
    DebugWindowState        debug = {};
    DebugPreviewView        preview = {};   // the debug window's preview of a window, if any
    AboutWindowState        about = {};
    bool                    fps_shown = false;
    const FpsMeter*         fps_meter = nullptr;
    double                  fps_target = 0.0;
    bool                    cursor_shown = false;   // the theme's cursor (a menu or window is up)
};

// The windows' own state (owned by the App): the drawing may change it.
struct UiWindows
{
    OnscreenPad*       pad = nullptr;
    MobileHome*        mobile_home = nullptr;
    PortsWidget*       ports = nullptr;
    SettingsWindow*    settings = nullptr;
    NetWindow*         network = nullptr;   // SETTINGS > NETWORK
    FilePicker*        picker = nullptr;
    QuickMenu*         quick = nullptr;
    CoreConfigWindow*  config = nullptr;
    CoreOptionsUi*     options_ui = nullptr;   // shared by CORE CONFIGURATION and the manager
    FilePicker*        shader_picker = nullptr;
    DvrWidget*         dvr = nullptr;
    bool*              manager_open = nullptr;   // the CORE OPTIONS MANAGER
    CoresWindow*       cores = nullptr;
    ShareWindow*       share = nullptr;
    PairWindow*        pair = nullptr;      // TOOLS > PAIR... (a pairing request too)
    GuestWindow*       guest = nullptr;
    InputWindow*       input = nullptr;
    MapWindow*         map = nullptr;
    TextInputWindow*   text_input = nullptr;
    AboutWindow*       about = nullptr;
    DebugPreviews*     previews = nullptr;   // the debug window's fake windows
};

// What the user did this frame, every window's action. Zeroed before
// drawing; a window not drawn leaves its action zeroed.
struct UiActions
{
    OnscreenPadAction        pad = {};
    int32_t                  menu_picked = -1;   // the menu bar's item id (-1: none)
    MobileHomeAction         mobile_home = {};   // the phone's HOME and ☰ (their picks are the bar's ids)
    ContinueViewButton       continue_pressed = ContinueViewButton::none;   // the CONTINUE card's button
    int32_t                  continue_tile = -1;   // ... the shelf's tile chosen (on the page shown)
    int32_t                  continue_page = 0;    // ... the shelf's page arrows (-1 / +1)
    SettingsWindowAction     settings = {};
    FilePickerAction         picker = {};
    QuickMenuAction          quick = {};
    CoreConfigWindowAction   config = {};
    FilePickerAction         shader_picker = {};
    PortsWidgetAction        ports = {};
    DvrWidgetAction          dvr = {};
    CoreOptionsManagerAction manager = {};
    CoresWindowAction        cores = {};
    ShareWindowAction        share = {};
    PairWindowAction         pair = {};
    GuestWindowAction        guest = {};
    InputWindowAction        input = {};
    MapWindowAction          map = {};
    SafeStartWindowAction    safe_start = {};
    SettingsErrorWindowAction settings_error = {};
    ShaderCheckChoice        shader_check = ShaderCheckChoice::none;
    TextInputWindowAction    text_input = {};
    DebugWindowAction        debug = {};
    DebugPreviewActions      preview = {};
    AboutWindowAction        about = {};
};

// All the frame's drawing, back to front: the game, the menu snow, the touch
// gamepad, the ports' tabs, the HUD, the CONTINUE card, the backdrop, the menu bar, the
// windows, the debug window, the FPS meter, the scrollbars, the cursor.
// Between the toolkit's begin and end of frame.
void ui_draw(const UiView* view, UiWindows* windows, RetroUiFont* pixel_font, int32_t scale, UiActions* actions);
