#pragma once

// piccolo's state: one App, owned by main and handed to every app_*.cpp.
// Its members are grouped by the file that works on them.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "app_icon.h"
#include "app_links.h"
#include "app_loop.h"
#include "app_paths.h"
#include "app_shell.h"
#include "app_storage.h"
#include "bundled_files.h"
#include "frame_delay.h"
#include "game_mode.h"
#include "host_libretro_version.h"
#include "shader_chain.h"   // ShaderLibrary
#include "video_output.h"
#include "web_pad.h"
#include "piccolo_version.h"
#include "piccolo_settings.h"
#include "piccolo_text.h"
#include "piccolo_window.h"
#include "onscreen_pad.h"
#include "ports_widget.h"
#include "core_config_window.h"
#include "quick_menu.h"
#include "recent_games.h"
#include "continue_panel.h"
#include "continue_view.h"
#include "touch_ui.h"
#include "safe_start_window.h"
#include "settings_error_window.h"
#include "shader_check.h"
#include "shader_check_view.h"
#include "text_input_window.h"
#include "ui.h"
#include "ui_mode.h"
#include "ui_toolkit.h"
#include "settings_window.h"
#include "archive.h"
#include "host_fs.h"
#include "settings_file.h"
#include "core_catalog.h"
#include "core_updater.h"
#include "content_archive.h"
#include "cores_window.h"
#include "share_window.h"
#include "pair_window.h"
#include "content_cache.h"
#include "core_session.h"
#include "dvr_widget.h"
#include "fps_meter.h"
#include "hud_view.h"
#include "remap_widget.h"
#include "screen_effects.h"
#include "screenshot_writer.h"
#include "message_widget.h"
#include "state_undo.h"
#include "snow_view.h"
#include "state_widget.h"
#include "debug_window.h"
#include "debug_preview.h"
#include "input_bindings.h"
#include "input_config.h"
#include "input_sdl.h"
#include "input_window.h"
#include "map_window.h"
#include "mobile_home.h"
#include "input_net.h"
#include "net_access.h"
#include "build_target.h"
#include "net_build.h"
#include "net_caps_ui.h"
#include "net_service.h"
#include "net_games.h"
#include "net_remote.h"
#include "net_speed.h"
#include "net_trust.h"
#include "net_window.h"
#include "guest_window.h"
#include "net_guest.h"
#if defined(PICCOLO_GUEST_CARD)
#include "net_http.h"
#endif
#include "file_picker.h"
#include "headless.h"
#include "pixel_font.h"
#include "menus.h"
#include "pixel_font_imgui.h"   // PIXEL_FONT_UTF8_* (no ImGui in it)
#include "retro_ui.h"

// Gamepads drive the menus and dialogs as the keyboard does: their
// interface actions become the toolkit's move and activate keys; BACK
// closes an open list or popup, otherwise the topmost window (as Escape).
struct UiKey
{
    InputAction action;
    UiNavKey    key;
};

static const UiKey UI_KEYS[] = {
    {InputAction::ui_up, UiNavKey::up},
    {InputAction::ui_down, UiNavKey::down},
    {InputAction::ui_left, UiNavKey::left},
    {InputAction::ui_right, UiNavKey::right},
    {InputAction::ui_accept, UiNavKey::accept},
};
constexpr size_t UI_KEY_COUNT = sizeof(UI_KEYS) / sizeof(UI_KEYS[0]);

// The menu bar's item ids: a MenuAction's number, or this plus the index of
// a game in RECENT.
constexpr int32_t MENU_ID_RECENT = 1000;

struct App
{
    // --- The shell and the loop (main.cpp) ---
    AppShell shell = {};
    AppLinks links;               // piccolo:// links (app_links.h): from the arguments, later launches, drops
    bool     running = false;
    PiccoloWindow window = {};   // Display settings applied to the window
    bool window_focused     = true;
    bool screensaver_allowed = false;   // what we last told SDL
    RetroUiFont* pixel_font = nullptr;   // piccolo's pixel font, as the GUI toolkit holds it (ui_toolkit_start)
    int32_t ui_scale   = 1;         // window pixels per retro pixel
    float   ui_display_width  = 0.0f;   // the screen's size last frame (a turn changes it)
    float   ui_display_height = 0.0f;
    // UI text (piccolo_text.h) and the language setting it was loaded for.
    Localization strings = {};
    uint32_t     language_setting = UINT32_MAX;
    // Playback devices connected now, for the Sound tab: listed at start and
    // again when one comes or goes (not every frame).
    bool                     audio_ready = false;   // we initialized SDL's audio subsystem
    std::vector<std::string> audio_devices;
    std::vector<const char*> audio_device_pointers;

