// Shader presets: loading the stages, the question when a preset is too
// slow (shader_check.h), and the preset picker.

#include "app_shaders.h"

#include "app_files.h"

// --- The stages -------------------------------------------------------------

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

// Loads what each shader stage should hold when it changes: stage 0 = pass 0,
// stage 1 = the preset setting, or piccolo's final scale for `filter` when
// there's none (or it failed). Then pass 0's parameters from the settings,
// only the ones that changed. A preset that fails isn't retried every frame.
void update_shader_stages(App* app, ViewportFilter filter)
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

// --- The slow preset question -----------------------------------------------

// The slow shader question is up (shader_check.h).
bool shader_check_asking(const App* app)
{
    return app->shader_check.state == ShaderCheckState::asking;
}

// KEEP: nothing changes. REVERT: every layer's preset setting as before the
// change, saved. Either way the check is over until the next change.
void shader_check_answer(App* app, ShaderCheckChoice choice)
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
void shader_check_end_for_game(App* app)
{
    if (shader_check_asking(app))
    {
        shader_check_answer(app, ShaderCheckChoice::revert);
    }
}

// Once per loop that ran core frames: times the loop for the check. The
// target is a display refresh in Vsync timing (each loop presents one), a
// core frame in Exact. Fast forward is fast on purpose: not a sample.
void measure_shader_check(App* app)
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

// --- The preset picker ------------------------------------------------------

// CORE CONFIGURATION > SHADER > CHOOSE: the picker opens in the
// chosen preset's folder, else in the curated presets. What's picked goes to
// `scope`, the scope the QUICK MENU's SHADER tab edits.
void open_shader_picker(App* app, SettingLayer scope)
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
void shader_picker_apply(App* app, const FilePickerAction* action)
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
