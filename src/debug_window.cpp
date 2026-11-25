// The debug window drawn with retroui (debug_window.h). No toolkit calls.

#include "debug_window.h"

#include "retro_ui.h"
#include "retro_ui_controls.h"

static void draw_about(const DebugWindowState* state)
{
    retro_ui_text_format("piccolo %s, host-libretro %s", state->piccolo_version, state->host_version);
    retro_ui_text_format("GPU: %s", state->gpu_device);
    retro_ui_text(state->gpu_api);
    retro_ui_text_format("Swapchain: %s", state->gpu_swapchain);
    retro_ui_text(state->gpu_validation);
    retro_ui_text_format("Retro UI scale: %dx", static_cast<int>(state->ui_scale));
    retro_ui_separator();
    double frame_ms = state->screen_hz > 0.0 ? 1000.0 / state->screen_hz : 0.0;
    retro_ui_text_format("%.3f ms/frame (%.1f FPS), %.1f ms of work", frame_ms, state->screen_hz, state->work_ms);
    if (!state->game_running)
    {
        return;
    }
    retro_ui_text_format("Core: %s, %.3f fps, timing %s", state->core_name, state->core_fps, state->timing_mode);
    retro_ui_text_format("Audio: x%.5f, %u ms queued (target %.0f ms%s), %llu underruns", state->audio_ratio,
                         static_cast<unsigned>(state->audio_queued_ms), state->audio_target_ms,
                         state->audio_automatic ? ", auto" : "",
                         static_cast<unsigned long long>(state->audio_underruns));
    retro_ui_text_format("Frames: %llu missed refreshes, %llu caught up; frame delay %u ms",
                         static_cast<unsigned long long>(state->missed_refreshes),
                         static_cast<unsigned long long>(state->caught_up_frames),
                         static_cast<unsigned>(state->frame_delay_ms));
}

static void press(DebugWindowAction* action, bool pressed, DebugWindowPress what)
{
    if (pressed)
    {
        action->press = what;
    }
}

static void draw_hud_widgets(DebugWindowAction* action)
{
    static const char* const ICON_NAMES[DEBUG_WINDOW_ICON_COUNT] = {"PAUSED icon", "REWINDING icon", "PLAY icon",
                                                                    "FAST FORWARD icon"};
    static const char* const ICON_IDS[DEBUG_WINDOW_ICON_COUNT] = {"pause", "rewind", "play", "fast_forward"};

    press(action, retro_ui_button("Remap card: port 1's first controller"), DebugWindowPress::remap_card);
    for (uint32_t i = 0; i < DEBUG_WINDOW_ICON_COUNT; i++)
    {
        if (i % 2 == 1)
        {
            retro_ui_same_line();
        }
        retro_ui_check(ICON_IDS[i], ICON_NAMES[i], &action->icons_forced[i]);
    }
    press(action, retro_ui_button("PLAY icon as after PLAY FROM HERE (2.5 s)"), DebugWindowPress::play_from_here);
    press(action, retro_ui_button("DVR card: OFF"), DebugWindowPress::dvr_card_off);
    retro_ui_same_line();
    press(action, retro_ui_button("DVR card: NO SAVESTATES"), DebugWindowPress::dvr_card_unsupported);
    retro_ui_same_line();
    press(action, retro_ui_button("DVR card: NOTHING YET"), DebugWindowPress::dvr_card_nothing);
    press(action, retro_ui_button("Ports' tabs rise"), DebugWindowPress::ports_rise);
}

static void draw_window_widgets(const DebugWindowState* state, DebugWindowAction* action)
{
    retro_ui_begin_disabled(!state->game_running);
    // Asked for by the user: they replace what's up, this window included.
    press(action, retro_ui_button("DEVICES") && state->devices_available, DebugWindowPress::open_devices);
    retro_ui_same_line();
    press(action, retro_ui_button("DVR"), DebugWindowPress::open_dvr);
    // Asked for by the game: they wait until the menu is closed.
    retro_ui_same_line();
    press(action, retro_ui_button("Text box") && state->text_box_allowed, DebugWindowPress::text_box);
    retro_ui_same_line();
    press(action, retro_ui_button("KEEP / REVERT") && state->keep_revert_allowed, DebugWindowPress::keep_revert);
    retro_ui_end_disabled();
    retro_ui_dim_text("Text box and KEEP / REVERT show once the menu is closed.", nullptr);
}

void debug_window_draw(const DebugWindowState* state, RetroUiFont* pixel_font, int32_t scale, DebugWindowAction* action)
{
    *action = {};
    for (uint32_t i = 0; i < DEBUG_WINDOW_ICON_COUNT; i++)
    {
        action->icons_forced[i] = state->icons_forced[i];
    }

    retro_ui_push_theme(pixel_font, scale);
    RetroUiWindow window = {};
    window.id = "debug";
    window.title = "piccolo";
    window.x = 40.0f;
    window.y = 80.0f;
    if (retro_ui_window_begin(&window, scale, &action->closed))
    {
        draw_about(state);
        // Every section after the numbers folds away (Andrés, testing round 1).
        if (retro_ui_section_header("hud", "Widgets (HUD)", true))
        {
            draw_hud_widgets(action);
        }
        if (retro_ui_section_header("windows", "Widgets (windows; a game must run)", true))
        {
            draw_window_widgets(state, action);
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}