    // --- The files read at start and kept up to date (app_files) ---
    // piccolo's folder, and what was used last (<settings>/last_used.cfg).
    AppPaths    paths = {};
    char last_core[LIBRETRO_PATH_MAX]      = {};
    char last_folder[LIBRETRO_PATH_MAX]    = {};
    // Settings (piccolo_settings.h) in <settings>/piccolo.cfg, and their window.
    Settings       settings = {};
    std::string    settings_path;
    // piccolo.cfg exists but settings_load could not read it (review 2026-11-26, H3): the settings
    // are the defaults, and nothing is saved all run so the user's file is not replaced.
    bool           settings_unreadable = false;
    bool           settings_error_open = false;   // its dialog (settings_error_window.h), until OK
    // The same for the other files read at start (a missing file is fine: only "there but not
    // readable" counts). Each one's writers return while its flag is set.
    bool           core_options_unreadable = false;   // core_options.cfg
    bool           input_config_unreadable = false;   // input.cfg
    bool           recent_unreadable = false;         // recent.cfg
    bool           last_used_unreadable = false;      // last_used.cfg
    char           settings_error_files[128] = {};    // "piccolo.cfg, input.cfg": for the dialog, built once at start
    std::string input_config_path;   // settings/input.cfg
    InputLayouts layouts = {};       // controller layouts (layouts/*.cfg)
    std::vector<PiccoloThemeFile> themes;   // interface themes from files (themes/*.cfg)
    // AUTO audio latency learned per core (<settings>/audio_latency.cfg,
    // "core = ms"): state, not a setting. A game on that core starts its
    // automatic target there instead of from the bottom again.
    SettingsFile audio_learned;
    std::string  audio_learned_path;
    bool         audio_learned_dirty = false;
    uint32_t     game_starts = 0;            // counts start_game calls
    uint32_t     learned_for_start = 0;      // the start the values below belong to (0 = none)
    uint32_t     learned_start_ms = 0;       // where this game's AUTO target starts (0 = the default)
    uint32_t     learned_recorded_ms = 0;    // the last target written to audio_learned
    // The locked AUTO delay per game (<settings>/frame_delay.cfg, a section
    // per "core/game", "delay_ms = N"): state, not a setting. A game found
    // before starts locked at it; SEARCH AGAIN forgets it.
    SettingsFile   frame_delay_saved;
    std::string    frame_delay_saved_path;
    // Core options per core / folder / game (settings/core_options.cfg) and
    // what each core declared (settings/core_options_cache.cfg), for the
    // session and the CORE OPTIONS tab.
    CoreOptionsStore option_store;
    CoreOptionsCache option_cache;
    std::string      option_store_path;
    std::string      option_cache_path;
    // Safe start (safe_start_window.h): settings/running.cfg says a game
    // session is in progress; found at launch, that session didn't end.
    std::string      running_marker_path;
    bool             marker_written = false;          // this run wrote it (a session is on)
    bool             marker_options_changed = false;  // and recorded an option change in it
    std::string      marker_core_path;
    std::string      marker_content_path;
    std::string      crashed_core;                    // short name of the core whose session didn't end
    bool             crashed_after_options = false;
    bool             safe_start_open = false;
    bool             safe_start_confirmed = false;    // the next start skips the check (answered)
    FilePickerChoice safe_start_pending = {};

    // --- The menu bar and RECENT (app_menus) ---
    bool        open_first_menu = false;
    // The RECENT menu: the games (<settings>/recent.cfg) and their labels,
    // rebuilt when the list or the language changes (not every frame).
    RecentGames              recent = {};
    std::string              recent_path;
    std::vector<std::string> recent_labels;
    std::vector<const char*> recent_label_pointers;
    std::vector<size_t>      recent_shown;   // RECENT's items: their index in recent.games
    std::vector<std::string>   recent_names;   // the phone's HOME: each game's name and core apart
    std::vector<std::string>   recent_cores;
    std::vector<MobileGameRow> recent_rows;
    // The menu bar as retroui draws it (rebuilt with the labels: language, RECENT).
    std::vector<std::string>     menu_texts;   // the translated labels the items point to
    std::vector<RetroUiMenuItem> menu_items;   // every menu's items, back to back
    std::vector<RetroUiMenu>     menus;
    bool       menus_built_paused = false;    // GAME > PAUSE read RESUME when the menus were built
    bool       menus_built_running = false;   // a game ran when the menus were built (GAME's items, GAME DVR)
    bool       menus_built_quick_used = false;   // the quick slot held a state then (QUICK LOAD greyed out if not)
    bool       menus_built_undo_save = false;    // UNDO SAVE STATE was enabled when the menus were built
    bool       menus_built_undo_load = false;    // UNDO LOAD STATE too

