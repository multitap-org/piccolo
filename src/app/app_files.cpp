// The files piccolo reads at start and keeps up to date: the last used core
// and folder, the settings (and the guard for the ones that could not be
// read), each core's learned latency, the running marker behind SAFE START,
// and the frame delays found per game.

#include "app_files.h"

#include "app_game.h"

// --- Last used core and folder (<settings>/last_used.cfg) -------------------

static std::string last_used_file(const App* app)
{
    return app->paths.settings + "/last_used.cfg";
}

void save_last_used(const App* app)
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

void load_last_used(App* app)
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

// --- Settings: folders, saving, the files that could not be read ------------

// A folder setting, or the program folder's own when it's empty.
std::string folder_setting(const App* app, const char* key, const std::string& default_folder)
{
    const char* value = settings_get_text(&app->settings, key);
    return value[0] != '\0' ? std::string(value) : default_folder;
}

// Folders from the settings: the catalog's cores folder now, the others for
// the next game the session starts.
void apply_folders(App* app)
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

void save_input_config(App* app)
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
void build_settings_error_files(App* app)
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

void save_settings(App* app)
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
void update_settings_context(App* app)
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

void save_core_options(App* app)
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

// The quick menu or the options manager closed: what was changed there in
// the core options is written now. (While they are open it waits, so an
// option that crashes the core at once is not kept.)
void save_after_options_windows(App* app)
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

// --- AUTO latency, learned per core -----------------------------------------

// AUTO latency per core: a game starts its automatic target where this core
// ended last time, and where it goes is recorded as it moves (saved with the
// other state: at each game start and on quit).
// A learned AUTO latency at or above this is taken for an anomaly, not this
// core's need, and learned again (AUTO settles around 50 ms on the RK3588,
// 112 ms at worst without GameMode).
constexpr uint32_t LEARNED_LATENCY_ANOMALY_MS = 120;

void apply_learned_latency(App* app)
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

void save_learned_latency(App* app)
{
    if (app->audio_learned_dirty && !app->audio_learned_path.empty())
    {
        settings_file_save(&app->audio_learned, app->audio_learned_path.c_str(),
                           "piccolo: automatic audio latency learned per core (ms)");
        app->audio_learned_dirty = false;
    }
}

// --- The running marker (SAFE START) ----------------------------------------

// settings/running.cfg: a game session is on (written before the core
// loads, so a crash while loading is noticed too).
void write_running_marker(App* app)
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
void remove_running_marker(App* app)
{
    if (app->marker_written)
    {
        SDL_RemovePath(app->running_marker_path.c_str());
        app->marker_written = false;
    }
    app->marker_options_changed = false;
}

// At launch: a marker left behind means the last session didn't end.
void check_running_marker(App* app)
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
bool needs_safe_start(App* app, const FilePickerChoice* choice)
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

// --- Frame delays found per game (frame_delay.cfg) --------------------------

// This game's section in frame_delay.cfg: "core/game" ("core" without content).
std::string frame_delay_section(const App* app)
{
    std::string section = app->core_name;
    if (app->game_name[0] != '\0')
    {
        section += "/";
        section += app->game_name;
    }
    return section;
}

void save_frame_delay_file(App* app)
{
    if (!app->frame_delay_saved_path.empty())
    {
        settings_file_save(&app->frame_delay_saved, app->frame_delay_saved_path.c_str(),
                           "piccolo: automatic frame delay found per game (ms)");
    }
}
