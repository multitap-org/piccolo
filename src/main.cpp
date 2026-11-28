// piccolo: a libretro frontend for playing games, with a retro-style UI
// (retro_ui.h: the retro-style widgets with piccolo's own pixel font).
//
// The game fills the window. The menu bar (GAME / CONFIG / MISC) is shown
// when nothing runs; while a game runs, Esc shows and hides it, and the game
// pauses while it's shown (or while the LOAD GAME window is open).
//
// Running the core is frontend_libretro's core session, shared with the
// host-libretro test frontend. Cores, saves, states and settings live in
// piccolo's own folder (app_paths.h: ~/.local/share/piccolo on Linux).

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>


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

// Files extracted from zip / 7z archives (content_cache.h) are kept up to
// this much: room for several disc images, small next to what the games
// themselves take. Trimmed after each game starts.
constexpr uint64_t PICCOLO_CONTENT_CACHE_MAX_BYTES = 4ull * 1024ull * 1024ull * 1024ull;


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

struct App
{
    AppShell shell = {};
    AppLinks links;               // piccolo:// links (app_links.h): from the arguments, later launches, drops
    InputSystem input = {};       // controllers, ports, hotkeys (input_bindings.h)
    InputSdl    input_sdl = {};   // the keyboard and gamepads, from SDL
    bool        ui_keys_down[UI_KEY_COUNT] = {};   // gamepad interface actions given to the toolkit as keys (drive_ui_with_gamepads)
    bool        ui_back_down = false;
    uint64_t    ui_back_since_ms = 0;      // BACK / SELECT held since (0 = not held)
    bool        ui_back_done = false;      // this hold already closed a window
    bool        open_first_menu = false;
    std::string input_config_path;   // settings/input.cfg
    InputWindow input_window = {};   // CONFIG > INPUT
    MapWindow   map_window = {};     // MAP CONTROLLER
    InputLayouts layouts = {};       // controller layouts (layouts/*.cfg)
    std::vector<PiccoloThemeFile> themes;   // interface themes from files (themes/*.cfg)
    std::vector<uint32_t> map_offered;   // joysticks offered MAP CONTROLLER this session

    bool     running = false;
    bool     show_debug_window = false;

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

    RetroUiFont* pixel_font = nullptr;   // piccolo's pixel font, as the GUI toolkit holds it (ui_toolkit_start)
    int32_t ui_scale   = 1;         // window pixels per retro pixel
    float   ui_display_width  = 0.0f;   // the screen's size last frame (a turn changes it)
    float   ui_display_height = 0.0f;
    MessageWidget messages = {};   // what piccolo reports, as HUD cards (set_status)

    CoreSession session = {};
    CoreCatalog catalog = {};

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
    // PAUSE WHEN CONTROLLERS DISCONNECT: what played last, and the pause it set.
    uint64_t   resumed_ms = 0;   // the player's pause last lifted: PLAY shows a moment (state_widget.h)
    bool       played_with_gamepad = false;   // the last game input came from a local gamepad (CONTROLLERS cause)
    bool       android_background = false;    // the app is in the background (BACKGROUND cause)
    bool       menus_built_paused = false;    // GAME > PAUSE read RESUME when the menus were built
    bool       menus_built_running = false;   // a game ran when the menus were built (GAME's items, GAME DVR)
    bool       menus_built_quick_used = false;   // the quick slot held a state then (QUICK LOAD greyed out if not)
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
    bool       menus_built_undo_save = false;    // UNDO SAVE STATE was enabled when the menus were built
    bool       menus_built_undo_load = false;    // UNDO LOAD STATE too
    bool       resume_skip = false;   // the next start doesn't load the AUTO state (HARD RESET, the card's START)
    ContinuePanel continue_panel;     // the CONTINUE card: the last game closed (continue_panel.h)
    Headless   headless;                      // GO HEADLESS: a streaming device has the game (headless.h)
    char       net_peer_language[NET_MAX_PEERS][8] = {};   // what each device asked for ("" = English)
    bool       net_fast_forward[NET_MAX_PEERS] = {};   // FAST FORWARD held, per peer
    char       net_guest_name[NET_MAX_PEERS][NET_GUEST_MAX_NAME + 1] = {};   // each guest's name: `closed` comes after
                                                                              // the peer was reset, with no name
    NetRemoteGame net_actions_sent;      // what the action lists last said (sent again when it changes)
    uint64_t   net_controllers_sent[NET_MAX_PEERS] = {};   // a summary of the controllers each device was last sent
    bool       net_devices_wanted[NET_MAX_PEERS] = {};     // asked for `devices` once: gets it again on changes
    uint64_t   net_devices_sent[NET_MAX_PEERS] = {};       // a summary of the `devices` each was last sent
    NetSpeedSender   net_speed_sender;     // a speed test, down (one at a time)
    NetSpeedReceiver net_speed_receiver;   // a speed test, up
    bool             net_speed_reporting = false;   // the sender finished: its report goes out
    NetWindow      net_window;         // SETTINGS > NETWORK between frames (the red question)
    GuestWindow    guest_window;       // TOOLS > REMOTE PLAY (guest links)
    std::vector<NetSpeedSummary> net_speed_results;  // what devices' speed tests found (SETTINGS > NETWORK)
    NetCaps          net_actions_caps[NET_MAX_PEERS] = {};    // what each device's action list was made for
    // What the devices were last told runs (game_state): sent again when it changes.
    std::string net_sent_core;           // "" = nothing runs
    std::string net_sent_content;
    FilePicker  picker  = {};
    FilePicker  shader_picker = {};   // CORE CONFIGURATION > SHADER > CHOOSE
    SettingLayer shader_picker_scope = SettingLayer::global;   // where its pick goes
    // What has the screen and the input (ui_mode.h): GAME, a window widget
    // (QUICK MENU is one) or MAIN MENU, and what follows from it this loop.
    UiState     ui;
    uint32_t    ui_changes_seen = 0;               // ui.changes already followed (ui_follow_changes)
    UiWidget    ui_widget_applied = UiWidget::none; // the widget the windows were last set up for
    UiDerived   ui_derived;

    // What the LOAD GAME window picked, started at the top of the NEXT loop:
    // starting stops the old core, which destroys the texture this frame's
    // picture may already have been queued with (a crash at render time).
    FilePickerChoice pending_start = {};
    bool             has_pending_start = false;

    // piccolo's folder, and what was used last (<settings>/last_used.cfg).
    AppPaths    paths = {};
    ShaderLibrary shader_library = {};   // librashader, when it's there
    VideoOutput   video = {};            // the game on screen: pass 0 + the shader preset, or plain (viewport + filter)
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
    // The part of the core's frame on screen (a cursor maps from it to the
    // whole frame), and the ports' cursors for the core (mouse, pointer, lightgun).
    uint32_t           view_source[4] = {};      // x, y, width, height in core pixels
    uint32_t           view_frame[2] = {};       // the frame's width, height
    double             shown_aspect = 0.0;       // the picture's width / height on screen (0: none)
    FpsMeter           fps_meter = {};           // SHOW FPS (fps_meter.h)
    bool               fps_from_env = false;     // PICCOLO_SHOW_FPS=1 (the page's DEV channel)
    LibretroInputState cursor_input = {};
    bool               mouse_grabbed = false;
    bool               mouse_released = false;   // MOUSE GRAB let it go

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
    SettingsWindow settings_window = {};
    const char*    choose_folder_key = nullptr;   // the folder setting CHOOSE FOLDER sets
    bool           settings_window_was_open = false;
    bool           options_window_was_open = false;   // the quick menu or the options manager
    std::string    cores_directory;   // the one the catalog lists now
    char           core_name[256] = {};   // for the core/game settings layers ("" = none)
    char           game_name[256] = {};

    PiccoloWindow window = {};   // Display settings applied to the window

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

    // UI text (piccolo_text.h) and the language setting it was loaded for.
    Localization strings = {};
    uint32_t     language_setting = UINT32_MAX;

    // Playback devices connected now, for the Sound tab: listed at start and
    // again when one comes or goes (not every frame).
    bool                     audio_ready = false;   // we initialized SDL's audio subsystem
    std::vector<std::string> audio_devices;
    std::vector<const char*> audio_device_pointers;

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

    // AUTO FRAME DELAY (frame_delay.h): the delay it found, for this game.
    AutoFrameDelay auto_delay = {};
    uint64_t       delay_last_ns = 0;      // last loop it was fed (0 = not applying)
    uint32_t       frame_delay_ms = 0;     // the delay used last loop
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
    // A core's TEXT request (text_input_window.h): open while it's pending.
    TextInputWindow  text_input_window = {};
    AboutWindow      about_window = {};       // MISC > ABOUT
    DebugPreviews    debug_previews;          // the debug window's fake cards and windows (debug_preview.h)

    bool             safe_start_open = false;
    bool             safe_start_confirmed = false;    // the next start skips the check (answered)
    FilePickerChoice safe_start_pending = {};

    // The core options UI's state, shared by the quick menu (live options)
    // and the CORE OPTIONS MANAGER window (CONFIG menu).
    CoreOptionsUi      core_options_ui;
    bool               core_options_manager = false;   // the window is open
    bool               core_options_manager_was_open = false;

    // TOOLS > CORES: the core downloader (core_updater.h) and its window. Set
    // up when the window opens (the server and cores folder may have changed).
    CoreUpdater        core_updater;
    CoresWindow        cores_window;
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

    QuickMenu quick_menu;              // F1 while a game runs: the actions (a window widget)
    CoreConfigWindow core_config;      // CORE CONFIGURATION: the core's options, controls, shader (a MAIN MENU window)
    PortsWidget ports_widget;          // the ports' tabs over the game, and DEVICES (the ports hotkey)
    uint64_t    ports_ping_ms = 0;     // when the tabs' pings were last read
    OnscreenPad onscreen_pad;          // the touch gamepad over the game (onscreen_pad.h)
    // The phone UI (mobile_home.h): always on Android, --mobile on the
    // desktop. HOME and ☰ instead of the menu bar, the windows as pages.
    bool        phone_ui = PICCOLO_MOBILE;
    MobileHome  mobile_home;
    DvrWidget   dvr;                   // the rewind DVR: its history and its GUI (dvr_widget.h)
    RemapWidget remap;                 // HOLD START TO REMAP: a card per undecided controller (remap_widget.h)
    StateWidget state_icons;           // PAUSED, REWINDING, PLAY, FAST FORWARD in the top right (state_widget.h)
    ScreenEffects screen_effects;      // SNAP, SHUTTER on the game picture (screen_effects.h)
    ScreenshotWriter screenshot_writer = {};   // PNGs on their own thread (started by the first screenshot)
    bool        screenshot_gpu_pending = false;   // a GPU capture asked for, not back yet
    std::string screenshot_gpu_path;              // where it goes
    HudStack    hud;                   // what the HUD widgets show this frame (retroui's hud_view)
    char        rewind_note[96] = {};  // SETTINGS > GENERAL: what AUTO chose for this game
    bool      pending_close_game = false;   // CLOSE GAME: done at the top of the next loop (like starts)

    GameMode game_mode = {};   // held while a game runs (GAME MODE setting)

    bool window_focused     = true;
    bool screensaver_allowed = false;   // what we last told SDL
    char last_core[LIBRETRO_PATH_MAX]      = {};
    // The running core's file name ("snes9x_libretro.so", "" = no game): set once when a game
    // starts, cleared when it ends. TOOLS > CORES never replaces or removes it.
    char running_core_file[LIBRETRO_PATH_MAX]            = {};
    char last_folder[LIBRETRO_PATH_MAX]    = {};
};

// ---------------------------------------------------------------------------
// Last used core and folder (<settings>/last_used.cfg)
// ---------------------------------------------------------------------------

static std::string last_used_file(const App* app)
{
    return app->paths.settings + "/last_used.cfg";
}

static void save_last_used(const App* app)
{
    // last_used.cfg was there but unreadable at start: what is saved now would replace it.
    if (app->last_used_unreadable || app->paths.settings.empty())
    {
        return;
    }
    SettingsFile file = {};
    bool ok = settings_file_set(&file, "", "core", app->last_core) && settings_file_set(&file, "", "folder", app->last_folder);
    if (ok && app->window.has_saved)
    {
        char number[16] = {};
        std::snprintf(number, sizeof(number), "%d", app->window.saved_x);
        ok = settings_file_set(&file, "", "window_x", number);
        std::snprintf(number, sizeof(number), "%d", app->window.saved_y);
        ok = ok && settings_file_set(&file, "", "window_y", number);
        std::snprintf(number, sizeof(number), "%d", app->window.saved_width);
        ok = ok && settings_file_set(&file, "", "window_width", number);
        std::snprintf(number, sizeof(number), "%d", app->window.saved_height);
        ok = ok && settings_file_set(&file, "", "window_height", number);
    }
    if (ok)
    {
        settings_file_save(&file, last_used_file(app).c_str(), "piccolo: last used core and folder");
    }
}

static void load_last_used(App* app)
{
    SettingsFile file = {};
    if (app->paths.settings.empty())
    {
        return;
    }
    if (!settings_file_load(&file, last_used_file(app).c_str()))
    {
        app->last_used_unreadable = true;   // a missing file loads fine: this is "there, not readable"
        return;
    }
    const char* core = settings_file_get(&file, "", "core");
    const char* folder = settings_file_get(&file, "", "folder");
    if (core != nullptr)
    {
        std::snprintf(app->last_core, sizeof(app->last_core), "%s", core);
    }
    if (folder != nullptr)
    {
        std::snprintf(app->last_folder, sizeof(app->last_folder), "%s", folder);
    }
    const char* x = settings_file_get(&file, "", "window_x");
    const char* y = settings_file_get(&file, "", "window_y");
    const char* width = settings_file_get(&file, "", "window_width");
    const char* height = settings_file_get(&file, "", "window_height");
    if (x != nullptr && y != nullptr && width != nullptr && height != nullptr)
    {
        app->window.saved_x = static_cast<int32_t>(std::strtol(x, nullptr, 10));
        app->window.saved_y = static_cast<int32_t>(std::strtol(y, nullptr, 10));
        app->window.saved_width = static_cast<int32_t>(std::strtol(width, nullptr, 10));
        app->window.saved_height = static_cast<int32_t>(std::strtol(height, nullptr, 10));
        app->window.has_saved = app->window.saved_width > 0 && app->window.saved_height > 0;
    }
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

// A folder setting, or the program folder's own when it's empty.
static std::string folder_setting(const App* app, const char* key, const std::string& default_folder)
{
    const char* value = settings_get_text(&app->settings, key);
    return value[0] != '\0' ? std::string(value) : default_folder;
}

// Folders from the settings: the catalog's cores folder now, the others for
// the next game the session starts.
static void apply_folders(App* app)
{
    if (app->paths.root.empty())
    {
        return;
    }
    std::string saves = folder_setting(app, PICCOLO_PATH_SAVES, app->paths.saves);
    std::string states = folder_setting(app, PICCOLO_PATH_STATES, app->paths.states);
    std::string system = folder_setting(app, PICCOLO_PATH_SYSTEM, app->paths.system);
    std::snprintf(app->session.saves_directory, sizeof(app->session.saves_directory), "%s", saves.c_str());
    std::snprintf(app->session.states_directory, sizeof(app->session.states_directory), "%s", states.c_str());
    std::snprintf(app->session.system_directory, sizeof(app->session.system_directory), "%s", system.c_str());
    std::snprintf(app->session.cache_directory, sizeof(app->session.cache_directory), "%s", app->paths.cache.c_str());

#if defined(SDL_PLATFORM_ANDROID)
    // Android: cores load only from the app's private storage (shared storage
    // is noexec), so the CORES setting isn't offered (settings_window.cpp).
    std::string cores = app->paths.cores;
#else
    std::string cores = folder_setting(app, PICCOLO_PATH_CORES, app->paths.cores);
#endif
    if (cores != app->cores_directory)
    {
        app->cores_directory = cores;
        std::string cache_path = app->paths.root + "/core_info_cache.cfg";
        core_catalog_init(&app->catalog, cores.c_str(), cache_path.c_str());
        core_catalog_scan(&app->catalog, nullptr);
    }
}

static void save_input_config(App* app)
{
    // Every write of input.cfg comes through here (shutdown too, which saves even when nothing
    // changed): a file that was there but unreadable at start left no controller profiles, and
    // saving would replace the user's.
    if (app->input_config_unreadable || app->input_config_path.empty())
    {
        return;
    }
    input_config_save(&app->input, app->input_config_path.c_str());
}

// Adds one file name to the list the dialog shows ("piccolo.cfg, input.cfg").
static void settings_error_files_add(App* app, const char* name)
{
    size_t used = std::strlen(app->settings_error_files);
    std::snprintf(app->settings_error_files + used, sizeof(app->settings_error_files) - used, "%s%s",
                  used > 0 ? ", " : "", name);
}

// Once at start, after every file was read: which ones are there but could not be read, and
// the dialog that says so (no per-frame work: the window only draws this text).
static void build_settings_error_files(App* app)
{
    app->settings_error_files[0] = '\0';
    if (app->settings_unreadable)
    {
        settings_error_files_add(app, "piccolo.cfg");
    }
    if (app->core_options_unreadable)
    {
        settings_error_files_add(app, "core_options.cfg");
    }
    if (app->input_config_unreadable)
    {
        settings_error_files_add(app, "input.cfg");
    }
    if (app->recent_unreadable)
    {
        settings_error_files_add(app, "recent.cfg");
    }
    if (app->last_used_unreadable)
    {
        settings_error_files_add(app, "last_used.cfg");
    }
    if (app->settings_error_files[0] != '\0')
    {
        app->settings_error_open = true;
    }
}

static void save_settings(App* app)
{
    // Every write of piccolo.cfg comes through here. A file that exists but could not be read
    // left the settings at their defaults: saving them would replace the user's file (the
    // rename succeeds even when the old file was unreadable).
    if (app->settings_unreadable)
    {
        return;
    }
    if (app->settings.dirty && !app->settings_path.empty())
    {
        piccolo_settings_save(&app->settings, app->settings_path.c_str());
    }
}

// The core/game layers follow what runs: core = the core's short name,
// game = the content file's name (the same names the save files use).
static void update_settings_context(App* app)
{
    app->core_name[0] = '\0';
    app->game_name[0] = '\0';
    const LibretroHost* host = app->session.host;
    if (core_session_running(&app->session))
    {
        libretro_core_short_name(host->core_path, app->core_name, sizeof(app->core_name));
        if (host->content_path[0] != '\0')
        {
            host_fs_stem(host->content_path, app->game_name, sizeof(app->game_name));
        }
    }
    settings_set_context(&app->settings, app->core_name, app->game_name);
}

// ---------------------------------------------------------------------------
// Recent games
// ---------------------------------------------------------------------------

// The menu bar's item ids: a MenuAction's number, or this plus the index of
// a game in RECENT.
constexpr int32_t MENU_ID_RECENT = 1000;

// piccolo's menus (menus.h) as retroui's menu bar takes them: RECENT first
// (the hamburger glyph: the games, then CLEAR ALL), then the menus in the
// current language, with the items that show while a game runs (or not).
// Built when the labels change, the pause and whether a game runs, never
// per frame.
static bool game_running(const App* app);   // (Game control, further down)
static bool continue_card_active(const App* app);   // (The CONTINUE card, further down)
static void continue_card_press(App* app, ContinueViewButton button);
static void continue_card_choose(App* app, uint32_t tile);

static bool menu_item_listed(int32_t menu, int32_t item, bool running)
{
    return running || menus_item_shown(menu, item) != MenuShown::with_game;
}

static void build_menus(App* app)
{
    app->menu_texts.clear();
    app->menu_items.clear();
    app->menus.clear();
    app->menus_built_paused = app->ui.user_paused;
    bool running = game_running(app);
    app->menus_built_running = running;
    app->menus_built_quick_used = app->quick_slot_used;
    app->menus_built_undo_save = app->undo_save_ready;
    app->menus_built_undo_load = app->undo_load_size > 0;
    // The labels first: the items point into them (push_back may move them).
    for (int32_t menu = 0; menu < menus_count(); menu++)
    {
        app->menu_texts.push_back(piccolo_text(&app->strings, menus_label(menu)));
        for (int32_t item = 0; item < menus_item_count(menu); item++)
        {
            if (!menu_item_listed(menu, item, running))
            {
                continue;
            }
            Str label = menus_item_label(menu, item);
            if (menus_item_action(menu, item) == MenuAction::pause && app->ui.user_paused)
            {
                label = Str::menu_resume;   // GAME > PAUSE reads RESUME while the player paused
            }
            app->menu_texts.push_back(piccolo_text(&app->strings, label));
        }
    }
    // The items, every menu's back to back (the menus point into them).
    for (size_t i = 0; i < app->recent_label_pointers.size(); i++)
    {
        app->menu_items.push_back(RetroUiMenuItem{app->recent_label_pointers[i], MENU_ID_RECENT + static_cast<int32_t>(i)});
    }
    if (!app->recent_label_pointers.empty())
    {
        app->menu_items.push_back(RetroUiMenuItem{"", RETRO_UI_MENU_SEPARATOR});
        app->menu_items.push_back(RetroUiMenuItem{piccolo_text(&app->strings, Str::menu_clear_recents),
                                                  static_cast<int32_t>(MenuAction::clear_recents)});
    }
    int32_t recent_count = static_cast<int32_t>(app->menu_items.size());
    std::vector<int32_t> counts;
    size_t text = 0;
    for (int32_t menu = 0; menu < menus_count(); menu++)
    {
        text++;   // (the menu's own label)
        int32_t count = 0;
        for (int32_t item = 0; item < menus_item_count(menu); item++)
        {
            if (!menu_item_listed(menu, item, running))
            {
                continue;
            }
            MenuAction action = menus_item_action(menu, item);
            int32_t id = action == MenuAction::separator ? RETRO_UI_MENU_SEPARATOR : static_cast<int32_t>(action);
            bool enabled = running || menus_item_shown(menu, item) != MenuShown::greyed_no_game;
            if (action == MenuAction::quick_load && !app->quick_slot_used)
            {
                enabled = false;   // as QUICK MENU's QUICK LOAD and LOAD: an empty slot has nothing to load
            }
            if ((action == MenuAction::undo_save_state && !app->undo_save_ready) ||
                (action == MenuAction::undo_load_state && app->undo_load_size == 0))
            {
                enabled = false;   // nothing to undo
            }
            app->menu_items.push_back(RetroUiMenuItem{app->menu_texts[text].c_str(), id, enabled});
            text++;
            count++;
        }
        counts.push_back(count);
    }
    app->menus.push_back(RetroUiMenu{PIXEL_FONT_UTF8_MENU, app->menu_items.data(), recent_count,
                                     piccolo_text(&app->strings, Str::menu_recent_empty)});
    size_t first_item = static_cast<size_t>(recent_count);
    text = 0;
    for (int32_t menu = 0; menu < menus_count(); menu++)
    {
        int32_t count = counts[static_cast<size_t>(menu)];
        app->menus.push_back(RetroUiMenu{app->menu_texts[text].c_str(), app->menu_items.data() + first_item, count, nullptr});
        first_item += static_cast<size_t>(count);
        text += static_cast<size_t>(count) + 1;
    }
}

static void refresh_recent_labels(App* app)
{
    app->recent_labels.clear();
    app->recent_label_pointers.clear();
    app->recent_shown.clear();
    app->recent_names.clear();
    app->recent_cores.clear();
    app->recent_rows.clear();
    const char* no_game = piccolo_text(&app->strings, Str::recent_no_game);
    // The browser: one page per core over one shared folder, so only the
    // games of the core this page has (the others can't start here).
    std::string linked_core = bundled_files_linked_core();
    for (size_t i = 0; i < app->recent.games.size(); i++)
    {
        const std::string* core = &app->recent.games[i].core_path;
        if (!linked_core.empty() &&
            (core->size() < linked_core.size() ||
             core->compare(core->size() - linked_core.size(), linked_core.size(), linked_core) != 0))
        {
            continue;
        }
        app->recent_shown.push_back(i);
        char label[600] = {};
        recent_games_label(&app->recent.games[i], no_game, label, sizeof(label));
        app->recent_labels.push_back(label);
        char name[300] = {};
        char core_name[300] = {};
        recent_games_names(&app->recent.games[i], no_game, name, sizeof(name), core_name, sizeof(core_name));
        app->recent_names.push_back(name);
        app->recent_cores.push_back(core_name);
    }
    // Pointers after all the strings exist (push_back may move them).
    for (size_t i = 0; i < app->recent_labels.size(); i++)
    {
        app->recent_label_pointers.push_back(app->recent_labels[i].c_str());
        app->recent_rows.push_back(MobileGameRow{app->recent_names[i].c_str(), app->recent_cores[i].c_str()});
    }
    build_menus(app);
}

static void remember_recent(App* app, const char* core_path, const char* content_path)
{
    recent_games_add(&app->recent, core_path, content_path);
    // recent.cfg unreadable at start: the list is empty, saving it would replace the user's.
    if (!app->recent_path.empty() && !app->recent_unreadable)
    {
        recent_games_save(&app->recent, app->recent_path.c_str());
    }
    refresh_recent_labels(app);
}

// ---------------------------------------------------------------------------
// Game control
// ---------------------------------------------------------------------------

static bool game_running(const App* app)
{
    return core_session_running(&app->session);
}

// The running core asked for a string (TEXT): its box is up, the game paused.
static bool text_input_pending(const App* app)
{
    return game_running(app) && app->session.host->text_input.mode == RETRO_EXT_TEXT_INPUT_TEXT;
}

// A message for the player: a HUD card for a few seconds (message_widget.h).
// (The name is the status line's, which these went to before.)
static void set_status(App* app, const char* text)
{
    message_widget_post(&app->messages, text, SDL_GetTicks());
}

// What the core session reports after an action (a state saved, a game
// started): its failures always; its successes only when `announce` (Andrés,
// testing round 3: a hotkey or another device's action, where no menu shows
// what happened).
static void report_session(App* app, bool ok, bool announce)
{
    if (!ok || announce)
    {
        set_status(app, app->session.message);
    }
}

// ---------------------------------------------------------------------------
// UNDO SAVE STATE / UNDO LOAD STATE (Andrés, 2026-11-26; design-notes.md).
// A load keeps the game as it was in memory, one level; a save moves the
// slot's old files aside on disk, one level per slot (state_undo.h).
// ---------------------------------------------------------------------------

// The game ends: its load undo goes (the buffers stay for the next game) and
// the slot saved last is forgotten. The slots' undo files stay on disk.
static void drop_undos(App* app)
{
    app->undo_load_size = 0;
    app->saved_slot_known = false;
    app->undo_save_checked = UINT32_MAX;   // checked again for the next game
}

// Every state load (a slot, the quick slot, a hotkey, a device) comes
// through here: the running game goes to the spare buffer first and becomes
// the undo once the load worked. The DVR's load at its cursor (PLAY FROM
// HERE) doesn't: it has its own lane.
static bool load_state_keeping_undo(App* app)
{
    bool kept = false;
    if (game_running(app))
    {
        // (0 = no savestates.) What the core really wrote is the vector's size, not the size asked.
        kept = libretro_host_state_size(app->session.host) > 0 &&
               libretro_host_serialize(app->session.host, &app->undo_load_spare);
    }
    bool loaded = core_session_load_state(&app->session);
    if (loaded && kept)
    {
        app->undo_load_size = app->undo_load_spare.size();
        app->undo_load.swap(app->undo_load_spare);
    }
    return loaded;
}

// UNDO LOAD STATE: the game back as before the last load. The state it
// replaces becomes the undo, so undoing twice is redo. False with nothing to
// undo, or when the core refused it.
static bool undo_load_state(App* app)
{
    if (!game_running(app) || app->undo_load_size == 0)
    {
        return false;
    }
    LibretroHost* host = app->session.host;
    bool kept = libretro_host_state_size(host) > 0 && libretro_host_serialize(host, &app->undo_load_spare);
    // A load as the player's (the rewind history jumps here, as for a slot).
    if (!libretro_host_load_state_memory(host, app->undo_load.data(), app->undo_load_size))
    {
        return false;
    }
    audio_output_clear(&app->session.audio);   // what was queued belongs to the time before
    app->undo_load_size = kept ? app->undo_load_spare.size() : 0;   // what the core wrote, not the size asked
    app->undo_load.swap(app->undo_load_spare);
    return true;
}

// The slot GAME > UNDO SAVE STATE, its hotkey and a device undo: the one
// saved last in this game, the selected one until then. (QUICK MENU's UNDO
// SAVE undoes the slot selected there.)
static uint32_t undo_save_slot(const App* app)
{
    return app->saved_slot_known ? app->saved_slot : app->session.state_slot;
}

// UNDO SAVE STATE: `slot` swapped with its undo files.
static bool undo_save_state(App* app, uint32_t slot)
{
    char path[LIBRETRO_PATH_MAX] = {};
    if (!game_running(app) || !libretro_host_state_path(app->session.host, slot, path, sizeof(path)))
    {
        return false;
    }
    bool undone = state_undo_swap(path);
    quick_menu_refresh(&app->quick_menu);
    app->quick_slot_stale = true;   // the quick slot and the undo files, read again
    return undone;
}

// An undo's card, as report_session decides a save's or a load's: NOTHING TO
// UNDO always, the undo done only when `announce` (a hotkey, a device).
static void report_undo_save(App* app, bool undone, uint32_t slot, bool announce)
{
    if (!undone)
    {
        set_status(app, piccolo_text(&app->strings, Str::status_undo_nothing));
    }
    else if (announce)
    {
        char text[128] = {};
        std::snprintf(text, sizeof(text), piccolo_text(&app->strings, Str::status_undo_save), slot);
        set_status(app, text);
    }
}

static void report_undo_load(App* app, bool undone, bool announce)
{
    if (!undone || announce)
    {
        set_status(app, piccolo_text(&app->strings, undone ? Str::status_undo_load : Str::status_undo_nothing));
    }
}

// ---------------------------------------------------------------------------
// SAVE STATE ON EXIT (on by default, Andrés 2026-11-26): closing a game
// (CLOSE GAME, another game, QUIT, Android sending piccolo to the background,
// where it may be killed) saves its AUTO state; starting it again loads it,
// with a card, and UNDO LOAD STATE goes back to the fresh start.
// ---------------------------------------------------------------------------

// The AUTO state's file: "<game>.state.auto", next to the slots. host-libretro
// names a slot ".state<N>": slot 0's name without its digit, plus ".auto".
// It's no slot, so the slot list, QUICK MENU and FILE TRANSFER's companions
// never see it.
static bool auto_state_path(const App* app, char* out, size_t out_size)
{
    char slot_path[LIBRETRO_PATH_MAX] = {};
    if (!game_running(app) || !libretro_host_state_path(app->session.host, 0, slot_path, sizeof(slot_path)))
    {
        return false;
    }
    size_t length = std::strlen(slot_path);
    if (length == 0 || slot_path[length - 1] != '0')
    {
        return false;
    }
    slot_path[length - 1] = '\0';
    int written = std::snprintf(out, out_size, "%s.auto", slot_path);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

// The picture of the game as shown, at x1 (IMAGE SETTINGS' crop and aspect):
// a slot's thumbnail is made at that size; `aspect_*` gets it too (the
// CONTINUE card draws the cropped frame at that shape itself).
static StateThumbnail picture_thumbnail(const App* app, uint32_t* aspect_width, uint32_t* aspect_height)
{
    StateThumbnail thumbnail = {};
    *aspect_width = 0;
    *aspect_height = 0;
    const LibretroVideoFrame* video = &app->session.host->video;
    if (game_running(app) && video->width > 0 && video->height > 0)
    {
        ViewportInput input = {};
        input.frame_width = video->width;
        input.frame_height = video->height;
        input.core_aspect = static_cast<double>(app->session.host->av_info.geometry.aspect_ratio);
        piccolo_settings_viewport(&app->settings, &input);
        thumbnail.crop = input.crop;
        if (!viewport_picture_size(&input, 1, &thumbnail.width, &thumbnail.height))
        {
            thumbnail.width = 0;
            thumbnail.height = 0;
        }
        *aspect_width = thumbnail.width;
        *aspect_height = thumbnail.height;
    }
    return thumbnail;
}

// Before the running game stops (between frames): its AUTO state (SAVE
// STATE ON EXIT), and for the CONTINUE card its last frame and record
// (continue_card.h), even when there's no state to save. No state for a core
// without savestates, nor one whose states are incomplete (the card says
// START for both; an incomplete state's old file goes, so it never loads).
static void save_state_on_exit(App* app)
{
    char path[LIBRETRO_PATH_MAX + 8] = {};
    if (!auto_state_path(app, path, sizeof(path)))
    {
        return;
    }
    LibretroHost* host = app->session.host;
    ContinueRecord record = {};
    record.saved = ContinueSaved::off;
    if (settings_get_bool(&app->settings, PICCOLO_SAVESTATE_ON_EXIT))
    {
        size_t size = libretro_host_state_size(host);
        std::vector<uint8_t> state;   // once, as the game closes: not a hot path
        if ((host->serialization_quirks & RETRO_SERIALIZATION_QUIRK_INCOMPLETE) != 0)
        {
            record.saved = ContinueSaved::incomplete;
            if (host_fs_exists(path))
            {
                host_fs_remove_file(path);
            }
        }
        else if (size == 0 || !libretro_host_serialize(host, &state))
        {
            record.saved = ContinueSaved::no_states;
        }
        else if (host_fs_write_file(path, state.data(), state.size()))   // what the host filled, not the size asked before
        {
            record.saved = ContinueSaved::state;
            SDL_Log("Save state on exit: %s (%zu bytes)", path, state.size());
        }
        else
        {
            record.saved = ContinueSaved::no_states;   // (logged) nothing to resume
        }
    }

    // The card's picture: the cropped frame, not scaled (sharp later).
    std::string picture = std::string(path) + ".png";
    StateThumbnail thumbnail = picture_thumbnail(app, &record.aspect_width, &record.aspect_height);
    thumbnail.native = true;
    if (thumbnail.width == 0 || !core_session_save_picture(&app->session, &thumbnail, picture.c_str()))
    {
        if (host_fs_exists(picture.c_str()))
        {
            host_fs_remove_file(picture.c_str());   // never an older game's picture
        }
    }
    record.core_path = app->marker_core_path;
    record.content_path = app->marker_content_path;
    char game[LIBRETRO_PATH_MAX] = {};
    if (record.content_path.empty() ||
        !content_archive_game_name(record.content_path.c_str(), game, sizeof(game)))
    {
        std::snprintf(game, sizeof(game), "%s", host->game_name);
    }
    record.game_name = game;
    record.core_name = host->system_info.library_name != nullptr ? host->system_info.library_name : "";
    record.state_path = path;
    record.picture_path = picture;
    // The save file a newer copy of would make resuming go back (SRAM).
    for (size_t i = 0; i < host->memory_files.size(); i++)
    {
        if (host->memory_files[i].type == RETRO_MEMORY_SAVE_RAM)
        {
            record.save_path = host->memory_files[i].path;
            break;
        }
    }
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    record.closed = static_cast<uint64_t>(now / SDL_NS_PER_SECOND);
    continue_panel_closed(&app->continue_panel, &record);
}

// Right after a game started: its AUTO state, if it has one. The fresh start
// becomes UNDO LOAD STATE's. Said with a card (nothing else shows it).
static void resume_state_on_start(App* app)
{
    char path[LIBRETRO_PATH_MAX + 8] = {};
    if (!settings_get_bool(&app->settings, PICCOLO_SAVESTATE_ON_EXIT) || !auto_state_path(app, path, sizeof(path)) ||
        !host_fs_exists(path))
    {
        return;
    }
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!host_fs_read_file(path, &data, &size))
    {
        set_status(app, piccolo_text(&app->strings, Str::status_resume_failed));
        return;
    }
    LibretroHost* host = app->session.host;
    bool kept = libretro_host_state_size(host) > 0 && libretro_host_serialize(host, &app->undo_load_spare);
    bool loaded = size > 0 && libretro_host_load_state_memory(host, data, size);
    delete[] data;
    if (!loaded)
    {
        set_status(app, piccolo_text(&app->strings, Str::status_resume_failed));
        return;
    }
    audio_output_clear(&app->session.audio);
    if (kept)
    {
        app->undo_load_size = app->undo_load_spare.size();   // what the core wrote, not the size asked
        app->undo_load.swap(app->undo_load_spare);
    }
    set_status(app, piccolo_text(&app->strings, Str::status_resumed));
}

// ---------------------------------------------------------------------------
// Screenshots (GAME > SCREENSHOT, the screenshot hotkey; SETTINGS > GENERAL:
// SCREENSHOTS mode and folder). RAW copies the core's frame now; GPU asks
// video_output for the picture as shown, which comes back a frame or two
// later (collect_screenshots). PNGs are written on screenshot_writer's thread.
// Memory is only taken when one is taken, never per frame.
// ---------------------------------------------------------------------------

// "<folder>/<game> 2026-11-22 15-04-05<suffix>.png", with " (2)"... when that
// file is already there (two in one second).
static std::string screenshot_path(const App* app, const std::string& folder, const char* suffix)
{
    SDL_Time now = 0;
    SDL_DateTime date = {};
    char stamp[32] = "";
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &date, true))
    {
        std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d-%02d-%02d", date.year, date.month, date.day,
                      date.hour, date.minute, date.second);
    }
    const char* name = app->game_name;
    if (name[0] == '\0')
    {
        name = app->core_name[0] != '\0' ? app->core_name : "piccolo";   // a core without content
    }
    std::string base = folder + "/" + name + " " + stamp + suffix;
    std::string path = base + ".png";
    for (uint32_t n = 2; n < 100 && host_fs_exists(path.c_str()); n++)
    {
        path = base + " (" + std::to_string(n) + ").png";
    }
    return path;
}

