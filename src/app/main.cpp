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

#include "app.h"
#include "app_files.h"
#include "app_game.h"
#include "app_input.h"
#include "app_menus.h"
#include "app_net.h"
#include "app_net_remote.h"
#include "app_picture.h"
#include "app_screenshots.h"
#include "app_shaders.h"
#include "app_states.h"
#include "app_ui.h"
#include "app_ui_modes.h"

#include <SDL3/SDL_main.h>

// --- The loop ---------------------------------------------------------------

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
        app_game_ending(app);
        core_session_stop(&app->session, &app->shell);   // writes save RAM
        app_game_ended(app);
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
        // The core shut down on its own: a proper end (its state can't be saved any more).
        app_game_ending(app);
        set_status(app, app->session.message);
        app_game_ended(app);
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

// --- Start and quit ---------------------------------------------------------

// Everything piccolo needs before its loop: the link hand-over, the window,
// the settings and the other files, shaders, controllers, sound. False when
// piccolo can't go on (or handed its link to the copy already running:
// link_handed_over); app_shutdown() then undoes what was made.
static bool app_startup(App* app, int argc, char** argv)
{
    // A launch with a piccolo:// link hands it to the copy already running
    // and quits before any window; otherwise this copy takes the links.
    if (!app_links_start(&app->links, "piccolo", app_links_from_args(argc, argv, "piccolo")))
    {
        app->link_handed_over = true;
        return false;
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
        return false;
    }
    app->shell_tried = true;
    if (!app_shell_create(&app->shell, &config))
    {
        return false;
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
        return false;
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

    app->started = true;
    return true;
}

// The end of every run, however far app_startup() got: after a full start,
// what piccolo writes at exit and everything it made, in order; after a
// start that stopped early, only the session, the window and the links.
static void app_shutdown(App* app)
{
    if (app->started)
    {
        headless_leave(&app->headless, app->shell.window);   // the tray icon goes, the window comes back for its size
        piccolo_window_capture(&app->window, app->shell.window);
        app_game_ending(app);   // before the settings are saved: an unanswered slow preset is reverted
        save_last_used(app);
        save_settings(app);
        save_learned_latency(app);
        core_updater_shutdown(&app->core_updater);   // cancels a download in progress
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
    }
    core_session_destroy(&app->session, &app->shell);   // (gone already after a full start: nothing happens)
    if (app->shell_tried)
    {
        app_shell_destroy(&app->shell);
    }
    if (!app->link_handed_over)
    {
        app_links_stop(&app->links);
    }
    delete app;
}

int main(int argc, char** argv)
{
    SDL_Log("piccolo %s, host-libretro %s", PICCOLO_VERSION, HOST_LIBRETRO_VERSION);
    app_storage_init(bundled_files_folder_name());   // the browser: piccolo's folder on OPFS

    // The session, catalog and file picker are large (paths, file lists): off the stack.
    App* app = new App{};
    bool started = app_startup(app, argc, argv);
    if (started)
    {
        app->running = true;
        app_loop_on_leave(app_save_on_leave, app);
        app_loop_on_open(app_open_file, app);
        app_loop_run(app_frame_callback, app);
    }
    int result = started || app->link_handed_over ? 0 : 1;
    app_shutdown(app);
    return result;
}