    // --- The game (app_game) ---
    CoreSession session = {};
    CoreCatalog catalog = {};
    std::string    cores_directory;   // the one the catalog lists now
    char           core_name[256] = {};   // for the core/game settings layers ("" = none)
    char           game_name[256] = {};
    // The running core's file name ("snes9x_libretro.so", "" = no game): set once when a game
    // starts, cleared when it ends. TOOLS > CORES never replaces or removes it.
    char running_core_file[LIBRETRO_PATH_MAX]            = {};
    // What the LOAD GAME window picked, started at the top of the NEXT loop:
    // starting stops the old core, which destroys the texture this frame's
    // picture may already have been queued with (a crash at render time).
    FilePickerChoice pending_start = {};
    bool             has_pending_start = false;
    bool      pending_close_game = false;   // CLOSE GAME: done at the top of the next loop (like starts)
    // PAUSE WHEN CONTROLLERS DISCONNECT: what played last, and the pause it set.
    uint64_t   resumed_ms = 0;   // the player's pause last lifted: PLAY shows a moment (state_widget.h)
    bool       played_with_gamepad = false;   // the last game input came from a local gamepad (CONTROLLERS cause)
    bool       android_background = false;    // the app is in the background (BACKGROUND cause)
    MessageWidget messages = {};   // what piccolo reports, as HUD cards (set_status)
    // A core's TEXT request (text_input_window.h): open while it's pending.
    TextInputWindow  text_input_window = {};
    GameMode game_mode = {};   // held while a game runs (GAME MODE setting)
    char        rewind_note[96] = {};  // SETTINGS > GENERAL: what AUTO chose for this game
    DvrWidget   dvr;                   // the rewind DVR: its history and its GUI (dvr_widget.h)

    // --- Savestates (app_states) ---
    bool       quick_slot_used = false;       // the quick slot holds a state for the running game...
    bool       quick_slot_stale = true;       // ...read again at the top of the next loop (a save, a start)
    // UNDO LOAD STATE: the game as it was before the last state load, and a
    // spare for the next one (swapped, never freed: they only grow to the
    // core's state size). Dropped when the game closes or another starts.
    std::vector<uint8_t> undo_load;
    std::vector<uint8_t> undo_load_spare;
    size_t     undo_load_size = 0;            // the state's bytes in undo_load (0: nothing to undo)
    // UNDO SAVE STATE from the GAME menu, its hotkey or a device: the slot
    // saved last in this game (the selected one until something is saved).
    // Its undo file is checked with the quick slot (quick_slot_stale) or
    // when that slot changes, never every frame.
    bool       saved_slot_known = false;
    uint32_t   saved_slot = 0;
    bool       undo_save_ready = false;       // that slot has an undo (state_undo.h)...
    uint32_t   undo_save_checked = UINT32_MAX;   // ...checked for this slot
    bool       resume_skip = false;   // the next start doesn't load the AUTO state (HARD RESET, the card's START)

    // --- Screenshots (app_screenshots) ---
    ScreenshotWriter screenshot_writer = {};   // PNGs on their own thread (started by the first screenshot)
    bool        screenshot_gpu_pending = false;   // a GPU capture asked for, not back yet
    std::string screenshot_gpu_path;              // where it goes
    ScreenEffects screen_effects;      // SNAP, SHUTTER on the game picture (screen_effects.h)

    // --- REMOTE PLAY's guest links (app_guest) ---
    GuestWindow    guest_window;       // TOOLS > REMOTE PLAY (guest links)
    char       net_guest_name[NET_MAX_PEERS][NET_GUEST_MAX_NAME + 1] = {};   // each guest's name: `closed` comes after
                                                                              // the peer was reset, with no name