// The core's frame as the host keeps it (its own pixel format), copied.
static bool raw_screenshot(App* app, const std::string& path)
{
    const LibretroVideoFrame* video = &app->session.host->video;
    if (video->pixels == nullptr || video->width == 0 || video->height == 0)
    {
        return false;
    }
    ScreenshotImage image = {};
    image.width = video->width;
    image.height = video->height;
    image.pitch = video->width * video->bytes_per_pixel;
    image.format = ScreenshotPixels::xrgb8888;
    if (video->format == RETRO_PIXEL_FORMAT_RGB565)
    {
        image.format = ScreenshotPixels::rgb565;
    }
    else if (video->format == RETRO_PIXEL_FORMAT_0RGB1555)
    {
        image.format = ScreenshotPixels::xrgb1555;
    }
    size_t size = static_cast<size_t>(image.pitch) * image.height;
    image.pixels = new uint8_t[size];
    std::memcpy(image.pixels, video->pixels, size);
    image.path = path;
    return screenshot_writer_submit(&app->screenshot_writer, &image);
}

// --- REMOTE PLAY (guest links) ---------------------------------------

// The beacon's host name for guest links: BEACON SERVER without a UDP port
// ("beacon.get-piccolo.app:55430" -> "beacon.get-piccolo.app"), the default
// when it's empty. The link and the card live on the same name.
static std::string guest_beacon_host(const App* app)
{
    std::string name = settings_get_text(&app->settings, PICCOLO_NET_BEACON);
    if (name.empty())
    {
        name = NET_BEACON_DEFAULT_SERVER;
    }
    size_t scheme = name.find("://");
    if (scheme != std::string::npos)
    {
        name = name.substr(scheme + 3);
    }
    size_t slash = name.find('/');
    if (slash != std::string::npos)
    {
        name.resize(slash);
    }
    if (!name.empty() && name[0] != '[')
    {
        size_t colon = name.find(':');
        if (colon != std::string::npos && name.find(':', colon + 1) == std::string::npos)
        {
            name.resize(colon);
        }
    }
    return name;
}

// The frame shown now, for the card and the window's preview. Under the
// network lock it only copies the frame (guest_card_frame_copy): the PNG is
// made by the upload thread (guest_card_send). The preview's conversion
// stays here: no PNG, a plain pixel conversion. A core without a frame in
// memory (none yet): no picture, the card goes up without one (the beacon
// shows white noise).
static void guest_capture(App* app)
{
    GuestWindow* window = &app->guest_window;
    window->frame.pixels.clear();
    window->has_preview = false;   // until a new picture is uploaded: NO SCREENSHOT AVAILABLE
    if (!game_running(app) || app->session.host == nullptr)
    {
        return;
    }
    const LibretroVideoFrame* video = &app->session.host->video;
    GuestCardPixels format = GuestCardPixels::xrgb8888;
    if (video->format == RETRO_PIXEL_FORMAT_RGB565)
    {
        format = GuestCardPixels::rgb565;
    }
    else if (video->format == RETRO_PIXEL_FORMAT_0RGB1555)
    {
        format = GuestCardPixels::xrgb1555;
    }
    uint32_t pitch = video->width * video->bytes_per_pixel;
    if (!guest_card_frame_copy(&window->frame, video->pixels, video->width, video->height, pitch, format))
    {
        SDL_Log("Guest link: no picture for the card");
        return;
    }
    if (guest_card_frame_xrgb(video->pixels, video->width, video->height, pitch, format, &window->preview_pixels))
    {
        window->preview_width = video->width;
        window->preview_height = video->height;
        window->preview_pending = true;
    }
}

// The card's title: the game's name cleaned (groups in () and [] dropped).
// A name that is all groups ("[BIOS] (World)") keeps their words instead,
// and a core without content its own name: never empty, so a link can
// always be made.
static void guest_title(const App* app, char* title)
{
    net_guest_title_clean(app->game_name, title);
    if (title[0] != '\0')
    {
        return;
    }
    char words[sizeof(app->game_name)] = {};
    std::snprintf(words, sizeof(words), "%s", app->game_name);
    for (size_t i = 0; words[i] != '\0'; i++)
    {
        if (words[i] == '(' || words[i] == ')' || words[i] == '[' || words[i] == ']')
        {
            words[i] = ' ';
        }
    }
    net_guest_title_clean(words, title);
    if (title[0] == '\0')
    {
        net_guest_title_clean(app->core_name[0] != '\0' ? app->core_name : "piccolo", title);
    }
}

// The window opens with the game's name cleaned for the card and the frame
// shown now as its picture (a link already open keeps its own).
static void open_guest_window(App* app)
{
    GuestWindow* window = &app->guest_window;
    if (!window->live)
    {
        guest_title(app, window->title);
        guest_capture(app);
    }
    guest_window_open(window);
}

// CREATE LINK (or REFRESH LINK: a fresh link, the guests in stay). Under the network lock.
static void guest_make_link(App* app)
{
    GuestWindow* window = &app->guest_window;
#if defined(PICCOLO_GUEST_CARD)
    // HTTP for the card, started here on the main thread (curl's setup isn't
    // thread safe): before, only the core downloader started it, so on
    // Windows (WinHTTP needs its session) every card failed until CORES had
    // run (Andrés's log: "WinHTTP is not initialized", 12172).
    net_http_init("piccolo/" PICCOLO_VERSION);
#endif
    NetGuestLink link;
    if (!net_guest_link_make(&link))
    {
        SDL_Log("Guest link: no randomness for a link");
        return;
    }
    std::string beacon = guest_beacon_host(app);
    net_access_guest_open(&app->net_service.access, &link, static_cast<uint32_t>(window->seats),
                          static_cast<NetGuestLevel>(window->level));
    window->link = link;
    net_guest_link_url(&link, beacon.c_str(), window->link_text, sizeof(window->link_text));
    net_guest_link_wipe(&link);
    if (!window->live)
    {
        window->core_path = app->marker_core_path;   // the game started with these (start_game)
        window->content_path = app->marker_content_path;
    }
    window->live = true;
    SDL_Log("Guest link: open for \"%s\", %d seats, %s", window->title, window->seats,
            window->level == static_cast<uint32_t>(NetGuestLevel::play) ? "watch + play" : "watch");
    // The link shows once its room is on the beacon and its card is sent
    // (guest_update_locked): pasted anywhere, it has its card at once.
    window->preparing = true;
    window->prepare_sent = false;
    window->prepare_started_ms = SDL_GetTicks();
}

// The session ends: STOP REMOTE PLAY, the game closed or changed, networking off, quitting.
// Under the network lock (or with the network stopped: `access` nullptr).
static void guest_end(App* app, NetAccess* access)
{
    if (!app->guest_window.live)
    {
        return;
    }
    guest_window_end(&app->guest_window, access);
    set_status(app, piccolo_text(&app->strings, Str::status_guests_ended));
    SDL_Log("Guest link: the session ended");
}

// Once a frame under the network lock: the session follows its game, and
// each guest learns its port. Another game (or core) ends it, a guest can't
// control what's shown; a hard reset starts the same files again and keeps
// it (Andrés, 2026-11-25).
static void guest_update_locked(App* app)
{
    GuestWindow* window = &app->guest_window;
    bool same_game = app->marker_core_path == window->core_path && app->marker_content_path == window->content_path;
    if (window->live && (!game_running(app) || !same_game))
    {
        guest_end(app, &app->net_service.access);
    }
    // A new link: its card goes up once its room is joined (or after
    // GUEST_PREPARE_MS: no WebRTC here, or a beacon that's slow), and the link
    // shows when that send is done, sent or not.
    if (window->live && window->preparing)
    {
        uint64_t now = SDL_GetTicks();
        bool ready = net_access_guest_ready(&app->net_service.access) ||
                     now - window->prepare_started_ms >= GUEST_PREPARE_MS;
        GuestCardState state = guest_card_state(&window->card);
        if (!window->prepare_sent && ready && state != GuestCardState::sending)
        {
            std::string beacon = guest_beacon_host(app);
            // The frame taken when the window opened was handed to the last
            // send (a REFRESH LINK sends again): take it again then.
            if (window->frame.pixels.empty())
            {
                guest_capture(app);
            }
            guest_card_send(&window->card, beacon.c_str(), &window->link, window->title, &window->frame);
            window->prepare_sent = true;
            window->next_refresh_ms = now + GUEST_CARD_REFRESH_MS;
        }
        else if (window->prepare_sent && state != GuestCardState::sending)
        {
            window->preparing = false;
            SDL_Log("Guest link: ready (%s)", state == GuestCardState::sent ? "its card is up" : "no card");
            guest_window_link_ready(window, now);   // CREATE / REFRESH LINK: copied now, and the card says so
        }
        return;
    }
    // A live link's picture follows the game (Andrés): taken again and sent
    // every GUEST_CARD_REFRESH_MS, unless the last send is still going.
    if (window->live && SDL_GetTicks() >= window->next_refresh_ms &&
        guest_card_state(&window->card) != GuestCardState::sending)
    {
        window->next_refresh_ms = SDL_GetTicks() + GUEST_CARD_REFRESH_MS;
        guest_capture(app);
        std::string beacon = guest_beacon_host(app);
        guest_card_send(&window->card, beacon.c_str(), &window->link, window->title, &window->frame);
    }
    guest_window_tell_ports(window, &app->net_service.access, &app->input_net, &app->input);
}

static void take_screenshot(App* app)
{
    if (!game_running(app) || app->session.host == nullptr)
    {
        return;
    }
    if (app->screenshot_gpu_pending || screenshot_writer_busy(&app->screenshot_writer))
    {
        set_status(app, piccolo_text(&app->strings, Str::status_screenshot_busy));
        return;
    }
    std::string folder = folder_setting(app, PICCOLO_PATH_SCREENSHOTS, app->paths.screenshots);
    if ((app->screenshot_writer.thread == nullptr && !screenshot_writer_create(&app->screenshot_writer)) ||
        folder.empty() || !host_fs_make_directories(folder.c_str()))
    {
        set_status(app, piccolo_text(&app->strings, Str::status_screenshot_failed));
        return;
    }
    const char* mode = settings_get(&app->settings, PICCOLO_SCREENSHOT_MODE, nullptr);
    bool both = std::strcmp(mode, "both") == 0;
    bool raw = both || std::strcmp(mode, "raw") == 0;
    bool gpu = both || !raw;
    std::string gpu_path = screenshot_path(app, folder, both ? " gpu" : "");
    if (gpu && !video_output_request_capture(&app->video))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Screenshot: no GPU capture here; saving the RAW frame instead");
        gpu = false;
        raw = true;
        both = false;
    }
    bool ok = true;
    if (raw)
    {
        ok = raw_screenshot(app, screenshot_path(app, folder, both ? " raw" : ""));
    }
    if (gpu)
    {
        app->screenshot_gpu_pending = true;
        app->screenshot_gpu_path = gpu_path;
    }
    if (!ok && !gpu)
    {
        set_status(app, piccolo_text(&app->strings, Str::status_screenshot_failed));
        return;
    }
    screen_effects_shutter(&app->screen_effects, SDL_GetTicks());
}

// Every frame, after the GPU work is recorded: a GPU capture that came back
// goes to the writer; finished files go to the status line.
static void collect_screenshots(App* app)
{
    if (!game_running(app))
    {
        app->screenshot_gpu_pending = false;   // the game closed first: its capture is dropped when it comes
    }
    VideoCapture capture = {};
    if (video_output_take_capture(&app->video, &capture))
    {
        if (app->screenshot_gpu_pending)
        {
            ScreenshotImage image = {};
            image.pixels = capture.pixels;   // handed over: the writer frees it
            image.width = capture.width;
            image.height = capture.height;
            image.pitch = capture.pitch;
            image.format = capture.format == VideoCaptureFormat::bgra8 ? ScreenshotPixels::bgra8 : ScreenshotPixels::rgba8;
            image.path = app->screenshot_gpu_path;
            capture = VideoCapture{};
            screenshot_writer_submit(&app->screenshot_writer, &image);
            app->screenshot_gpu_pending = false;
        }
        video_capture_free(&capture);
    }
    ScreenshotResult result = {};
    while (screenshot_writer_poll(&app->screenshot_writer, &result))
    {
        if (!result.ok)
        {
            set_status(app, piccolo_text(&app->strings, Str::status_screenshot_failed));
            continue;
        }
        set_status(app, piccolo_text(&app->strings, Str::status_screenshot_saved));
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Screenshot: %s", result.path.c_str());
    }
}

// The USER pause cause (the PAUSE hotkey, QUICK MENU's PAUSE, GAME > PAUSE):
// only the player lifts it. Paused, the PAUSE icon says so; lifted, PLAY
// shows a moment (no message: testing round 3).
static void toggle_user_pause(App* app)
{
    if (!game_running(app))
    {
        return;
    }
    app->ui.user_paused = !app->ui.user_paused;
    if (!app->ui.user_paused)
    {
        app->resumed_ms = SDL_GetTicks();
    }
}

// (UI modes, further down.)
static UiDerived app_derive(const App* app);
static void ui_follow_changes(App* app);
static void shader_check_end_for_game(App* app);

// The game pauses for the causes ui_derive() finds (ui_mode.h), and its
// queued audio is dropped so resuming doesn't play stale sound.
static void update_pause(App* app)
{
    app->ui_derived = app_derive(app);
    bool pause = game_running(app) && app->ui_derived.pause != 0;
    if (pause && !app->session.paused)
    {
        audio_output_clear(&app->session.audio);
    }
    app->session.paused = pause;

    // SDL blocks the screensaver; let it through while nothing plays, if allowed.
    bool allow = settings_get_bool(&app->settings, PICCOLO_SCREENSAVER_WHEN_PAUSED) && (!game_running(app) || pause);
    if (allow != app->screensaver_allowed)
    {
        app->screensaver_allowed = allow;
        if (allow)
        {
            SDL_EnableScreenSaver();
        }
        else
        {
            SDL_DisableScreenSaver();
        }
    }
}

// AUTO latency per core: a game starts its automatic target where this core
// ended last time, and where it goes is recorded as it moves (saved with the
// other state: at each game start and on quit).
// A learned AUTO latency at or above this is taken for an anomaly, not this
// core's need, and learned again (AUTO settles around 50 ms on the RK3588,
// 112 ms at worst without GameMode).
constexpr uint32_t LEARNED_LATENCY_ANOMALY_MS = 120;

static void apply_learned_latency(App* app)
{
    AudioOutputConfig* config = &app->session.audio_config;
    if (!game_running(app) || !config->automatic_latency || app->core_name[0] == '\0')
    {
        app->learned_for_start = 0;   // read again if AUTO comes back
        return;
    }
    if (app->learned_for_start != app->game_starts)
    {
        // This game's start value, read once (the same for the whole game:
        // a new start value restarts the target).
        app->learned_for_start = app->game_starts;
        const char* value = settings_file_get(&app->audio_learned, "", app->core_name);
        long ms = value != nullptr ? std::strtol(value, nullptr, 10) : 0;
        app->learned_start_ms = ms > 0 && ms <= static_cast<long>(RATE_CONTROL_MAX_TARGET_MS) ? static_cast<uint32_t>(ms) : 0;
        if (app->learned_start_ms >= LEARNED_LATENCY_ANOMALY_MS)
        {
            // An anomaly (a session that struggled: a heavy shader in a debug
            // build, a busy machine): learned again from AUTO's own start.
            SDL_Log("Audio latency: learned %u ms for %s ignored (%u ms or more), learning again",
                    app->learned_start_ms, app->core_name, LEARNED_LATENCY_ANOMALY_MS);
            app->learned_start_ms = 0;
        }
        app->learned_recorded_ms = app->learned_start_ms;
    }
    if (app->learned_start_ms > 0)
    {
        config->latency_ms = app->learned_start_ms;
    }

    const AudioOutput* audio = &app->session.audio;
    if (audio->stream == nullptr || !audio->rate.automatic)
    {
        return;
    }
    uint32_t now = static_cast<uint32_t>(audio->rate.target_ms + 0.5);
    if (now != app->learned_recorded_ms)
    {
        char number[16] = {};
        std::snprintf(number, sizeof(number), "%u", now);
        settings_file_set(&app->audio_learned, "", app->core_name, number);
        app->learned_recorded_ms = now;
        app->audio_learned_dirty = true;
    }
}

static void save_learned_latency(App* app)
{
    if (app->audio_learned_dirty && !app->audio_learned_path.empty())
    {
        settings_file_save(&app->audio_learned, app->audio_learned_path.c_str(),
                           "piccolo: automatic audio latency learned per core (ms)");
        app->audio_learned_dirty = false;
    }
}

static void save_core_options(App* app)
{
    // The options store only: core_options.cfg unreadable at start left it empty, and saving it
    // would replace the user's file. The cache below is rebuilt by the cores and stays free.
    if (app->option_store.dirty && !app->option_store_path.empty() && !app->core_options_unreadable)
    {
        core_options_store_save(&app->option_store, app->option_store_path.c_str());
    }
    // The cache right away: a core that crashes later can still be cleaned.
    if (app->option_cache.dirty && !app->option_cache_path.empty())
    {
        core_options_cache_save(&app->option_cache, app->option_cache_path.c_str());
    }
}

// settings/running.cfg: a game session is on (written before the core
// loads, so a crash while loading is noticed too).
static void write_running_marker(App* app)
{
    if (app->running_marker_path.empty())
    {
        return;
    }
    SettingsFile file = {};
    settings_file_set(&file, "", "core", app->marker_core_path.c_str());
    settings_file_set(&file, "", "content", app->marker_content_path.c_str());
    settings_file_set(&file, "", "options_changed", app->marker_options_changed ? "true" : "false");
    app->marker_written = settings_file_save(&file, app->running_marker_path.c_str(),
                                             "piccolo: a game is running (removed when it ends properly)");
}

// The session ended properly.
static void remove_running_marker(App* app)
{
    if (app->marker_written)
    {
        SDL_RemovePath(app->running_marker_path.c_str());
        app->marker_written = false;
    }
    app->marker_options_changed = false;
}

// At launch: a marker left behind means the last session didn't end.
static void check_running_marker(App* app)
{
    SettingsFile file = {};
    if (app->running_marker_path.empty() || !host_fs_exists(app->running_marker_path.c_str()) ||
        !settings_file_load(&file, app->running_marker_path.c_str()))
    {
        return;
    }
    const char* core_path = settings_file_get(&file, "", "core");
    const char* changed = settings_file_get(&file, "", "options_changed");
    char core[256] = {};
    if (core_path != nullptr && libretro_core_short_name(core_path, core, sizeof(core)))
    {
        app->crashed_core = core;
        app->crashed_after_options = changed != nullptr && std::strcmp(changed, "true") == 0;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "The last session with %s didn't end properly%s", core,
                    app->crashed_after_options ? " (its options had been changed)" : "");
    }
}

// Ask before starting the core whose last session didn't end, when it has
// stored options (with none there's nothing to choose).
static bool needs_safe_start(App* app, const FilePickerChoice* choice)
{
    char core[256] = {};
    if (app->crashed_core.empty() || !libretro_core_short_name(choice->core_path, core, sizeof(core)) ||
        app->crashed_core != core)
    {
        return false;
    }
    std::vector<std::string> sections;
    core_options_store_sections_of(&app->option_store, core, &sections);
    if (sections.empty())
    {
        app->crashed_core.clear();
        return false;
    }
    return true;
}

// True if the game started (false also when it waits for SAFE START).
static bool start_game(App* app, const FilePickerChoice* choice)
{
    // The last session didn't end (check_running_marker): no AUTO state for
    // the first start after it, in case loading it was the crash (SAFE START
    // only asks for a core with stored options).
    bool after_crash = !app->crashed_core.empty();
    if (!app->safe_start_confirmed && needs_safe_start(app, choice))
    {
        app->safe_start_pending = *choice;
        app->safe_start_open = true;   // started (or not) when it's answered
        return false;
    }
    // No resume after HARD RESET (the point is starting over) nor after a
    // crash (SAFE START or not).
    bool resume = !app->resume_skip && !app->safe_start_confirmed && !after_crash;
    app->resume_skip = false;
    app->safe_start_confirmed = false;
    shader_check_end_for_game(app);   // while the previous game's settings are current
    if (game_running(app))
    {
        save_state_on_exit(app);   // the previous game, in its own settings' layer
    }
    remove_running_marker(app);   // the previous game, if any, ended properly
    save_core_options(app);   // what was changed for the previous game
    save_learned_latency(app);   // what the previous game learned
    app->game_starts++;
    auto_frame_delay_reset(&app->auto_delay);   // each game finds its own
    app->quick_slot_stale = true;   // the new game's own quick slot
    drop_undos(app);                // the previous game's
    app->marker_core_path = choice->core_path;
    app->marker_content_path = choice->content_path;
    write_running_marker(app);
    bool started = core_session_start(&app->session, &app->shell, choice->core_path, choice->content_path);
    update_settings_context(app);
    if (started)
    {
        dvr_widget_game_started(&app->dvr, &app->session, &app->settings);   // the game's REWIND settings
        remap_widget_game_started(&app->remap);   // nothing asked yet for this game
        // The emulated device on each port, as kept for this core (or game).
        for (unsigned port = 0; port < LIBRETRO_MAX_PORTS; port++)
        {
            int32_t device = settings_get_int(&app->settings, PICCOLO_INPUT_PORT_DEVICES[port]);
            if (device >= 0 && static_cast<unsigned>(device) != app->session.host->port_devices[port])
            {
                libretro_host_set_port_device(app->session.host, port, static_cast<unsigned>(device));
            }
        }
        if (resume)
        {
            resume_state_on_start(app);
        }
    }
    if (!started)
    {
        remove_running_marker(app);   // failed cleanly: not a crash
        if (!game_running(app))
        {
            app->running_core_file[0] = '\0';   // the failed start replaced the previous game
        }
    }
    if (started)
    {
        app->crashed_core.clear();   // answered (or not this core's turn): it runs again
        app->played_with_gamepad = false;   // what plays this game is seen again
        ui_game_started(&app->ui);   // straight into the game, everything closed
        ui_follow_changes(app);
        std::snprintf(app->last_core, sizeof(app->last_core), "%s", choice->core_path);
        // The part after the last '/' or '\\' (a core path may be either on Windows).
        const char* core_file = choice->core_path;
        for (const char* c = choice->core_path; *c != '\0'; c++)
        {
            if (*c == '/' || *c == '\\')
            {
                core_file = c + 1;
            }
        }
        std::snprintf(app->running_core_file, sizeof(app->running_core_file), "%s", core_file);
        if (choice->content_path[0] != '\0')
        {
            // The folder of the file, for the next LOAD GAME.
            std::snprintf(app->last_folder, sizeof(app->last_folder), "%s", choice->content_path);
            char* slash = std::strrchr(app->last_folder, '/');
            if (slash != nullptr && slash != app->last_folder)
            {
                *slash = '\0';
            }
        }
        save_last_used(app);
        remember_recent(app, choice->core_path, choice->content_path);
        // Files extracted from archives: keep the cache bounded, never
        // touching the game that just started.
        content_cache_trim(app->session.cache_directory, PICCOLO_CONTENT_CACHE_MAX_BYTES,
                           app->session.host->content_path);
    }
    report_session(app, started, false);   // "Running ..." isn't news: the game shows
    return started;
}

// Saves the session's slot with a thumbnail of the picture as shown: the
// Picture settings' crop and aspect, at x1 (core_session.h).
static bool save_state_with_thumbnail(App* app)
{
    uint32_t aspect_width = 0;
    uint32_t aspect_height = 0;
    StateThumbnail thumbnail = picture_thumbnail(app, &aspect_width, &aspect_height);
    // UNDO SAVE STATE: the slot's old files moved aside first (state_undo.h).
    char path[LIBRETRO_PATH_MAX] = {};
    bool kept = game_running(app) &&
                libretro_host_state_path(app->session.host, app->session.state_slot, path, sizeof(path)) &&
                state_undo_keep(path);
    bool saved = core_session_save_state(&app->session, &thumbnail);
    if (!saved && kept)
    {
        state_undo_cancel(path);   // the slot as it was
    }
    if (saved)
    {
        app->saved_slot_known = true;
        app->saved_slot = app->session.state_slot;
    }
    quick_menu_refresh(&app->quick_menu);
    app->quick_slot_stale = true;
    return saved;
}

// What the core options UI (CORE CONFIGURATION's options, the manager) shows this frame.
static void core_options_state(App* app, CoreOptionsUiState* state)
{
    const std::string& folder_section = app->option_store.directory_section;
    size_t folder_at = folder_section.find("/dir:");
    const char* folder = folder_at != std::string::npos ? folder_section.c_str() + folder_at + 5 : "";
    core_options_ui_state(state, &app->strings, &app->session, &app->option_store, &app->option_cache,
                          game_running(app) ? app->core_name : "", folder, app->game_name);
}

// TOOLS > CORES: the downloader is set up again on every open (unless it's
// busy), so a changed CORE SERVER or cores folder applies.
static void open_cores_window(App* app)
{
    if (app->core_updater.state == CoreUpdaterState::idle && !app->paths.root.empty())
    {
        const char* keys[CORE_UPDATER_MAX_KEYS] = {};
        int32_t key_count = core_updater_key_count();
        key_count = key_count < CORE_UPDATER_MAX_KEYS ? key_count : CORE_UPDATER_MAX_KEYS;
        for (int32_t i = 0; i < key_count; i++)
        {
            keys[i] = core_updater_key_hex(i);
        }
        std::string work = app->paths.cache + "/downloads";
        core_updater_init(&app->core_updater, "piccolo/" PICCOLO_VERSION,
                          settings_get_text(&app->settings, PICCOLO_CORE_SERVER),
                          settings_get_bool(&app->settings, PICCOLO_CORE_SERVER_INSECURE), keys, key_count,
                          core_index_build_target(), app->cores_directory.c_str(), work.c_str());
    }
    cores_window_open(&app->cores_window, &app->core_updater);
}

// What this piccolo shares with devices granted FILE TRANSFER (and lists
// on FILE TRANSFER's own side): the game folders, SAVES, STATES and
// SCREENSHOTS as the settings say now, and CONFIG, the settings folder
// without what belongs to this device (Andrés, 2026-11-25): the global
// settings, the recent games and this machine's own files (multitap-net
// refuses the identity, the paired devices and cache/ itself). No RECORDINGS
// folder yet: piccolo has none of its own.
static void share_folder_paths(const App* app, NetShareFolderPaths* paths)
{
    *paths = {};
    piccolo_settings_game_folders(&app->settings, &paths->games);
    paths->saves = folder_setting(app, PICCOLO_PATH_SAVES, app->paths.saves);
    paths->states = folder_setting(app, PICCOLO_PATH_STATES, app->paths.states);
    paths->screenshots = folder_setting(app, PICCOLO_PATH_SCREENSHOTS, app->paths.screenshots);
    paths->config = app->paths.settings;
    paths->config_refused = {"piccolo.cfg",        "recent.cfg",      "last_used.cfg",   "imgui.ini",   "continue.cfg",
                             "audio_latency.cfg",  "build_proof.cfg", "build_proof.cfg.sig", "running.cfg",
                             "frame_delay.cfg"};
    // The default screenshots folder is made by the first screenshot; made
    // here too, so SCREENSHOTS is offered (and can receive) before that, like
    // SAVES and STATES. A folder of the user's that isn't there (a drive not
    // mounted) is left alone and not offered.
    if (paths->screenshots == app->paths.screenshots && !paths->screenshots.empty())
    {
        host_fs_make_directories(paths->screenshots.c_str());
    }
}

// This device as its hello tells it (net_pairing.h): its name (read again
// each time: SETTINGS may have changed it), piccolo, what it offers and
// uses, and its build proof when this is a signed release (net_build.h; only
// tags are signed). A proof not kept yet is fetched on a thread of its own
// and used from the next launch.
static const NetDeviceInfo* net_device_info(App* app)
{
    NetDeviceInfo* info = &app->net_info;
    std::snprintf(info->name, sizeof(info->name), "%s", settings_get_text(&app->settings, PICCOLO_NET_NAME));
    info->kind = NetAppKind::piccolo;
    info->offers = net_caps_ui_kind_offers(NetAppKind::piccolo);
    info->uses = net_caps_ui_kind_uses(NetAppKind::piccolo);
    NetBuildIdentity build;
    // (In the browser the page may still be hashing the program: asked again next time.)
    if (!app->net_proof_tried && !app->paths.settings.empty() && net_build_find_program("piccolo", &build))
    {
        app->net_proof_tried = true;
        build.app = "piccolo";
        build.version = PICCOLO_VERSION;
        build.target = build_target();   // as the release names its assets
        if (!net_build_proof_load(&build, app->paths.settings.c_str(), false, info))
        {
            net_build_proof_fetch_later(&build, app->paths.settings.c_str());
        }
        SDL_Log("Network: this build is %s", info->record_size > 0 ? "signed" : "unsigned");
    }
    return info;
}

static bool net_prepare(App* app);

// TOOLS > SHARE: this piccolo's shared folders as they are now, and the
// connection to another piccolo (started the first time, kept afterwards so
// copies go on with the window closed).
// SHARE's connection to other piccolos, started on first use (TOOLS > SHARE
// and TOOLS > PAIR use the same one).
static bool start_share_link(App* app)
{
    if (!net_share_link_started(&app->share_link) && net_prepare(app))
    {
        std::string devices_path = app->paths.settings + "/net_devices.cfg";
        net_share_link_start(&app->share_link, &app->net_identity, net_device_info(app), devices_path.c_str());
    }
    return net_share_link_started(&app->share_link);
}

// TOOLS > PAIR, before drawing: the connection's status, which SHARE reads
// itself while open (pair_window.h).
static void pair_update(App* app)
{
    if (net_share_link_started(&app->share_link) && app->pair_window.open && !app->share_window.open)
    {
        net_share_link_status(&app->share_link, &app->share_status);
    }
}

// TOOLS > PAIR...: what was pressed. PAIR A DEVICE on FILE TRANSFER's
// connection; GENERATE PAIRING CODE (and a device's request) on the
// NetAccess, so under the network lock (the network thread waits meanwhile).
static void pair_apply(App* app, const PairWindowAction* action)
{
    pair_window_apply_device(&app->pair_window, action, &app->share_link, &app->share_status,
                             settings_get_text(&app->settings, PICCOLO_NET_BEACON), app->share_window.open);
    if (app->net_running)
    {
        net_service_lock(&app->net_service);
        pair_window_apply_code(&app->pair_window, action, &app->net_service.access);
        net_service_unlock(&app->net_service);
    }
    else
    {
        pair_window_apply_code(&app->pair_window, action, nullptr);
    }
}

// TOOLS > PAIR...'s waiting steps, every frame (also closed: a request in
// the game waits as a card): 5 minutes at most (pair_window.h). Our own
// code's wait and a request are the NetAccess's, so under the network lock.
static void pair_expire(App* app)
{
    uint64_t now_ms = SDL_GetTicks();
    // (The lock only when a NetAccess step waits: not taken every frame for nothing.)
    if (app->net_running && (app->pair_window.request.active || app->pair_window.shown.active))
    {
        net_service_lock(&app->net_service);
        pair_window_expire(&app->pair_window, &app->share_link, &app->net_service.access, now_ms);
        net_service_unlock(&app->net_service);
    }
    else
    {
        pair_window_expire(&app->pair_window, &app->share_link, nullptr, now_ms);
    }
}

static void open_share_window(App* app)
{
    NetShareFolderPaths paths;
    share_folder_paths(app, &paths);
    net_share_set_folders(&app->share_folders, &paths);
    if (start_share_link(app))
    {
        // The paired piccolos' ONLINE / OFFLINE is asked through BEACON SERVER.
        net_share_link_set_beacon(&app->share_link, settings_get_text(&app->settings, PICCOLO_NET_BEACON));
        net_share_link_set_folders(&app->share_link, &paths);
    }
    share_window_open(&app->share_window);
}

// What a copy that didn't go well says on SHARE's bottom line.
static Str share_done_text(NetShareStatus status)
{
    switch (status)
    {
    case NetShareStatus::exists:
        return Str::share_done_exists;
    case NetShareStatus::same:
        return Str::share_done_same;
    case NetShareStatus::no_space:
        return Str::share_done_no_space;
    case NetShareStatus::mismatch:
        return Str::share_done_mismatch;
    case NetShareStatus::refused:
    case NetShareStatus::not_found:
        return Str::share_done_refused;
    case NetShareStatus::busy:
        return Str::share_done_busy;
    default:
        return Str::share_done_failed;
    }
}

// SHARE, once per loop while the window is open or the connection runs,
// before drawing: the connection's answers go to the window (its clicks go
// to the connection in share_window_apply, after drawing).
static void share_update(App* app, ShareTransferLine* line)
{
    ShareWindow* window = &app->share_window;
    NetShareLink* link = &app->share_link;
    if (!net_share_link_started(link))
    {
        return;
    }
    if (window->open)
    {
        net_share_link_status(link, &app->share_status);
    }
    bool connected = app->share_status.state == NetShareLinkState::ready &&
                     (app->share_status.caps & NET_CAP_SHARE) != 0;
    if (connected != app->share_connected)
    {
        app->share_connected = connected;
        share_window_remote_reset(window);   // a new piccolo, or none: its side starts over
    }
    std::string path;
    if (connected && share_window_remote_wants_list(window, &path))
    {
        net_share_link_list(link, path.c_str());
    }
    NetShareStatus status = NetShareStatus::ok;
    std::vector<NetShareEntry> entries;
    if (connected && net_share_link_listing(link, &path, &status, &entries))
    {
        share_window_set_remote_list(window, path.c_str(), status, &entries);
    }
    net_share_link_take_done(link, &app->share_done);
    for (size_t i = 0; i < app->share_done.size(); i++)
    {
        const NetShareClientDone* done = &app->share_done[i];
        if (done->status == NetShareStatus::ok)
        {
            window->local.stale = true;   // a new file on one side
            window->remote.stale = true;
            continue;
        }
        if (done->status == NetShareStatus::exists)
        {
            // Another file with that name there: SHARE asks whether to replace it.
            ShareCopy conflict;
            conflict.to_remote = done->copy.to_remote;
            conflict.from = done->copy.to_remote ? done->copy.local : done->copy.remote;
            conflict.to = done->copy.to_remote ? done->copy.remote : done->copy.local;
            conflict.size = done->copy.size;
            conflict.mtime = done->copy.mtime;
            share_window_add_conflict(window, &conflict);
            continue;
        }
        const std::string* name = done->copy.to_remote ? &done->copy.local : &done->copy.remote;
        window->message = std::string(piccolo_text(&app->strings, share_done_text(done->status))) + ": " + *name;
    }

    // The bottom line, with the rate over about a second.
    NetShareProgress progress;
    NetShareClientCopy copy;
    uint32_t queued = 0;
    net_share_link_progress(link, &progress, &copy, &queued);
    line->active = progress.role != NetShareRole::none;
    if (line->active)
    {
        uint64_t now = SDL_GetTicks();
        if (progress.id != app->share_rate_id)
        {
            app->share_rate_id = progress.id;
            app->share_rate_done = progress.done;
            app->share_rate_ticks = now;
            app->share_rate_kbps = 0;
        }
        else if (now - app->share_rate_ticks >= 1000)
        {
            uint64_t bytes = progress.done - app->share_rate_done;
            app->share_rate_kbps = static_cast<uint32_t>(bytes * 8 / (now - app->share_rate_ticks));
            app->share_rate_done = progress.done;
            app->share_rate_ticks = now;
        }
        size_t slash = copy.remote.rfind('/');
        line->name = slash == std::string::npos ? copy.remote : copy.remote.substr(slash + 1);
        line->sending = copy.to_remote;
        line->done = progress.done;
        line->size = progress.size;
        line->rate_kbps = app->share_rate_kbps;
        line->queued = queued;
    }
}

// SHARE's PAIR: pairing lives in TOOLS > PAIR... only: it opens on PAIR A
// DEVICE with that piccolo (or ENTER A CODE) and starts at once (unless the
// window is in the middle of something else). Once paired and closed, SHARE
// stays connected.
static void share_pair(App* app, bool internet)
{
    pair_window_open(&app->pair_window, nullptr);
    if (app->pair_window.mode != PairMode::device || app->pair_window.phase != PairDevicePhase::list)
    {
        return;
    }
    app->pair_window.selected = internet ? -1 : app->share_window.found_selected;
    app->pair_window.enter_selected = internet;
    PairWindowAction pair_action = {};
    pair_action.press = PairWindowPress::pair;
    pair_action.mode = PairMode::device;
    pair_apply(app, &pair_action);
}

// (UI modes, further down.)
static void resume_game(App* app);
static void app_enter_game(App* app);
static void app_leave_main_menu(App* app);
static void net_answer_load(App* app, NetContentStatus status);

