#pragma once

// piccolo's settings window, in the retro style:
//
//   +- SETTINGS ------------------------------------------+
//   | SCOPE [ THIS CORE (SNES9X)                       v ] |   only while a game runs
//   | [GENERAL] [INTERFACE] [VIDEO] [SOUND] [NETWORK]     |
//   |   [PICTURE] [DISPLAY] [SYNC]                        |
//   |   ASPECT RATIO  [ CORE PROVIDED            v ]      |
//   | C SCALING       [ INTEGER (OVER)           v ] [RESET]
//   |   ...                                               |
//   |   [IMAGE SETTINGS...]                               |
//   +-----------------------------------------------------+
//
// The scope says which layer changes go to: GLOBAL, THIS CORE or THIS GAME
// (plan-settings: avoids "which layer did my change go to"). When the window
// opens it starts at the most specific layer already in use (Andrés,
// 2026-11-13): a game with its own settings opens on THIS GAME, else a core
// with its own on THIS CORE, else GLOBAL. Changes keep going to the override.

#include <cstdint>
#include <string>
#include <vector>

#include "app_paths.h"
#include "image_window.h"
#include "localization.h"
#include "net_window.h"
#include "piccolo_settings.h"
#include "settings.h"

#include "retro_ui_font.h"

enum class SettingsTab
{
    general,
    interface,
    video,
    sound,
    network,
};

// Which of GENERAL's folder sections is open (one at a time).
enum class FolderSection
{
    none,
    folders,        // CORES, SYSTEM, SAVES, STATES
    game_folders,
};

// What the window keeps between frames (the drawing may change it).
struct SettingsWindow
{
    bool         open        = false;
    bool         image_open  = false;   // the IMAGE SETTINGS window
    SettingsTab  tab         = SettingsTab::general;
    bool         select_tab  = false;   // select `tab` on the next draw
    SettingLayer scope       = SettingLayer::global;
    bool         choose_scope = false;  // pick the scope on the next update (just opened)
    bool         save_theme_open = false;      // the SAVE THEME popup (asks for the name)
    std::string  save_theme_target;            // the custom theme SAVE replaces; "" = NEW (asks for a name)
    char         save_theme_name[48] = {};
    std::string  theme_choice;                 // the theme last picked or saved: the lists show it first when several match
    bool         theme_custom = false;         // THEME was set to CUSTOM (else CUSTOM only when no shipped theme matches)
    FolderSection folder_section = FolderSection::none;   // both closed at first (testing round 2)
};

// What piccolo says about itself this frame, as text for the tabs (main.cpp
// builds it: the window, the GPU, the audio, AUTO's verdicts).
struct SettingsWindowNotes
{
    const char*        window_size = "";   // what the window mode gives, for the Display tab
    const char*        present_wait = "";  // how WAIT FOR PRESENT works on this GPU, for the Sync tab
    const char*        frame_delay = "";   // the frame delay in use (AUTO), for the Sync tab
    const char*        run_ahead = "";     // what the running core does with RUN-AHEAD, for the Sync tab
    bool               frame_delay_found = false;   // this game has a locked AUTO delay (SEARCH AGAIN shown)
    const char* const* audio_devices = nullptr;   // playback devices connected now, for the Sound tab
    uint32_t           audio_device_count = 0;
    const char*        audio_latency = "";  // the latency in use, for the Sound tab
    const char*        game_mode = "";      // GameMode's state, for the General tab
    const char*        rewind_note = "";    // the running game's rewind: AUTO's choice, the mode now ("" = no game)
    bool               rewind_known = false;   // the running game has AUTO's verdict (MEASURE AGAIN shows)
};

constexpr uint32_t SETTINGS_FOLDER_COUNT = 5;   // CORES, SYSTEM, SAVES, STATES, SCREENSHOTS

