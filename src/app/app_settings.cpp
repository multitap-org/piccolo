// The glue of SETTINGS, CORE OPTIONS and CORES: what the windows show,
// from the App, and what their choices change.

#include "app_settings.h"

#include "app_files.h"
#include "app_game.h"
#include "app_picture.h"
#include "app_share.h"

// --- Choosing a folder ------------------------------------------------------

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

void choose_folder_done(App* app, const FilePickerChoice* picked)
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

// --- SETTINGS ---------------------------------------------------------------

// SETTINGS' state: its notes as text, IMAGE SETTINGS' picture, and the
// NETWORK tab (read under the network lock).
void settings_view(App* app, UiView* view, const AppUiFrame* frame)
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
void settings_apply(App* app, const AppUiFrame* frame, SettingsWindowAction* action)
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

// --- CORE OPTIONS and CORES -------------------------------------------------

// What the core options UI (CORE CONFIGURATION's options, the manager) shows this frame.
void core_options_state(App* app, CoreOptionsUiState* state)
{
    const std::string& folder_section = app->option_store.directory_section;
    size_t folder_at = folder_section.find("/dir:");
    const char* folder = folder_at != std::string::npos ? folder_section.c_str() + folder_at + 5 : "";
    core_options_ui_state(state, &app->strings, &app->session, &app->option_store, &app->option_cache,
                          game_running(app) ? app->core_name : "", folder, app->game_name);
}

// TOOLS > CORES: the downloader is set up again on every open (unless it's
// busy), so a changed CORE SERVER or cores folder applies.
void open_cores_window(App* app)
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