static void app_open_core_config(App* app, uint32_t port, uint32_t controller);   // (the UI modes, further down)
static void app_open_widget(App* app, UiWidget widget);

// The actions on the running game that the GAME menu and QUICK MENU share,
// each in one place. Where the UI goes afterwards is the caller's: the
// menu goes back to the game, QUICK MENU may stay.

// QUICK SAVE / QUICK LOAD: the quick slot, whatever slot is selected.
static bool quick_save(App* app)
{
    if (!game_running(app))
    {
        return false;   // report_session would show the session's last message
    }
    uint32_t selected = app->session.state_slot;
    app->session.state_slot = QUICK_SLOT;
    bool saved = save_state_with_thumbnail(app);
    app->session.state_slot = selected;
    report_session(app, saved, false);
    return saved;
}

static bool quick_load(App* app)
{
    if (!game_running(app))
    {
        return false;
    }
    uint32_t selected = app->session.state_slot;
    app->session.state_slot = QUICK_SLOT;
    bool loaded = load_state_keeping_undo(app);
    app->session.state_slot = selected;
    report_session(app, loaded, false);
    return loaded;
}

// SOFT RESET: the core's own reset (the console's reset button).
static bool soft_reset(App* app)
{
    if (!game_running(app))
    {
        return false;
    }
    libretro_host_reset(app->session.host);
    return true;
}

// HARD RESET: the core and the game loaded again, as LOAD GAME would load
// them (the same files the game was started from), at the top of the next
// loop (see pending_start). The start stops the running game first.
static bool hard_reset(App* app)
{
    if (!game_running(app) || app->has_pending_start)
    {
        return false;
    }
    app->pending_start = FilePickerChoice{};
    std::snprintf(app->pending_start.core_path, sizeof(app->pending_start.core_path), "%s",
                  app->marker_core_path.c_str());
    std::snprintf(app->pending_start.content_path, sizeof(app->pending_start.content_path), "%s",
                  app->marker_content_path.c_str());
    app->has_pending_start = true;
    app->resume_skip = true;   // starting over: no AUTO state (it's still saved as the game stops)
    return true;
}

// SAVE STATE / LOAD STATE in the selected slot (GAME menu, hotkeys, another
// device): back to the game when it worked. `announce`: say so when it
// worked too (a hotkey's or a device's; the menus show it themselves).
static void save_state_from(App* app, bool announce)
{
    if (!game_running(app))
    {
        return;   // nothing to save; the session's last message is not about this
    }
    bool saved = save_state_with_thumbnail(app);
    if (saved)
    {
        app_leave_main_menu(app);
    }
    report_session(app, saved, announce);
}

static void load_state_from(App* app, bool announce)
{
    if (!game_running(app))
    {
        return;   // as save_state_from
    }
    bool loaded = load_state_keeping_undo(app);
    if (loaded)
    {
        app_leave_main_menu(app);
    }
    report_session(app, loaded, announce);
}

// UNDO SAVE STATE / UNDO LOAD STATE from the GAME menu, the hotkeys or
// another device, as SAVE / LOAD STATE: back to the game when it worked.
static void undo_save_from(App* app, bool announce)
{
    uint32_t slot = undo_save_slot(app);
    bool undone = undo_save_state(app, slot);
    if (undone)
    {
        app_leave_main_menu(app);
    }
    report_undo_save(app, undone, slot, announce);
}

static void undo_load_from(App* app, bool announce)
{
    bool undone = undo_load_state(app);
    if (undone)
    {
        app_leave_main_menu(app);
    }
    report_undo_load(app, undone, announce);
}

// CLOSE GAME: at the top of the next loop (the game's texture may be in this
// frame already).
static void close_game(App* app)
{
    if (game_running(app))
    {
        app->pending_close_game = true;
    }
}

// TOOLS > GAME DVR, QUICK MENU's GAME DVR: the rewind DVR, a window widget
// (it refuses, its HUD saying why, with nothing to go back to).
static void open_game_dvr(App* app)
{
    app_open_widget(app, UiWidget::dvr);
}

// The burger menu's CONTROLLERS: the ports widget, as the DEVICES hotkey
// opens it (Andrés, 2026-11-27: the widget, not INPUT). Only with a game and
// ports to show.
static void open_controllers(App* app)
{
    if (game_running(app) && app->ports_widget.port_count > 0)
    {
        app_open_widget(app, UiWidget::ports);
    }
}

static void handle_menu_action(App* app, MenuAction action)
{
    switch (action)
    {
        case MenuAction::none:
            return;
        case MenuAction::load_game:
        {
            core_catalog_scan(&app->catalog, game_running(app) ? app->session.host : nullptr);
            std::vector<std::string> game_folders;
            piccolo_settings_game_folders(&app->settings, &game_folders);
            file_picker_open(&app->picker, &app->catalog, app->last_folder, app->last_core, &game_folders);
            break;
        }
        case MenuAction::pause:
            // PAUSE: the player's pause, back to the (paused) game; RESUME
            // lifts it. Either way everything closes and any input grab is
            // cancelled: the way out when something holds the input.
            toggle_user_pause(app);
            resume_game(app);
            break;
        case MenuAction::close_game:
            close_game(app);
            break;
        case MenuAction::reset:
            if (soft_reset(app))
            {
                app_leave_main_menu(app);
            }
            break;
        case MenuAction::hard_reset:
            hard_reset(app);   // the game starts again straight into play
            break;
        case MenuAction::quick_save:
            if (quick_save(app))
            {
                app_leave_main_menu(app);
            }
            break;
        case MenuAction::quick_load:
            if (quick_load(app))
            {
                app_leave_main_menu(app);
            }
            break;
        case MenuAction::game_dvr:
            open_game_dvr(app);
            break;
        case MenuAction::clear_recents:
            app->recent.games.clear();
            if (!app->recent_path.empty() && !app->recent_unreadable)   // see remember_recent
            {
                recent_games_save(&app->recent, app->recent_path.c_str());
            }
            refresh_recent_labels(app);
            break;
        case MenuAction::about:
            about_window_open(&app->about_window);
            break;
        case MenuAction::save_state:
            save_state_from(app, false);
            break;
        case MenuAction::screenshot:
            if (game_running(app))
            {
                app_leave_main_menu(app);   // back to the game: the shutter plays on it
            }
            take_screenshot(app);
            break;
        case MenuAction::load_state:
            load_state_from(app, false);
            break;
        case MenuAction::undo_save_state:
            undo_save_from(app, false);
            break;
        case MenuAction::undo_load_state:
            undo_load_from(app, false);
            break;
        case MenuAction::quit:
            app->running = false;
            break;
        case MenuAction::recent_game:
            break;   // handled in app_apply_ui (it needs the index)
        case MenuAction::general:
            settings_window_open(&app->settings_window, SettingsTab::general);
            break;
        case MenuAction::input:
            input_window_open(&app->input_window);
            break;
        case MenuAction::interface:
            settings_window_open(&app->settings_window, SettingsTab::interface);
            break;
        case MenuAction::video:
            settings_window_open(&app->settings_window, SettingsTab::video);
            break;
        case MenuAction::sound:
            settings_window_open(&app->settings_window, SettingsTab::sound);
            break;
        case MenuAction::options_manager:
            core_options_manager_open(&app->core_options_manager, &app->core_options_ui);
            break;
        case MenuAction::core_config:
            app_open_core_config(app, UINT32_MAX, 0);
            break;
        case MenuAction::cores:
            open_cores_window(app);
            break;
        case MenuAction::share:
            open_share_window(app);
            break;
        case MenuAction::pair:
            pair_window_open(&app->pair_window, nullptr);   // app_ui_update starts FILE TRANSFER's connection
            break;
        case MenuAction::guests:
            open_guest_window(app);
            break;
        case MenuAction::network:
            settings_window_open(&app->settings_window, SettingsTab::network);
            break;
        case MenuAction::debug_window:
            app->show_debug_window = !app->show_debug_window;
            break;
        default:
            SDL_Log("Menu: action %d isn't there yet", static_cast<int>(action));
            break;
    }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

// F11 / Alt+Enter: the global fullscreen setting, saved on exit.
static void toggle_fullscreen(App* app)
{
    bool fullscreen = settings_get_bool(&app->settings, PICCOLO_DISPLAY_FULLSCREEN);
    settings_set_bool(&app->settings, SettingLayer::global, PICCOLO_DISPLAY_FULLSCREEN, !fullscreen);
}

// The running game's picture for the dynamic window mode: the core's base
// size (what it normally shows) with the Picture settings. False when no
// game runs.
static bool game_picture(const App* app, ViewportInput* picture)
{
    if (!game_running(app))
    {
        return false;
    }
    const retro_game_geometry* geometry = &app->session.host->av_info.geometry;
    if (geometry->base_width == 0 || geometry->base_height == 0)
    {
        return false;
    }
    *picture = ViewportInput{};
    picture->frame_width = geometry->base_width;
    picture->frame_height = geometry->base_height;
    picture->core_aspect = static_cast<double>(geometry->aspect_ratio);
    piccolo_settings_viewport(&app->settings, picture);
    return true;
}

// Low latency: in Vsync timing the loop is paced by the display, so waiting
// a few ms here (right after presenting) moves the next input read and core
// frame closer to the next refresh. Only after waiting for present: without
// it the loop blocks in acquire later, after the input read, and a delay here
// only adds latency (the setting is greyed out then).
// FRAME DELAY, or with AUTO FRAME DELAY the delay it is finding or found for
// this game (frame_delay.h). AUTO watches this loop's missed refresh
// (frame_timing) and needs a minute of uninterrupted play to lock: the menu,
// a pause, fast forward or another window having focus start that minute over.
// This game's section in frame_delay.cfg: "core/game" ("core" without content).
static std::string frame_delay_section(const App* app)
{
    std::string section = app->core_name;
    if (app->game_name[0] != '\0')
    {
        section += "/";
        section += app->game_name;
    }
    return section;
}

static void save_frame_delay_file(App* app)
{
    if (!app->frame_delay_saved_path.empty())
    {
        settings_file_save(&app->frame_delay_saved, app->frame_delay_saved_path.c_str(),
                           "piccolo: automatic frame delay found per game (ms)");
    }
}

// SEARCH AGAIN: forget this game's delay and start the search over.
static void search_frame_delay_again(App* app)
{
    if (app->core_name[0] != '\0' &&
        settings_file_remove(&app->frame_delay_saved, frame_delay_section(app).c_str(), "delay_ms"))
    {
        save_frame_delay_file(app);
    }
    auto_frame_delay_reset(&app->auto_delay);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Frame delay: searching again (automatic)");
}

// Does a streaming device's tuning decide the frame delay now (not "host")?
static bool stream_delay_overrides(const App* app)
{
    return app->stream_tuning_on && app->stream_tuning.delay != NetStreamDelay::host;
}

// The frame delay a streaming device asked for (stream_tuning): OFF, N ms or
// AUTO with its own search. Only in memory: nothing is saved, and the game's
// local AUTO isn't fed (its minute starts over, as for a pause).
static void stream_frame_delay(App* app)
{
    auto_frame_delay_interrupt(&app->auto_delay);
    app->frame_delay_ms = 0;
    bool applies = game_running(app) && !app->session.paused && !core_session_fast_forwarding(&app->session) &&
                   app->session.timing.effective == TimingMode::vsync &&
                   app->stream_tuning.delay != NetStreamDelay::off;
    if (!applies)
    {
        app->stream_delay_last_ns = 0;
        auto_frame_delay_interrupt(&app->stream_auto_delay);
        return;
    }
    uint32_t use = app->stream_tuning.delay_ms;
    if (app->stream_tuning.delay == NetStreamDelay::automatic)
    {
        // The player is on the phone: focus doesn't matter here, the menu does.
        if (app->ui_derived.frame_delay_interrupt)
        {
            auto_frame_delay_interrupt(&app->stream_auto_delay);
        }
        uint64_t now = SDL_GetTicksNS();
        double elapsed =
            app->stream_delay_last_ns > 0 ? static_cast<double>(now - app->stream_delay_last_ns) / 1e9 : 0.0;
        app->stream_delay_last_ns = now;
        use = auto_frame_delay_update(&app->stream_auto_delay, app->session.timing.missed, elapsed,
                                      auto_frame_delay_start_ms(app->session.timing.display_hz));
    }
    app->frame_delay_ms = use;
    if (use > 0)
    {
        SDL_DelayPrecise(static_cast<Uint64>(use) * 1000000u);
    }
}

static void frame_delay(App* app)
{
    if (stream_delay_overrides(app))
    {
        stream_frame_delay(app);
        return;
    }
    bool automatic = settings_get_bool(&app->settings, PICCOLO_LATENCY_FRAME_DELAY_AUTO);
    int32_t manual_ms = settings_get_int(&app->settings, PICCOLO_LATENCY_FRAME_DELAY);
    bool waits = settings_get_bool(&app->settings, PICCOLO_LATENCY_WAIT_FOR_PRESENT);
    app->frame_delay_ms = 0;
    // Not while fast-forwarding: the delay would eat the time fast forward runs
    // extra frames in, and those loops say nothing about this game's pacing
    // (AUTO's minute starts over, like a pause; the delay comes back after).
    bool applies = waits && game_running(app) && !app->session.paused &&
                   !core_session_fast_forwarding(&app->session) &&
                   app->session.timing.effective == TimingMode::vsync && (automatic || manual_ms > 0);
    if (!applies)
    {
        app->delay_last_ns = 0;
        auto_frame_delay_interrupt(&app->auto_delay);
        return;
    }
    uint32_t use = static_cast<uint32_t>(manual_ms > 0 ? manual_ms : 0);
    if (automatic)
    {
        if (app->ui_derived.frame_delay_interrupt || !app->window_focused)
        {
            auto_frame_delay_interrupt(&app->auto_delay);
        }
        uint64_t now = SDL_GetTicksNS();
        double elapsed = app->delay_last_ns > 0 ? static_cast<double>(now - app->delay_last_ns) / 1e9 : 0.0;
        app->delay_last_ns = now;
        if (!app->auto_delay.started && app->core_name[0] != '\0')
        {
            // Found before for this game: locked at it from the start.
            const char* saved = settings_file_get(&app->frame_delay_saved, frame_delay_section(app).c_str(), "delay_ms");
            long ms = saved != nullptr ? std::strtol(saved, nullptr, 10) : -1;
            if (ms >= 0 && ms <= 1000)
            {
                auto_frame_delay_restore(&app->auto_delay, static_cast<uint32_t>(ms));
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Frame delay: %ld ms, found before for this game (automatic)", ms);
            }
        }
        bool started = app->auto_delay.started;
        uint32_t before = app->auto_delay.delay_ms;
        bool was_locked = app->auto_delay.locked;
        use = auto_frame_delay_update(&app->auto_delay, app->session.timing.missed, elapsed,
                                      auto_frame_delay_start_ms(app->session.timing.display_hz));
        if (!started)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Frame delay: starting at %u ms (automatic)", use);
        }
        else if (use != before)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Frame delay: %u -> %u ms (automatic)", before, use);
        }
        if (app->auto_delay.locked && !was_locked)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Frame delay: locked at %u ms", use);
            if (app->core_name[0] != '\0')
            {
                char number[16] = {};
                std::snprintf(number, sizeof(number), "%u", use);
                settings_file_set(&app->frame_delay_saved, frame_delay_section(app).c_str(), "delay_ms", number);
                save_frame_delay_file(app);
            }
        }
    }
    app->frame_delay_ms = use;
    if (use > 0)
    {
        SDL_DelayPrecise(static_cast<Uint64>(use) * 1000000u);
    }
}

// The language setting -> the UI text and what cores are told (GET_LANGUAGE,
// from the next core start). Only redone when the setting changes: "auto"
// asks the system, which allocates.
static void update_language(App* app)
{
    uint32_t setting = settings_get_enum(&app->settings, PICCOLO_LANGUAGE);
    if (setting == app->language_setting)
    {
        return;
    }
    app->language_setting = setting;
    uint32_t language = piccolo_settings_language(&app->settings);
    app->session.language = piccolo_language_retro(language);
    piccolo_text_load_language(&app->strings, language, app->paths.root.c_str());
    web_page_set_language(piccolo_language_code(language));
    refresh_recent_labels(app);
}

static void update_window(App* app)
{
    ViewportInput picture = {};
    bool has_picture = game_picture(app, &picture);
    if (!app_loop_window_follows_page())
    {
        piccolo_window_apply(&app->window, app->shell.window, &app->settings, has_picture ? &picture : nullptr);
    }
}

// Keyboard as the game's joypad only while the game has the screen.

static void refresh_audio_devices(App* app)
{
    audio_output_list_devices(&app->audio_devices);
    app->audio_device_pointers.clear();
    for (size_t i = 0; i < app->audio_devices.size(); i++)
    {
        app->audio_device_pointers.push_back(app->audio_devices[i].c_str());
    }
}

// The slow shader question is up (shader_check.h).
static bool shader_check_asking(const App* app)
{
    return app->shader_check.state == ShaderCheckState::asking;
}

constexpr SettingLayer SHADER_LAYERS[3] = {SettingLayer::global, SettingLayer::core, SettingLayer::game};

// The preset setting as each layer holds it now (absent or a value).
static void shader_layers_read(const App* app, bool set[3], std::string value[3])
{
    for (uint32_t i = 0; i < 3; i++)
    {
        const char* text = settings_get_layer(&app->settings, SHADER_LAYERS[i], PICCOLO_VIDEO_SHADER_PRESET);
        set[i] = text != nullptr;
        value[i] = text != nullptr ? text : "";
    }
}

// KEEP: nothing changes. REVERT: every layer's preset setting as before the
// change, saved. Either way the check is over until the next change.
static void shader_check_answer(App* app, ShaderCheckChoice choice)
{
    shader_check_stop(&app->shader_check);
    if (choice != ShaderCheckChoice::revert)
    {
        return;
    }
    for (uint32_t i = 0; i < 3; i++)
    {
        if (app->shader_revert_set[i])
        {
            settings_set(&app->settings, SHADER_LAYERS[i], PICCOLO_VIDEO_SHADER_PRESET, app->shader_revert_value[i].c_str());
        }
        else
        {
            settings_reset(&app->settings, SHADER_LAYERS[i], PICCOLO_VIDEO_SHADER_PRESET);
        }
    }
    app->shader_reverted = true;
    save_settings(app);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Shader preset too slow: reverted");
}

// The running game ends or is replaced: the question can't outlive it. This
// must come BEFORE update_settings_context switches the core and game layers,
// or the revert would write the old preset into the new game's settings
// (widget_left's own revert then finds the check stopped and does nothing).
static void shader_check_end_for_game(App* app)
{
    if (shader_check_asking(app))
    {
        shader_check_answer(app, ShaderCheckChoice::revert);
    }
}

// Once per loop that ran core frames: times the loop for the check. The
// target is a display refresh in Vsync timing (each loop presents one), a
// core frame in Exact. Fast forward is fast on purpose: not a sample.
static void measure_shader_check(App* app)
{
    const FrameTiming* timing = &app->session.timing;
    double hz = timing->effective == TimingMode::vsync ? timing->display_hz : timing->core_fps;
    if (hz <= 0.0)
    {
        return;
    }
    bool sample = !core_session_fast_forwarding(&app->session);
    if (shader_check_loop(&app->shader_check, sample, SDL_GetTicksNS(), 1e9 / hz))
    {
        app->shader_check_asked_ms = SDL_GetTicks();
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Shader preset too slow: %.1f fps, target %.1f",
                    1e9 / app->shader_check.measured_ns, hz);
    }
}

// ---------------------------------------------------------------------------
// UI modes (ui_mode.h): what has the screen and the input
// ---------------------------------------------------------------------------
//
// app->ui is the truth. The windows keep their own open flags (their
// modules close themselves: an X, a choice); ui_sync_windows() keeps MAIN
// MENU's stack in step with them, once per loop. Every mode change goes
// through ui_follow_changes(): held keys are let go, input grabs cancelled,
// and whatever the old mode had open closes.

// A MAIN MENU window's own open flag, in its module.
static bool window_flag(const App* app, UiWindow window)
{
    switch (window)
    {
        case UiWindow::load_game:
            return app->picker.open && app->picker.mode != FilePickerMode::folder;
        case UiWindow::choose_folder:
            return app->picker.open && app->picker.mode == FilePickerMode::folder;
        case UiWindow::settings:
            return app->settings_window.open;
        case UiWindow::image_settings:
            return app->settings_window.image_open;
        case UiWindow::core_options:
            return app->core_options_manager;
        case UiWindow::core_config:
            return app->core_config.open;
        case UiWindow::cores:
            return app->cores_window.open;
        case UiWindow::share:
            return app->share_window.open;
        case UiWindow::input:
            return app->input_window.open;
        case UiWindow::map_controller:
            return map_window_open_now(&app->map_window);
        case UiWindow::safe_start:
            return app->safe_start_open;
        case UiWindow::debug:
            return app->show_debug_window;
        case UiWindow::about:
            return app->about_window.open;
        case UiWindow::pad_editor:
            return app->onscreen_pad.editor.active;
        case UiWindow::none:
        case UiWindow::count:
            break;
    }
    return false;
}

// Closes a MAIN MENU window through its module.
static void window_close(App* app, UiWindow window)
{
    switch (window)
    {
        case UiWindow::load_game:
        case UiWindow::choose_folder:
            app->picker.open = false;
            break;
        case UiWindow::settings:
            app->settings_window.open = false;
            break;
        case UiWindow::image_settings:
            app->settings_window.image_open = false;
            break;
        case UiWindow::core_options:
            app->core_options_manager = false;
            break;
        case UiWindow::core_config:
            app->core_config.open = false;
            app->shader_picker.open = false;   // CORE CONFIGURATION > SHADER > CHOOSE
            break;
        case UiWindow::cores:
            app->cores_window.open = false;
            break;
        case UiWindow::share:
            app->share_window.open = false;
            break;
        case UiWindow::input:
            app->input_window.open = false;
            break;
        case UiWindow::map_controller:
            map_window_close(&app->map_window, &app->input);
            break;
        case UiWindow::safe_start:
            app->safe_start_open = false;   // no answer: the game doesn't start
            // This can run with the network lock held, and answering takes it:
            // the main loop answers (net_answer_load_refused).
            app->net_load_refused = true;
            break;
        case UiWindow::debug:
            app->show_debug_window = false;
            break;
        case UiWindow::about:
            app->about_window.open = false;
            break;
        case UiWindow::pad_editor:
            onscreen_pad_edit_end(&app->onscreen_pad);   // EDIT LAYOUT: DONE
            save_settings(app);                           // its FILL / BORDER / LABELS
            break;
        case UiWindow::none:
        case UiWindow::count:
            break;
    }
}

// What grabs the input now, from the modules' own state.
static UiGrab grab_now(const App* app)
{
    if (app->core_config.macro_record != 0)
    {
        return UiGrab::macro;
    }
    if (app->input.suspended)
    {
        return UiGrab::mapping;
    }
    if (app->input.capture.active)
    {
        return UiGrab::capture;
    }
    return UiGrab::none;
}

// Every input grab ends: a SET waiting for a key, MAP CONTROLLER (it
// closes: it can't go on without the router suspended), the macro
// recorder. The router runs again.
static void cancel_grabs(App* app)
{
    if (app->core_config.macro_record != 0)
    {
        app->core_config.macro_record = 0;
    }
    if (map_window_open_now(&app->map_window) && app->input.suspended)
    {
        map_window_close(&app->map_window, &app->input);
    }
    input_capture_cancel(&app->input);
    app->input.suspended = false;
    app->input_window.capturing = false;
    app->core_config.capturing = false;
    ui_grab_end(&app->ui);
}

// What changed hands: nothing held acts in the new mode (the key that did
// it included), on the router's side and the toolkit's.
static void release_input(App* app)
{
    input_hold_until_released(&app->input);
    cancel_grabs(app);
    for (size_t i = 0; i < UI_KEY_COUNT; i++)
    {
        if (app->ui_keys_down[i])
        {
            ui_toolkit_nav_key(UI_KEYS[i].key, false);
            app->ui_keys_down[i] = false;
        }
    }
    ui_toolkit_clear_keys();
    app->ui_back_since_ms = 0;
    app->ui_back_done = false;
}

// The widget that just stopped being the mode gets its own back: the core's
// text box is cancelled (the core waits for an answer), KEEP / REVERT reverts.
static void widget_left(App* app, UiWidget widget)
{
    switch (widget)
    {
        case UiWidget::quick_menu:
            app->quick_menu.open = false;
            break;
        case UiWidget::ports:
            if (app->ports_widget.open)
            {
                ports_widget_close(&app->ports_widget, &app->input);
            }
            break;
        case UiWidget::text_input:
            if (text_input_pending(app))
            {
                libretro_host_text_input_answer(app->session.host, nullptr);
            }
            break;
        case UiWidget::shader_check:
            if (shader_check_asking(app))
            {
                shader_check_answer(app, ShaderCheckChoice::revert);
            }
            break;
        case UiWidget::dvr:
            if (app->dvr.open)
            {
                dvr_widget_close(&app->dvr);   // BACK TO NOW
            }
            break;
        case UiWidget::none:
            break;
    }
}

// After a transition: if the mode changed, input is released and what the
// old mode had open closes.
static void ui_follow_changes(App* app)
{
    if (app->ui.changes == app->ui_changes_seen)
    {
        return;
    }
    app->ui_changes_seen = app->ui.changes;
    release_input(app);
    if (app->ui.mode == UiMode::game)
    {
        remap_widget_rescan(&app->remap);   // a remap may have been decided meanwhile
    }
    for (uint32_t w = 1; w < UI_WINDOW_MAX; w++)
    {
        UiWindow window = static_cast<UiWindow>(w);
        if (window_flag(app, window) && !ui_window_open(&app->ui, window))
        {
            window_close(app, window);
        }
    }
    UiWidget widget = app->ui.mode == UiMode::widget ? app->ui.widget : UiWidget::none;
    if (app->ui_widget_applied != widget)
    {
        widget_left(app, app->ui_widget_applied);
        app->ui_widget_applied = widget;
    }
}

static void app_enter_game(App* app)
{
    ui_enter_game(&app->ui);
    ui_follow_changes(app);
}

// A menu bar action done (RESET, SAVE STATE...): back to the game. From a
// hotkey in QUICK MENU or a widget, what's up stays.
static void app_leave_main_menu(App* app)
{
    if (app->ui.mode == UiMode::main_menu)
    {
        app_enter_game(app);
    }
}

static void app_enter_main_menu(App* app)
{
    ui_enter_main_menu(&app->ui);
    ui_follow_changes(app);
}

// QUICK MENU is the window widget that's up.
static bool quick_menu_up(const App* app)
{
    return app->ui.mode == UiMode::widget && app->ui.widget == UiWidget::quick_menu;
}

static void app_open_quick_menu(App* app)
{
    app_open_widget(app, UiWidget::quick_menu);
}

static void app_open_widget(App* app, UiWidget widget)
{
    // The widget opens first and the mode changes only if it did: a refusal
    // (the DVR with rewind off, no savestates or no history yet: its HUD says
    // why) leaves what was up, the QUICK MENU included.
    if (widget == UiWidget::dvr)
    {
        if (!game_running(app) || !dvr_widget_open(&app->dvr, &app->strings, SDL_GetTicks()))
        {
            return;
        }
    }
    if (ui_enter_widget(&app->ui, widget))
    {
        if (widget == UiWidget::ports)
        {
            ports_widget_open(&app->ports_widget, &app->input);
        }
        else if (widget == UiWidget::quick_menu)
        {
            quick_menu_open(&app->quick_menu, app->session.state_slot);
        }
    }
    ui_follow_changes(app);
}

// CORE CONFIGURATION (QUICK MENU's CONFIG, GAME > CORE CONFIGURATION, HOLD
// START TO REMAP): a MAIN MENU window, so opening it closes QUICK MENU.
// `port` / `controller`: open on that controller's remap (UINT32_MAX: no).
static void app_open_core_config(App* app, uint32_t port, uint32_t controller)
{
    if (!game_running(app))
    {
        return;
    }
    if (port != UINT32_MAX)
    {
        core_config_window_open_remap(&app->core_config, port, controller);
    }
    else
    {
        core_config_window_open(&app->core_config);
    }
    app->core_options_ui.choose_layer = true;   // SAVE TO: the most specific layer in use
    ui_push_window(&app->ui, UiWindow::core_config);
    ui_follow_changes(app);
}

// MAIN MENU's stack follows the windows' own flags: one opened by its
// module (SETTINGS' CHOOSE FOLDER, INPUT's MAP CONTROLLER, a MAP CONTROLLER
// offer during play) is pushed, entering MAIN MENU; one that closed itself
// leaves the stack.
static void ui_sync_windows(App* app)
{
    for (uint32_t w = 1; w < UI_WINDOW_MAX; w++)
    {
        UiWindow window = static_cast<UiWindow>(w);
        bool open = window_flag(app, window);
        bool listed = ui_window_open(&app->ui, window);
        if (open && !listed)
        {
            ui_push_window(&app->ui, window);
        }
        else if (!open && listed)
        {
            ui_remove_window(&app->ui, window);
        }
    }
    ui_follow_changes(app);
}

// Widgets that closed themselves go back to the game; the windows the
// game asks for (a core's text box, KEEP / REVERT) open from GAME or QUICK
// MENU. In MAIN MENU they wait until it's closed: they don't take over what
// the player is doing there.
static void ui_follow_widgets(App* app)
{
    if (app->ui.mode == UiMode::widget)
    {
        bool up = true;
        switch (app->ui.widget)
        {
            case UiWidget::quick_menu:
                up = app->quick_menu.open;
                break;
            case UiWidget::ports:
                up = app->ports_widget.open;
                break;
            case UiWidget::text_input:
                up = text_input_pending(app);
                break;
            case UiWidget::shader_check:
                up = shader_check_asking(app);
                break;
            case UiWidget::dvr:
                up = app->dvr.open;
                break;
            case UiWidget::none:
                break;
        }
        if (!up)
        {
            app->ui_widget_applied = UiWidget::none;   // answered: nothing more to do for it
            app_enter_game(app);
        }
    }
    if (app->ui.mode == UiMode::game || quick_menu_up(app))
    {
        if (text_input_pending(app))
        {
            app_open_widget(app, UiWidget::text_input);
        }
        else if (shader_check_asking(app))
        {
            app_open_widget(app, UiWidget::shader_check);
        }
    }
}

// A grab's start and end, as the modules report them; a grab whose owner
// is gone (its window closed some way that didn't end it) is cancelled.
static void ui_follow_grabs(App* app)
{
    UiGrab now = grab_now(app);
    if (now != UiGrab::none && app->ui.grab == UiGrab::none)
    {
        ui_grab_begin(&app->ui, now);
    }
    else if (now == UiGrab::none && app->ui.grab != UiGrab::none)
    {
        ui_grab_end(&app->ui);
    }
    if (ui_grab_orphaned(&app->ui))
    {
        SDL_Log("Input grab left behind by its window: cancelled");
        cancel_grabs(app);
    }
}

static bool gamepad_on_a_port(const App* app);   // (input, further down)

// The facts ui_derive() needs, and what it says for this loop.
static UiDerived app_derive(const App* app)
{
    UiFacts facts = {};
    facts.pause_in_menu = settings_get_bool(&app->settings, PICCOLO_PAUSE_IN_MENU);
    facts.pause_on_focus_loss = settings_get_bool(&app->settings, PICCOLO_PAUSE_ON_FOCUS_LOSS);
    facts.focused = app->window_focused;
    facts.streaming = app->net_streaming;
    facts.headless = app->headless.on;
    facts.pause_on_disconnect = settings_get_bool(&app->settings, PICCOLO_PAUSE_ON_DISCONNECT);
    facts.played_with_gamepad = app->played_with_gamepad;
    facts.gamepad_on_port = gamepad_on_a_port(app);
    facts.background = app->android_background;
    // A request pauses once its window shows (in the game it's a card first:
    // no pause until START held opens the window).
    facts.pairing_request = app->pair_window.request.active && app->pair_window.open;
    return ui_derive(&app->ui, &facts);
}

// The game has the keyboard and the controllers (GAME mode): its keys go
// to the core. Anything else up (QUICK MENU, a window widget, MAIN MENU)
// stops the core's input; HUD widgets don't.
// A controller layout by its shown name (touch_pad_style keeps the name),
// or nullptr (RETROPAD, AUTO's "" or a layout that's gone: the RetroPad).
static const InputLayout* find_layout_by_name(const InputLayouts* layouts, const char* name)
{
    for (size_t i = 0; name[0] != '\0' && i < layouts->layouts.size(); i++)
    {
        if (layouts->layouts[i].name == name)
        {
            return &layouts->layouts[i];
        }
    }
    return nullptr;
}

// STYLE's AUTO ("", the default): the layout listing the running game's
// extension (layouts/*.cfg extensions =; "Game.sfc", or the file inside an
// archive, "snes.7z#USA/Game.sfc"), or nullptr (the RetroPad).
static const InputLayout* auto_layout(const App* app)
{
    if (!core_session_running(&app->session))
    {
        return nullptr;
    }
    std::string path = !app->marker_content_path.empty() ? app->marker_content_path : app->session.host->content_path;
    size_t cut = path.find_last_of("/\\#");
    std::string file = cut == std::string::npos ? path : path.substr(cut + 1);
    size_t dot = file.find_last_of('.');
    if (dot == std::string::npos)
    {
        return nullptr;
    }
    std::string extension = file.substr(dot + 1);
    for (size_t i = 0; i < extension.size(); i++)
    {
        extension[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(extension[i])));
    }
    return input_layouts_for_extension(&app->layouts, extension.c_str());
}

static bool game_has_keyboard(const App* app)
{
    return game_running(app) && app->ui.mode == UiMode::game;
}

static bool ui_is_up(const App* app)
{
    return !game_has_keyboard(app);
}

// A pairing request shows as a card, not its window (testing round 1): the
// player is in the game with no window over it (pair_window.h).
static bool pair_request_in_game(const App* app)
{
    return game_has_keyboard(app) && !app->pair_window.open && !app->guest_window.open;
}

// START held on any controller, for the pairing request's card: REMAP on a
// gamepad (START by default, as HOLD START TO REMAP), the keyboard player's
// START (Enter by default: the keyboard has no REMAP key).
static bool pair_accept_held(const App* app)
{
    for (size_t i = 0; i < app->input.controllers.size(); i++)
    {
        uint32_t controller = static_cast<uint32_t>(i);
        const InputDevice* device = input_devices_find_const(&app->input.devices, app->input.controllers[i].device_id);
        if (device == nullptr)
        {
            continue;
        }
        InputAction action = device->kind == InputDeviceKind::keyboard ? InputAction::pad_start : InputAction::hotkey_remap;
        if (input_controller_action_held(&app->input, controller, action))
        {
            return true;
        }
    }
    return false;
}

// Escape / B: back, the same in every mode (ui_back): a grab is cancelled,
// CORE CONFIGURATION's view, the widget or MAIN MENU's top window closes,
// then the game.
static void ui_do_back(App* app)
{
    // The debug window's preview first: it's on top, and not a MAIN MENU window.
    if (debug_preview_shown(&app->debug_previews))
    {
        debug_preview_end(&app->debug_previews);
        return;
    }
    // A view inside CORE CONFIGURATION (its shader picker, the remap view)
    // closes before the window, while it's the top one.
    bool sub_view = ui_top_window(&app->ui) == UiWindow::core_config &&
                    (app->shader_picker.open || core_config_window_sub_view(&app->core_config));
    // The phone's ☰: from a menu's items back to the names; then over HOME
    // it closes (over a game, back goes to the game below).
    if (app->phone_ui && app->ui.window_count == 0 && app->mobile_home.submenu > 0)
    {
        app->mobile_home.submenu = 0;
        app->mobile_home.menu_scroll = 0.0f;
        return;
    }
    if (app->phone_ui && app->mobile_home.menu_open && app->ui.window_count == 0)
    {
        app->mobile_home.menu_open = false;
        return;
    }
    switch (ui_back(&app->ui, sub_view))
    {
        case UiBack::none:
            break;
        case UiBack::cancel_grab:
            cancel_grabs(app);
            break;
        case UiBack::close_sub_view:
            if (app->shader_picker.open)
            {
                app->shader_picker.open = false;
            }
            else
            {
                core_config_window_close_sub_view(&app->core_config);
            }
            break;
        case UiBack::close_widget:
        case UiBack::to_game:
            app_enter_game(app);   // (a widget's own back runs as it stops being the mode)
            break;
        case UiBack::close_window:
        {
            UiWindow top = ui_top_window(&app->ui);
            if (top == UiWindow::share && !app->share_window.conflicts.empty())
            {
                break;   // its REPLACE? question takes Escape
            }
            window_close(app, top);
            ui_remove_window(&app->ui, top);
            break;
        }
    }
}