// What the window shows from the rest of piccolo, read only.
struct SettingsWindowState
{
    const Settings*     settings = nullptr;   // the rows read it (settings_ui)
    const Localization* strings = nullptr;    // UI text (piccolo_text.h)
    const char*         core_name = "";       // "" = no core running
    const char*         game_name = "";       // "" = no game (or running without content)
    SettingsWindowNotes notes;
    // FOLDERS: the default folders, shown in empty fields (in the order above).
    const char*         folder_hints[SETTINGS_FOLDER_COUNT] = {};
    // GAME FOLDERS: the ones set, like piccolo_settings_game_folders (a file
    // edited by hand may have gaps). game_folder_slots[i] is the slot row i
    // came from (what REMOVE and CHOOSE FOLDER take); game_folder_free is the
    // first empty slot, where "+" adds.
    const char*         game_folders[PICCOLO_GAME_FOLDER_MAX] = {};
    uint32_t            game_folder_slots[PICCOLO_GAME_FOLDER_MAX] = {};
    uint32_t            game_folder_count = 0;
    uint32_t            game_folder_free = 0;
    bool                game_mode_supported = false;   // Linux only: GAME MODE shows
    // At the scope being edited:
    bool                window_fixed = false;      // WINDOW MODE is FIXED (ASPECT and HEIGHT, else SCALE)
    uint32_t            latency_preset = 0;        // the PRESET the latency values match (or CUSTOM)
    bool                frame_delay_usable = false;   // WAIT FOR PRESENT is on
    bool                frame_delay_auto = false;     // FRAME DELAY AUTO is on
    bool                rate_control = false;         // RATE CONTROL is on (MAX DEVIATION usable)
    const std::vector<PiccoloThemeFile>* themes = nullptr;   // themes/*.cfg, for THEME
    ImageWindowState    image;                   // IMAGE SETTINGS (and the crop shown under its button)
    const NetWindowState* network = nullptr;     // the NETWORK tab; nullptr: no tab
};

// What the user did this frame. main.cpp acts on the requests itself
// (CHOOSE FOLDER, SEARCH AGAIN, MEASURE AGAIN, SAVE THEME); settings_window_apply
// does the rest, net_window_apply the NETWORK tab's.
struct SettingsWindowAction
{
    SettingsUiEdit setting;                    // a settings row changed
    bool           remove_game_folder = false; // GAME FOLDERS' "-" on row `game_folder`
    uint32_t       game_folder = 0;
    bool           set_latency_preset = false; // PRESET picked: its values at `latency_scope`
    uint32_t       latency_preset = 0;
    SettingLayer   latency_scope = SettingLayer::global;
    bool           set_palette = false;        // THEME or CUSTOM THEME picked: every interface color
    RetroUiPalette palette = {};
    std::string    theme_id;                   // ... and the theme piccolo.cfg names (ui_theme)
    const char*    choose_folder = nullptr;    // a folder field or "+": that setting (CHOOSE FOLDER)
    bool           search_frame_delay = false; // SEARCH AGAIN
    bool           rewind_measure_again = false;   // MEASURE AGAIN
    bool           save_theme = false;         // SAVE (SAVE THEME) with `save_theme_name`
    std::string    save_theme_name;
    ImageWindowAction image;                   // IMAGE SETTINGS
    NetWindowAction   network;                 // the NETWORK tab
};

void settings_window_open(SettingsWindow* window, SettingsTab tab);

// Before the state: picks the scope when just opened (the most specific one
// in use), or back to GLOBAL when the core or game it edited has gone.
void settings_window_update(SettingsWindow* window, const Settings* settings, const char* core_name,
                            const char* game_name);

// Fills *state. `image` may be nullptr (no picture); `network` nullptr: no
// NETWORK tab.
void settings_window_state(SettingsWindowState* state, const SettingsWindow* window, const Settings* settings,
                           const Localization* strings, const AppPaths* paths, const char* core_name,
                           const char* game_name, const SettingsWindowNotes* notes,
                           const std::vector<PiccoloThemeFile>* themes, const ImageWindowSource* image,
                           const NetWindowState* network);

// Draws the window (and IMAGE SETTINGS) while open; `network_window` is the
// NETWORK tab's own state. The caller saves when both have closed (Esc may
// close them too, outside this function).
void settings_window_draw(SettingsWindow* window, NetWindow* network_window, const SettingsWindowState* state,
                          RetroUiFont* pixel_font, int32_t scale, SettingsWindowAction* action);

// The rows' edits (IMAGE SETTINGS' too), GAME FOLDERS' "-", PRESET and THEME.
// True when a setting changed.
bool settings_window_apply(const SettingsWindowAction* action, Settings* settings);
