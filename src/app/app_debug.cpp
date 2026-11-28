// The debug window's glue (MENU > DEBUG): what it shows and what it forces.

#include "app_debug.h"

#include "app_game.h"
#include "app_shaders.h"
#include "app_states.h"
#include "app_ui_modes.h"

// --- The debug window -------------------------------------------------------

// The debug window's text box answers here (a fake TEXT request).
static void debug_text_done(const char* text)
{
    SDL_Log("Debug text box: %s", text != nullptr ? text : "(cancelled)");
}

// The debug window (debug_window.h): what it shows, from the App.
static_assert(DEBUG_WINDOW_ICON_COUNT == STATE_ICON_COUNT, "the debug window forces each state icon");
void debug_window_state(App* app, DebugWindowState* state)
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
void debug_window_apply(App* app, const DebugWindowAction* action)
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