// Android's back button (SDL_SCANCODE_AC_BACK): in a game the QUICK MENU;
// anywhere else back, as Escape (the grab, the pad's burger menu, an open
// list, then the window or widget on top closes). Andrés, 2026-11-27.
static void app_back_button(App* app)
{
    if (grab_now(app) != UiGrab::none)
    {
        cancel_grabs(app);
        return;
    }
    if (app->onscreen_pad.menu_open)
    {
        app->onscreen_pad.menu_open = false;
        return;
    }
    if (ui_toolkit_has_popup())
    {
        ui_toolkit_nav_key(UiNavKey::back, true);   // the toolkit's own: it closes on Escape
        ui_toolkit_nav_key(UiNavKey::back, false);
        return;
    }
    if (app->ui.mode == UiMode::game && game_running(app) && !debug_preview_shown(&app->debug_previews))
    {
        app_open_quick_menu(app);
        return;
    }
    ui_do_back(app);
}

// GAME > RESUME: back to the game whatever is up, every grab cancelled
// (the way out when something holds the input).
static void resume_game(App* app)
{
    if (!game_running(app))
    {
        return;
    }
    cancel_grabs(app);
    app_enter_game(app);
}

// The hotkeys of this frame (input_bindings: the keyboard's F1 / F2 / F3 /
// F4 / Escape, or HOTKEY ENABLE + a button on a gamepad; the keyboard's
// without Shift in MAIN MENU).
static void app_handle_hotkeys(App* app, const InputFrame* frame)
{
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_menu)])
    {
        if (app->ui.mode == UiMode::main_menu)
        {
            ui_do_back(app);   // its top window, then the game
        }
        else
        {
            app_enter_main_menu(app);
        }
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_quick_menu)])
    {
        if (quick_menu_up(app))
        {
            app_enter_game(app);
        }
        else if (game_running(app))
        {
            app_open_quick_menu(app);
        }
    }
    // DEVICES: the same hotkey closes it; from any mode it replaces what's up.
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_ports)])
    {
        if (app->ui.mode == UiMode::widget && app->ui.widget == UiWidget::ports)
        {
            app_enter_game(app);
        }
        else if (game_running(app) && app->ports_widget.port_count > 0)
        {
            app_open_widget(app, UiWidget::ports);
        }
    }
    // The DVR: a window widget (it replaces what's up); the same hotkey goes
    // back to now.
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_dvr)])
    {
        if (app->ui.mode == UiMode::widget && app->ui.widget == UiWidget::dvr)
        {
            app_enter_game(app);
        }
        else if (game_running(app))
        {
            app_open_widget(app, UiWidget::dvr);
        }
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_save_state)])
    {
        save_state_from(app, true);
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_load_state)])
    {
        load_state_from(app, true);
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_undo_save_state)])
    {
        undo_save_from(app, true);
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_undo_load_state)])
    {
        undo_load_from(app, true);
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_screenshot)])
    {
        take_screenshot(app);   // the menus stay as they are (they're not in the picture)
    }
    // RESET (testing round 3; nothing bound by default): the core's own
    // reset, said with a card (no menu shows it).
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_reset)] && soft_reset(app))
    {
        set_status(app, piccolo_text(&app->strings, Str::status_reset));
    }
    // PAUSE: the player's own pause; what's up stays up.
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_pause)] && game_running(app))
    {
        toggle_user_pause(app);
    }
}

constexpr uint64_t UI_BACK_HOLD_MS = 600;   // hold BACK (or SELECT alone) this long to close a window

static void drive_ui_with_gamepads(App* app, const InputFrame* source_frame, bool ui_up)
{
    const InputFrame* frame = source_frame;
    for (size_t i = 0; i < UI_KEY_COUNT; i++)
    {
        uint32_t action = static_cast<uint32_t>(UI_KEYS[i].action);
        bool down = ui_up && frame->gamepad_held[action];
        if (down != app->ui_keys_down[i])
        {
            ui_toolkit_nav_key(UI_KEYS[i].key, down);
            app->ui_keys_down[i] = down;
        }
    }
    // BACK (RetroPad B): a tap closes an open list or popup, or backs out of
    // a view inside a window; a window closes only when BACK, or SELECT by
    // itself, is held (a tap must not close a window by accident).
    uint32_t back = static_cast<uint32_t>(InputAction::ui_back);
    bool back_down = ui_up && frame->gamepad_held[back];
    bool hold_down = ui_up && (back_down || frame->gamepad_enable_alone);
    uint64_t now = SDL_GetTicks();
    if (back_down && !app->ui_back_down)
    {
        if (ui_toolkit_has_popup())
        {
            ui_toolkit_nav_key(UiNavKey::back, true);   // a list or popup closes first
            ui_toolkit_nav_key(UiNavKey::back, false);
        }
        else if (ui_top_window(&app->ui) == UiWindow::core_config && core_config_window_sub_view(&app->core_config))
        {
            core_config_window_close_sub_view(&app->core_config);   // a view inside a window: back out of it
        }
    }
    if (!hold_down)
    {
        app->ui_back_since_ms = 0;
        app->ui_back_done = false;
    }
    else if (app->ui_back_since_ms == 0)
    {
        app->ui_back_since_ms = now;
    }
    else if (!app->ui_back_done && now - app->ui_back_since_ms >= UI_BACK_HOLD_MS)
    {
        app->ui_back_done = true;   // once per hold
        ui_do_back(app);
    }
    app->ui_back_down = back_down;

    // OPEN MENU (Y on a gamepad, START too on a RetroPad device): the menu
    // bar's first menu, when the bar is up and nothing else is open on top of it.
    // Not for the START that just joined a port.
    bool bar_alone = app->ui.mode == UiMode::main_menu && app->ui.window_count == 0 &&
                     !ui_toolkit_has_popup();
    if (bar_alone && frame->joined_port == INPUT_NO_PORT && frame->pressed[static_cast<uint32_t>(InputAction::ui_open_menu)])
    {
        app->open_first_menu = true;
    }
    // The CONTINUE card, while nothing covers it: the directions move its
    // focus (buttons, the shelf's tiles or the list's rows), A / Enter presses
    // the button or chooses the tile that has it.
    if (bar_alone && !app->phone_ui && frame->joined_port == INPUT_NO_PORT && !ui_toolkit_wants_text() &&
        continue_card_active(app) && app->continue_panel.preview != ContinuePreview::phone)
    {
        ContinuePanel* panel = &app->continue_panel;
        if (frame->pressed[static_cast<uint32_t>(InputAction::ui_up)])
        {
            continue_panel_key(panel, ContinueKey::up);
        }
        if (frame->pressed[static_cast<uint32_t>(InputAction::ui_down)])
        {
            continue_panel_key(panel, ContinueKey::down);
        }
        if (frame->pressed[static_cast<uint32_t>(InputAction::ui_left)])
        {
            continue_panel_key(panel, ContinueKey::left);
        }
        if (frame->pressed[static_cast<uint32_t>(InputAction::ui_right)])
        {
            continue_panel_key(panel, ContinueKey::right);
        }
        if (frame->pressed[static_cast<uint32_t>(InputAction::ui_accept)])
        {
            if (panel->focus_tile >= 0)
            {
                continue_card_choose(app, static_cast<uint32_t>(panel->focus_tile));
            }
            else
            {
                continue_card_press(app, continue_panel_focused_button(panel));
            }
        }
    }
}

// MAP CONTROLLER offered: once per session for a controller SDL doesn't
// know (it can't be used otherwise), and the first time a known one joins
// (unless mapped or declined before). Its results go to the status line
// and input.cfg.
static void offer_mapping(App* app, const InputFrame* frame)
{
    MapWindow* map = &app->map_window;
    uint64_t now = SDL_GetTicks();
    if (!map_window_open_now(map))
    {
        for (size_t u = 0; u < app->input_sdl.unmapped.size(); u++)
        {
            uint32_t joystick = app->input_sdl.unmapped[u];
            bool offered = false;
            for (size_t o = 0; o < app->map_offered.size(); o++)
            {
                offered = offered || app->map_offered[o] == joystick;
            }
            if (!offered)
            {
                app->map_offered.push_back(joystick);
                map_window_offer(map, joystick, true, &app->layouts, &app->input, now);
                break;
            }
        }
    }
    if (!map_window_open_now(map) && frame->joined_controller >= 0)
    {
        uint32_t device_id = app->input.controllers[static_cast<size_t>(frame->joined_controller)].device_id;
        const InputDevice* device = input_devices_find_const(&app->input.devices, device_id);
        if (device != nullptr && device->kind == InputDeviceKind::gamepad)
        {
            const InputProfile* profile = input_profile(&app->input, device->guid);
            uint32_t joystick = input_sdl_joystick_of(&app->input_sdl, device_id);
            if ((profile == nullptr || profile->appearance.empty()) && joystick != 0)
            {
                map_window_offer(map, joystick, false, &app->layouts, &app->input, now);
            }
        }
    }
    if (app->input_window.map_request != 0 && !map_window_open_now(map))
    {
        map_window_open(map, app->input_window.map_request, &app->layouts, &app->input);
    }
    app->input_window.map_request = 0;
    if (!map->result.empty())
    {
        SDL_Log("Map: %s", map->result.c_str());   // (the MAP window showed it: no message)
        map->result.clear();
    }
    if (map->profiles_dirty && !app->input_config_path.empty())
    {
        save_input_config(app);
        map->profiles_dirty = false;
    }
}

// The ports' cursors for the core: from the part of the frame on screen to
// the whole frame (a crop shows less of it). And the mouse grabbed while a
// port's core device is a mouse with a mouse on it (no UI up, not let go
// with MOUSE GRAB).
static void update_cursors(App* app, const InputFrame* frame, bool ui_up)
{
    for (uint32_t p = 0; p < INPUT_PORT_COUNT && p < LIBRETRO_MAX_PORTS; p++)
    {
        LibretroCursorState* cursor = &app->cursor_input.cursor[p];
        float x = (static_cast<float>(frame->cursor_x[p]) / 32767.0f + 1.0f) * 0.5f;
        float y = (static_cast<float>(frame->cursor_y[p]) / 32767.0f + 1.0f) * 0.5f;
        if (app->view_frame[0] > 0 && app->view_frame[1] > 0 && app->view_source[2] > 0 && app->view_source[3] > 0)
        {
            x = (static_cast<float>(app->view_source[0]) + x * static_cast<float>(app->view_source[2])) /
                static_cast<float>(app->view_frame[0]);
            y = (static_cast<float>(app->view_source[1]) + y * static_cast<float>(app->view_source[3])) /
                static_cast<float>(app->view_frame[1]);
        }
        cursor->x = static_cast<int16_t>(x * 65534.0f - 32767.0f);
        cursor->y = static_cast<int16_t>(y * 65534.0f - 32767.0f);
        cursor->off_screen = frame->cursor_off[p] || app->input.view_width <= 0.0f;
        cursor->mouse_dx = frame->mouse_dx[p];
        cursor->mouse_dy = frame->mouse_dy[p];
        cursor->buttons = frame->mouse_buttons[p];   // (the same bits: left, right, middle, wheel, reload, the lightgun's)
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_grab_mouse)])
    {
        app->mouse_released = !app->mouse_released;
    }
    bool mouse_port = false;
    for (uint32_t p = 0; game_running(app) && p < INPUT_PORT_COUNT; p++)
    {
        if ((app->session.host->port_devices[p] & RETRO_DEVICE_MASK) != RETRO_DEVICE_MOUSE)
        {
            continue;
        }
        for (size_t m = 0; m < app->input.ports[p].size(); m++)
        {
            uint32_t controller = app->input.ports[p][m];
            const InputDevice* device = input_devices_find_const(&app->input.devices, app->input.controllers[controller].device_id);
            mouse_port = mouse_port || (device != nullptr && device->kind == InputDeviceKind::mouse);
        }
    }
    bool grab = mouse_port && !ui_up && !app->mouse_released && app->window_focused;
    if (grab != app->mouse_grabbed)
    {
        SDL_SetWindowRelativeMouseMode(app->shell.window, grab);
        app->mouse_grabbed = grab;
    }
}

// Once per loop, after the events: what the controllers say (ports,
// hotkeys), handed to the core session before it runs frames.

// Is a connected gamepad (local or remote) on any port?
static bool gamepad_on_a_port(const App* app)
{
    for (size_t c = 0; c < app->input.controllers.size(); c++)
    {
        const InputDevice* device = input_devices_find_const(&app->input.devices, app->input.controllers[c].device_id);
        if (device != nullptr && device->connected && device->kind == InputDeviceKind::gamepad &&
            input_controller_port(&app->input, static_cast<uint32_t>(c)) != INPUT_NO_PORT)
        {
            return true;
        }
    }
    return false;
}

// PAUSE WHEN CONTROLLERS DISCONNECT: notes whether a local gamepad played
// last (a RetroPad input held in the game), or something that never
// disconnects that way: the keyboard, a mouse, a device's pad over the
// network, the on-screen pad (ui_derive's CONTROLLERS cause).
static void follow_what_plays(App* app)
{
    uint32_t pad_actions = static_cast<uint32_t>(InputAction::pad_turbo);   // the RetroPad's buttons and sticks
    for (size_t c = 0; c < app->input.controllers.size(); c++)
    {
        const InputController* controller = &app->input.controllers[c];
        if (controller->layer != InputLayer::game)
        {
            continue;
        }
        bool playing = false;
        for (uint32_t a = 0; a < pad_actions && !playing; a++)
        {
            playing = controller->held_last[a];
        }
        const InputDevice* device = input_devices_find_const(&app->input.devices, controller->device_id);
        if (playing && device != nullptr)
        {
            app->played_with_gamepad = device->kind == InputDeviceKind::gamepad && !device->game_only && !device->retropad;
        }
    }
}

// The remote pads' ping in the ports' tabs and DEVICES: read from the
// network (ENet's round trip) twice a second while they show.
constexpr uint64_t PORT_PING_EVERY_MS = 500;

static void update_port_pings(App* app)
{
    uint64_t now = SDL_GetTicks();
    if (!ports_widget_showing(&app->ports_widget) || !app->net_running || now - app->ports_ping_ms < PORT_PING_EVERY_MS)
    {
        return;
    }
    app->ports_ping_ms = now;
    bool known[NET_MAX_PEERS] = {};
    uint32_t ping_ms[NET_MAX_PEERS] = {};
    net_service_lock(&app->net_service);
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        NetLinkStats stats;
        known[peer] = net_access_peer_stats(&app->net_service.access, peer, &stats);
        ping_ms[peer] = stats.round_trip_ms;
    }
    net_service_unlock(&app->net_service);
    ports_widget_set_pings(&app->ports_widget, &app->input, &app->input_net, known, ping_ms);
}

static void app_update_input(App* app)
{
    // The mode first (windows that opened or closed themselves, widgets,
    // grabs), then what it means for the router.
    if (!game_running(app))
    {
        ui_game_stopped(&app->ui);
    }
    ui_sync_windows(app);
    ui_follow_widgets(app);
    ui_follow_grabs(app);
    app->ui_derived = app_derive(app);
    bool ui_up = app->ui_derived.interface_layer;
    piccolo_settings_input(&app->settings, &app->input.settings);   // SETTINGS > INPUT, per core / game too
    app->input.settings.ui_active = ui_up;   // the INTERFACE layer: the core gets no input
    app->input.settings.keyboard_hotkeys_free = app->ui_derived.keyboard_hotkeys_free;   // MAIN MENU: no Shift
    text_input_set_osk(&app->shell.text_input, piccolo_settings_osk(&app->settings) ? TextInputOsk::on : TextInputOsk::off);
    // The core and the device on each port: a controller's bindings for them.
    app->input.core_name = game_running(app) ? app->core_name : "";
    app->input.game_name = game_running(app) ? app->game_name : "";   // a game's own remaps
    for (unsigned port = 0; port < LIBRETRO_MAX_PORTS; port++)
    {
        app->input.port_devices[port] = app->session.host->port_devices[port];
    }
    // The touch gamepad: a controller while SHOW allows it; what its parts
    // held (drawn last frame) goes to the router while a game plays.
    OnscreenPadShow show = static_cast<OnscreenPadShow>(piccolo_settings_touch_pad(&app->settings));
    // The browser's page pad (web_pad.h) takes its place while it shows.
    bool onscreen_on = onscreen_pad_enabled(&app->onscreen_pad, show) && !web_pad_active();
    onscreen_pad_update(&app->onscreen_pad, &app->input, onscreen_on, show, app->ui_derived.overlay_active);
    InputFrame frame = {};
    input_update(&app->input, SDL_GetTicks(), &frame);
    offer_mapping(app, &frame);
    update_cursors(app, &frame, ui_up);
    if (frame.profiles_changed && !app->input_config_path.empty())
    {
        save_input_config(app);   // a turbo's speed, by taps (in the game)
    }
    // Joins and disconnects: the ports' tabs show them (no status line); logged.
    char message[300] = {};
    if (frame.joined_port != INPUT_NO_PORT && frame.joined_controller >= 0)
    {
        std::snprintf(message, sizeof(message), piccolo_text(&app->strings, Str::input_joined),
                      input_controller_label(&app->input, static_cast<uint32_t>(frame.joined_controller)).c_str(), frame.joined_port + 1);
        SDL_Log("%s", message);
    }
    if (frame.left_port != INPUT_NO_PORT && frame.left_controller >= 0)
    {
        std::snprintf(message, sizeof(message), piccolo_text(&app->strings, Str::input_left),
                      input_controller_label(&app->input, static_cast<uint32_t>(frame.left_controller)).c_str(), frame.left_port + 1);
        SDL_Log("%s", message);
        // The ports' tabs show who left; the game goes on (joining again is
        // just START). Only when that was the last gamepad and nobody was
        // playing with the keyboard or the on-screen pad (played_with_gamepad)
        // does it pause (the setting) and CONTROLS (the ports' board) open,
        // where a controller joins again (Andrés, testing round 3; no message).
        // (ui_derive's CONTROLLERS cause; it lifts by itself when one is back.)
        if (game_running(app) && settings_get_bool(&app->settings, PICCOLO_PAUSE_ON_DISCONNECT) &&
            app->played_with_gamepad && !gamepad_on_a_port(app))
        {
            SDL_Log("Paused: the last gamepad playing went away");
            if (app->ui.mode == UiMode::game && app->ports_widget.port_count > 0)
            {
                app_open_widget(app, UiWidget::ports);
            }
        }
    }
    follow_what_plays(app);
    // The ports' tabs and DEVICES: its moves first (each controller reads
    // its own buttons), then who is where.
    bool board_was_open = app->ports_widget.open;
    ports_widget_board_update(&app->ports_widget, &app->input);
    // Without a game, the PORTS setting's: a pad joining at the menu (START)
    // makes the tabs rise too, as in a game (Andrés: that's what shows a join).
    uint32_t shown_ports = game_running(app) ? piccolo_settings_ports(&app->settings, app->session.host->input_info.port_count)
                                             : piccolo_settings_ports(&app->settings, 0);
    ports_widget_update(&app->ports_widget, &app->input, &app->input_net, &frame, shown_ports, SDL_GetTicks());
    // HOLD START TO REMAP: its cards watch REMAP held (in GAME); holding it
    // opens QUICK MENU on that controller's remap.
    RemapWidgetResult remap = remap_widget_update(&app->remap, &app->input, &app->input_sdl, &app->layouts,
                                                  game_running(app), app->ui.mode == UiMode::game, app->headless.on,
                                                  SDL_GetTicks());
    if (remap.save && !app->input_config_path.empty())
    {
        save_input_config(app);   // a RetroPad remembered as decided
    }
    if (remap.open_remap)
    {
        app_open_core_config(app, remap.port, remap.controller);   // CONTROLS, that controller's remap
    }
    // A pairing request's card (in GAME): START held opens its window (the
    // game pauses then, as for a request that opens it by itself).
    if (pair_window_hold_accept(&app->pair_window, pair_request_in_game(app), pair_accept_held(app), SDL_GetTicks()))
    {
        SDL_Log("Network: pairing request accepted from the game: its window opens");
    }
    // The debug window's fake request card: the same hold, nothing accepted.
    debug_preview_hold_request(&app->debug_previews, game_has_keyboard(app), pair_accept_held(app), SDL_GetTicks());
    update_port_pings(app);
    // The DVR reads its buttons itself (before the hotkeys: the one that
    // opens it doesn't act in it the same frame).
    bool dvr_was_open = app->dvr.open;
    dvr_widget_update(&app->dvr, &frame, SDL_GetTicks());
    app_handle_hotkeys(app, &frame);
    // DEVICES reads the pads itself: they don't drive the menus meanwhile, and
    // its BACK (Escape too) only closes it. The DVR the same.
    bool board = board_was_open || app->ports_widget.open || dvr_was_open || app->dvr.open;
    drive_ui_with_gamepads(app, &frame, ui_up && !board);
    // Escape in the UI: back, at once (an open list or popup is the toolkit's:
    // it closes on the same key).
    if (frame.keyboard_pressed[static_cast<uint32_t>(InputAction::ui_back)] && !board_was_open && !dvr_was_open &&
        !ui_toolkit_has_popup())
    {
        ui_do_back(app);
    }
    // PAGE LEFT / RIGHT change tabs (not while typing in a field).
    int page = 0;
    if (ui_up && !ui_toolkit_wants_text())
    {
        page = frame.pressed[static_cast<uint32_t>(InputAction::ui_page_right)] ? 1
             : frame.pressed[static_cast<uint32_t>(InputAction::ui_page_left)]  ? -1 : 0;
    }
    ui_toolkit_page_request(page);
    core_session_set_pads(&app->session, frame.pad_buttons, frame.analog);
    bool net_fast_forward = false;
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        net_fast_forward = net_fast_forward || app->net_fast_forward[i];
    }
    app->session.fast_forward_held =
        game_running(app) && (frame.held[static_cast<uint32_t>(InputAction::hotkey_fast_forward)] || net_fast_forward);
    if (app->session.host != nullptr)
    {
        input_sdl_rumble(&app->input_sdl, &app->input, app->session.host->rumble, LIBRETRO_MAX_PORTS);
    }
}

// piccolo:// links (app_links.h; Andrés, 2026-11-22: for links to come, such
// as joining a game from the lobby: piccolo://join/<room> is kept for that).
// Nothing opens one yet: the window comes forward and says so.
static void handle_links(App* app)
{
    std::string link;
    while (app_links_next(&app->links, &link))
    {
        SDL_RaiseWindow(app->shell.window);
        // A guest link (piccolo://join/...): guests play in piccoletto; piccolo
        // has no player of its own yet (it may get one), so it only says so
        // (Andrés, testing round 3: no more handing it to the browser).
        NetGuestLink guest;
        char beacon[NET_GUEST_MAX_BEACON];
        if (net_guest_link_parse(link.c_str(), &guest, beacon, sizeof(beacon)))
        {
            net_guest_link_wipe(&guest);
            set_status(app, piccolo_text(&app->strings, Str::status_guest_link));
            continue;
        }
        // A join link that didn't parse may still hold a token: only its
        // scheme and host are shown or logged. Any other link is logged cut at
        // the query / fragment and at a sane length.
        std::string shown = link;
        bool join_link = link.compare(0, 14, "piccolo://join") == 0;
        if (join_link)
        {
            shown = link.substr(0, 14);   // "piccolo://join"
        }
        else
        {
            size_t cut = shown.find_first_of("?#");
            if (cut != std::string::npos)
            {
                shown.resize(cut);
            }
            if (shown.size() > 200)
            {
                shown.resize(200);
            }
        }
        SDL_Log("Link: %s", shown.c_str());
        std::string text = std::string(piccolo_text(&app->strings, Str::status_link_unknown)) + " " +
                           (join_link ? shown : link);
        set_status(app, text.c_str());
    }
}