    // --- FILE TRANSFER and TOOLS > PAIR (app_share) ---
    PairWindow         pair_window;      // TOOLS > PAIR (our code, to be let into another piccolo)
    ShareWindow        share_window;     // TOOLS > SHARE (files with another piccolo granting FILE TRANSFER)
    NetShareFolders    share_folders;    // this piccolo's shared folders: game folders, saves, states
    NetShareLink       share_link;       // SHARE's connection to another piccolo, as its client
    NetShareLinkStatus share_status;     // read once per loop while SHARE is open
    bool               share_connected = false;   // last loop: connected with FILE TRANSFER (SHARE) granted
    std::vector<NetShareClientDone> share_done;
    uint32_t           share_rate_id = 0;    // the copy the rate is measured for
    uint64_t           share_rate_done = 0;
    uint64_t           share_rate_ticks = 0;
    uint32_t           share_rate_kbps = 0;

    // --- The picture and its timing (app_picture) ---
    VideoOutput   video = {};            // the game on screen: pass 0 + the shader preset, or plain (viewport + filter)
    // The part of the core's frame on screen (a cursor maps from it to the
    // whole frame), and the ports' cursors for the core (mouse, pointer, lightgun).
    uint32_t           view_source[4] = {};      // x, y, width, height in core pixels
    uint32_t           view_frame[2] = {};       // the frame's width, height
    double             shown_aspect = 0.0;       // the picture's width / height on screen (0: none)
    FpsMeter           fps_meter = {};           // SHOW FPS (fps_meter.h)
    bool               fps_from_env = false;     // PICCOLO_SHOW_FPS=1 (the page's DEV channel)
    // AUTO FRAME DELAY (frame_delay.h): the delay it found, for this game.
    AutoFrameDelay auto_delay = {};
    uint64_t       delay_last_ns = 0;      // last loop it was fed (0 = not applying)
    uint32_t       frame_delay_ms = 0;     // the delay used last loop
    // The menu snow (retroui's snow_view), an easter egg with no setting
    // (Andrés, 2026-11-25): it falls by itself in season on the empty
    // background and MAIN MENU. MENU > DEBUG forces it (memory only).
    SnowView  snow = {};
    bool      snow_started = false;          // snow_view_start done once
    bool      snow_was_shown = false;        // it showed last frame
    DebugSnow snow_debug = DebugSnow::automatic;
    bool      snow_in_season = false;        // the local date, checked once a minute
    bool      snow_sleigh_day = false;
    uint64_t  snow_date_checked_ms = 0;
    HudStack    hud;                   // what the HUD widgets show this frame (retroui's hud_view)
    StateWidget state_icons;           // PAUSED, REWINDING, PLAY, FAST FORWARD in the top right (state_widget.h)

    // --- Shader presets (app_shaders) ---
    ShaderLibrary shader_library = {};   // librashader, when it's there
    // piccolo's own presets (shaders/): pass 0, and the final scale per
    // filter (nearest, bilinear, sharp bilinear) when no preset is chosen.
    std::string   shader_pass0_path;
    std::string   shader_presets_folder;   // the curated presets (shaders/presets/): where CHOOSE opens
    std::string   shader_scale_paths[3];
    std::vector<std::string> shader_failed;   // presets that failed: not retried until the setting changes
    std::string   shader_preset_seen;         // the preset setting those failures are for
    // A preset just applied that's too slow asks KEEP / REVERT
    // (shader_check.h). REVERT puts the preset setting of every layer
    // (global, core, game) back as it was before the change.
    ShaderCheck   shader_check = {};
    uint64_t      shader_check_asked_ms = 0;
    uint32_t      shader_layers_start = UINT32_MAX;   // game_starts the layers below are from
    bool          shader_layer_set[3] = {};           // the preset setting in each layer now
    std::string   shader_layer_value[3];
    bool          shader_revert_set[3] = {};          // ... and before the change being checked
    std::string   shader_revert_value[3];
    bool          shader_reverted = false;            // the next change is REVERT's: not checked
    float         pass0_applied[PICCOLO_PASS0_CONTROL_COUNT] = {};   // what pass 0's parameters hold
    float         profile_applied[PICCOLO_PROFILE_PARAM_COUNT] = {};  // the same for the color profile's
    bool          pass0_stale = true;         // set them all (pass 0 was just loaded)
    FilePicker  shader_picker = {};   // CORE CONFIGURATION > SHADER > CHOOSE
    SettingLayer shader_picker_scope = SettingLayer::global;   // where its pick goes

