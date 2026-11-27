#include "app_ui.h"

#include "app_continue.h"
#include "app_debug.h"
#include "app_files.h"
#include "app_game.h"
#include "app_guest.h"
#include "app_input.h"
#include "app_menus.h"
#include "app_net.h"
#include "app_picture.h"
#include "app_screenshots.h"
#include "app_settings.h"
#include "app_shaders.h"
#include "app_share.h"
#include "app_states.h"
#include "app_ui_modes.h"

// ---------------------------------------------------------------------------
// The frame's UI: updates, view, (ui_draw), apply. None of these draw: a
// different GUI toolkit would leave them as they are.
// ---------------------------------------------------------------------------

// Before the view: what moves on its own each frame (placing the picture and
// the touch gamepad, the windows' polling of jobs and connections), in the
// order the loop always did it.
void app_ui_update(App* app, AppUiFrame* frame, UiView* view)
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
    // STYLE (per core / game): made again only when the setting changes.
    {
        const char* style = settings_get_text(&app->settings, PICCOLO_TOUCH_PAD_STYLE);
        if (app->onscreen_pad.style_name != style)
        {
            onscreen_pad_set_style(&app->onscreen_pad, find_layout_by_name(&app->layouts, style), style);
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
    // CONTROLLERS from anywhere: the page's pad shows in the menus too.
    if (web_pad_take_controllers())
    {
        input_window_open(&app->input_window);   // (no CONTROLLERS window yet: INPUT, as the touch pad's)
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

// Everything the frame shows, as plain data (ui.h), after the updates: no
// GUI calls, nothing changes but what the states themselves fill in.
void app_ui_view(App* app, AppUiFrame* frame, UiView* view)
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
void app_ui_windows(App* app, UiWindows* windows)
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

// What the user did this frame, applied in the order the windows are drawn
// (no GUI calls). What a window changes here, windows drawn after it see
// from the next frame.
void app_apply_ui(App* app, const UiView* view, const AppUiFrame* frame, UiActions* actions)
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
        // CONTROLLERS: there is no window of that name yet; INPUT (where the
        // controllers are set up) opens in its place, a MAIN MENU window
        // like any opened by its module (ui_sync_windows enters MAIN MENU).
        input_window_open(&app->input_window);
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