static void app_handle_events(App* app)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        // A link dropped on the window, or Android's (PiccoloActivity hands a
        // link to the running app as a dropped file).
        if ((event.type == SDL_EVENT_DROP_TEXT || event.type == SDL_EVENT_DROP_FILE) && event.drop.data != nullptr)
        {
            app_links_push(&app->links, event.drop.data);
        }
        // The keyboard's Escape always cancels an input grab (SET, MAP
        // CONTROLLER, the macro recorder), before anything else sees it:
        // the router is off or reading raw controls meanwhile.
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE && grab_now(app) != UiGrab::none)
        {
            SDL_Log("Escape: input grab cancelled");
            cancel_grabs(app);
            continue;
        }
        // Android's back button is the app's, before a core that has the keyboard.
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_AC_BACK)
        {
            if (!event.key.repeat)
            {
                SDL_Log("Back button");
                app_back_button(app);
            }
            continue;
        }
        if (event.type == SDL_EVENT_KEY_UP && event.key.scancode == SDL_SCANCODE_AC_BACK)
        {
            continue;
        }
        if (core_session_handle_event(&app->session, &app->shell, &event, game_has_keyboard(app)))
        {
            continue;
        }
        // While INPUT waits for a key (SET), keys are the binding's, not the menus'.
        bool key_event = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_TEXT_INPUT;
        if (!(key_event && (app->input_window.capturing || app->core_config.capturing)))
        {
            app_shell_process_event(&app->shell, &event);
        }
        input_sdl_handle_event(&app->input_sdl, &app->input, &event);
        float screen_width = 0.0f;
        float screen_height = 0.0f;
        ui_toolkit_screen_size(&screen_width, &screen_height);
        onscreen_pad_event(&app->onscreen_pad, &event, screen_width, screen_height);

        if ((event.type == SDL_EVENT_AUDIO_DEVICE_ADDED || event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED) &&
            !event.adevice.recording)
        {
            refresh_audio_devices(app);
        }
        if (event.type == SDL_EVENT_QUIT)
        {
            app->running = false;
        }
        // Android: the BACKGROUND pause cause (SDL keeps running a little while
        // the app is behind another one).
        if (event.type == SDL_EVENT_DID_ENTER_BACKGROUND)
        {
            app->android_background = true;
            if (game_running(app))
            {
                // Android may kill piccolo back there without a quit: the
                // game saved, and that end counts as a proper one (no SAFE
                // START, the AUTO state loads next time).
                save_state_on_exit(app);
                remove_running_marker(app);
            }
        }
        if (event.type == SDL_EVENT_WILL_ENTER_FOREGROUND || event.type == SDL_EVENT_DID_ENTER_FOREGROUND)
        {
            if (app->android_background && game_running(app))
            {
                write_running_marker(app);   // running again
            }
            app->android_background = false;
        }
        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            app->window_focused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(app->shell.window))
        {
            app->running = false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            switch (event.key.key)
            {
                case SDLK_F11:
                    toggle_fullscreen(app);
                    break;
                case SDLK_RETURN:
                    if (event.key.mod & SDL_KMOD_ALT)
                    {
                        toggle_fullscreen(app);
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// Loads what each shader stage should hold when it changes: stage 0 = pass 0,
// stage 1 = the preset setting, or piccolo's final scale for `filter` when
// there's none (or it failed). Then pass 0's parameters from the settings,
// only the ones that changed. A preset that fails isn't retried every frame.
static bool shader_failed_before(const App* app, const std::string& path)
{
    for (size_t i = 0; i < app->shader_failed.size(); i++)
    {
        if (app->shader_failed[i] == path)
        {
            return true;
        }
    }
    return false;
}

// The presets this GPU path takes: librashader's .slangp on Vulkan, our own
// .glpreset on OpenGL ES (frontends-common's gl_shader_chain.h).
static const char* shader_extension(const App* app)
{
    return app->shell.api == GpuApi::gl ? ".glpreset" : ".slangp";
}

// A preset chosen on the other path (the setting is shared) is left unused.
static bool shader_fits(const App* app, const std::string& path)
{
    const char* extension = shader_extension(app);
    size_t length = std::strlen(extension);
    return path.size() > length && SDL_strcasecmp(path.c_str() + path.size() - length, extension) == 0;
}

static void load_shader_stage(App* app, uint32_t stage, const std::string& path)
{
    if (video_output_stage_preset(&app->video, stage) == path || shader_failed_before(app, path))
    {
        return;
    }
    if (!video_output_load_stage(&app->video, &app->shader_library, &app->shell, stage, path.c_str()))
    {
        app->shader_failed.push_back(path);
    }
    if (stage == 0)
    {
        app->pass0_stale = true;
    }
}

static void update_shader_stages(App* app, ViewportFilter filter)
{
    // Vulkan's presets need librashader; OpenGL ES's are ours.
    if (app->shell.api == GpuApi::vulkan && !app->shader_library.loaded)
    {
        return;
    }
    const char* preset = settings_get_text(&app->settings, PICCOLO_VIDEO_SHADER_PRESET);
    // A preset applied while this game runs is checked for speed; the one a
    // game starts with (its core / game layers) isn't: that's not a change
    // made now, and there's nothing to revert to.
    bool new_game = app->shader_layers_start != app->game_starts;
    if (new_game || app->shader_preset_seen != preset)
    {
        if (!new_game && preset[0] != '\0' && !app->shader_reverted)
        {
            // Changed again while a check runs: REVERT still goes back to
            // before the first change (the last preset that was fine).
            if (app->shader_check.state == ShaderCheckState::idle)
            {
                for (uint32_t i = 0; i < 3; i++)
                {
                    app->shader_revert_set[i] = app->shader_layer_set[i];
                    app->shader_revert_value[i] = app->shader_layer_value[i];
                }
            }
            shader_check_start(&app->shader_check);
        }
        else
        {
            shader_check_stop(&app->shader_check);
        }
        shader_layers_read(app, app->shader_layer_set, app->shader_layer_value);
        app->shader_layers_start = app->game_starts;
        app->shader_reverted = false;
    }
    if (app->shader_preset_seen != preset)
    {
        app->shader_preset_seen = preset;   // a new choice: everything may be tried again
        app->shader_failed.clear();
    }
    load_shader_stage(app, 0, app->shader_pass0_path);
    const std::string* scale = &app->shader_scale_paths[static_cast<uint32_t>(filter)];
    if (preset[0] != '\0' && shader_fits(app, app->shader_preset_seen) &&
        !shader_failed_before(app, app->shader_preset_seen))
    {
        load_shader_stage(app, 1, app->shader_preset_seen);
    }
    else
    {
        load_shader_stage(app, 1, *scale);
    }

    if (!video_output_stage_loaded(&app->video, 0))
    {
        return;
    }
    for (uint32_t i = 0; i < PICCOLO_PASS0_CONTROL_COUNT; i++)
    {
        float value = static_cast<float>(settings_get_real(&app->settings, PICCOLO_PASS0_CONTROLS[i].key));
        if (app->pass0_stale || value != app->pass0_applied[i])
        {
            video_output_set_param(&app->video, &app->shader_library, &app->shell, 0, PICCOLO_PASS0_CONTROLS[i].param, value);
            app->pass0_applied[i] = value;
        }
    }
    float profile[PICCOLO_PROFILE_PARAM_COUNT] = {};
    piccolo_color_profile_params(&app->settings, profile);
    for (uint32_t i = 0; i < PICCOLO_PROFILE_PARAM_COUNT; i++)
    {
        if (app->pass0_stale || profile[i] != app->profile_applied[i])
        {
            video_output_set_param(&app->video, &app->shader_library, &app->shell, 0, PICCOLO_PROFILE_PARAM_NAMES[i], profile[i]);
            app->profile_applied[i] = profile[i];
        }
    }
    app->pass0_stale = false;
}

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

// The game's picture for this frame: placed, its shaders and screen effect
// set, and what the game view widget shows (view->game_image), behind every
// window. The cursors learn where it is.
static void prepare_game_picture(App* app, UiView* view)
{
    CoreSession* session = &app->session;
    video_output_skip(&app->video);
    app->input.view_width = 0.0f;   // (until it is drawn)
    app->shown_aspect = 0.0;
    if (!game_running(app) || !session->texture.has_content || session->texture.ui_texture == 0)
    {
        return;
    }
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    ViewportInput input = {};
    input.frame_width = session->texture.content_width;
    input.frame_height = session->texture.content_height;
    input.core_aspect = static_cast<double>(session->host->av_info.geometry.aspect_ratio);
    input.window_width = static_cast<uint32_t>(screen_width);
    input.window_height = static_cast<uint32_t>(screen_height);
    if (app->onscreen_pad.below)
    {
        // Upright with the touch gamepad under it: the picture keeps the top.
        input.window_height = static_cast<uint32_t>(app->onscreen_pad.picture_height);
    }
    float strip = app->phone_ui ? 0.0f : dvr_widget_strip_height(&app->dvr, app->ui_scale);
    if (strip > 0.0f && static_cast<float>(input.window_height) > strip)
    {
        // The DVR's strip at the bottom: the game shrinks into the space above.
        input.window_height -= static_cast<uint32_t>(strip);
    }
    // The phone's DVR: the game in the room its panel leaves (upright the
    // top, full width; sideways the left), centered at the top of it.
    float dvr_width = 0.0f;
    float dvr_height = 0.0f;
    bool phone_dvr = app->phone_ui && dvr_widget_phone_game_area(&app->dvr, screen_width, screen_height, app->ui_scale,
                                                                 static_cast<float>(input.core_aspect), &dvr_width,
                                                                 &dvr_height);
    if (phone_dvr)
    {
        input.window_width = static_cast<uint32_t>(dvr_width);
        input.window_height = static_cast<uint32_t>(dvr_height);
    }
    piccolo_settings_viewport(&app->settings, &input);
    if (phone_dvr)
    {
        input.anchor = ViewportAnchor::top;
    }
    // A phone held upright: the picture always at the top, centered, menu up
    // or not (Andrés, 2026-11-25: opening the menu hid the pad and the picture
    // moved to the middle; the top left corner looked lost).
    if (app->phone_ui && screen_height > screen_width)
    {
        input.anchor = ViewportAnchor::top;
    }

    update_shader_stages(app, input.filter);

    // This frame's screen effect (the DVR's PLAY FROM HERE starts SNAP).
    ScreenEffectParams effect = {};
    app->screen_effects.snap_on = settings_get_bool(&app->settings, PICCOLO_SCREEN_EFFECT_REWIND);
    app->screen_effects.shutter_on = settings_get_bool(&app->settings, PICCOLO_SCREEN_EFFECT_SCREENSHOT);
    screen_effects_update(&app->screen_effects, app->dvr.played_from_ms, SDL_GetTicks(),
                          settings_get_bool(&app->settings, PICCOLO_SCREEN_EFFECTS), &effect);
    video_output_set_effect(&app->video, &effect);

    ViewportResult viewport = {};
    bool shown = false;
    if (viewport_compute(&input, &viewport))
    {
        shown = video_output_prepare(&app->video, &app->shell, &session->texture, &viewport);
    }
    if (shown)
    {
        // Where the cursors live (window pixels), and the part of the frame it shows.
        app->input.view_x = static_cast<float>(viewport.x);
        app->input.view_y = static_cast<float>(viewport.y);
        app->input.view_width = static_cast<float>(viewport.width);
        app->input.view_height = static_cast<float>(viewport.height);
        app->view_source[0] = viewport.source_x;
        app->view_source[1] = viewport.source_y;
        app->view_source[2] = viewport.source_width;
        app->view_source[3] = viewport.source_height;
        app->view_frame[0] = input.frame_width;
        app->view_frame[1] = input.frame_height;
        if (viewport.height > 0)
        {
            app->shown_aspect = static_cast<double>(viewport.width) / static_cast<double>(viewport.height);
        }
        // The game view widget: behind every window, menu bar included.
        view->game_shown = video_output_shown_image(&app->video, &session->texture, 0.0f, 0.0f, &view->game_image);
    }
}

// A folder field or GAME FOLDERS' "+" in SETTINGS: CHOOSE FOLDER, starting
// at the folder the setting has (or the default one, or the first game
// folder).
static void choose_folder_start(App* app, const char* key)
{
    if (key == nullptr)
    {
        return;
    }
    std::vector<std::string> game_folders;
    piccolo_settings_game_folders(&app->settings, &game_folders);
    // Not set: where the default is (what the empty field shows).
    const char* start = settings_get_text(&app->settings, key);
    if (start[0] == '\0' && std::strcmp(key, PICCOLO_PATH_CORES) == 0)
    {
        start = app->paths.cores.c_str();
    }
    else if (start[0] == '\0' && std::strcmp(key, PICCOLO_PATH_SYSTEM) == 0)
    {
        start = app->paths.system.c_str();
    }
    else if (start[0] == '\0' && std::strcmp(key, PICCOLO_PATH_SAVES) == 0)
    {
        start = app->paths.saves.c_str();
    }
    else if (start[0] == '\0' && std::strcmp(key, PICCOLO_PATH_STATES) == 0)
    {
        start = app->paths.states.c_str();
    }
    else if (start[0] == '\0' && std::strcmp(key, PICCOLO_PATH_SCREENSHOTS) == 0)
    {
        start = app->paths.screenshots.c_str();
    }
    app->choose_folder_key = key;
    file_picker_open_folder(&app->picker, start, &game_folders);
}

static bool is_game_folder_key(const char* key, uint32_t* slot)
{
    for (uint32_t i = 0; i < PICCOLO_GAME_FOLDER_MAX; i++)
    {
        if (std::strcmp(key, PICCOLO_PATH_GAMES[i]) == 0)
        {
            *slot = i;
            return true;
        }
    }
    return false;
}

static void choose_folder_done(App* app, const FilePickerChoice* picked)
{
    const char* key = app->choose_folder_key;
    app->choose_folder_key = nullptr;
    if (key == nullptr)
    {
        return;
    }
    uint32_t wanted = 0;
    if (!is_game_folder_key(key, &wanted))
    {
        settings_set(&app->settings, SettingLayer::global, key, picked->content_path);
    }
    else
    {
        // Into that row, or the first empty one: the slots have no gaps, and
        // a row may have been removed while the picker was open.
        uint32_t slot = 0;
        while (slot < wanted && settings_get_text(&app->settings, PICCOLO_PATH_GAMES[slot])[0] != '\0')
        {
            slot++;
        }
        for (uint32_t i = 0; i < PICCOLO_GAME_FOLDER_MAX; i++)
        {
            if (i != slot &&
                std::strcmp(settings_get_text(&app->settings, PICCOLO_PATH_GAMES[i]), picked->content_path) == 0)
            {
                return;   // already in the list
            }
        }
        settings_set(&app->settings, SettingLayer::global, PICCOLO_PATH_GAMES[slot], picked->content_path);
    }
    if (!app->settings_window.open)
    {
        apply_folders(app);
        save_settings(app);   // SETTINGS closed (and saved) while the folder was being chosen
    }
}

// CORE CONFIGURATION > SHADER > CHOOSE: the picker opens in the
// chosen preset's folder, else in the curated presets. What's picked goes to
// `scope`, the scope the QUICK MENU's SHADER tab edits.
static void open_shader_picker(App* app, SettingLayer scope)
{
    std::string folder = settings_get_at(&app->settings, scope, PICCOLO_VIDEO_SHADER_PRESET, nullptr);
    size_t slash = folder.find_last_of("/\\");
    folder = slash == std::string::npos || !shader_fits(app, folder) ? app->shader_presets_folder
                                                                      : folder.substr(0, slash);
    if (!host_fs_exists(folder.c_str()))
    {
        folder = app->shader_presets_folder;
    }
    app->shader_picker_scope = scope;
    file_picker_open_shader(&app->shader_picker, folder.c_str(), shader_extension(app));
}

// CORE CONFIGURATION > SHADER > CHOOSE's picker (drawn over that window):
// the pick goes to the scope it was opened for, and is saved (App's).
static void shader_picker_apply(App* app, const FilePickerAction* action)
{
    FilePickerChoice choice = {};
    if (!file_picker_apply(&app->shader_picker, action, &app->catalog, &choice))
    {
        return;
    }
    // NONE: no preset at this scope. Globally that's the default; a core or
    // game says it explicitly, so a global preset doesn't apply there.
    SettingLayer scope = app->shader_picker_scope;
    if (choice.content_path[0] != '\0' || scope != SettingLayer::global)
    {
        settings_set(&app->settings, scope, PICCOLO_VIDEO_SHADER_PRESET, choice.content_path);
    }
    else
    {
        settings_reset(&app->settings, scope, PICCOLO_VIDEO_SHADER_PRESET);
    }
    save_settings(app);
}

// SETTINGS' state: its notes as text, IMAGE SETTINGS' picture, and the
// NETWORK tab (read under the network lock).
static void settings_view(App* app, UiView* view, const AppUiFrame* frame)
{
    // Only the drawing reads this state, and settings_window_draw returns first while both
    // windows are closed: nothing to build, and no network lock taken for the NETWORK tab.
    // (The tab showing is only known after drawing, so the lock stays for "window open".)
    if (!app->settings_window.open && !app->settings_window.image_open)
    {
        return;
    }
    const CoreSession* session = &app->session;
    ImageWindowSource image = {};
    if (game_running(app) && session->texture.has_content && session->texture.ui_texture != 0)
    {
        image.texture_id = session->texture.ui_texture;
        video_texture_uv_max(&session->texture, &image.uv_max_x, &image.uv_max_y);
        image.frame_width = session->texture.content_width;
        image.frame_height = session->texture.content_height;
        // Pass 0's result (the shown part, after the crop): drawn over the
        // preview so the picture controls show.
        video_output_stage0_result(&app->video, &image.processed_texture_id, &image.processed_width,
                                   &image.processed_height);
    }
    image.controls_available = app->shader_library.loaded || app->shell.api == GpuApi::gl;
    SettingsWindowNotes notes = {};
    // The NETWORK tab: its state is filled under the network lock, below.
    // FILE TRANSFER's connection (our side): read in app_ui_update.
    const NetShareLinkStatus* link = frame->hosts_known ? &view->settings_hosts : nullptr;
    // Text with numbers: the translated part, then the numbers (no formats in translations).
    const char* window_label = piccolo_text(&app->strings, Str::section_window);
    char* window_size = view->settings_text.window_size;
    size_t window_size_max = sizeof(view->settings_text.window_size);
    if (app->window.target_width == 0)
    {
        std::snprintf(window_size, window_size_max, "%s: %s", window_label,
                      piccolo_text(&app->strings, Str::window_sized_when_running));
    }
    else if (app->window.target_scale > 0)
    {
        std::snprintf(window_size, window_size_max, "%s: %u X %u (X%u)", window_label, app->window.target_width,
                      app->window.target_height, app->window.target_scale);
    }
    else
    {
        std::snprintf(window_size, window_size_max, "%s: %u X %u", window_label, app->window.target_width,
                      app->window.target_height);
    }
    notes.window_size = window_size;
    notes.present_wait = piccolo_text(&app->strings, app_shell_waits_for_screen(&app->shell)
                                                         ? Str::present_wait_screen
                                                         : Str::present_wait_gpu);
    notes.audio_devices = app->audio_device_pointers.data();
    notes.audio_device_count = static_cast<uint32_t>(app->audio_device_pointers.size());
    char* audio_latency = view->settings_text.audio_latency;
    size_t audio_latency_max = sizeof(view->settings_text.audio_latency);
    const AudioOutput* audio = &app->session.audio;
    if (audio->stream == nullptr)
    {
        std::snprintf(audio_latency, audio_latency_max, "%s",
                      piccolo_text(&app->strings, Str::audio_latency_no_game));
    }
    else
    {
        std::snprintf(audio_latency, audio_latency_max, "%s: %.0f MS%s, %s: %u MS",
                      piccolo_text(&app->strings, Str::audio_latency_now), audio->rate.target_ms,
                      audio->rate.automatic ? " (AUTO)" : "", piccolo_text(&app->strings, Str::audio_latency_minimum),
                      audio->rate.floor_ms);
    }
    notes.audio_latency = audio_latency;
    dvr_widget_note(&app->dvr, &app->strings, app->rewind_note, sizeof(app->rewind_note));
    notes.rewind_note = app->rewind_note;
    notes.rewind_known = settings_get_layer(&app->settings, SettingLayer::game, PICCOLO_REWIND_KNOWN) != nullptr;
    // The mode the running game records in now: "CURRENT MODE: SCRUB"
    // (Andrés, testing round 3; REWIND's header used to say it). Measuring or
    // no savestates: that note instead.
    if (game_running(app) && (app->dvr.rewind.state == CoreRewindState::off ||
                              app->dvr.rewind.state == CoreRewindState::scrub ||
                              app->dvr.rewind.state == CoreRewindState::snapshots))
    {
        Str mode = Str::value_rewind_off;
        if (app->dvr.rewind.state == CoreRewindState::scrub)
        {
            mode = Str::value_rewind_scrub;
        }
        else if (app->dvr.rewind.state == CoreRewindState::snapshots)
        {
            mode = Str::value_rewind_snapshots;
        }
        size_t used = std::strlen(app->rewind_note);
        std::snprintf(app->rewind_note + used, sizeof(app->rewind_note) - used, "%s%s %s", used > 0 ? " / " : "",
                      piccolo_text(&app->strings, Str::rewind_note_current), piccolo_text(&app->strings, mode));
    }
    Str game_mode = Str::game_mode_off;
    switch (app->game_mode.state)
    {
    case GameModeState::off:
        game_mode = Str::game_mode_off;
        break;
    case GameModeState::active:
        game_mode = Str::game_mode_active;
        break;
    case GameModeState::not_available:
        game_mode = Str::game_mode_not_available;
        break;
    case GameModeState::not_supported:
        game_mode = Str::game_mode_not_supported;
        break;
    }
    notes.game_mode = piccolo_text(&app->strings, game_mode);
    char* frame_delay_text = view->settings_text.frame_delay;
    size_t frame_delay_max = sizeof(view->settings_text.frame_delay);
    const AutoFrameDelay* auto_delay = &app->auto_delay;
    if (game_running(app) && app->session.timing.effective != TimingMode::vsync)
    {
        std::snprintf(frame_delay_text, frame_delay_max, "%s",
                      piccolo_text(&app->strings, Str::frame_delay_needs_vsync));
    }
    else if (!game_running(app) || !auto_delay->started)
    {
        std::snprintf(frame_delay_text, frame_delay_max, "%s",
                      piccolo_text(&app->strings, Str::frame_delay_needs_game));
    }
    else if (auto_delay->locked)
    {
        // While paused (this window pauses the game) the delay isn't applied:
        // what's shown is what resuming uses.
        std::snprintf(frame_delay_text, frame_delay_max, "%s: %u MS - %s",
                      piccolo_text(&app->strings, Str::frame_delay_now), auto_delay->delay_ms,
                      piccolo_text(&app->strings, Str::frame_delay_locked));
    }
    else
    {
        std::snprintf(frame_delay_text, frame_delay_max, "%s: %u MS - %s %.0f S",
                      piccolo_text(&app->strings, Str::frame_delay_now), auto_delay->delay_ms,
                      piccolo_text(&app->strings, Str::frame_delay_searching),
                      auto_frame_delay_seconds_to_lock(auto_delay));
    }
    notes.frame_delay = frame_delay_text;
    // RUN-AHEAD: what the running core does with it.
    char* run_ahead_text = view->settings_text.run_ahead;
    size_t run_ahead_max = sizeof(view->settings_text.run_ahead);
    const LibretroHost* run_ahead_host = app->session.host;
    if (!game_running(app))
    {
        std::snprintf(run_ahead_text, run_ahead_max, "%s", piccolo_text(&app->strings, Str::run_ahead_no_game));
    }
    else if (run_ahead_host->core_run_ahead_max == 0)
    {
        std::snprintf(run_ahead_text, run_ahead_max, "%s", piccolo_text(&app->strings, Str::run_ahead_none));
    }
    else
    {
        std::snprintf(run_ahead_text, run_ahead_max, "%s: %u (%s %u)",
                      piccolo_text(&app->strings, Str::run_ahead_in_use), libretro_host_run_ahead_applied(run_ahead_host),
                      piccolo_text(&app->strings, Str::run_ahead_most), run_ahead_host->core_run_ahead_max);
    }
    notes.run_ahead = run_ahead_text;
    notes.frame_delay_found = game_running(app) && auto_delay->locked;
    // The NETWORK tab reads the NetAccess: the network thread waits
    // meanwhile. The paired devices are copied (it's drawn without the lock).
    bool network_locked = app->net_running;
    if (network_locked)
    {
        net_service_lock(&app->net_service);
    }
    NetAccess* network_access = app->net_running ? &app->net_service.access : nullptr;
    net_window_state(&view->network, &app->settings, &app->strings, network_access, link, app->net_listen_failed,
                     app->net_fingerprint, &app->net_speed_results,
                     PICCOLO_BROWSER || settings_get_bool(&app->settings, PICCOLO_NET_INTERNET),
                     net_caps_ui_kind_offers(NetAppKind::piccolo), net_caps_ui_kind_uses(NetAppKind::piccolo));
    if (network_locked)
    {
        net_service_unlock(&app->net_service);
    }
    settings_window_state(&view->settings, &app->settings_window, &app->settings, &app->strings, &app->paths,
                          app->core_name, app->game_name, &notes, &app->themes, &image, &view->network);
}

// What was done in SETTINGS (its NETWORK tab under the network lock: it
// changes the NetAccess, pairing, levels, FORGET).
static void settings_apply(App* app, const AppUiFrame* frame, SettingsWindowAction* action)
{
    // Closed and not just closed: the window gave no action, so there is nothing to apply (nor a
    // lock to take). The step that saves on closing below needs this frame's open or last
    // frame's open, so it still runs on the frame it closes.
    bool open = app->settings_window.open || app->settings_window.image_open;
    if (!open && !app->settings_window_was_open)
    {
        return;
    }
    bool network_locked = app->net_running;
    if (network_locked)
    {
        net_service_lock(&app->net_service);
    }
    NetAccess* network_access = app->net_running ? &app->net_service.access : nullptr;
    // DEVICES names its devices by key: whatever the network thread changed
    // since drawing, a press reaches the device it was meant for (or none).
    NetShareLinkStatus link_status;
    NetShareLink* link = nullptr;
    if (frame->hosts_known)
    {
        net_share_link_status(&app->share_link, &link_status);
        link = &app->share_link;
    }
    settings_window_apply(action, &app->settings);   // SETTINGS saves on closing
    net_window_apply(&action->network, &app->settings, network_access, link, &link_status);
    if (network_locked)
    {
        net_service_unlock(&app->net_service);
    }
    // CONNECT on a paired device: FILE TRANSFER opens, connected to it (the
    // only thing a connection to it is for here), across the internet
    // through BEACON SERVER as FILE TRANSFER's own CONNECT does.
    if (action->network.connect && link != nullptr)
    {
        for (size_t i = 0; i < link_status.paired_devices.size(); i++)
        {
            if (std::memcmp(link_status.paired_devices[i].key, action->network.key, NET_KEY_SIZE) == 0)
            {
                open_share_window(app);
                net_share_link_set_beacon(&app->share_link, settings_get_text(&app->settings, PICCOLO_NET_BEACON));
                net_share_link_connect_paired(&app->share_link, static_cast<uint32_t>(i));
            }
        }
    }
    choose_folder_start(app, action->choose_folder);
    if (action->search_frame_delay)
    {
        search_frame_delay_again(app);
    }
    // MEASURE AGAIN: AUTO forgets this game's verdict (measured at its next start).
    if (action->rewind_measure_again)
    {
        settings_reset(&app->settings, SettingLayer::game, PICCOLO_REWIND_KNOWN);
        save_settings(app);
    }
    // SAVE THEME: into themes/ in piccolo's folder, then listed in THEME.
    if (action->save_theme)
    {
        std::string name = action->save_theme_name;
        if (app->paths.root.empty() ||
            !piccolo_themes_save(&app->settings, name.c_str(), (app->paths.root + "/themes").c_str(), &app->themes))
        {
            set_status(app, piccolo_text(&app->strings, Str::ui_theme_save_failed));
        }
        else
        {
            app->settings_window.theme_choice = name;   // CUSTOM THEME shows the saved theme
            app->settings_window.theme_custom = true;
            for (size_t i = 0; i < app->themes.size(); i++)
            {
                if (app->themes[i].custom && app->themes[i].name == name)
                {
                    // The saved theme is piccolo's theme from now on.
                    settings_set(&app->settings, SettingLayer::global, PICCOLO_UI_THEME,
                                 piccolo_ui_theme_file_id(&app->themes[i]).c_str());
                }
            }
            save_settings(app);
        }
    }

    // Closed (by its button or Esc): apply the folders and save.
    if (app->settings_window_was_open && !open)
    {
        apply_folders(app);
        save_settings(app);
        save_core_options(app);
    }
    app->settings_window_was_open = open;
}

// The quick menu or the options manager closed: what was changed there in
// the core options is written now. (While they are open it waits, so an
// option that crashes the core at once is not kept.)
static void save_after_options_windows(App* app)
{
    bool open = app->core_config.open || app->core_options_manager;
    if (app->options_window_was_open && !open)
    {
        save_core_options(app);
        save_settings(app);   // a port's DEVICE (CONTROLS)
        if (app->core_config.input_dirty && !app->input_config_path.empty())
        {
            save_input_config(app);   // a remap (CONTROLS)
            app->core_config.input_dirty = false;
        }
    }
    app->options_window_was_open = open;
}

// The debug window's text box answers here (a fake TEXT request).
static void debug_text_done(const char* text)
{
    SDL_Log("Debug text box: %s", text != nullptr ? text : "(cancelled)");
}

// The debug window (debug_window.h): what it shows, from the App.
static_assert(DEBUG_WINDOW_ICON_COUNT == STATE_ICON_COUNT, "the debug window forces each state icon");
static void debug_window_state(App* app, DebugWindowState* state)
{
    static AppShellGpuInfo gpu = {};   // the strings must outlive this function
    app_shell_gpu_info(&app->shell, &gpu);
    state->piccolo_version = PICCOLO_VERSION;
    state->host_version = HOST_LIBRETRO_VERSION;
    state->gpu_device = gpu.device;
    state->gpu_api = gpu.api;
    state->gpu_swapchain = gpu.swapchain;
    state->gpu_validation = gpu.validation;
    state->ui_scale = app->ui_scale;
    state->screen_hz = app->fps_meter.screen_hz;
    state->work_ms = app->fps_meter.work_ms;
    state->game_running = game_running(app);
    if (state->game_running)
    {
        const CoreSession* session = &app->session;
        state->core_name = session->host->system_info.library_name;
        state->core_fps = session->host->av_info.timing.fps;
        state->timing_mode = frame_timing_mode_name(session->timing.effective);
        state->audio_ratio = session->audio.current_ratio;
        state->audio_queued_ms = audio_output_queued_ms(&session->audio);
        state->audio_target_ms = session->audio.rate.target_ms;
        state->audio_automatic = session->audio.rate.automatic;
        state->audio_underruns = session->audio.underruns;
        state->missed_refreshes = session->timing.missed_total;
        state->caught_up_frames = session->timing.caught_up_total;
    }
    state->frame_delay_ms = app->frame_delay_ms;
    for (uint32_t i = 0; i < DEBUG_WINDOW_ICON_COUNT; i++)
    {
        state->icons_forced[i] = app->state_icons.debug_show[i];
    }
    state->devices_available = app->ports_widget.port_count > 0;
    // Any request of the core's (TEXT or KEYS) is left alone: the debug box would overwrite it.
    state->text_box_allowed = state->game_running && app->session.host->text_input.mode == RETRO_EXT_TEXT_INPUT_OFF;
    state->keep_revert_allowed = state->game_running && !shader_check_asking(app);
    state->pair_request_card = app->debug_previews.request_card;
    state->pair_open = app->pair_window.open;
    state->about_open = app->about_window.open;
    state->settings_error_open = app->settings_error_open;
    state->guest_open = app->guest_window.open;
    state->snow = app->snow_debug;
    state->snow_in_season = app->snow_in_season;
    state->snow_falling = app->snow_was_shown;
    state->snow_params = app->snow.params;
}

// What was pressed in the debug window.
static void debug_window_apply(App* app, const DebugWindowAction* action)
{
    if (action->closed)
    {
        app->show_debug_window = false;
    }
    for (uint32_t i = 0; i < DEBUG_WINDOW_ICON_COUNT; i++)
    {
        app->state_icons.debug_show[i] = action->icons_forced[i];
    }
    app->snow_debug = action->snow;
    snow_view_set_params(&app->snow, &action->snow_params);

    uint64_t now_ms = SDL_GetTicks();
    debug_preview_request_card(&app->debug_previews, action->pair_request_card, now_ms);
    if (action->preview != DebugPreview::none)
    {
        debug_preview_show(&app->debug_previews, action->preview);
    }
    static const Str DVR_CARDS[3] = {Str::dvr_hud_off, Str::dvr_hud_unsupported, Str::dvr_hud_nothing};
    uint32_t dvr_card = 3;
    switch (action->press)
    {
    case DebugWindowPress::none:
        break;
    case DebugWindowPress::remap_card:
        if (!remap_widget_debug_card(&app->remap, &app->input, &app->input_sdl, &app->layouts, now_ms))
        {
            SDL_Log("Debug: no controller on port 1 for a remap card");
        }
        break;
    case DebugWindowPress::effect_snap:
        screen_effects_test_snap(&app->screen_effects, SDL_GetTicks());
        break;
    case DebugWindowPress::effect_shutter:
        screen_effects_test_shutter(&app->screen_effects, SDL_GetTicks());
        break;
    case DebugWindowPress::play_from_here:
        app->dvr.played_from_ms = now_ms;
        break;
    case DebugWindowPress::dvr_card_off:
        dvr_card = 0;
        break;
    case DebugWindowPress::dvr_card_unsupported:
        dvr_card = 1;
        break;
    case DebugWindowPress::dvr_card_nothing:
        dvr_card = 2;
        break;
    case DebugWindowPress::ports_rise:
    {
        bool changed[PORTS_VIEW_MAX_PORTS] = {};
        for (uint32_t p = 0; p < PORTS_VIEW_MAX_PORTS; p++)
        {
            changed[p] = true;
        }
        ports_view_pop(&app->ports_widget.view, changed, now_ms);
        break;
    }
    case DebugWindowPress::open_devices:
        app_open_widget(app, UiWidget::ports);
        break;
    case DebugWindowPress::open_dvr:
        app_open_widget(app, UiWidget::dvr);
        break;
    case DebugWindowPress::text_box:
    {
        // Offered only while the core has no request of its own (debug_window_state),
        // so this never replaces one.
        LibretroTextInput* request = &app->session.host->text_input;
        request->mode = RETRO_EXT_TEXT_INPUT_TEXT;
        request->type = RETRO_EXT_TEXT_INPUT_TYPE_TEXT;
        std::snprintf(request->prompt, sizeof(request->prompt), "%s", "NAME (DEBUG)");
        std::snprintf(request->initial, sizeof(request->initial), "%s", "PICCOLO");
        request->max_length = 8;
        request->done = debug_text_done;
        request->serial++;
        break;
    }
    case DebugWindowPress::keep_revert:
        // REVERT goes back to the preset in use now: nothing changes.
        for (uint32_t i = 0; i < 3; i++)
        {
            app->shader_revert_set[i] = app->shader_layer_set[i];
            app->shader_revert_value[i] = app->shader_layer_value[i];
        }
        app->shader_check.state = ShaderCheckState::asking;
        app->shader_check.measured_ns = 1e9 / 41.0;
        app->shader_check_asked_ms = now_ms;
        break;
    case DebugWindowPress::card_link_copied:
        debug_preview_show_card(&app->debug_previews, DebugCard::link_copied, now_ms);
        break;
    case DebugWindowPress::card_pair_timed_out:
        debug_preview_show_card(&app->debug_previews, DebugCard::pair_timed_out, now_ms);
        break;
    case DebugWindowPress::card_pair_failed:
        debug_preview_show_card(&app->debug_previews, DebugCard::pair_failed, now_ms);
        break;
    case DebugWindowPress::card_undo_save:
        report_undo_save(app, true, 3, true);   // the real cards, nothing undone
        break;
    case DebugWindowPress::card_undo_load:
        report_undo_load(app, true, true);
        break;
    case DebugWindowPress::card_undo_nothing:
        report_undo_load(app, false, true);
        break;
    case DebugWindowPress::card_resumed:
        set_status(app, piccolo_text(&app->strings, Str::status_resumed));
        break;
    case DebugWindowPress::card_resume_failed:
        set_status(app, piccolo_text(&app->strings, Str::status_resume_failed));
        break;
    case DebugWindowPress::sleigh_now:
        snow_view_sleigh_now(&app->snow);
        break;
    case DebugWindowPress::snow_restart:
        snow_view_restart(&app->snow);
        break;
    case DebugWindowPress::continue_resume:
        continue_panel_preview(&app->continue_panel, ContinuePreview::resume);
        break;
    case DebugWindowPress::continue_no_states:
        continue_panel_preview(&app->continue_panel, ContinuePreview::no_states);
        break;
    case DebugWindowPress::continue_newer_save:
        continue_panel_preview(&app->continue_panel, ContinuePreview::newer_save);
        break;
    case DebugWindowPress::continue_vertical:
        continue_panel_preview(&app->continue_panel, ContinuePreview::vertical);
        break;
    case DebugWindowPress::continue_phone:
        continue_panel_preview(&app->continue_panel, ContinuePreview::phone);
        break;
    case DebugWindowPress::continue_real:
        continue_panel_preview(&app->continue_panel, ContinuePreview::none);
        break;
    }
    if (dvr_card < 3)
    {
        std::snprintf(app->dvr.hud, sizeof(app->dvr.hud), "%s", piccolo_text(&app->strings, DVR_CARDS[dvr_card]));
        app->dvr.hud_since_ms = now_ms;
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

// game_state (what runs now) to one authorized peer, or to all of them
// (NET_NO_PEER).
static void net_send_game_state(App* app, uint32_t peer)
{
    size_t size = net_games_write_state(&app->net_games, &app->catalog, app->net_sent_core.c_str(),
                                        app->net_sent_content.c_str());
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        // A guest hears what runs too (net_access_send lets game_state through to it).
        NetAccessPeerState state = app->net_service.access.peers[i].state;
        if ((peer == NET_NO_PEER || peer == i) && (state == NetAccessPeerState::authorized || state == NetAccessPeerState::guest))
        {
            net_access_send(&app->net_service.access, i, NetChannel::control, app->net_games.reply.data(), size);
        }
    }
}

// Tells every device when what runs changed, however it changed (LOAD GAME,
// the network, CLOSE GAME, a core that quit). Compared every loop, copied
// only on a change.
static void net_follow_game_state(App* app)
{
    bool running = game_running(app);
    const char* core = running ? app->marker_core_path.c_str() : "";
    const char* content = running ? app->marker_content_path.c_str() : "";
    if (app->net_sent_core == core && app->net_sent_content == content)
    {
        return;
    }
    app->net_sent_core = core;
    app->net_sent_content = content;
    net_send_game_state(app, NET_NO_PEER);
}

static NetRemoteGame net_remote_game(const App* app)
{
    NetRemoteGame game;
    game.running = game_running(app);
    game.slot = app->session.state_slot;
    game.pause = 0;
    if (game.running)
    {
        // The wire's bits (net_settings.h), not UiPause's: a device reads them.
        const uint32_t pause = app->ui_derived.pause;
        if (app->ui.user_paused)
        {
            game.pause |= NET_PAUSE_USER;
        }
        if ((pause & ui_pause_bit(UiPause::menu)) != 0)
        {
            game.pause |= NET_PAUSE_MENU;
        }
        if ((pause & ui_pause_bit(UiPause::focus)) != 0)
        {
            game.pause |= NET_PAUSE_FOCUS;
        }
        if ((pause & ui_pause_bit(UiPause::controllers)) != 0)
        {
            game.pause |= NET_PAUSE_CONTROLLERS;
        }
        if ((pause & ui_pause_bit(UiPause::background)) != 0)
        {
            game.pause |= NET_PAUSE_BACKGROUND;
        }
        if ((pause & ui_pause_bit(UiPause::pairing)) != 0)
        {
            game.pause |= NET_PAUSE_PAIRING;
        }
    }
    return game;
}

// What a connected device may use here now (its grants, what we offer and it uses).
static NetCaps net_peer_caps(const App* app, uint32_t peer)
{
    return net_access_peer_caps(&app->net_service.access, peer);
}

// GO HEADLESS (headless.h, net_stream.h): the state, to every authorized
// device that may stream (each learns whether it's the one holding it).
static void headless_tell(App* app)
{
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        if (app->net_service.access.peers[i].state != NetAccessPeerState::authorized ||
            (net_peer_caps(app, i) & NET_CAP_STREAM) == 0)
        {
            continue;
        }
        uint8_t out[4];
        size_t size = net_stream_write_headless_state(out, sizeof(out), app->headless.on,
                                                      app->headless.on && app->headless.peer == i);
        net_access_send(&app->net_service.access, i, NetChannel::control, out, size);
    }
}

// Back on piccolo's window. `pause`: the device that had the game is gone
// (or stopped streaming), so nobody plays it: paused until someone here does.
static void headless_back(App* app, bool pause)
{
    if (!app->headless.on)
    {
        return;
    }
    headless_leave(&app->headless, app->shell.window);
    if (pause && game_running(app))
    {
        app->ui.user_paused = true;   // it stays paused until the player here goes on
        set_status(app, piccolo_text(&app->strings, Str::status_headless_paused));
    }
    else
    {
        set_status(app, piccolo_text(&app->strings, Str::status_headless_back));
    }
    SDL_Log("Headless: back on the window%s", pause ? " (paused)" : "");
}

// The actions one authorized peer may trigger (`id` 0: sent unasked), or
// to all of them (NET_NO_PEER), each with its own level.
static void net_send_actions(App* app, uint32_t peer, uint32_t id)
{
    if (!app->net_remote_ready)
    {
        return;
    }
    NetRemoteGame game = net_remote_game(app);
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        if ((peer == NET_NO_PEER || peer == i) && app->net_service.access.peers[i].state == NetAccessPeerState::authorized)
        {
            app->net_actions_caps[i] = net_peer_caps(app, i);
            size_t size = net_remote_write_actions(&app->net_remote, id, app->net_peer_language[i],
                                                   app->paths.root.c_str(), app->net_actions_caps[i], &game);
            net_access_send(&app->net_service.access, i, NetChannel::control, app->net_remote.reply.data(), size);
        }
    }
}

// The ports the core has, as the quick menu's CONTROLS shows them: as many
// as SETTINGS > INPUT > PORTS says, no more than the core describes. 0 when
// no game runs.
static uint32_t net_core_ports(const App* app)
{
    const LibretroHost* host = app->session.host;
    if (!game_running(app) || host == nullptr)
    {
        return 0;
    }
    return piccolo_settings_ports(&app->settings, host->input_info.port_count);
}

// The port a peer's pad plays on, or -1.
static int32_t net_pad_port(const App* app, uint32_t peer, uint32_t pad)
{
    const InputNetPad* entry = &app->input_net.pads[peer][pad];
    if (!entry->active)
    {
        return -1;
    }
    int32_t controller = input_first_controller(&app->input, entry->device);
    return controller < 0 ? -1 : input_controller_port(&app->input, static_cast<uint32_t>(controller));
}

// What the core expects on each port, and where this peer's pads play.
static void net_controllers_for(const App* app, uint32_t peer, NetControllers* controllers)
{
    for (uint32_t pad = 0; pad < NET_CONTROLLERS_PADS; pad++)
    {
        controllers->pad_ports[pad] = static_cast<int8_t>(net_pad_port(app, peer, pad));
    }
    controllers->ports.clear();
    uint32_t ports = net_core_ports(app);
    const LibretroHost* host = app->session.host;
    for (uint32_t p = 0; p < ports; p++)
    {
        NetPort port;
        port.device = host->port_devices[p];
        const LibretroInputInfo* info = &host->input_info;
        if (p < info->port_count)
        {
            for (uint32_t t = 0; t < info->ports[p].count; t++)
            {
                NetPortDevice device;
                device.id = info->ports[p].types[t].id;
                device.name = info->ports[p].types[t].description;
                port.devices.push_back(device);
            }
        }
        for (uint32_t d = 0; d < info->descriptor_count; d++)
        {
            const LibretroInputDescriptor* in = &info->descriptors[d];
            if (in->port != p)
            {
                continue;
            }
            NetPortDescriptor descriptor;
            descriptor.device = static_cast<uint8_t>(in->device);
            descriptor.index = static_cast<uint8_t>(in->index);
            descriptor.id = static_cast<uint8_t>(in->id);
            descriptor.name = in->description;
            port.descriptors.push_back(descriptor);
        }
        controllers->ports.push_back(port);
    }
}

// A summary of what net_controllers_for would say, compared every loop
// (no allocation) to know when to send it again.
static uint64_t net_controllers_summary(const App* app, uint32_t peer)
{
    uint64_t summary = 1469598103934665603ull;   // FNV-1a over the numbers
    uint32_t values[16] = {};
    uint32_t count = 0;
    for (uint32_t pad = 0; pad < NET_CONTROLLERS_PADS; pad++)
    {
        values[count++] = static_cast<uint32_t>(net_pad_port(app, peer, pad) + 1);
    }
    uint32_t ports = net_core_ports(app);
    values[count++] = ports;
    if (ports > 0)
    {
        values[count++] = app->session.host->input_info.controller_serial;
        values[count++] = app->session.host->input_info.descriptor_serial;
        for (uint32_t p = 0; p < ports && count < 16; p++)
        {
            values[count++] = app->session.host->port_devices[p];
        }
    }
    for (uint32_t i = 0; i < count; i++)
    {
        summary = (summary ^ values[i]) * 1099511628211ull;
    }
    return summary;
}

static void net_send_controllers(App* app, uint32_t peer, uint32_t id)
{
    NetControllers controllers;
    net_controllers_for(app, peer, &controllers);
    size_t size = net_settings_write_controllers(app->net_remote.reply.data(), app->net_remote.reply.size(), id,
                                                 &controllers);
    net_access_send(&app->net_service.access, peer, NetChannel::control, app->net_remote.reply.data(), size);
    app->net_controllers_sent[peer] = net_controllers_summary(app, peer);
}

// port_device: the device on the port this peer's pad plays on, as the
// quick menu's CONTROLS changes it (the core is told; kept for the core).
static void net_change_port_device(App* app, uint32_t peer, uint8_t pad, uint32_t device_id, const char* device)
{
    int32_t port = net_pad_port(app, peer, pad);
    const LibretroHost* host = app->session.host;
    if (port < 0 || static_cast<uint32_t>(port) >= net_core_ports(app) ||
        static_cast<uint32_t>(port) >= host->input_info.port_count)
    {
        return;   // not on a port, or the core offers nothing to choose there
    }
    const LibretroPortControllers* types = &host->input_info.ports[port];
    bool offered = false;
    for (uint32_t t = 0; t < types->count && !offered; t++)
    {
        offered = types->types[t].id == device_id;
    }
    if (!offered)
    {
        return;
    }
    libretro_host_set_port_device(app->session.host, static_cast<unsigned>(port), device_id);
    settings_set_int(&app->settings, SettingLayer::core, PICCOLO_INPUT_PORT_DEVICES[port],
                     static_cast<int32_t>(device_id));
    if (!app->settings_window.open)
    {
        save_settings(app);
    }
    SDL_Log("Network: %s set port %d to device %u", device, port + 1, device_id);
}

// Who plays on piccolo's ports now (devices): only controllers that joined
// a port and whose device is connected. `peer` is the device asking (its own
// pads are marked).
static void net_devices_for(const App* app, uint32_t peer, std::vector<NetDevice>* devices)
{
    devices->clear();
    for (size_t c = 0; c < app->input.controllers.size(); c++)
    {
        uint32_t controller = static_cast<uint32_t>(c);
        uint8_t ports = 0;
        for (uint32_t p = 0; p < INPUT_PORT_COUNT; p++)
        {
            if (input_port_has(&app->input, p, controller))
            {
                ports = static_cast<uint8_t>(ports | (1u << p));
            }
        }
        const InputDevice* device = input_devices_find_const(&app->input.devices, app->input.controllers[c].device_id);
        if (ports == 0 || device == nullptr || !device->connected)
        {
            continue;
        }
        NetDevice entry;
        entry.controller = controller;
        entry.device = device->id;
        entry.ports = ports;
        entry.name = input_controller_label(&app->input, controller);
        entry.kind = device->kind == InputDeviceKind::keyboard ? NetDeviceKind::keyboard
                     : device->kind == InputDeviceKind::mouse  ? NetDeviceKind::mouse
                                                                : NetDeviceKind::gamepad;
        // A paired device's pad: whose, and which of its pads.
        for (uint32_t owner = 0; owner < NET_MAX_PEERS; owner++)
        {
            for (uint32_t pad = 0; pad < NET_INPUT_MAX_PADS; pad++)
            {
                const InputNetPad* net_pad = &app->input_net.pads[owner][pad];
                if (net_pad->active && net_pad->device == device->id)
                {
                    entry.kind = NetDeviceKind::network;
                    entry.owner = owner == peer ? NetDeviceOwner::yours : NetDeviceOwner::other;
                    entry.pad = static_cast<uint8_t>(pad);
                }
            }
        }
        devices->push_back(entry);
    }
}

// A summary of net_devices_for, compared every loop (no allocation).
static uint64_t net_devices_summary(const App* app, uint32_t peer)
{
    uint64_t summary = 1469598103934665603ull;
    for (size_t c = 0; c < app->input.controllers.size(); c++)
    {
        uint32_t controller = static_cast<uint32_t>(c);
        uint32_t ports = 0;
        for (uint32_t p = 0; p < INPUT_PORT_COUNT; p++)
        {
            ports |= input_port_has(&app->input, p, controller) ? (1u << p) : 0u;
        }
        const InputDevice* device = input_devices_find_const(&app->input.devices, app->input.controllers[c].device_id);
        if (ports == 0 || device == nullptr || !device->connected)
        {
            continue;
        }
        uint32_t values[3] = {controller, device->id, ports};
        for (uint32_t i = 0; i < 3; i++)
        {
            summary = (summary ^ values[i]) * 1099511628211ull;
        }
    }
    return (summary ^ peer) * 1099511628211ull;
}

static void net_send_devices(App* app, uint32_t peer, uint32_t id)
{
    std::vector<NetDevice> devices;
    net_devices_for(app, peer, &devices);
    size_t size = net_settings_write_devices(app->net_remote.reply.data(), app->net_remote.reply.size(), id, &devices);
    net_access_send(&app->net_service.access, peer, NetChannel::control, app->net_remote.reply.data(), size);
    app->net_devices_sent[peer] = net_devices_summary(app, peer);
}

// device_port: that controller on that port only (-1: on none), as the quick
// menu's CONTROLS does it. Nothing is saved (START joins again next time).
// A stale controller / device pair (the list changed meanwhile) is ignored:
// the summary check sends the device a fresh `devices`.
static void net_move_device(App* app, uint32_t controller, uint32_t device_id, int8_t port, const char* device)
{
    if (controller >= app->input.controllers.size() || app->input.controllers[controller].device_id != device_id ||
        port >= static_cast<int8_t>(INPUT_PORT_COUNT) || port < -1)
    {
        return;
    }
    for (uint32_t p = 0; p < INPUT_PORT_COUNT; p++)
    {
        input_port_remove(&app->input, p, controller);
    }
    if (port >= 0)
    {
        input_port_add(&app->input, static_cast<uint32_t>(port), controller);
    }
    SDL_Log("Network: %s moved %s to port %d", device, input_controller_label(&app->input, controller).c_str(), port + 1);
}

// Every loop: settings that changed (from anywhere) go to the devices with SETTINGS,
// and the action lists again when what they show changed.
static void net_follow_remote(App* app)
{
    if (!app->net_remote_ready)
    {
        return;
    }
    size_t size = net_remote_next_change(&app->net_remote, &app->settings);
    while (size > 0)
    {
        for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
        {
            if (app->net_service.access.peers[i].state == NetAccessPeerState::authorized &&
                (net_peer_caps(app, i) & NET_CAP_SETTINGS) != 0)
            {
                net_access_send(&app->net_service.access, i, NetChannel::control, app->net_remote.reply.data(), size);
            }
        }
        size = net_remote_next_change(&app->net_remote, &app->settings);
    }
    // A device whose permissions changed (DEVICES): its new welcome went out
    // with net_access_set_grants; its action list follows at once, and what it
    // may no longer use stops: remote play and a FAST FORWARD it holds
    // (STREAM since 2026-11-20; a PAUSE it made stays: only a player lifts
    // it). Speed tests and pads are GAMEPAD.
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        if (app->net_service.access.peers[i].state != NetAccessPeerState::authorized ||
            net_peer_caps(app, i) == app->net_actions_caps[i])
        {
            continue;
        }
        net_send_actions(app, i, 0);
        if ((net_peer_caps(app, i) & NET_CAP_STREAM) == 0)
        {
            app->net_fast_forward[i] = false;
            if (app->headless.on && app->headless.peer == i)
            {
                headless_back(app, true);   // remote play (and so the game) needs STREAM
                headless_tell(app);
            }
            net_service_set_stream(&app->net_service, i, 0);   // remote play needs STREAM
            app->stream_tuning_set[i] = false;
        }
        if ((net_peer_caps(app, i) & NET_CAP_GAMEPAD) == 0)
        {
            // Pads are GAMEPAD: a device that lost it (net_send_actions above
            // already took the new caps as seen) has its pads unplugged.
            input_net_peer_gone(&app->input_net, &app->input, i);
        }
    }
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        if (app->net_devices_wanted[i] && app->net_service.access.peers[i].state == NetAccessPeerState::authorized &&
            (net_peer_caps(app, i) & NET_CAP_SETTINGS) != 0 && net_devices_summary(app, i) != app->net_devices_sent[i])
        {
            net_send_devices(app, i, 0);   // someone joined, left or moved
        }
    }
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        if (app->net_service.access.peers[i].state == NetAccessPeerState::authorized &&
            net_controllers_summary(app, i) != app->net_controllers_sent[i])
        {
            net_send_controllers(app, i, 0);   // a pad joined or left a port, another game, another device
        }
    }
    NetRemoteGame game = net_remote_game(app);
    if (game.running != app->net_actions_sent.running || game.slot != app->net_actions_sent.slot ||
        game.pause != app->net_actions_sent.pause)
    {
        app->net_actions_sent = game;
        net_send_actions(app, NET_NO_PEER, 0);
    }
}