    // --- What has the screen and the input (app_ui_modes) ---
    // What has the screen and the input (ui_mode.h): GAME, a window widget
    // (QUICK MENU is one) or MAIN MENU, and what follows from it this loop.
    UiState     ui;
    uint32_t    ui_changes_seen = 0;               // ui.changes already followed (ui_follow_changes)
    UiWidget    ui_widget_applied = UiWidget::none; // the widget the windows were last set up for
    UiDerived   ui_derived;
    QuickMenu quick_menu;              // F1 while a game runs: the actions (a window widget)
    CoreConfigWindow core_config;      // CORE CONFIGURATION: the core's options, controls, shader (a MAIN MENU window)

    // --- Input (app_input) ---
    InputSystem input = {};       // controllers, ports, hotkeys (input_bindings.h)
    InputSdl    input_sdl = {};   // the keyboard and gamepads, from SDL
    bool        ui_keys_down[UI_KEY_COUNT] = {};   // gamepad interface actions given to the toolkit as keys (drive_ui_with_gamepads)
    bool        ui_back_down = false;
    uint64_t    ui_back_since_ms = 0;      // BACK / SELECT held since (0 = not held)
    bool        ui_back_done = false;      // this hold already closed a window
    InputWindow input_window = {};   // CONFIG > INPUT
    MapWindow   map_window = {};     // MAP CONTROLLER
    std::vector<uint32_t> map_offered;   // joysticks offered MAP CONTROLLER this session
    LibretroInputState cursor_input = {};
    bool               mouse_grabbed = false;
    bool               mouse_released = false;   // MOUSE GRAB let it go
    PortsWidget ports_widget;          // the ports' tabs over the game, and DEVICES (the ports hotkey)
    uint64_t    ports_ping_ms = 0;     // when the tabs' pings were last read
    OnscreenPad onscreen_pad;          // the touch gamepad over the game (onscreen_pad.h)
    // The phone UI (mobile_home.h): always on Android, --mobile on the
    // desktop. HOME and ☰ instead of the menu bar, the windows as pages.
    bool        phone_ui = PICCOLO_MOBILE;
    MobileHome  mobile_home;
    RemapWidget remap;                 // HOLD START TO REMAP: a card per undecided controller (remap_widget.h)

    // --- SETTINGS, CORE OPTIONS and CORES (app_settings) ---
    SettingsWindow settings_window = {};
    const char*    choose_folder_key = nullptr;   // the folder setting CHOOSE FOLDER sets
    bool           settings_window_was_open = false;
    bool           options_window_was_open = false;   // the quick menu or the options manager
    FilePicker  picker  = {};
    // The core options UI's state, shared by the quick menu (live options)
    // and the CORE OPTIONS MANAGER window (CONFIG menu).
    CoreOptionsUi      core_options_ui;
    bool               core_options_manager = false;   // the window is open
    bool               core_options_manager_was_open = false;
    // TOOLS > CORES: the core downloader (core_updater.h) and its window. Set
    // up when the window opens (the server and cores folder may have changed).
    CoreUpdater        core_updater;
    CoresWindow        cores_window;

    // --- The debug window, ABOUT (app_debug) ---
    bool     show_debug_window = false;
    DebugPreviews    debug_previews;          // the debug window's fake cards and windows (debug_preview.h)
    AboutWindow      about_window = {};       // MISC > ABOUT

    // --- The network service (app_net) ---
    // Network (CONFIG > NETWORK): the listener with access control and
    // pairing, while net_enabled. The device key and the paired devices live
    // in <settings>/net_identity.cfg and net_devices.cfg.
    bool       net_ready = false;        // net_init() succeeded
    bool       net_running = false;      // `net` is listening
    bool       net_listen_failed = false;
    bool       net_has_identity = false;
    NetKeypair net_identity = {};
    char       net_fingerprint[32] = "";
    NetDeviceInfo net_info;              // this device, as the hello tells it (net_device_info)
    bool       net_proof_tried = false;  // the build proof was looked for (net_device_info)
    uint16_t   net_port = 0;             // what `net` listens on
    std::string net_beacon_applied;      // the beacon server handed to the service ("" = INTERNET off)
    std::string net_share_applied;     // the shared folders the service has (piccolo-share), as one text
    uint32_t    net_share_revision = 0;   // the settings' revision they were looked at for
    bool        net_share_set = false;    // the service has them
    NetService net_service;              // the listener on its own thread (net_service.h); its NetAccess:
                                         // net_service.access, used between net_service_lock / _unlock
    InputNet   input_net = {};           // paired devices' pads, in the input router
    NetGames   net_games = {};           // games from devices with SETTINGS (net_games.h)
    bool       net_load_waiting = false; // a load_request is in pending_start: answered once it ran
    uint32_t   net_load_peer = NET_NO_PEER;
    uint32_t   net_load_id = 0;
    bool       net_load_refused = false; // SAFE START was closed (maybe under the lock): the main loop answers the load
    NetRemote  net_remote = {};          // settings and actions from devices (net_remote.h)
    bool       net_remote_ready = false;

