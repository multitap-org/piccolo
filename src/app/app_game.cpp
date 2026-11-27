// A game's life: starting it (from a file, a menu, another device), its
// pause, and the actions on it while it runs.

#include "app_game.h"

#include "app_files.h"
#include "app_menus.h"
#include "app_shaders.h"
#include "app_states.h"
#include "app_ui_modes.h"

// --- What runs, and telling the player --------------------------------------

bool game_running(const App* app)
{
    return core_session_running(&app->session);
}

// The running core asked for a string (TEXT): its box is up, the game paused.
bool text_input_pending(const App* app)
{
    return game_running(app) && app->session.host->text_input.mode == RETRO_EXT_TEXT_INPUT_TEXT;
}

// A message for the player: a HUD card for a few seconds (message_widget.h).
// (The name is the status line's, which these went to before.)
void set_status(App* app, const char* text)
{
    message_widget_post(&app->messages, text, SDL_GetTicks());
}

// What the core session reports after an action (a state saved, a game
// started): its failures always; its successes only when `announce` (Andrés,
// testing round 3: a hotkey or another device's action, where no menu shows
// what happened).
void report_session(App* app, bool ok, bool announce)
{
    if (!ok || announce)
    {
        set_status(app, app->session.message);
    }
}

// --- Pause ------------------------------------------------------------------

// The USER pause cause (the PAUSE hotkey, QUICK MENU's PAUSE, GAME > PAUSE):
// only the player lifts it. Paused, the PAUSE icon says so; lifted, PLAY
// shows a moment (no message: testing round 3).
void toggle_user_pause(App* app)
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

// The game pauses for the causes ui_derive() finds (ui_mode.h), and its
// queued audio is dropped so resuming doesn't play stale sound.
void update_pause(App* app)
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

// --- Starting a game --------------------------------------------------------

// Files extracted from zip / 7z archives (content_cache.h) are kept up to
// this much: room for several disc images, small next to what the games
// themselves take. Trimmed after each game starts.
constexpr uint64_t PICCOLO_CONTENT_CACHE_MAX_BYTES = 4ull * 1024ull * 1024ull * 1024ull;

// True if the game started (false also when it waits for SAFE START).
bool start_game(App* app, const FilePickerChoice* choice)
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
void app_open_file(void* data, const char* path)
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

// The actions on the running game that the GAME menu and QUICK MENU share,
// each in one place. Where the UI goes afterwards is the caller's: the
// menu goes back to the game, QUICK MENU may stay.

// SOFT RESET: the core's own reset (the console's reset button).
bool soft_reset(App* app)
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
bool hard_reset(App* app)
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

// CLOSE GAME: at the top of the next loop (the game's texture may be in this
// frame already).
void close_game(App* app)
{
    if (game_running(app))
    {
        app->pending_close_game = true;
    }
}

// TOOLS > GAME DVR, QUICK MENU's GAME DVR: the rewind DVR, a window widget
// (it refuses, its HUD saying why, with nothing to go back to).
void open_game_dvr(App* app)
{
    app_open_widget(app, UiWidget::dvr);
}