// An action from a device, with the same effect as its hotkey or quick
// menu item.
static void net_do_action(App* app, uint32_t peer, const std::string* key, bool pressed, const char* device)
{
    NetRemoteGame game = net_remote_game(app);
    if (!net_remote_action_allowed(key->c_str(), net_peer_caps(app, peer), &game))
    {
        return;
    }
    if (*key == "fast_forward")
    {
        app->net_fast_forward[peer] = pressed;   // held until released (or the device leaves)
        return;
    }
    if (!pressed)
    {
        return;   // the others act on the press
    }
    if (*key == "save_state")
    {
        save_state_from(app, true);
    }
    else if (*key == "load_state")
    {
        load_state_from(app, true);
    }
    else if (*key == "undo_save_state")
    {
        undo_save_from(app, true);
    }
    else if (*key == "undo_load_state")
    {
        undo_load_from(app, true);
    }
    else if (*key == "slot_next" || *key == "slot_previous")
    {
        uint32_t slot = app->session.state_slot;
        if (*key == "slot_next")
        {
            slot = slot >= QUICK_MENU_SLOT_LAST ? QUICK_MENU_SLOT_FIRST : slot + 1;
        }
        else
        {
            slot = slot <= QUICK_MENU_SLOT_FIRST ? QUICK_MENU_SLOT_LAST : slot - 1;
        }
        app->session.state_slot = slot;
        app->quick_menu.slot = slot;
        char status[64];
        std::snprintf(status, sizeof(status), "%s %u", piccolo_text(&app->strings, Str::quick_menu_slot), slot);
        set_status(app, status);
    }
    else if (*key == "pause")
    {
        // The USER pause, the same as here: only a player lifts it (this
        // device, another one, or here), even after this device leaves.
        app->ui.user_paused = game_running(app) && !app->ui.user_paused;
        if (app->ui.user_paused)
        {
            char status[256];
            std::snprintf(status, sizeof(status), "%s %s", piccolo_text(&app->strings, Str::status_paused_remotely),
                          device);
            set_status(app, status);
        }
        else
        {
            app->resumed_ms = SDL_GetTicks();   // PLAY, as here
        }
    }
    else if (*key == "reset")
    {
        if (soft_reset(app))
        {
            app_leave_main_menu(app);
            set_status(app, piccolo_text(&app->strings, Str::status_reset));   // nothing here showed it
        }
    }
    else if (*key == "close_game")
    {
        app->pending_close_game = true;   // at the top of the next loop, like the quick menu's
    }
}

// Settings and actions messages from a device (net_remote.h).
static void net_remote_message(App* app, const NetAccessEvent* event, const char* device)
{
    if (!app->net_remote_ready)
    {
        return;
    }
    size_t size = net_remote_answer_settings(&app->net_remote, &app->settings, app->paths.root.c_str(),
                                             &app->audio_devices, event->data, event->size);
    if (size > 0)
    {
        net_access_send(&app->net_service.access, event->peer, NetChannel::control, app->net_remote.reply.data(), size);
        uint32_t request_id = 0;
        uint32_t start = 0;
        std::string language;
        if (event->peer < NET_MAX_PEERS &&
            net_settings_read_request(event->data, event->size, &request_id, &start, &language))
        {
            std::snprintf(app->net_peer_language[event->peer], sizeof(app->net_peer_language[event->peer]), "%s",
                          language.c_str());
        }
        bool changed = event->data[0] == static_cast<uint8_t>(NetSettingsMessage::setting_set);
        if (changed && !app->settings_window.open)
        {
            save_settings(app);   // nobody will close SETTINGS to save it
        }
        return;
    }
    uint32_t id = 0;
    std::string language;
    if (net_settings_read_actions_request(event->data, event->size, &id, &language))
    {
        if (event->peer < NET_MAX_PEERS)
        {
            std::snprintf(app->net_peer_language[event->peer], sizeof(app->net_peer_language[event->peer]), "%s",
                          language.c_str());
        }
        NetRemoteGame game = net_remote_game(app);
        size = net_remote_write_actions(&app->net_remote, id, language.c_str(), app->paths.root.c_str(),
                                        net_peer_caps(app, event->peer), &game);
        net_access_send(&app->net_service.access, event->peer, NetChannel::control, app->net_remote.reply.data(), size);
        return;
    }
    std::string key;
    bool pressed = false;
    if (net_settings_read_action(event->data, event->size, &key, &pressed))
    {
        net_do_action(app, event->peer, &key, pressed, device);
        return;
    }
    if (event->peer >= NET_MAX_PEERS)
    {
        return;
    }
    if (net_settings_read_controllers_request(event->data, event->size, &id))
    {
        net_send_controllers(app, event->peer, id);
        return;
    }
    uint8_t pad = 0;
    uint32_t device_id = 0;
    if (net_settings_read_port_device(event->data, event->size, &pad, &device_id))
    {
        net_change_port_device(app, event->peer, pad, device_id, device);
        return;
    }
    // devices / device_port: SETTINGS (net_access let them through only then);
    // devices_watch, the same list read only, at any capability (the ports' tabs).
    if (net_settings_read_devices_request(event->data, event->size, &id) ||
        net_settings_read_devices_watch(event->data, event->size, &id))
    {
        app->net_devices_wanted[event->peer] = true;
        net_send_devices(app, event->peer, id);
        return;
    }
    uint32_t controller = 0;
    int8_t port = 0;
    if (net_settings_read_device_port(event->data, event->size, &controller, &device_id, &port))
    {
        net_move_device(app, controller, device_id, port, device);
    }
}

// A speed test request (net_speed.h; net_access let it through only for
// STREAM devices). One at a time: a request during another is ignored.
static void net_speed_request(App* app, const NetAccessEvent* event)
{
    uint32_t id = 0;
    NetSpeedDirection direction = NetSpeedDirection::down;
    uint32_t rate = 0;
    uint32_t duration = 0;
    if (!net_speed_read_request(event->data, event->size, &id, &direction, &rate, &duration) ||
        app->net_speed_sender.active || app->net_speed_reporting || app->net_speed_receiver.active)
    {
        return;
    }
    double now = net_seconds();
    if (direction == NetSpeedDirection::down)
    {
        net_speed_sender_start(&app->net_speed_sender, id, event->peer, rate, duration, now);
    }
    else
    {
        net_speed_receiver_start(&app->net_speed_receiver, id, event->peer,
                                 now + static_cast<double>(duration) / 1000.0 + NET_SPEED_GRACE_SECONDS);
    }
}

// speed_result: one step of a device's speed test, as it measured it.
// `device_now`: the device's index in the list as it is now (net_trust_find), -1 when it is gone.
static void net_speed_result(App* app, const NetAccessEvent* event, int32_t device_now)
{
    NetSpeedResult result;
    if (device_now < 0 || !net_speed_read_result(event->data, event->size, &result))
    {
        return;
    }
    const uint8_t* key = app->net_service.access.trust.devices[static_cast<size_t>(device_now)].key;
    NetSpeedSummary* summary = nullptr;
    for (size_t i = 0; i < app->net_speed_results.size() && summary == nullptr; i++)
    {
        if (std::memcmp(app->net_speed_results[i].key, key, NET_KEY_SIZE) == 0)
        {
            summary = &app->net_speed_results[i];
        }
    }
    if (summary == nullptr)
    {
        NetSpeedSummary entry;
        std::memcpy(entry.key, key, NET_KEY_SIZE);
        app->net_speed_results.push_back(entry);
        summary = &app->net_speed_results.back();
    }
    bool down = result.direction == NetSpeedDirection::down;
    uint32_t* best = down ? &summary->down_kbps : &summary->up_kbps;
    uint32_t* last_asked = down ? &summary->down_last_asked : &summary->up_last_asked;
    bool* tested = down ? &summary->down_tested : &summary->up_tested;
    if (result.asked_kbps <= *last_asked)
    {
        *best = 0;   // a new test (its first, lowest step)
    }
    *last_asked = result.asked_kbps;
    *tested = true;
    if (result.loss_per_mille <= NET_WINDOW_SPEED_MAX_LOSS && result.got_kbps > *best)
    {
        *best = result.got_kbps;
    }
}

// Every loop: the down test's packets that are due, and the reports.
static void net_speed_update(App* app)
{
    double now = net_seconds();
    NetSpeedSender* sender = &app->net_speed_sender;
    if (sender->active)
    {
        uint8_t packet[NET_SPEED_PACKET_SIZE];
        uint32_t due = net_speed_sender_due(sender, now);
        for (uint32_t i = 0; i < due; i++)
        {
            size_t size = net_speed_write_data(packet, sizeof(packet), sender->id, sender->sent);
            net_access_send(&app->net_service.access, sender->peer, NetChannel::input, packet, size);
            net_speed_sender_sent(sender, size);   // counted as sent even if ENet dropped it here: that's loss too
        }
        app->net_speed_reporting = !sender->active;
    }
    uint8_t out[64];
    if (app->net_speed_reporting)
    {
        NetSpeedReport report;
        report.id = sender->id;
        report.direction = NetSpeedDirection::down;
        report.packets = sender->sent;
        report.bytes = sender->sent_bytes;
        report.seconds = sender->duration;
        size_t size = net_speed_write_report(out, sizeof(out), &report);
        net_access_send(&app->net_service.access, sender->peer, NetChannel::control, out, size);
        app->net_speed_reporting = false;
    }
    NetSpeedReceiver* receiver = &app->net_speed_receiver;
    if (receiver->active && now >= receiver->until)
    {
        NetSpeedReport report;
        net_speed_receiver_report(receiver, NetSpeedDirection::up, &report);
        size_t size = net_speed_write_report(out, sizeof(out), &report);
        net_access_send(&app->net_service.access, receiver->peer, NetChannel::control, out, size);
        receiver->active = false;
    }
}

// Which streaming device's stream_tuning applies (the newest request of the
// ones still streaming), and what it gives: told to the devices that asked,
// again whenever it changes (AUTO coming down).
static void net_follow_stream_tuning(App* app)
{
    int32_t from = -1;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        if (app->stream_tuning_set[peer] && app->net_service.stream.flags[peer] != 0 &&
            (from < 0 || app->stream_tuning_order[peer] > app->stream_tuning_order[from]))
        {
            from = static_cast<int32_t>(peer);
        }
    }
    bool changed = from != app->stream_tuning_from ||
                   (from >= 0 && (app->stream_tuning_asked[from].delay != app->stream_tuning.delay ||
                                  app->stream_tuning_asked[from].delay_ms != app->stream_tuning.delay_ms));
    if (changed)
    {
        app->stream_auto_delay = AutoFrameDelay{};   // a new request searches from the top
        app->stream_delay_last_ns = 0;
    }
    app->stream_tuning_from = from;
    app->stream_tuning_on = from >= 0;
    if (from >= 0)
    {
        app->stream_tuning = app->stream_tuning_asked[from];
    }
    // What applies, as each asking device should hear it.
    NetStreamTuning result;
    result.delay = app->stream_tuning_on ? app->stream_tuning.delay : NetStreamDelay::host;
    result.delay_ms = static_cast<uint8_t>(app->frame_delay_ms < 255 ? app->frame_delay_ms : 255);
    // What the running core really does (only cores with their own, for now).
    result.run_ahead = app->session.host != nullptr && game_running(app)
                           ? static_cast<uint8_t>(libretro_host_run_ahead_applied(app->session.host))
                           : 0;
    result.flags = app_shell_wait_for_present(&app->shell) ? NET_STREAM_TUNING_WAITS_FOR_PRESENT : 0;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        if (!app->stream_tuning_set[peer])
        {
            continue;
        }
        const NetStreamTuning* sent = &app->stream_tuning_sent[peer];
        if (app->stream_tuning_told[peer] && sent->delay == result.delay && sent->delay_ms == result.delay_ms &&
            sent->run_ahead == result.run_ahead && sent->flags == result.flags)
        {
            continue;
        }
        uint8_t out[8];
        size_t size = net_stream_write_tuning(out, sizeof(out), NET_STREAM_TUNING_RESULT, &result);
        net_access_send(&app->net_service.access, peer, NetChannel::control, out, size);
        app->stream_tuning_sent[peer] = result;
        app->stream_tuning_told[peer] = true;
    }
}

// stream_start / stream_stop / keyframe_request (net_stream.h; net_access let
// them through only for STREAM devices).
static void net_stream_message(App* app, const NetAccessEvent* event, const char* device)
{
    if (event->size == 0)
    {
        return;
    }
    uint8_t type = event->data[0];
    uint8_t flags = 0;
    bool headless_on = false;
    if (net_stream_read_start(event->data, event->size, &flags))
    {
        net_service_set_stream(&app->net_service, event->peer, flags);
        app->stream_frame_owed = true;
        SDL_Log("Network: %s started remote play%s%s", device, (flags & NET_STREAM_WANT_VIDEO) != 0 ? " (video)" : "",
                (flags & NET_STREAM_WANT_AUDIO) != 0 ? " (audio)" : "");
    }
    else if (type == NET_STREAM_STOP && event->size == 1)
    {
        if (app->headless.on && app->headless.peer == event->peer)
        {
            headless_back(app, true);   // it no longer sees the game
            headless_tell(app);
        }
        net_service_set_stream(&app->net_service, event->peer, 0);
        if (event->peer < NET_MAX_PEERS)
        {
            app->stream_tuning_set[event->peer] = false;
        }
        SDL_Log("Network: %s stopped remote play", device);
    }
    else if (type == NET_STREAM_KEYFRAME_REQUEST && event->size == 1)
    {
        net_service_request_keyframe(&app->net_service, event->peer);
        app->stream_frame_owed = true;
    }
    else if (net_stream_read_go_headless(event->data, event->size, &headless_on))
    {
        // Taken only by a device streaming a running game, and only if no
        // other device holds it; given back only by the one holding it.
        bool streaming = event->peer < NET_MAX_PEERS && app->net_service.stream.flags[event->peer] != 0;
        if (headless_on && !app->headless.on && game_running(app) && streaming)
        {
            headless_enter(&app->headless, app->shell.window, event->peer, &app_icon_images[2],
                           piccolo_text(&app->strings, Str::headless_tooltip),
                           piccolo_text(&app->strings, Str::headless_show),
                           piccolo_text(&app->strings, Str::headless_play_here));
            SDL_Log("Headless: %s took the game", device);
        }
        else if (!headless_on && app->headless.on && app->headless.peer == event->peer)
        {
            headless_back(app, false);
        }
        headless_tell(app);   // the asker learns the result either way
    }
    else if (event->peer < NET_MAX_PEERS &&
             net_stream_read_tuning(event->data, event->size, NET_STREAM_TUNING, &app->stream_tuning_asked[event->peer]))
    {
        app->stream_tuning_set[event->peer] = true;
        app->stream_tuning_order[event->peer] = ++app->stream_tuning_count;
        app->stream_tuning_told[event->peer] = false;   // answered with what applies, below
        SDL_Log("Network: %s asks for frame delay mode %u (%u ms), run-ahead %u", device,
                static_cast<unsigned>(app->stream_tuning_asked[event->peer].delay),
                static_cast<unsigned>(app->stream_tuning_asked[event->peer].delay_ms),
                static_cast<unsigned>(app->stream_tuning_asked[event->peer].run_ahead));
    }
}

// After the frames of this loop ran: the frame shown and the audio heard go
// to the network thread (it codes and sends; this loop never waits for it).
// With run-ahead (later) this stays the shown frame and the real timeline's
// audio, numbered by this loop.
// Also called when no frame ran but one is owed (a stream started or asked
// for a keyframe while paused): the network thread codes keyframes only
// from a frame handed over after the request, so the last shown frame goes
// again, or a paused game would show that device nothing until it resumes.
static void net_stream_frame(App* app)
{
    bool owed = app->stream_frame_owed;
    app->stream_frame_owed = false;
    if (!app->net_streaming || !game_running(app))
    {
        app->session.captured_audio.clear();
        return;
    }
    if (owed)
    {
        app->stream_serial = 0;   // the same core frame again (serials start at 1)
    }
    app->stream_loop++;
    const LibretroHost* host = app->session.host;
    if (!app->session.captured_audio.empty())
    {
        net_service_submit_audio(&app->net_service, app->session.captured_audio.data(),
                                 app->session.captured_audio.size() / 2, app->stream_loop);
        app->session.captured_audio.clear();
    }
    const LibretroVideoFrame* video = &host->video;
    FramePixelFormat format = FramePixelFormat::xrgb8888;
    if (video->format == RETRO_PIXEL_FORMAT_RGB565)
    {
        format = FramePixelFormat::rgb565;
    }
    else if (video->format == RETRO_PIXEL_FORMAT_0RGB1555)
    {
        format = FramePixelFormat::xrgb1555;
    }
    else if (video->format != RETRO_PIXEL_FORMAT_XRGB8888)
    {
        return;   // the HDR formats aren't streamed
    }
    if (video->pixels == nullptr || video->width == 0 || video->height == 0 || video->serial == app->stream_serial)
    {
        return;   // nothing new to show
    }
    app->stream_serial = video->serial;
    NetStreamInfo info;
    info.width = video->width;
    info.height = video->height;
    info.format = format;
    info.aspect = static_cast<double>(host->av_info.geometry.aspect_ratio);
    info.fps = host->av_info.timing.fps;
    info.sample_rate = host->av_info.timing.sample_rate;
    net_service_submit_frame(&app->net_service, video->pixels, video->width, video->height, format, app->stream_loop,
                             &info);
}

// The load_result for the load_request waiting in pending_start.
static void net_answer_load(App* app, NetContentStatus status)
{
    app->net_load_waiting = false;
    if (!app->net_running)
    {
        return;
    }
    uint8_t reply[16];
    size_t size = net_content_write_load_result(reply, sizeof(reply), app->net_load_id, status);
    // Called from the main loop, outside net_update: the lock is ours to take.
    net_service_lock(&app->net_service);
    net_access_send(&app->net_service.access, app->net_load_peer, NetChannel::control, reply, size);
    net_service_unlock(&app->net_service);
}

// A load_request whose start was refused (SAFE START closed without starting):
// answered failed, unless another start is on its way. Outside the lock.
static void net_answer_load_refused(App* app)
{
    if (app->net_load_waiting && !app->has_pending_start)
    {
        net_answer_load(app, NetContentStatus::failed);   // not started after all
    }
}

// A control message from a paired device (net_games.h). Folders and cores
// are read when asked, never every frame.
static void net_games_message(App* app, const NetAccessEvent* event, const char* device)
{
    uint8_t type = event->size > 0 ? event->data[0] : 0;
    if (type < static_cast<uint8_t>(NetContentMessage::cores_request) ||
        type > static_cast<uint8_t>(NetContentMessage::load_result))
    {
        return;   // not ours (yet)
    }
    std::vector<std::string> game_folders;
    piccolo_settings_game_folders(&app->settings, &game_folders);
    net_games_set_folders(&app->net_games, &game_folders);

    size_t size = net_games_answer(&app->net_games, &app->catalog, event->data, event->size);
    if (size > 0)
    {
        net_access_send(&app->net_service.access, event->peer, NetChannel::control, app->net_games.reply.data(), size);
        return;
    }
    uint32_t id = 0;
    NetContentStatus status = NetContentStatus::failed;
    FilePickerChoice choice;
    if (!net_games_load(&app->net_games, &app->catalog, event->data, event->size, &id, &status, &choice))
    {
        return;   // a result type, or a damaged request: nothing to answer
    }
    if (status != NetContentStatus::ok || app->has_pending_start || app->net_load_waiting)
    {
        if (status == NetContentStatus::ok)
        {
            status = NetContentStatus::failed;   // another start is already on its way
        }
        uint8_t reply[16];
        size_t reply_size = net_content_write_load_result(reply, sizeof(reply), id, status);
        net_access_send(&app->net_service.access, event->peer, NetChannel::control, reply, reply_size);
        return;
    }
    SDL_Log("Network: %s starts %s %s", device, choice.core_path, choice.content_path);
    app->pending_start = choice;   // started at the top of the next loop, like LOAD GAME's
    app->has_pending_start = true;
    app->picker.open = false;      // what was being picked here is overtaken
    app->net_load_waiting = true;
    app->net_load_peer = event->peer;
    app->net_load_id = id;
}

// The listener, when SETTINGS has it on. Started at launch and restarted
// when ALLOW CONNECTIONS or PORT change (net_update).
// The network library and this device's key (made once, kept in the
// settings folder): for the listener and for SHARE's connection alike.
static bool net_prepare(App* app)
{
    if (app->paths.settings.empty())
    {
        return false;   // nowhere to keep the device key: networking stays off
    }
    if (!app->net_ready)
    {
        app->net_ready = net_init();
        if (!app->net_ready)
        {
            return false;
        }
    }
    if (!app->net_has_identity)
    {
        std::string identity_path = app->paths.settings + "/net_identity.cfg";
        app->net_has_identity = net_identity_load_or_create(identity_path.c_str(), &app->net_identity);
        if (!app->net_has_identity)
        {
            return false;
        }
        net_key_fingerprint(app->net_identity.public_key, app->net_fingerprint, sizeof(app->net_fingerprint));
        SDL_Log("Network: this device is %s", app->net_fingerprint);
        // Before 2026-11-25 the devices we connect to had a list of their
        // own: it joins the paired devices once (one record per device).
        std::string devices_path = app->paths.settings + "/net_devices.cfg";
        std::string hosts_path = app->paths.settings + "/net_hosts.cfg";
        NetTrust trust;
        net_trust_file_lock();
        net_trust_load(&trust, devices_path.c_str());
        net_trust_merge_old_hosts(&trust, devices_path.c_str(), hosts_path.c_str());
        net_trust_file_unlock();
    }
    return true;
}

static void net_start(App* app)
{
    // Any failure below is remembered with the port it was tried on, so it
    // isn't retried every frame (net_update).
    app->net_port = static_cast<uint16_t>(settings_get_int(&app->settings, PICCOLO_NET_PORT));
    app->net_listen_failed = true;
    if (!net_prepare(app))
    {
        return;
    }
    app->net_service.access = NetAccess{};
    net_games_init(&app->net_games);
    if (!app->net_remote_ready)
    {
        app->net_remote_ready = net_remote_init(&app->net_remote, &app->settings);
    }
    std::string devices_path = app->paths.settings + "/net_devices.cfg";
    if (!net_service_start(&app->net_service, app->net_port, net_device_info(app), &app->net_identity,
                           devices_path.c_str()))
    {
        return;
    }
    app->net_listen_failed = false;
    app->net_running = true;
    app->net_beacon_applied.clear();   // a new service: net_update hands it the beacon again
    app->net_share_set = false;        // and its shared folders
}

// What a peer slot keeps in App: gone with the device (`closed`, net_stop), and
// cleared again when a new one takes the slot (a lost `closed` leaves it behind).
// Only plain state: what needs the lock or the network stays with the caller.
static void net_peer_forget(App* app, uint32_t peer)
{
    if (peer >= NET_MAX_PEERS)
    {
        return;
    }
    if (app->net_speed_sender.peer == peer)
    {
        app->net_speed_sender.active = false;   // its test ends with it
        app->net_speed_reporting = false;
    }
    if (app->net_speed_receiver.peer == peer)
    {
        app->net_speed_receiver.active = false;
    }
    app->stream_tuning_set[peer] = false;   // what it asked for
    app->net_fast_forward[peer] = false;    // a dropped device doesn't keep it held
    app->net_peer_language[peer][0] = '\0';
    app->net_devices_wanted[peer] = false;
    app->net_guest_name[peer][0] = '\0';
    if (app->net_load_waiting && app->net_load_peer == peer)
    {
        app->net_load_waiting = false;   // the game still starts; nobody to tell
    }
    input_net_peer_gone(&app->input_net, &app->input, peer);
}

static void net_stop(App* app)
{
    if (app->net_running)
    {
        net_service_lock(&app->net_service);
        guest_end(app, &app->net_service.access);   // guests told before the network goes
        net_service_unlock(&app->net_service);
        for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
        {
            net_peer_forget(app, peer);   // their pads are unplugged, their state dropped
        }
        // net_update stops looking at these once the network is off.
        app->net_streaming = false;
        app->session.capture_audio = false;
        app->stream_tuning_on = false;
        net_service_stop(&app->net_service);
        app->net_running = false;
        // A pairing request (it pauses the game) or our code's wait can't be
        // answered any more, and a restarted listener numbers its peers anew.
        pair_window_network_stopped(&app->pair_window);
    }
}

static void net_shutdown_all(App* app)
{
    net_share_link_stop(&app->share_link);
    net_stop(app);
    guest_card_finish(&app->guest_window.card);
    net_keypair_wipe(&app->net_identity);
    if (app->net_ready)
    {
        net_shutdown();
        app->net_ready = false;
    }
}

// Once per loop, before the input is read: follow the settings, then the
// network's events. Paired devices are let in and their pads join the input
// router (input_net); everyone else needs the pairing code.
static void net_update_locked(App* app);

static void net_update(App* app)
{
    bool enabled = settings_get_bool(&app->settings, PICCOLO_NET_ENABLED);
    uint16_t port = static_cast<uint16_t>(settings_get_int(&app->settings, PICCOLO_NET_PORT));
    if (app->net_running && (!enabled || port != app->net_port))
    {
        net_stop(app);
        app->net_listen_failed = false;
    }
    // After a failure (port in use) it's tried again only with another port.
    if (enabled && !app->net_running && (!app->net_listen_failed || port != app->net_port))
    {
        net_start(app);
    }
    if (!enabled)
    {
        app->net_listen_failed = false;
    }
    if (!app->net_running)
    {
        headless_back(app, false);   // networking went off: nobody streams
        return;
    }
    // INTERNET and BEACON SERVER: the service resolves the name on its own
    // thread. Only when they change (an empty name is off too).
    // Compared as text: a std::string is built only when the name changed (not every frame).
    const char* beacon = PICCOLO_BROWSER || settings_get_bool(&app->settings, PICCOLO_NET_INTERNET)
                             ? settings_get_text(&app->settings, PICCOLO_NET_BEACON)
                             : "";
    if (std::strcmp(beacon, app->net_beacon_applied.c_str()) != 0)
    {
        app->net_beacon_applied = beacon;
        net_service_set_beacon(&app->net_service, beacon);
    }
    // The shared folders the devices with FILE TRANSFER reach (piccolo-share): looked at
    // again only when a setting changed, handed over only when they did.
    bool share_check = !app->net_share_set || app->settings.revision != app->net_share_revision;
    NetShareFolderPaths share_paths;
    std::string share_key;
    if (share_check)
    {
        app->net_share_revision = app->settings.revision;
        share_folder_paths(app, &share_paths);
        share_key = share_paths.saves + "\n" + share_paths.states + "\n" + share_paths.screenshots + "\n" +
                    share_paths.config;
        for (size_t i = 0; i < share_paths.games.size(); i++)
        {
            share_key += "\n" + share_paths.games[i];
        }
    }
    // The network thread polls; this loop handles what it queued, holding
    // the lock for all of it (the NetAccess, its peers and lists).
    net_service_lock(&app->net_service);
    if (share_check && (!app->net_share_set || share_key != app->net_share_applied))
    {
        app->net_share_applied = share_key;
        app->net_share_set = true;
        net_service_set_share_folders(&app->net_service, &share_paths);
    }
    net_update_locked(app);
    net_service_unlock(&app->net_service);
}

static void net_update_locked(App* app)
{
    guest_update_locked(app);
    // GO HEADLESS ends from the tray, or when no game runs any more.
    // (Without a tray the taskbar restoring the window asks as well.)
    if (app->headless.on &&
        (app->headless.back_asked || !game_running(app) || headless_window_restored(&app->headless, app->shell.window)))
    {
        headless_back(app, false);
        headless_tell(app);
    }
    // A new NAME is used at once (discovery and pairing show it).
    const char* name = settings_get_text(&app->settings, PICCOLO_NET_NAME);
    if (app->net_service.access.name != name)
    {
        app->net_service.access.name = name;
        app->net_service.access.session.host.name = name;
    }

    NetAccessEvent event;
    while (net_service_next(&app->net_service, &event))
    {
        char address[64];
        net_address_to_text(event.address, address, sizeof(address));
        // The device as the list is now, by its key: event.device was an index when the event was
        // made on the network thread, and the list may have shrunk since (an unpair, FORGET).
        int32_t device_now = event.device >= 0 ? net_trust_find(&app->net_service.access.trust, event.key) : -1;
        const char* device = device_now >= 0
                                 ? app->net_service.access.trust.devices[static_cast<size_t>(device_now)].name.c_str()
                                 : "?";
        const NetAccessPeer* guest = event.guest ? net_access_guest(&app->net_service.access, event.peer) : nullptr;
        if (guest != nullptr)
        {
            device = guest->guest_name;
        }
        else if (event.guest && event.peer < NET_MAX_PEERS && app->net_guest_name[event.peer][0] != '\0')
        {
            device = app->net_guest_name[event.peer];   // already reset: the name kept at guest_joined
        }
        switch (event.type)
        {
        case NetAccessEventType::authorized:
            // First: a lost `closed` must not leave the previous device's state to this one.
            net_peer_forget(app, event.peer);
            net_service_set_stream(&app->net_service, event.peer, 0);
            SDL_Log("Network: \"%s\" connected from %s", device, address);
            net_send_game_state(app, event.peer);   // what runs, from the start
            net_send_actions(app, event.peer, 0);
            net_send_controllers(app, event.peer, 0);
            break;
        case NetAccessEventType::paired:
        {
            net_peer_forget(app, event.peer);   // as at `authorized`: this slot's new device
            net_service_set_stream(&app->net_service, event.peer, 0);
            // PAIRED WITH: the window says it when it's open; a card only
            // when it's closed (the player went back to the game meanwhile).
            if (!app->pair_window.open)
            {
                char status[256];
                std::snprintf(status, sizeof(status), "%s %s", piccolo_text(&app->strings, Str::status_net_paired),
                              device);
                set_status(app, status);
            }
            net_send_game_state(app, event.peer);
            net_send_actions(app, event.peer, 0);
            net_send_controllers(app, event.peer, 0);
            // GENERATE PAIRING CODE, if it waited for it: PAIRED WITH.
            pair_window_paired(&app->pair_window, &event, device);
            break;
        }
        case NetAccessEventType::closed:
            if (event.device < 0)
            {
                // It cancelled, or left.
                pair_window_request_gone(&app->pair_window, event.peer);
            }
            if (app->headless.on && app->headless.peer == event.peer)
            {
                headless_back(app, true);   // the device that had the game left
                headless_tell(app);
            }
            net_service_set_stream(&app->net_service, event.peer, 0);   // its stream ends with it
            SDL_Log("Network: \"%s\" disconnected", device);
            if (event.guest)
            {
                char status[128];
                std::snprintf(status, sizeof(status), piccolo_text(&app->strings, Str::status_guest_left),
                              std::strcmp(device, "?") != 0 ? device : event.info.name);
                set_status(app, status);
            }
            net_peer_forget(app, event.peer);
            break;
        case NetAccessEventType::pairing_failed:
            // PAIRING FAILED is the pairing window's own notice (in it, or its card in the game).
            SDL_Log("Network: pairing failed (too many wrong codes)");
            pair_window_request_gone(&app->pair_window, event.peer);
            pair_window_failed(&app->pair_window, SDL_GetTicks());
            break;
        case NetAccessEventType::pairing_requested:
            // A device asks (Andrés, 2026-11-25: the one asked has the code):
            // TOOLS > PAIR... opens on GENERATE PAIRING CODE showing it, with
            // the permissions to tick; the game pauses while it's up. One at a
            // time: another waits until this one is done (it asks again on its
            // own if refused).
            pair_window_take_request(&app->pair_window, &event);
            if (app->pair_window.request.active && app->pair_window.request.peer == event.peer)
            {
                SDL_Log("Network: \"%s\" asks to pair%s", event.info.name, event.retry ? " (new code)" : "");
            }
            break;
        case NetAccessEventType::code_expired:
            pair_window_code_expired(&app->pair_window, SDL_GetTicks());
            break;
        case NetAccessEventType::guest_joined:
        {
            // A guest from the link: what runs at once (it starts the stream itself).
            // First: a lost `closed` must not leave the previous device's state to this guest.
            net_peer_forget(app, event.peer);
            net_service_set_stream(&app->net_service, event.peer, 0);
            if (event.peer < NET_MAX_PEERS)
            {
                // "%.*s": a hello's name may be longer than a guest's; cut, not a warning.
                std::snprintf(app->net_guest_name[event.peer], sizeof(app->net_guest_name[event.peer]), "%.*s",
                              static_cast<int>(NET_GUEST_MAX_NAME), guest != nullptr ? guest->guest_name : event.info.name);
            }
            SDL_Log("Network: guest \"%s\" joined from %s", event.info.name, address);
            char status[128];
            std::snprintf(status, sizeof(status), piccolo_text(&app->strings, Str::status_guest_joined), event.info.name);
            set_status(app, status);
            net_send_game_state(app, event.peer);
            break;
        }
        case NetAccessEventType::guest_full:
            SDL_Log("Network: guest \"%s\" turned away: every seat is taken", event.info.name);
            break;
        case NetAccessEventType::granted:
            // A paired device says what we may use on it (stored as granted):
            // the DONE screen shows it, SETTINGS > NETWORK > DEVICES too.
            SDL_Log("Network: \"%s\" lets us use: %s", device, net_caps_name(event.caps).c_str());
            pair_window_granted(&app->pair_window, &event);
            break;
        case NetAccessEventType::unpaired:
        {
            // The device forgot this piccolo, so it's gone from PAIRED DEVICES
            // too (a card only while the pairing window is closed, as PAIRED WITH).
            if (!app->pair_window.open)
            {
                char status[256];
                std::snprintf(status, sizeof(status), "%s %s", piccolo_text(&app->strings, Str::status_net_unpaired),
                              event.info.name);
                set_status(app, status);
            }
            SDL_Log("Network: \"%s\" unpaired itself", event.info.name);
            break;
        }
        case NetAccessEventType::message:
            if (event.channel == NetChannel::input && event.size > 0 && event.data[0] == NET_INPUT_TYPE_SPEED)
            {
                // A speed test's packet (up), from the device running the test.
                if (event.peer == app->net_speed_receiver.peer)
                {
                    net_speed_receive(&app->net_speed_receiver, event.data, event.size, net_seconds());
                }
            }
            else if (event.channel == NetChannel::input && guest != nullptr)
            {
                // A PLAY guest's pad (net_access let it through only at PLAY):
                // game only, never the game's hotkeys; START joins a free port.
                input_net_set_hotkeys(&app->input_net, &app->input, event.peer, false);
                input_net_receive(&app->input_net, &app->input, event.peer, guest->key, guest->guest_name, event.data,
                                  event.size);
            }
            else if (event.channel == NetChannel::input && event.device >= 0)
            {
                // A paired device's pad needs GAMEPAD (net_access drops the rest too: this is the
                // frontend's own guard), and the device must still be paired: a packet from one
                // forgotten meanwhile is dropped, never plugged in under another device's key.
                if (device_now >= 0 && (event.caps & NET_CAP_GAMEPAD) != 0)
                {
                    const NetTrustedDevice* paired = &app->net_service.access.trust.devices[static_cast<size_t>(device_now)];
                    input_net_set_hotkeys(&app->input_net, &app->input, event.peer,
                                          (event.caps & NET_CAP_STREAM) != 0);
                    input_net_receive(&app->input_net, &app->input, event.peer, paired->key, paired->name.c_str(),
                                      event.data, event.size);
                }
            }
            else if (event.channel == NetChannel::control)
            {
                if (event.size > 0 && event.data[0] == NET_SPEED_REQUEST)
                {
                    net_speed_request(app, &event);
                }
                if (event.size > 0 && event.data[0] == NET_SPEED_RESULT)
                {
                    net_speed_result(app, &event, device_now);
                }
                net_stream_message(app, &event, device);
                net_games_message(app, &event, device);   // net_access let through only what its level covers
                net_remote_message(app, &event, device);
            }
            break;
        case NetAccessEventType::none:
            break;
        }
    }
    net_follow_game_state(app);
    net_follow_remote(app);
    net_speed_update(app);
    // Remote play: frames and audio are handed over only while someone streams.
    app->net_streaming = net_service_streaming(&app->net_service);
    if (app->net_streaming && !app->session.capture_audio)
    {
        app->session.captured_audio.reserve(16384);   // once: a few frames' worth
    }
    app->session.capture_audio = app->net_streaming;
    net_follow_stream_tuning(app);
}