    // --- Connected devices (app_net_remote) ---
    bool       net_streaming = false;    // a device is streaming (remote play): frames and audio are handed over
    // stream_tuning (net_stream.h): what streaming devices asked for, kept only
    // here (never in the settings or the files: a crash leaves nothing changed).
    bool            stream_tuning_set[NET_MAX_PEERS] = {};
    NetStreamTuning stream_tuning_asked[NET_MAX_PEERS];
    uint64_t        stream_tuning_order[NET_MAX_PEERS] = {};   // the newest request wins
    uint64_t        stream_tuning_count = 0;
    bool            stream_tuning_on = false;   // one applies now (`stream_tuning`)
    NetStreamTuning stream_tuning;
    int32_t         stream_tuning_from = -1;    // the peer whose request applies
    AutoFrameDelay  stream_auto_delay = {};     // AUTO for the stream: its own, never saved
    uint64_t        stream_delay_last_ns = 0;
    NetStreamTuning stream_tuning_sent[NET_MAX_PEERS];   // the result each was last told
    bool            stream_tuning_told[NET_MAX_PEERS] = {};
    uint32_t   stream_loop = 0;          // the loop number the stream carries
    uint64_t   stream_serial = 0;        // the core frame last handed over (a repeat isn't sent again)
    bool       stream_frame_owed = false;   // a stream started or asked for a keyframe: the shown frame goes again
                                            // even if no core frame runs (paused), or that device gets nothing
    Headless   headless;                      // GO HEADLESS: a streaming device has the game (headless.h)
    char       net_peer_language[NET_MAX_PEERS][8] = {};   // what each device asked for ("" = English)
    bool       net_fast_forward[NET_MAX_PEERS] = {};   // FAST FORWARD held, per peer
    NetRemoteGame net_actions_sent;      // what the action lists last said (sent again when it changes)
    uint64_t   net_controllers_sent[NET_MAX_PEERS] = {};   // a summary of the controllers each device was last sent
    bool       net_devices_wanted[NET_MAX_PEERS] = {};     // asked for `devices` once: gets it again on changes
    uint64_t   net_devices_sent[NET_MAX_PEERS] = {};       // a summary of the `devices` each was last sent
    NetSpeedSender   net_speed_sender;     // a speed test, down (one at a time)
    NetSpeedReceiver net_speed_receiver;   // a speed test, up
    bool             net_speed_reporting = false;   // the sender finished: its report goes out
    NetWindow      net_window;         // SETTINGS > NETWORK between frames (the red question)
    std::vector<NetSpeedSummary> net_speed_results;  // what devices' speed tests found (SETTINGS > NETWORK)
    NetCaps          net_actions_caps[NET_MAX_PEERS] = {};    // what each device's action list was made for
    // What the devices were last told runs (game_state): sent again when it changes.
    std::string net_sent_core;           // "" = nothing runs
    std::string net_sent_content;

    // --- The CONTINUE card (app_continue) ---
    ContinuePanel continue_panel;     // the CONTINUE card: the last game closed (continue_panel.h)
};

// What the frame's UI phases decided that its apply needs too: the clocks
// the windows were updated with, and what stood before drawing.
struct AppUiFrame
{
    uint64_t    config_now = 0;
    uint64_t    dvr_now = 0;
    uint64_t    input_now = 0;
    uint64_t    map_now = 0;
    bool        hosts_known = false;     // SETTINGS > NETWORK listed SHARE's hosts (its FORGET goes there)
    bool        input_was_open = false;  // INPUT was open before drawing (closing it saves)
};