// INTERFACE > BACKGROUND TRANSPARENCY: while the menu or any window is up
// (DEVICES too), the whole screen is covered by the theme's BACKGROUND
// color, with that much of what's behind showing through, behind every
// window (the menu bar stays on top of it): the UI reads well over any
// picture, in the theme's color (Andrés, testing round 3: black looked like
// the background had gone). One backdrop for everything (testing round 1):
// with or without a game, and also for the windows that open over the game
// itself (a pairing request, PAIR, REMOTE PLAY), but never under the DVR.
// Widgets and modals don't dim the screen themselves. ARGB; alpha 0 for none.
// BACKGROUND at its opacity (100 - BACKGROUND TRANSPARENCY) over black: the
// screen's color without a game, as the backdrop looks over a game's borders.
static uint32_t background_over_black(const RetroUiPalette* palette)
{
    uint32_t transparency = palette->background_transparency > 100 ? 100 : palette->background_transparency;
    uint32_t opacity = 100u - transparency;
    uint32_t red = ((palette->background >> 16) & 0xFFu) * opacity / 100u;
    uint32_t green = ((palette->background >> 8) & 0xFFu) * opacity / 100u;
    uint32_t blue = (palette->background & 0xFFu) * opacity / 100u;
    return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

static uint32_t menu_backdrop(const App* app)
{
    RetroUiPalette palette = {};
    piccolo_settings_palette(&app->settings, &palette);
    uint32_t transparency = palette.background_transparency > 100 ? 100 : palette.background_transparency;
    // (Not DEBUG: it's watched while playing.)
    bool window_over_game = app->pair_window.open || app->guest_window.open ||
                            (app->pair_window.request.active && !pair_request_in_game(app));
    // (No game: the screen is that color already, background_over_black.)
    if ((!ui_is_up(app) && !window_over_game) || transparency >= 100 || !game_running(app))
    {
        return 0;
    }
    // The DVR is about the game's picture: never dimmed under it (Andrés,
    // 2026-11-25: "the game darkens a lot on DVR"), on every platform.
    if (app->ui.mode == UiMode::widget && app->ui.widget == UiWidget::dvr)
    {
        return 0;
    }
    uint32_t alpha = (100u - transparency) * 255u / 100u;
    return (alpha << 24) | (palette.background & 0xFFFFFFu);
}

// The HUD widgets' state icons and notification cards (retroui's hud_view),
// behind every window. The cards stand above the DVR's strip while it's open.

static void hud_fill(App* app, UiView* view)
{
    uint64_t now_ms = SDL_GetTicks();
    hud_view_clear(&app->hud);
    StateFacts facts = {};
    facts.paused = game_running(app) && app->session.paused;
    facts.rewinding = app->dvr.open;
    facts.fast_forwarding = game_running(app) && core_session_fast_forwarding(&app->session);
    facts.played_from_ms = app->dvr.played_from_ms;
    facts.resumed_ms = app->resumed_ms;
    state_widget_add_hud(&app->state_icons, &facts, &app->hud, now_ms);
    dvr_widget_add_hud(&app->dvr, &app->hud, now_ms);
    remap_widget_add_hud(&app->remap, &app->strings, &app->hud, now_ms);
    guest_window_add_hud(&app->guest_window, &app->strings, &app->hud, now_ms);
    pair_window_add_hud(&app->pair_window, &app->strings, pair_request_in_game(app), &app->hud, now_ms);
    debug_preview_add_hud(&app->debug_previews, &app->strings, &app->hud, now_ms);
    message_widget_add_hud(&app->messages, &app->hud, now_ms);

    float block = static_cast<float>(app->ui_scale > 0 ? app->ui_scale : 1);
    HudArea area = {};
    area.right = view->screen_width / block;
    area.top = 0.0f;
    area.bottom = view->screen_height / block - HUD_VIEW_MARGIN;
    float strip = app->phone_ui ? 0.0f : dvr_widget_strip_height(&app->dvr, app->ui_scale) / block;
    if (strip > 0.0f && view->screen_height / block - strip - HUD_VIEW_GAP < area.bottom)
    {
        area.bottom = view->screen_height / block - strip - HUD_VIEW_GAP;
    }
    view->hud = &app->hud;
    view->hud_area = area;
    view->hud_now_ms = now_ms;
}

// The menu snow: in season (December 24 to 28, local date) or forced
// from MENU > DEBUG, whenever the menu or a widget is up (the empty
// background, the menu over a game, QUICK MENU, DEVICES, the DVR...); never
// in GAME or EDIT LAYOUT (Andrés, 2026-11-26). Shown again after a
// while away, it starts from the sky. The sleigh crosses on December 25.
constexpr uint64_t SNOW_DATE_CHECK_MS = 60 * 1000;

static void snow_fill(App* app, UiView* view)
{
    uint64_t now_ms = SDL_GetTicks();
    if (app->snow_date_checked_ms == 0 || now_ms - app->snow_date_checked_ms >= SNOW_DATE_CHECK_MS)
    {
        app->snow_date_checked_ms = now_ms > 0 ? now_ms : 1;
        SDL_Time time = 0;
        SDL_DateTime date = {};
        if (SDL_GetCurrentTime(&time) && SDL_TimeToDateTime(time, &date, true))
        {
            app->snow_in_season = snow_view_in_season(date.month, date.day);
            app->snow_sleigh_day = snow_view_is_sleigh_day(date.month, date.day);
        }
    }
    bool wanted = app->snow_in_season;
    if (app->snow_debug == DebugSnow::on)
    {
        wanted = true;
    }
    else if (app->snow_debug == DebugSnow::off)
    {
        wanted = false;
    }
    bool shown = wanted && (view->menu_shown || app->ui.mode == UiMode::widget);
    if (shown && !app->snow_was_shown)
    {
        if (!app->snow_started)
        {
            snow_view_start(&app->snow, static_cast<uint32_t>(SDL_GetPerformanceCounter()));
            app->snow_started = true;
        }
        else
        {
            snow_view_restart(&app->snow);
        }
    }
    app->snow_was_shown = shown;
    view->snow = nullptr;
    if (!shown)
    {
        return;
    }
    float block = static_cast<float>(app->ui_scale > 0 ? app->ui_scale : 1);
    // Whole retro pixels, the last one cut by the screen's edge included.
    int32_t width = static_cast<int32_t>(std::ceil(view->screen_width / block));
    int32_t height = static_cast<int32_t>(std::ceil(view->screen_height / block));
    snow_view_set_area(&app->snow, width, height);
    snow_view_set_sleigh_day(&app->snow, app->snow_sleigh_day);
    snow_view_update(&app->snow, now_ms);
    view->snow = &app->snow;
}

// ---------------------------------------------------------------------------
// The frame's UI: updates, view, (ui_draw), apply. None of these draw: a
// different GUI toolkit would leave them as they are.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The CONTINUE card (continue_panel.h): the last game closed, on the empty
// screen. Part of it, not a window: no game running, MAIN MENU's bar alone
// or with things over it; it's never in Esc's chain. A, Enter or a click
// press its first button only while nothing is open over it.
// ---------------------------------------------------------------------------

static bool continue_card_wanted(const App* app)
{
    return !game_running(app) && app->ui.mode == UiMode::main_menu && !app->onscreen_pad.editor.active;
}

// Shown at all this frame (the real one has a card to show; a fake one
// from the debug window shows over a game too).
static bool continue_card_shown(const App* app)
{
    if (app->continue_panel.preview != ContinuePreview::none)
    {
        return true;
    }
    return continue_card_wanted(app) && continue_panel_choice(&app->continue_panel).kind != ContinueKind::none;
}

// Nothing open over it: its first button is highlighted and takes A / Enter.
static bool continue_card_active(const App* app)
{
    if (app->continue_panel.preview == ContinuePreview::phone)
    {
        return true;   // over everything
    }
    bool nothing_open = app->ui.window_count == 0 && !ui_toolkit_has_popup() && !app->pair_window.open &&
                        !app->guest_window.open && !debug_preview_shown(&app->debug_previews) &&
                        app->ui.mode == UiMode::main_menu;
    return continue_card_shown(app) && nothing_open;
}

// What the card's button does: RESUME starts the game in the card (not
// necessarily RECENT's top) with its core (SAVE STATE ON EXIT loads the AUTO
// state); START / RESTART starts it without that. A fake card's buttons only
// end the preview.
static void continue_card_press(App* app, ContinueViewButton button)
{
    if (button == ContinueViewButton::none)
    {
        return;
    }
    if (app->continue_panel.preview != ContinuePreview::none)
    {
        continue_panel_preview(&app->continue_panel, ContinuePreview::none);
        return;
    }
    const RecentGame* game = continue_panel_card_game(&app->continue_panel);
    if (game == nullptr || game_running(app))
    {
        return;
    }
    app->pending_start = FilePickerChoice{};
    std::snprintf(app->pending_start.core_path, sizeof(app->pending_start.core_path), "%s", game->core_path.c_str());
    std::snprintf(app->pending_start.content_path, sizeof(app->pending_start.content_path), "%s",
                  game->content_path.c_str());
    app->has_pending_start = true;   // started at the top of the next loop, like RECENT
    app->resume_skip = button == ContinueViewButton::start;
}

// A tile / row of the shelf chosen (a click, or A / Enter on it): its game
// goes into the card. Nothing starts: that is RESUME or RESTART.
static void continue_card_choose(App* app, uint32_t tile)
{
    continue_panel_choose(&app->continue_panel, tile);
}

// Before the view: the files and the picture, scaled for its size on screen.
static void continue_card_update(App* app, const UiView* view)
{
    ContinuePanel* panel = &app->continue_panel;
    const ContinueRecord* record = continue_panel_record(panel);
    ContinueViewCard card = {};
    card.aspect_width = record->aspect_width;
    card.aspect_height = record->aspect_height;
    float width = 0.0f;
    float height = 0.0f;
    float tile_width = 0.0f;   // the shelf's / the list's pictures: only on the desktop
    float tile_height = 0.0f;
    bool as_list = false;
    // RECENT as shown: the panel keeps the order of its games from it.
    std::vector<RecentGame> shown;
    for (size_t i = 0; i < app->recent_shown.size(); i++)
    {
        shown.push_back(app->recent.games[app->recent_shown[i]]);
    }
    if (panel->preview == ContinuePreview::phone)
    {
        int32_t touch_scale = touch_ui_scale_for(view->screen_width, view->screen_height);
        float phone = view->screen_width / static_cast<float>(touch_scale);
        phone = phone < 195.0f ? phone : 195.0f;
        continue_view_card_picture_size(&card, phone - 16.0f, &width, &height);
        width *= static_cast<float>(touch_scale);
        height *= static_cast<float>(touch_scale);
    }
    else if (app->phone_ui)
    {
        // HOME's card (mobile_home.h), at the phone UI's scale. Upright its
        // picture may be drawn a little shorter when the list needs the
        // room: still sharp, a touch softer.
        float scale = static_cast<float>(app->ui_scale);
        float screen_width = std::floor(view->screen_width / scale);
        float screen_height = std::floor(view->screen_height / scale);
        if (screen_width > screen_height)
        {
            float box_width = 0.0f;
            float box_height = 0.0f;
            mobile_home_cover_box(screen_width, screen_height, &box_width, &box_height);
            continue_view_cover_picture_size(&card, box_width, box_height, &width, &height);
        }
        else
        {
            continue_view_card_picture_size(&card, mobile_home_card_width(screen_width), &width, &height);
        }
        width *= scale;
        height *= scale;
    }
    else
    {
        // The sizes the panel will be drawn at (ui.cpp, the same call). It
        // shows only in MAIN MENU on the desktop, under the menu bar (the
        // view's menu_shown is set later this frame).
        float bar = retro_ui_menu_bar_height(app->ui_scale);
        ContinueViewPanelGeometry geometry;
        continue_view_panel_geometry(app->ui_scale, shown.size() > 1, bar, view->screen_width, view->screen_height,
                                     &geometry);
        continue_view_panel_picture_size(&card, &geometry, &width, &height);
        as_list = geometry.layout == ContinueViewLayout::side_list;
        tile_width = geometry.tile_width;
        tile_height = geometry.tile_height;
    }
    continue_panel_update(panel, &shown, continue_card_wanted(app), width, height, tile_width, tile_height, as_list,
                          SDL_GetTicks());
}

// The desktop's panel, or the card on the phone's HOME (or a fake one).
static void continue_card_view(const App* app, UiView* view)
{
    bool fake = app->continue_panel.preview != ContinuePreview::none;
    bool real = !fake && continue_card_shown(app);
    view->continue_card.shown = fake || (real && !app->phone_ui);
    if (!fake && !real)
    {
        return;
    }
    SDL_Time now_time = 0;
    SDL_GetCurrentTime(&now_time);
    SDL_DateTime date = {};
    SDL_TimeToDateTime(now_time, &date, true);
    ContinueDate now = {};
    now.year = date.year;
    now.month = date.month;
    now.day = date.day;
    now.weekday = date.day_of_week;
    now.hour = date.hour;
    now.minute = date.minute;
    continue_panel_state(&view->continue_card, &app->continue_panel, &app->strings, &now, continue_card_active(app),
                         app->phone_ui);
    // The phone's HOME shows a card only for a game with a record (a game
    // without one is just in its list).
    if (real && app->phone_ui && view->mobile_home.home_shown && continue_panel_card_has_record(&app->continue_panel))
    {
        view->continue_card.card.hint = "";   // the phone shows no keys
        view->mobile_home.card = &view->continue_card.card;
    }
}

// Before the view: what moves on its own each frame (placing the picture and
// the touch gamepad, the windows' polling of jobs and connections), in the
// order the loop always did it.
static void app_ui_update(App* app, AppUiFrame* frame, UiView* view)
{
    // Where the touch gamepad goes (under the picture upright, if it fits),
    // before the picture is placed.
    {
        const retro_game_geometry* geometry = &app->session.host->av_info.geometry;
        float aspect = geometry->aspect_ratio > 0.0f ? geometry->aspect_ratio
                     : geometry->base_height > 0 ? static_cast<float>(geometry->base_width) / static_cast<float>(geometry->base_height)
                                                 : 4.0f / 3.0f;
        onscreen_pad_place(&app->onscreen_pad, app->ui_derived.overlay_active, aspect, view->screen_width,
                           view->screen_height);
    }
    prepare_game_picture(app, view);   // background, behind everything
    continue_card_update(app, view);
    // The browser's page: its strip hides while a game is played, and an
    // upright phone gives the picture only the height its shape needs.
    web_pad_tell_page(game_has_keyboard(app), app->shown_aspect);
    web_pad_set_vibrate(settings_get_bool(&app->settings, PICCOLO_TOUCH_PAD_VIBRATE));
    // The touch gamepad over the game, a GAME-mode device (it takes touches
    // only in GAME), or its EDIT LAYOUT (MAIN MENU's top window, drawn by
    // the overlay); its burger menu opens the MAIN MENU, the QUICK MENU, the
    // GAME DVR, CONTROLLERS and EDIT LAYOUT (as the web page's pad: without a
    // keyboard they're the way to the menus).
    // STYLE (per core / game): made again only when it changes. AUTO ("",
    // the default) is the game's controller (auto_layout: NES / Game Boy,
    // SUPER NINTENDO in their MINIMAL arrangement), else the RetroPad.
    {
        const char* style = settings_get_text(&app->settings, PICCOLO_TOUCH_PAD_STYLE);
        const InputLayout* layout = style[0] == '\0' ? auto_layout(app) : find_layout_by_name(&app->layouts, style);
        std::string made_from = style[0] == '\0' ? "AUTO:" + (layout != nullptr ? layout->id : std::string()) : style;
        if (app->onscreen_pad.style_name != made_from)
        {
            onscreen_pad_set_style(&app->onscreen_pad, layout, made_from.c_str());
        }
    }
    // The page pad's burger menu (the browser, no keyboard needed): MAIN MENU
    // like the MAIN MENU hotkey; QUICK MENU like the QUICK MENU hotkey in a
    // game, back from anything else; GAME DVR and CONTROLLERS as the touch
    // pad's own. Taken before drawing, as the hotkeys are (they came right
    // after the touch gamepad's own picks when those were applied in the
    // middle of drawing).
    if (web_pad_take_main_menu())
    {
        if (app->ui.mode == UiMode::main_menu)
        {
            ui_do_back(app);   // its top window, then the game
        }
        else
        {
            app_enter_main_menu(app);
        }
    }
    if (web_pad_take_quick_menu() && game_running(app))
    {
        if (app->ui.mode == UiMode::game)
        {
            app_open_quick_menu(app);
        }
        else if (quick_menu_up(app))
        {
            app_enter_game(app);
        }
        else
        {
            ui_do_back(app);
        }
    }
    if (web_pad_take_game_dvr() && game_has_keyboard(app))
    {
        open_game_dvr(app);
    }
    // CONTROLLERS from any mode, like the DEVICES hotkey (the page's pad
    // shows in the menus too).
    if (web_pad_take_controllers())
    {
        open_controllers(app);
    }
    // SETTINGS > NETWORK's hosts list (our client side): SHARE's connection
    // knows it, started for it.
    frame->hosts_known = false;
    if (app->settings_window.open && app->settings_window.tab == SettingsTab::network && start_share_link(app))
    {
        net_share_link_status(&app->share_link, &view->settings_hosts);
        net_share_link_set_beacon(&app->share_link, settings_get_text(&app->settings, PICCOLO_NET_BEACON));
        frame->hosts_known = true;
    }
    settings_window_update(&app->settings_window, &app->settings, app->core_name, app->game_name);
    file_picker_update(&app->picker, &app->catalog);
    quick_menu_update(&app->quick_menu, &app->session);
    if (!game_running(app))
    {
        app->core_config.open = false;   // the core it configured is gone
    }
    frame->config_now = SDL_GetTicks();
    core_config_window_update(&app->core_config, &app->input, app->session.host, &app->settings, &app->strings,
                              app->core_name, app->game_name, frame->config_now);
    file_picker_update(&app->shader_picker, &app->catalog);
    frame->dvr_now = SDL_GetTicks();
    dvr_widget_name_keys(&app->dvr, &app->input, &app->input_sdl, &app->layouts);
    dvr_widget_prepare(&app->dvr, &app->strings, app->onscreen_pad.touch_screen, frame->dvr_now);
    // The core downloader: pick up what finished; the catalog sees
    // installed or removed cores right away.
    core_updater_poll(&app->core_updater);
    if (app->core_updater.cores_changed)
    {
        app->core_updater.cores_changed = false;
        core_catalog_scan(&app->catalog, game_running(app) ? app->session.host : nullptr);
    }
    cores_window_update(&app->cores_window, &app->core_updater, &app->settings, app->running_core_file);
    // SHARE: this side from our folders, the other from the connection.
    if (app->share_window.open)
    {
        share_update(app, &view->share_line);
        const NetShareLinkStatus* share_link = nullptr;   // nullptr: no connection
        if (net_share_link_started(&app->share_link))
        {
            share_link = &app->share_status;
        }
        app->share_window.mirror_folders = settings_get_bool(&app->settings, PICCOLO_SHARE_MIRROR_FOLDERS);
        share_window_update(&app->share_window, &app->share_folders, share_link);
    }
    // TOOLS > PAIR... (PAIR WITH A HOST on SHARE's connection). Also while
    // closed: a request opens it. However it opened (the menu, SHARE's PAIR,
    // a request, a pairing), FILE TRANSFER's connection starts here, the one
    // place, so PAIR WITH A HOST never finds it missing.
    if (app->pair_window.open)
    {
        start_share_link(app);
    }
    pair_update(app);
    {
        const NetShareLinkStatus* pair_link = nullptr;   // nullptr: networking couldn't start
        if (net_share_link_started(&app->share_link))
        {
            pair_link = &app->share_status;
        }
        pair_window_update(&app->pair_window, pair_link, pair_request_in_game(app), SDL_GetTicks());
    }
    // The debug window's preview of a window ends when the real one opens.
    debug_preview_update(&app->debug_previews, app->pair_window.open, app->about_window.open, app->guest_window.open,
                         app->quick_menu.open, app->settings_error_open, app->ports_widget.open, app->dvr.open);
    frame->input_now = SDL_GetTicks();
    input_window_update(&app->input_window, &app->input, frame->input_now);
    frame->map_now = SDL_GetTicks();
    map_window_update(&app->map_window, &app->input, frame->map_now);
}

// Everything the frame shows, as plain data (ui.h), after the updates: no
// GUI calls, nothing changes but what the states themselves fill in.
// The phone's HOME and ☰ (mobile_home.h). HOME while no game runs; ☰ over
// it when opened, or over a paused game whenever MAIN MENU has nothing open
// (the pad's burger menu's MAIN MENU: Andrés, 2026-11-25, the way back to the game must show).
static void phone_view(const App* app, bool main_menu, MobileHomeState* state)
{
    bool running = game_running(app);
    bool nothing_open = app->ui.window_count == 0 && !app->pair_window.open && !app->guest_window.open &&
                        !debug_preview_shown(&app->debug_previews);
    state->strings = &app->strings;
    state->home_shown = main_menu && !running && !debug_preview_covers_screen(&app->debug_previews);
    state->menu_in_game = main_menu && running && nothing_open;
    state->menu_shown = nothing_open && (state->menu_in_game || (state->home_shown && app->mobile_home.menu_open));
    state->games = app->recent_rows.data();
    state->game_count = static_cast<int32_t>(app->recent_rows.size());
    state->first_game_id = MENU_ID_RECENT;
    state->load_game_id = static_cast<int32_t>(MenuAction::load_game);
    state->clear_recents_id = static_cast<int32_t>(MenuAction::clear_recents);
    state->menus = app->menus.data();
    state->menu_count = static_cast<int32_t>(app->menus.size());
}

static void app_ui_view(App* app, AppUiFrame* frame, UiView* view)
{
    onscreen_pad_state(&view->pad, &app->settings, &app->strings);
    // Where the picture is (prepare_game_picture, a moment ago): the pad
    // draws its empty space around it.
    view->pad.picture.shown = app->input.view_width > 0.0f;
    view->pad.picture.x = app->input.view_x;
    view->pad.picture.y = app->input.view_y;
    view->pad.picture.width = app->input.view_width;
    view->pad.picture.height = app->input.view_height;
    // (EDIT LAYOUT has the whole screen: no menu bar meanwhile.)
    bool main_menu = app->ui.mode == UiMode::main_menu && !app->onscreen_pad.editor.active;
    // The phone has HOME and ☰ instead of the bar.
    view->menu_shown = main_menu && !app->phone_ui;
    view->phone = app->phone_ui;
    if (app->phone_ui)
    {
        phone_view(app, main_menu, &view->mobile_home);
    }
    ports_widget_state(&view->ports_tabs, &app->strings, view->menu_shown, false, SDL_GetTicks());
    snow_fill(app, view);
    hud_fill(app, view);
    continue_card_view(app, view);
    view->backdrop = menu_backdrop(app);
    view->menus = app->menus.data();
    view->menu_count = static_cast<int32_t>(app->menus.size());
    view->open_first_menu = app->open_first_menu;

    settings_view(app, view, frame);
    file_picker_state(&view->picker, &app->picker, &app->catalog, &app->strings);
    quick_menu_state(&view->quick, &app->quick_menu, &app->strings, app->core_name, app->game_name, app->ui.user_paused,
                     app->undo_load_size > 0);
    core_options_state(app, &view->config_options);
    core_config_window_state(&view->config, &app->core_config, &app->strings, &view->config_options, &app->input,
                             app->session.host, &app->settings, &app->input_sdl, &app->layouts, app->core_name,
                             app->game_name, app->shader_library.loaded, frame->config_now);
    file_picker_state(&view->shader_picker, &app->shader_picker, &app->catalog, &app->strings);
    // A phone held upright: DEVICES has a row per port (five columns don't fit).
    bool upright_phone = app->phone_ui && view->screen_height > view->screen_width;
    ports_widget_state(&view->ports_board, &app->strings, view->menu_shown, upright_phone, SDL_GetTicks());
    float game_aspect = game_running(app) ? static_cast<float>(app->session.host->av_info.geometry.aspect_ratio) : 0.0f;
    app->dvr.core_aspect = game_aspect;
    dvr_widget_state(&view->dvr, &app->dvr, &app->strings, app->phone_ui, game_aspect);
    core_options_state(app, &view->manager);
    cores_window_state(&view->cores, &app->core_updater, &app->settings, &app->strings, app->running_core_file);

    view->share_shown = app->share_window.open;
    if (view->share_shown)
    {
        const NetShareLinkStatus* share_link = nullptr;   // nullptr: no connection
        if (net_share_link_started(&app->share_link))
        {
            share_link = &app->share_status;
        }
        share_window_state(&view->share, &app->strings, share_link, &view->share_line);
    }
    view->pair_shown = app->pair_window.open;
    if (view->pair_shown)
    {
        const NetShareLinkStatus* pair_link = nullptr;   // nullptr: networking couldn't start
        if (net_share_link_started(&app->share_link))
        {
            pair_link = &app->share_status;
        }
        // (A request shows it over anything, the game paused: ui_derive.)
        pair_window_state(&view->pair, &app->strings, pair_link, app->net_running);
    }
    // TOOLS > REMOTE PLAY: reads the NetAccess, so under the lock.
    view->guest_shown = app->guest_window.open;
    if (view->guest_shown)
    {
        NetAccess* access = app->net_running ? &app->net_service.access : nullptr;
        if (access != nullptr)
        {
            net_service_lock(&app->net_service);
        }
        guest_window_state(&view->guest, access, &app->input_net, &app->input, game_running(app),
                           !app->net_beacon_applied.empty(), &app->strings);
        if (access != nullptr)
        {
            net_service_unlock(&app->net_service);
        }
    }
    input_window_state(&view->input, &app->input, &app->input_sdl, &app->strings, &app->settings, app->core_name,
                       app->game_name, &app->layouts, frame->input_now);
    frame->input_was_open = app->input_window.open;
    map_window_state(&view->map, &app->map_window, &app->strings, &app->layouts, frame->map_now);

    view->safe_start_shown = app->safe_start_open;
    if (view->safe_start_shown)
    {
        safe_start_window_state(&view->safe_start, &app->strings, app->crashed_core.c_str(),
                                app->crashed_after_options);
    }
    view->settings_error_shown = app->settings_error_open;
    if (view->settings_error_shown)
    {
        settings_error_window_state(&view->settings_error, &app->strings);
        view->settings_error.files = app->settings_error_files;   // built once at start
    }
    view->shader_check_shown = shader_check_asking(app);
    if (view->shader_check_shown)
    {
        // The preset's file name without its folder and extension (the
        // pixel font draws it in capitals).
        std::string name = app->shader_preset_seen;
        size_t slash = name.find_last_of("/\\");
        name = slash == std::string::npos ? name : name.substr(slash + 1);
        size_t dot = name.rfind('.');
        name = dot == std::string::npos ? name : name.substr(0, dot);
        view->shader_check_preset = name;
        const FrameTiming* timing = &app->session.timing;
        view->shader_check_target_fps = timing->effective == TimingMode::vsync ? timing->display_hz : timing->core_fps;
        view->shader_check_measured_fps = 1e9 / app->shader_check.measured_ns;
        view->shader_check_text.title = piccolo_text(&app->strings, Str::shader_slow_title);
        view->shader_check_text.speed = piccolo_text(&app->strings, Str::shader_slow_text);
        view->shader_check_text.question = piccolo_text(&app->strings, Str::shader_slow_question);
        view->shader_check_text.keep = piccolo_text(&app->strings, Str::shader_slow_keep);
        view->shader_check_text.revert = piccolo_text(&app->strings, Str::shader_slow_revert_n);
        view->shader_check_asked_ms = app->shader_check_asked_ms;
        view->shader_check_now_ms = SDL_GetTicks();
    }
    view->text_input_shown = text_input_pending(app);
    if (view->text_input_shown)
    {
        text_input_window_state(&view->text_input, &app->session.host->text_input, &app->strings);
        text_input_window_update(&app->text_input_window, &view->text_input);
    }
    about_window_state(&view->about, &app->strings, PICCOLO_VERSION);
    view->debug_shown = app->show_debug_window && !debug_preview_covers_screen(&app->debug_previews);
    if (view->debug_shown)
    {
        debug_window_state(app, &view->debug);
    }
    debug_preview_state(&view->preview, &app->debug_previews, &app->strings, PICCOLO_VERSION, app->menus.data(),
                        static_cast<int32_t>(app->menus.size()), app->phone_ui && view->screen_height > view->screen_width,
                        &view->pad);
    view->fps_shown = app->fps_from_env || settings_get_bool(&app->settings, PICCOLO_SHOW_FPS);
    if (view->fps_shown)
    {
        view->fps_meter = &app->fps_meter;
        view->fps_target = game_running(app) ? app->session.host->av_info.timing.fps : 0.0;
    }
    view->cursor_shown = ui_is_up(app);
}

// The windows' own state, which the drawing may change (all the App's).
static void app_ui_windows(App* app, UiWindows* windows)
{
    windows->pad = &app->onscreen_pad;
    windows->mobile_home = &app->mobile_home;
    windows->ports = &app->ports_widget;
    windows->settings = &app->settings_window;
    windows->network = &app->net_window;
    windows->picker = &app->picker;
    windows->quick = &app->quick_menu;
    windows->config = &app->core_config;
    windows->options_ui = &app->core_options_ui;
    windows->shader_picker = &app->shader_picker;
    windows->dvr = &app->dvr;
    windows->manager_open = &app->core_options_manager;
    windows->cores = &app->cores_window;
    windows->share = &app->share_window;
    windows->pair = &app->pair_window;
    windows->guest = &app->guest_window;
    windows->input = &app->input_window;
    windows->map = &app->map_window;
    windows->text_input = &app->text_input_window;
    windows->about = &app->about_window;
    windows->previews = &app->debug_previews;
}

// A pick on the menu bar, or the phone's HOME / ☰ (the same ids): a recent
// game starts, any other item is its MenuAction. RETRO_UI_MENU_NONE: nothing.
static void apply_menu_pick(App* app, int32_t picked)
{
    int32_t recent_picked = -1;
    MenuAction action = MenuAction::none;
    if (picked >= MENU_ID_RECENT)
    {
        action = MenuAction::recent_game;
        recent_picked = picked - MENU_ID_RECENT;
    }
    else if (picked >= 0)
    {
        action = static_cast<MenuAction>(picked);
    }
    if (action == MenuAction::recent_game && recent_picked >= 0)
    {
        // Started at the top of the next loop, like LOAD GAME (see pending_start).
        const RecentGame* game = &app->recent.games[app->recent_shown[static_cast<size_t>(recent_picked)]];
        app->pending_start = FilePickerChoice{};
        std::snprintf(app->pending_start.core_path, sizeof(app->pending_start.core_path), "%s",
                      game->core_path.c_str());
        std::snprintf(app->pending_start.content_path, sizeof(app->pending_start.content_path), "%s",
                      game->content_path.c_str());
        app->has_pending_start = true;
    }
    handle_menu_action(app, action);
}

// What the user did this frame, applied in the order the windows are drawn
// (no GUI calls). What a window changes here, windows drawn after it see
// from the next frame.
static void app_apply_ui(App* app, const UiView* view, const AppUiFrame* frame, UiActions* actions)
{
    // The pickers as they were drawn: an earlier apply below may open one
    // again (SETTINGS' CHOOSE FOLDER, CORE CONFIGURATION's shader CHOOSE), and
    // what was clicked in the old one must not land in the new one.
    bool picker_drawn = app->picker.open;
    FilePickerMode picker_mode = app->picker.mode;
    bool shader_picker_drawn = app->shader_picker.open;

    onscreen_pad_apply(&app->onscreen_pad, &actions->pad, &app->settings);
    if (actions->pad.edit_done)
    {
        save_settings(app);   // EDIT LAYOUT's FILL / BORDER / LABELS / BACKGROUND
    }
    if (actions->pad.edit_layout)
    {
        onscreen_pad_edit(&app->onscreen_pad);   // the burger menu's EDIT LAYOUT
    }
    continue_card_press(app, actions->continue_pressed);
    if (actions->continue_tile >= 0)
    {
        continue_card_choose(app, static_cast<uint32_t>(actions->continue_tile));
    }
    continue_panel_page(&app->continue_panel, actions->continue_page);
    // The burger menu's other picks (the pad shows in GAME mode, with a
    // game, or in EDIT LAYOUT: there they have no use).
    if (actions->pad.main_menu && game_has_keyboard(app))
    {
        app_enter_main_menu(app);
    }
    if (actions->pad.quick_menu && game_has_keyboard(app))
    {
        app_open_quick_menu(app);
    }
    if (actions->pad.game_dvr && game_has_keyboard(app))
    {
        open_game_dvr(app);
    }
    if (actions->pad.controllers && game_has_keyboard(app))
    {
        open_controllers(app);
    }

    if (view->menu_shown)
    {
        app->open_first_menu = false;
        apply_menu_pick(app, actions->menu_picked);
    }
    if (view->phone)
    {
        const MobileHomeAction* phone = &actions->mobile_home;
        mobile_home_apply(&app->mobile_home, phone, view->mobile_home.menu_shown);
        if (phone->rotate)
        {
            piccolo_window_rotate(app->shell.window);
        }
        if (phone->close_menu && view->mobile_home.menu_in_game)
        {
            app_enter_game(app);   // RESUME, ›, a tap outside: back to the paused game
        }
        apply_menu_pick(app, phone->picked);
        continue_card_press(app, phone->continue_pressed);
    }

    settings_apply(app, frame, &actions->settings);
    // LOAD GAME, or CHOOSE FOLDER (opened from SETTINGS).
    FilePickerChoice picked;
    bool choosing_folder = picker_mode == FilePickerMode::folder;
    bool picker_same = picker_drawn && app->picker.open && app->picker.mode == picker_mode;
    if (picker_same && file_picker_apply(&app->picker, &actions->picker, &app->catalog, &picked))
    {
        if (choosing_folder)
        {
            choose_folder_done(app, &picked);
        }
        else
        {
            app->pending_start = picked;
            app->has_pending_start = true;   // see pending_start
        }
    }
    quick_menu_apply(&app->quick_menu, &actions->quick, &app->session);
    CoreConfigRequest config_request = core_config_window_apply(&app->core_config, &actions->config, &app->input,
                                                                app->session.host, &app->settings, frame->config_now);
    core_options_ui_apply(&app->core_options_ui, &actions->config.options, &app->session);
    bool shader_picker_same = shader_picker_drawn;
    if (config_request == CoreConfigRequest::choose_shader)
    {
        shader_picker_same = false;   // opened again: this frame's clicks were in the old one
        open_shader_picker(app, app->core_config.shader_scope);
    }
    if (shader_picker_same)
    {
        shader_picker_apply(app, &actions->shader_picker);
    }
    ports_widget_apply(&app->ports_widget, &actions->ports, &app->input);
    dvr_widget_apply(&app->dvr, &actions->dvr, frame->dvr_now);
    // QUICK MENU's presses: the actions it shares with the GAME menu are the
    // same functions; what stays up afterwards is QUICK MENU's own.
    switch (actions->quick.press)
    {
    case QuickMenuPress::save:
    {
        bool saved = save_state_with_thumbnail(app);   // QUICK MENU stays: the slot's new picture shows
        report_session(app, saved, false);
        break;
    }
    case QuickMenuPress::load:
    {
        bool loaded = load_state_keeping_undo(app);
        if (loaded)
        {
            app_enter_game(app);   // back to the game
        }
        report_session(app, loaded, false);
        break;
    }
    case QuickMenuPress::undo_save:
    {
        uint32_t slot = app->quick_menu.slot;   // the slot selected there
        report_undo_save(app, undo_save_state(app, slot), slot, false);   // QUICK MENU stays: the picture shows it
        break;
    }
    case QuickMenuPress::undo_load:
    {
        bool undone = undo_load_state(app);
        if (undone)
        {
            app_enter_game(app);   // back to the game, as LOAD
        }
        report_undo_load(app, undone, false);
        break;
    }
    case QuickMenuPress::quick_save:
        quick_save(app);   // QUICK MENU stays, as SAVE
        break;
    case QuickMenuPress::quick_load:
        if (quick_load(app))
        {
            app_enter_game(app);
        }
        break;
    case QuickMenuPress::screenshot:
        app_enter_game(app);   // back to the game: the shutter plays on it (as GAME > TAKE SCREENSHOT)
        take_screenshot(app);
        break;
    case QuickMenuPress::soft_reset:
        if (soft_reset(app))
        {
            app_enter_game(app);
        }
        break;
    case QuickMenuPress::hard_reset:
        hard_reset(app);   // the game starts again straight into play
        break;
    case QuickMenuPress::close_game:
        close_game(app);
        app_enter_game(app);
        break;
    case QuickMenuPress::pause:
        toggle_user_pause(app);   // QUICK MENU stays: RESUME, or back to the game
        break;
    case QuickMenuPress::dvr:
        open_game_dvr(app);   // a window widget: it replaces the QUICK MENU
        break;
    case QuickMenuPress::config:
        app_open_core_config(app, UINT32_MAX, 0);   // a MAIN MENU window: QUICK MENU closes
        break;
    case QuickMenuPress::none:
        break;
    }
    core_options_manager_apply(&app->core_options_ui, &actions->manager, &app->option_store, &app->session,
                               game_running(app) ? app->core_name : "");
    if (app->core_options_manager_was_open && !app->core_options_manager)
    {
        save_core_options(app);   // what was deleted
    }
    app->core_options_manager_was_open = app->core_options_manager;
    if (cores_window_apply(&app->cores_window, &actions->cores, &app->core_updater, &app->settings,
                           app->running_core_file))
    {
        save_settings(app);   // a SHOW switch
    }

    if (view->share_shown)
    {
        ShareWindowRequest share_request =
            share_window_apply(&app->share_window, &actions->share, &app->share_folders, &app->share_link,
                               &app->share_status, settings_get_text(&app->settings, PICCOLO_NET_BEACON));
        if (share_request != ShareWindowRequest::none)
        {
            share_pair(app, share_request == ShareWindowRequest::pair_internet);
        }
        if (actions->share.mirror_folders_set &&
            settings_set_bool(&app->settings, SettingLayer::global, PICCOLO_SHARE_MIRROR_FOLDERS,
                              actions->share.mirror_folders))
        {
            save_settings(app);   // MIRROR FOLDER STRUCTURE: a global setting shown in FILE TRANSFER
        }
    }
    if (view->pair_shown)
    {
        pair_apply(app, &actions->pair);
    }
    pair_expire(app);
    // TOOLS > REMOTE PLAY: acts on the NetAccess, so under the lock.
    if (view->guest_shown)
    {
        NetAccess* access = app->net_running ? &app->net_service.access : nullptr;
        if (access != nullptr)
        {
            net_service_lock(&app->net_service);
        }
        GuestWindowPress press = guest_window_apply(&app->guest_window, &actions->guest, access, SDL_GetTicks());
        if (press == GuestWindowPress::make_link || press == GuestWindowPress::new_link)
        {
            guest_make_link(app);
        }
        else if (press == GuestWindowPress::end)
        {
            guest_end(app, access);
        }
        if (access != nullptr && actions->guest.level_changed && access->guest_level == NetGuestLevel::watch)
        {
            // Now WATCH: net_access drops the guests' input, so no release would
            // ever come; their pads are unplugged here.
            for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
            {
                if (access->peers[i].state == NetAccessPeerState::guest)
                {
                    input_net_peer_gone(&app->input_net, &app->input, i);
                }
            }
        }
        if (access != nullptr)
        {
            net_service_unlock(&app->net_service);
        }
    }
    InputWindowRequest input_request =
        input_window_apply(&app->input_window, &actions->input, &app->input, &app->settings, frame->input_now);
    map_window_apply(&app->map_window, &actions->map, &app->input, &app->strings, &app->layouts, frame->map_now);
    if (input_request == InputWindowRequest::edit_touch_layout)
    {
        // EDIT LAYOUT: INPUT closes, the touch gamepad's editor takes the screen.
        app->input_window.open = false;
        onscreen_pad_edit(&app->onscreen_pad);
    }
    if (frame->input_was_open && !app->input_window.open)
    {
        save_settings(app);   // SETTINGS tab
        if (app->input_window.dirty && !app->input_config_path.empty())
        {
            save_input_config(app);   // closed: keep what changed
            app->input_window.dirty = false;
        }
    }
    if (view->safe_start_shown)
    {
        SafeStartResult answer = safe_start_window_apply(&actions->safe_start, &app->option_store,
                                                         app->crashed_core.c_str());
        if (answer.options_deleted)
        {
            save_core_options(app);
        }
        if (answer.skip_stored_options_once)
        {
            app->session.skip_stored_options_once = true;
        }
        if (answer.start)
        {
            app->pending_start = app->safe_start_pending;   // started at the top of the next loop
            app->has_pending_start = true;
            app->safe_start_confirmed = true;
        }
        if (answer.closed)
        {
            app->safe_start_open = false;
            net_answer_load_refused(app);
        }
    }
    if (view->settings_error_shown && settings_error_window_apply(&actions->settings_error))
    {
        app->settings_error_open = false;   // only informs: nothing else waits for it
    }
    if (view->shader_check_shown && actions->shader_check != ShaderCheckChoice::none)
    {
        shader_check_answer(app, actions->shader_check);
    }
    if (view->text_input_shown)
    {
        if (app->session.host->text_input.type == RETRO_EXT_TEXT_INPUT_TYPE_NUMBER)
        {
            text_input_ui_field_number(&app->shell.text_input);   // the OSK's number keypad
        }
        text_input_window_apply(&app->text_input_window, &actions->text_input, app->session.host);
    }
    // (Before the debug window's: a preview it starts now gets this frame's
    // presses of the one before.)
    debug_preview_apply(&app->debug_previews, &actions->preview);
    if (view->debug_shown)
    {
        debug_window_apply(app, &actions->debug);
    }
    about_window_apply(&app->about_window, &actions->about);
    save_after_options_windows(app);
    onscreen_pad_end_frame(&app->onscreen_pad);   // (the fingers seen by this frame are done)
}

// One turn of piccolo's loop: events, the game's frame, the UI, present.
// False when piccolo should end (QUIT, or a fatal rendering error). Called
// by app_loop_run: a plain loop on desktop, the browser's animation frames
// in the web build (app_loop.h).
static bool app_frame(App* app)
{
    uint64_t loop_start_ns = SDL_GetTicksNS();   // SHOW FPS: the loop's own work
    app_handle_events(app);
    handle_links(app);
    net_update(app);
    if (app->pending_close_game)
    {
        app->pending_close_game = false;
        shader_check_end_for_game(app);   // before the settings context changes
        save_core_options(app);
        save_state_on_exit(app);
        core_session_stop(&app->session, &app->shell);   // writes save RAM
        app->running_core_file[0] = '\0';
        drop_undos(app);
        remove_running_marker(app);   // a proper end
        update_settings_context(app);
        ui_game_stopped(&app->ui);
        ui_follow_changes(app);
    }
    if (app->has_pending_start)
    {
        app->has_pending_start = false;
        bool started = start_game(app, &app->pending_start);   // outside any frame: nothing drawn with the old texture yet
        // SAFE START asking: answered once it's decided (below, or the
        // start that follows its answer).
        if (app->net_load_waiting && !app->safe_start_open)
        {
            net_answer_load(app, started ? NetContentStatus::ok : NetContentStatus::failed);
        }
    }
    if (app->net_load_refused)
    {
        app->net_load_refused = false;
        net_answer_load_refused(app);
    }
    update_pause(app);
    if (app->quick_slot_stale)
    {
        // Read only after a save or a start, never every frame (a file check).
        app->quick_slot_stale = false;
        StateSlotInfo info = {};
        app->quick_slot_used = game_running(app) && core_session_slot_info(&app->session, QUICK_SLOT, &info) && info.used;
        app->undo_save_checked = UINT32_MAX;   // a save or an undo: its undo file too
    }
    if (app->undo_save_checked != undo_save_slot(app))
    {
        // GAME > UNDO SAVE STATE's slot changed, or its files did: one file check.
        app->undo_save_checked = undo_save_slot(app);
        char path[LIBRETRO_PATH_MAX] = {};
        app->undo_save_ready = game_running(app) &&
                               libretro_host_state_path(app->session.host, app->undo_save_checked, path, sizeof(path)) &&
                               state_undo_available(path);
    }
    if (app->menus_built_paused != app->ui.user_paused || app->menus_built_running != game_running(app) ||
        app->menus_built_quick_used != app->quick_slot_used || app->menus_built_undo_save != app->undo_save_ready ||
        app->menus_built_undo_load != (app->undo_load_size > 0))
    {
        build_menus(app);   // GAME > PAUSE / RESUME, what shows with a game
    }
    app->session.timing.mode = piccolo_settings_timing(&app->settings);
    if (app->headless.on)
    {
        // No presents (a hidden window gets no frame callbacks): the
        // Exact timer paces the core, frames only go to the stream.
        app->session.timing.mode = TimingMode::exact;
    }
    app->session.timing.catch_up = settings_get_bool(&app->settings, PICCOLO_VIDEO_CATCH_UP);
    update_window(app);
    update_language(app);
    RetroUiPalette palette = {};
    piccolo_settings_palette(&app->settings, &palette);   // the Interface settings, live
    ui_toolkit_set_palette(&palette);
    {
        // The browser's page wears the theme too (a no-op elsewhere).
        PadLook pad_look = onscreen_pad_look(&app->settings);
        const float look[4] = {pad_look.fill, pad_look.border, pad_look.labels, pad_look.background};
        web_page_set_theme(&palette, look);
    }
    // While a game runs, black around its picture (the menu's backdrop covers
    // it in BACKGROUND at its transparency). Without one, the same color that
    // makes over black: BACKGROUND at 100 - TRANSPARENCY over black, so the
    // menu bar stands out as it does over a game (Andrés, testing round 3).
    uint32_t back = game_running(app) ? 0xFF000000u : background_over_black(&palette);
    ClearColor background = {static_cast<float>((back >> 16) & 0xFFu) / 255.0f,
                             static_cast<float>((back >> 8) & 0xFFu) / 255.0f, static_cast<float>(back & 0xFFu) / 255.0f,
                             1.0f};
    app_shell_set_frames_in_flight(&app->shell, piccolo_settings_frames_in_flight(&app->settings));
    // RUN-AHEAD: the setting, or a streaming device's tuning (in memory
    // only) when it isn't "host's".
    {
        int32_t run_ahead = settings_get_int(&app->settings, PICCOLO_LATENCY_RUN_AHEAD);
        if (stream_delay_overrides(app))
        {
            run_ahead = app->stream_tuning.run_ahead;
        }
        if (app->session.host != nullptr)
        {
            libretro_host_set_run_ahead(app->session.host, static_cast<unsigned>(run_ahead > 0 ? run_ahead : 0));
        }
    }
    // A streaming device's frame delay needs present waits (piccolo's
    // frame delay works only that way): turned on here, in memory only.
    // Never in the browser: on GL it's a glFinish after every swap, a stall
    // per frame there, and the browser paces presents itself (app_loop.h).
    app_shell_set_wait_for_present(&app->shell,
                                   app_loop_can_wait() &&
                                       (settings_get_bool(&app->settings, PICCOLO_LATENCY_WAIT_FOR_PRESENT) ||
                                        (stream_delay_overrides(app) &&
                                         app->stream_tuning.delay != NetStreamDelay::off)));
    piccolo_settings_audio(&app->settings, &app->session.audio_config);
    if (app->headless.on)
    {
        app->session.audio_config.muted = true;   // the device plays it
    }
    apply_learned_latency(app);
    // Held for the whole game, paused or not: a request is a D-Bus call
    // that may block, so not on every pause and resume.
    game_mode_set(&app->game_mode, game_running(app) && settings_get_bool(&app->settings, PICCOLO_GAME_MODE));
    core_session_update(&app->session, &app->shell, game_has_keyboard(app));
    if (app->option_cache.dirty)
    {
        save_core_options(app);   // a core declared new options: on disk before it can crash
    }
    if (app->marker_written && !app->marker_options_changed && app->option_store.dirty)
    {
        app->marker_options_changed = true;   // recorded in case this change brings the core down
        write_running_marker(app);
    }
    app_update_input(app);
    uint64_t frames_before = app->session.stats.frames;   // SHOW FPS counts what really ran (fast forward too)
    CoreSessionFrame ran = core_session_run_frame(&app->session, &app->shell, &app->cursor_input, game_has_keyboard(app));
    if (ran == CoreSessionFrame::ran)
    {
        net_stream_frame(app);
        measure_shader_check(app);
    }
    else if (app->stream_frame_owed)
    {
        net_stream_frame(app);   // paused (or the frame not due yet): the shown frame for a new viewer
    }
    // Taps (a key down and up inside one loop) are kept through a loop whose
    // core frame wasn't due yet, so that frame still sees them; otherwise
    // they're done. A paused game (the menu, focus loss) or a minimized
    // window runs no frame at all: kept there, a tap would stay "held" for
    // good, and the router ignores what's held as it enters a layer until
    // it's let go (Escape opened the menu, then neither Escape nor the
    // hotkey closed it: the browser, and the desktop whenever the menu pauses).
    // The DVR's window the same: the game waits (no pause cause), so a
    // LEFT let go stayed "held" and the cursor kept stepping (Andrés,
    // 2026-11-22). It reads its keys in app_update_input, earlier this loop.
    bool minimized = (SDL_GetWindowFlags(app->shell.window) & SDL_WINDOW_MINIMIZED) != 0;
    bool frame_coming = game_running(app) && !app->session.paused && !app->dvr.open && !minimized;
    if (ran != CoreSessionFrame::skipped || !frame_coming)
    {
        input_devices_end_frame(&app->input.devices);
    }
    // AUTO decided: kept as the game's REWIND KNOWN, so it's measured once.
    RewindMode known = RewindMode::automatic;
    if (dvr_widget_take_verdict(&app->dvr, &known))
    {
        settings_set(&app->settings, SettingLayer::game, PICCOLO_REWIND_KNOWN,
                     known == RewindMode::scrub ? "scrub" : "snapshots");
        save_settings(app);
    }
    if (ran == CoreSessionFrame::shut_down)
    {
        shader_check_end_for_game(app);   // before the settings context changes
        app->running_core_file[0] = '\0';
        set_status(app, app->session.message);
        ui_game_stopped(&app->ui);
        ui_follow_changes(app);
        update_settings_context(app);
        remove_running_marker(app);   // the core shut down on its own: a proper end
    }

    if (!app->headless.on && app_shell_begin_frame(&app->shell))
    {
        float screen_width = 0.0f;
        float screen_height = 0.0f;
        ui_toolkit_screen_size(&screen_width, &screen_height);
        app->ui_scale = app->phone_ui ? ui_toolkit_scale_for_phone(screen_width, screen_height)
                                      : ui_toolkit_scale_for_screen(screen_width, screen_height);
        ui_toolkit_set_scale(app->ui_scale);
        // A phone turned (or the window resized): the open windows back to
        // the middle, or they'd keep the old one, half off the screen. Not on
        // the first frame (nothing is up yet).
        bool resized = screen_width != app->ui_display_width || screen_height != app->ui_display_height;
        if (resized && app->ui_display_width > 0.0f)
        {
            ui_toolkit_center_windows();
        }
        app->ui_display_width = screen_width;
        app->ui_display_height = screen_height;

        // The UI: what moves on its own, what the frame shows (plain data),
        // all of it drawn at once (ui.h), then what the user did applied.
        AppUiFrame ui_frame;
        UiView view;
        view.screen_width = screen_width;
        view.screen_height = screen_height;
        app_ui_update(app, &ui_frame, &view);
        app_ui_view(app, &ui_frame, &view);
        UiWindows windows;
        app_ui_windows(app, &windows);
        UiActions actions;
        ui_draw(&view, &windows, app->pixel_font, app->ui_scale, &actions);
        app_apply_ui(app, &view, &ui_frame, &actions);

        dvr_widget_upload(&app->dvr, &app->session, &app->shell);   // a thumbnail (moves the core and back)
        core_session_upload_video(&app->session, &app->shell);
        quick_menu_upload(&app->quick_menu, &app->shell);
        continue_panel_upload(&app->continue_panel, &app->shell);
        guest_window_upload(&app->guest_window, &app->shell);
        // The loop's work, before the display may make it wait (acquire / present).
        uint64_t now_ns = SDL_GetTicksNS();
        // Every core frame this loop ran: catch-up frames (a late loop runs
        // them too) and fast forward's many (Andrés, testing round 3: SHOW FPS
        // said 60 while fast forwarding).
        uint64_t frames_run = app->session.stats.frames >= frames_before ? app->session.stats.frames - frames_before : 0;
        uint32_t core_frames = ran == CoreSessionFrame::ran ? static_cast<uint32_t>(frames_run) : 0;
        fps_meter_loop(&app->fps_meter, now_ns, core_frames, now_ns - loop_start_ns);
        if (app_shell_acquire(&app->shell))
        {
            video_output_record(&app->video, &app->shader_library, &app->shell, &app->session.texture);
        }
        collect_screenshots(app);
        if (!app_shell_present(&app->shell, background))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal rendering error, exiting");
            return false;
        }
        if (app_loop_can_wait())
        {
            frame_delay(app);
        }
    }
    if (app_loop_can_wait())
    {
        core_session_wait(&app->session);   // the browser paces its own frames (app_loop.h)
    }
    return app->running;
}

static bool app_frame_callback(void* data)
{
    return app_frame(static_cast<App*>(data));
}

// The first core in the catalog that takes `path` (by its extension), or nullptr.
static const CoreCatalogEntry* core_for_file(const App* app, const char* path)
{
    for (int32_t i = 0; i < app->catalog.count; i++)
    {
        if (libretro_core_info_accepts(&app->catalog.entries[i].info, path))
        {
            return &app->catalog.entries[i];
        }
    }
    return nullptr;
}

// A file the page opened (app_loop_on_open, the browser): kept in
// <piccolo>/games (so RECENT finds it again after a reload), then started
// with the first core that takes its extension, like LOAD GAME would.
static void app_open_file(void* data, const char* path)
{
    App* app = static_cast<App*>(data);
    std::string name = path;
    size_t slash = name.find_last_of('/');
    if (slash != std::string::npos)
    {
        name = name.substr(slash + 1);
    }
    std::string kept = app->paths.root.empty() ? std::string(path) : app->paths.root + "/games/" + name;
    if (kept != path)
    {
        std::string folder = app->paths.root + "/games";
        if (!host_fs_make_directories(folder.c_str()) || !host_fs_copy(path, kept.c_str()))
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Can't keep %s in %s: playing it from where it is", path,
                         folder.c_str());
            kept = path;
        }
    }
    // The file itself, or (a zip / 7z) the first file inside that a core takes:
    // "<archive>#<inside>", the path LOAD GAME gives for a file in an archive.
    std::string content = kept;
    const CoreCatalogEntry* core = core_for_file(app, kept.c_str());
    if (core == nullptr && archive_format(kept.c_str()) != ArchiveFormat::none)
    {
        std::vector<ArchiveEntry> entries;
        archive_list(kept.c_str(), &entries);   // failure: logged
        for (size_t i = 0; i < entries.size() && core == nullptr; i++)
        {
            if (!entries[i].is_directory)
            {
                core = core_for_file(app, entries[i].name.c_str());
                content = kept + "#" + entries[i].name;
            }
        }
    }
    if (core == nullptr)
    {
        set_status(app, piccolo_text(&app->strings, Str::status_no_core_for_file));
        return;
    }
    app->pending_start = FilePickerChoice{};
    core_catalog_path(&app->catalog, core, app->pending_start.core_path, sizeof(app->pending_start.core_path));
    std::snprintf(app->pending_start.content_path, sizeof(app->pending_start.content_path), "%s", content.c_str());
    app->has_pending_start = true;   // started at the top of the next frame, like LOAD GAME
}

// The browser is leaving (app_loop_on_leave): what piccolo writes at exit,
// written now, since its exit never runs there. Hidden: the game keeps
// running if the page comes back. Unloading: a proper end, so the running
// marker goes too (or the next visit would think the core crashed).
static void app_save_on_leave(void* data, bool unloading)
{
    App* app = static_cast<App*>(data);
    if (app->session.host != nullptr)
    {
        libretro_host_save_memory(app->session.host);
    }
    save_settings(app);
    save_core_options(app);
    if (unloading)
    {
        remove_running_marker(app);
    }
    app_storage_flush();
}

int main(int argc, char** argv)
{

    SDL_Log("piccolo %s, host-libretro %s", PICCOLO_VERSION, HOST_LIBRETRO_VERSION);
    app_storage_init(bundled_files_folder_name());   // the browser: piccolo's folder on OPFS

    // The session, catalog and file picker are large (paths, file lists): off the stack.
    App* app = new App{};

    // A launch with a piccolo:// link hands it to the copy already running
    // and quits before any window; otherwise this copy takes the links.
    if (!app_links_start(&app->links, "piccolo", app_links_from_args(argc, argv, "piccolo")))
    {
        delete app;
        return 0;
    }
    app_links_register("piccolo", "piccolo");   // Windows: this program opens piccolo:// links (per user)
    // Android: the back button comes to piccolo (app_back_button) instead of
    // leaving the app (SDLActivity reads this when it's pressed).
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");

    AppShellConfig config = {};
    config.name       = "piccolo";
    config.identifier = "site.multitap.piccolo";
    config.icons      = app_icon_images;
    config.icon_count = app_icon_image_count;

    // Desktop testing of the phone UI (Andrés, 2026-11-25): --mobile opens a
    // phone-shaped window with HOME, ☰ and the windows as pages (Android
    // always has them); --landscape turns it on its side (ROTATE does too).
    // --menu NAME runs a menu item at start, by its log name (menus.cpp:
    // SOUND, LOAD_GAME); --phone-menu opens ☰; --continue-preview CASE shows
    // DEBUG's fake CONTINUE card (resume, no_states, newer_save, vertical).
    // For trying screens out.
    bool landscape = false;
    const char* start_menu = nullptr;
    const char* continue_preview = nullptr;
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--mobile") == 0)
        {
            app->phone_ui = true;
            app->window.phone_window = !PICCOLO_MOBILE;
        }
        else if (std::strcmp(argv[i], "--landscape") == 0)
        {
            landscape = true;
        }
        else if (std::strcmp(argv[i], "--phone-menu") == 0)
        {
            app->mobile_home.menu_open = true;
        }
        else if (std::strcmp(argv[i], "--menu") == 0 && i + 1 < argc)
        {
            start_menu = argv[i + 1];
            i++;
        }
        else if (std::strcmp(argv[i], "--continue-preview") == 0 && i + 1 < argc)
        {
            continue_preview = argv[i + 1];
            i++;
        }
    }
    if (app->window.phone_window)
    {
        // piccoletto's desktop window: a phone's shape, 2 window pixels per
        // retro pixel (its short side holds touch_ui's 195).
        config.window_width  = landscape ? 900 : 420;
        config.window_height = landscape ? 420 : 900;
        SDL_Log("Phone UI: %s window", landscape ? "landscape" : "portrait");
    }

    remap_widget_create(&app->remap);
    if (!pixel_font_init() || !core_session_create(&app->session))
    {
        delete app;
        return 1;
    }
    if (!app_shell_create(&app->shell, &config))
    {
        app_shell_destroy(&app->shell);
        core_session_destroy(&app->session, &app->shell);
        delete app;
        return 1;
    }
    // A build from a branch other than main has the branch in its version ("0.3-osk"):
    // show it in the window title, so a test build says which branch opened it.
    constexpr std::string_view version = PICCOLO_VERSION;
    if (version.find('-') != std::string_view::npos)
    {
        SDL_SetWindowTitle(app->shell.window, "piccolo " PICCOLO_VERSION);
    }
    app_loop_prepare_window(app->shell.window);
    app->pixel_font = ui_toolkit_start();
    ui_toolkit_set_phone(app->phone_ui);
    if (app->pixel_font == nullptr)
    {
        core_session_destroy(&app->session, &app->shell);
        app_shell_destroy(&app->shell);
        delete app;
        return 1;
    }

    if (!settings_init(&app->settings, piccolo_settings_table(), piccolo_settings_count()))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "piccolo's settings table is broken (see above)");
    }
    piccolo_settings_check_ui_keys(&app->settings);
    if (!localization_init(&app->strings, piccolo_strings_table(), text_id(Str::count)))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "piccolo's strings table is broken (see above)");
    }

    // Everything in piccolo's folder, migrated once from ~/.host-libretro.
    // Without it nothing is saved, but games still run.
    if (app_paths_init(&app->paths, bundled_files_folder_name(), "piccolo.cfg"))
    {
        bundled_files_install_cores(app->paths.cores.c_str());   // before the catalog first looks
        ui_toolkit_set_layout_file((app->paths.settings + "/imgui.ini").c_str());
        app->settings_path = app->paths.settings + "/piccolo.cfg";
        if (settings_load(&app->settings, app->settings_path.c_str()))
        {
            // Before anything reads the game folders (apply_folders, the share paths).
            piccolo_settings_compact_game_folders(&app->settings);
        }
        else
        {
            app->settings_unreadable = true;
            app->settings_error_open = true;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s exists but could not be read: defaults for this run, nothing is saved over it",
                         app->settings_path.c_str());
        }
        apply_folders(app);
        load_last_used(app);
        app->recent_path = app->paths.settings + "/recent.cfg";
        app->recent_unreadable = !recent_games_load(&app->recent, app->recent_path.c_str());
        continue_panel_load(&app->continue_panel, (app->paths.settings + "/continue.cfg").c_str());
        app->audio_learned_path = app->paths.settings + "/audio_latency.cfg";
        settings_file_load(&app->audio_learned, app->audio_learned_path.c_str());
        app->running_marker_path = app->paths.settings + "/running.cfg";
        check_running_marker(app);
        app->option_store_path = app->paths.settings + "/core_options.cfg";
        app->option_cache_path = app->paths.settings + "/core_options_cache.cfg";
        app->core_options_unreadable = !core_options_store_load(&app->option_store, app->option_store_path.c_str());
        core_options_cache_load(&app->option_cache, app->option_cache_path.c_str());
        app->session.option_store = &app->option_store;
        app->session.option_cache = &app->option_cache;
        app->frame_delay_saved_path = app->paths.settings + "/frame_delay.cfg";
        settings_file_load(&app->frame_delay_saved, app->frame_delay_saved_path.c_str());
        piccolo_window_restore(&app->window, app->shell.window, &app->settings);
    }
    else
    {
        ui_toolkit_set_layout_file(nullptr);   // nowhere to keep it
    }

    // The files that come with piccolo (layouts, themes, shaders): next to the
    // program; on Android a copy out of the APK (bundled_files.h).
    std::string program_folder = bundled_files_data_folder();

    // librashader (shader presets): next to the program, in piccolo's folder,
    // or wherever the system finds it (on Android: the APK's native
    // libraries, where dlopen looks for a bare name). Without it, presets are off.
    std::vector<std::string> shader_library_folders;
    const char* program_folder_for_shaders = SDL_GetBasePath();   // SDL owns it
    if (program_folder_for_shaders != nullptr)
    {
        shader_library_folders.push_back(program_folder_for_shaders);
    }
    shader_library_folders.push_back(app->paths.root);
    shader_library_load(&app->shader_library, shader_library_folders);
    // piccolo's own presets: the user's copy in piccolo's folder wins over
    // the ones that come with piccolo (next to the program), like lang/.
    {
        // OpenGL ES: our own presets, in shaders/gl/ (same layout).
        const char* subfolder = app->shell.api == GpuApi::gl ? "shaders/gl/" : "shaders/";
        std::string user_folder = app->paths.root.empty() ? std::string() : app->paths.root + "/" + subfolder;
        std::string bundled_folder = std::string(program_folder_for_shaders != nullptr ? program_folder_for_shaders : "") +
                                     subfolder;
        host_fs_forward_slashes(bundled_folder.data());
        const char* vulkan_names[4] = {"pass0.slangp", "scale-nearest.slangp", "scale-bilinear.slangp",
                                       "scale-sharp-bilinear.slangp"};
        const char* gl_names[4] = {"pass0.glpreset", "scale-nearest.glpreset", "scale-bilinear.glpreset",
                                   "scale-sharp-bilinear.glpreset"};
        const char* const* names = app->shell.api == GpuApi::gl ? gl_names : vulkan_names;
        std::string* targets[4] = {&app->shader_pass0_path, &app->shader_scale_paths[0], &app->shader_scale_paths[1],
                                   &app->shader_scale_paths[2]};
        for (uint32_t i = 0; i < 4; i++)
        {
            std::string user_copy = user_folder.empty() ? std::string() : user_folder + names[i];
            *targets[i] = !user_copy.empty() && host_fs_exists(user_copy.c_str()) ? user_copy : bundled_folder + names[i];
        }
        // The curated presets: the copy in piccolo's folder when the build
        // put one there, else the one next to the program. On OpenGL ES they
        // come from deps/piccolo-shaders-legacy (the build copies them to
        // shaders/gl/presets, like deps/piccolo-shaders to shaders/presets).
        std::string user_presets = user_folder.empty() ? std::string() : user_folder + "presets";
        app->shader_presets_folder = !user_presets.empty() && host_fs_exists(user_presets.c_str()) ? user_presets
                                                                                                  : bundled_folder + "presets";
    }

    // Controllers: MAP CONTROLLER's mappings are in their profiles (input.cfg);
    // settings/gamecontrollerdb.txt, if the user drops one in (SDL's format),
    // adds to the ones SDL knows.
    std::string database_path = app->paths.settings.empty() ? "" : app->paths.settings + "/gamecontrollerdb.txt";
    // Controller layouts: the ones next to the program, then piccolo's folder's
    // (the same id there wins), and GENERIC built in if neither has it.
    std::string bundled_layouts = program_folder + "layouts";
    host_fs_forward_slashes(bundled_layouts.data());
    input_layouts_load_directory(&app->layouts, bundled_layouts.c_str());
    if (!app->paths.root.empty())
    {
        input_layouts_load_directory(&app->layouts, (app->paths.root + "/layouts").c_str());
    }
    input_layouts_ensure_generic(&app->layouts);
    // Interface themes from files: the ones next to the program, then piccolo's
    // folder's (the same name there wins). The built-in ones are in retro_ui.cpp.
    std::string bundled_themes = program_folder + "themes";
    host_fs_forward_slashes(bundled_themes.data());
    piccolo_themes_load_directory(&app->themes, bundled_themes.c_str());
    if (!app->paths.root.empty())
    {
        piccolo_themes_load_directory(&app->themes, (app->paths.root + "/themes").c_str());
    }
    // The interface's colors from the theme piccolo.cfg names (never its own colors).
    // It applies the theme's colors with settings_set, which marks the settings dirty although
    // those colors are never written (piccolo_settings_save strips them): that alone must not
    // save at every start. Dirty stays only when it changed ui_theme itself (an old file's colors
    // became a saved theme). The text is copied: the setting's pointer may move.
    bool settings_dirty_before_theme = app->settings.dirty;
    std::string ui_theme_before = settings_get_text(&app->settings, PICCOLO_UI_THEME);
    piccolo_settings_start_ui_theme(&app->settings, &app->themes,
                                    app->paths.root.empty() ? "" : (app->paths.root + "/themes").c_str());
    if (ui_theme_before == settings_get_text(&app->settings, PICCOLO_UI_THEME))
    {
        app->settings.dirty = settings_dirty_before_theme;
    }
    // settings/input.cfg first: controllers are made from its profiles as their devices appear.
    app->input_config_path = app->paths.settings.empty() ? "" : app->paths.settings + "/input.cfg";
    if (!app->input_config_path.empty())
    {
        app->input_config_unreadable = !input_config_load(&app->input, app->input_config_path.c_str());
        build_settings_error_files(app);
    }
    input_sdl_init(&app->input_sdl, &app->input, database_path.empty() ? nullptr : database_path.c_str());
    onscreen_pad_init(&app->onscreen_pad, app->paths.settings.c_str());
    web_pad_attach(&app->input);   // the browser: the page's gamepad goes into this router
    const char* show_fps = SDL_getenv("PICCOLO_SHOW_FPS");
    app->fps_from_env = show_fps != nullptr && show_fps[0] == '1';
    update_language(app);

    // SDL's audio subsystem from the start (no device is opened until a core
    // runs): the Sound tab lists the devices and follows hotplug.
    app->audio_ready = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!app->audio_ready)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Audio subsystem: %s", SDL_GetError());
    }
    refresh_audio_devices(app);

    if (start_menu != nullptr)
    {
        // "LOAD_GAME" works as "LOAD GAME" (no quotes needed in a shell).
        char wanted[64] = {};
        std::snprintf(wanted, sizeof(wanted), "%s", start_menu);
        for (size_t i = 0; wanted[i] != '\0'; i++)
        {
            wanted[i] = wanted[i] == '_' ? ' ' : wanted[i];
        }
        bool found = false;
        for (int32_t i = 0; i <= static_cast<int32_t>(MenuAction::separator); i++)
        {
            MenuAction action = static_cast<MenuAction>(i);
            if (SDL_strcasecmp(menu_action_name(action), wanted) == 0)
            {
                handle_menu_action(app, action);
                found = true;
            }
        }
        if (!found)
        {
            SDL_Log("--menu %s: no such menu item", start_menu);
        }
    }
    if (continue_preview != nullptr)
    {
        const char* names[] = {"resume", "no_states", "newer_save", "vertical"};
        const ContinuePreview previews[] = {ContinuePreview::resume, ContinuePreview::no_states,
                                            ContinuePreview::newer_save, ContinuePreview::vertical};
        bool found = false;
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        {
            if (SDL_strcasecmp(names[i], continue_preview) == 0)
            {
                continue_panel_preview(&app->continue_panel, previews[i]);
                found = true;
            }
        }
        if (!found)
        {
            SDL_Log("--continue-preview %s: no such card", continue_preview);
        }
    }

    app->running = true;
    app_loop_on_leave(app_save_on_leave, app);
    app_loop_on_open(app_open_file, app);
    app_loop_run(app_frame_callback, app);

    headless_leave(&app->headless, app->shell.window);   // the tray icon goes, the window comes back for its size
    piccolo_window_capture(&app->window, app->shell.window);
    save_last_used(app);
    save_settings(app);
    save_learned_latency(app);
    save_core_options(app);
    core_updater_shutdown(&app->core_updater);   // cancels a download in progress
    if (game_running(app))
    {
        save_state_on_exit(app);
    }
    core_session_destroy(&app->session, &app->shell);   // writes save RAM
    dvr_widget_destroy(&app->dvr, &app->shell);
    remove_running_marker(app);   // quit properly (a marker found at launch stays if no game ran)
    game_mode_set(&app->game_mode, false);
    if (app->audio_ready)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    screenshot_writer_destroy(&app->screenshot_writer);   // writes what's still waiting
    quick_menu_destroy(&app->quick_menu, &app->shell);
    continue_panel_destroy(&app->continue_panel, &app->shell);
    guest_window_destroy(&app->guest_window, &app->shell);
    video_output_destroy(&app->video, &app->shader_library, &app->shell);
    shader_library_unload(&app->shader_library);
    if (!app->input_config_path.empty())
    {
        save_input_config(app);
    }
    input_sdl_shutdown(&app->input_sdl);
    onscreen_pad_shutdown(&app->onscreen_pad);
    net_shutdown_all(app);
    app_shell_destroy(&app->shell);
    app_links_stop(&app->links);
    delete app;
    return 0;
}
