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
    // The cards REMOTE PLAY and PAIR leave (debug_preview.h: fake ones, the
    // real windows don't know).
    press(action, retro_ui_button("LINK COPIED card"), DebugWindowPress::card_link_copied);
    retro_ui_same_line();
    press(action, retro_ui_button("PAIRING TIMED OUT card"), DebugWindowPress::card_pair_timed_out);
    retro_ui_same_line();
    press(action, retro_ui_button("PAIRING FAILED card"), DebugWindowPress::card_pair_failed);
    retro_ui_check("pair_request_card", "Pairing request card (a fake device)", &action->pair_request_card);
    retro_ui_dim_text("In the game, START held fills its key and the card goes; nothing opens.", nullptr);
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

// The screen effects on the game picture, played now (SCREEN EFFECTS off:
// nothing plays; each effect's own switch is ignored here).
static void draw_screen_effects(const DebugWindowState* state, DebugWindowAction* action)
{
    retro_ui_begin_disabled(!state->game_running);
    press(action, retro_ui_button("REWIND (SNAP)"), DebugWindowPress::effect_snap);
    retro_ui_same_line();
    press(action, retro_ui_button("SCREENSHOT (SHUTTER)"), DebugWindowPress::effect_shutter);
    retro_ui_end_disabled();
}

static void preview_button(DebugWindowAction* action, const char* label, DebugPreview what, bool same_line)
{
    if (same_line)
    {
        retro_ui_same_line();
    }
    if (retro_ui_button(label))
    {
        action->preview = what;
    }
}

// Every new or rebuilt window, with fake state (debug_preview.h).
static void draw_previews(const DebugWindowState* state, DebugWindowAction* action)
{
    retro_ui_dim_text("Fake state: presses do nothing; X, CLOSE or Escape ends a preview.", nullptr);
    retro_ui_begin_disabled(state->pair_open);
    retro_ui_text("PAIR A DEVICE");
    preview_button(action, "Devices", DebugPreview::pair_device_list, false);
    preview_button(action, "Code: this network", DebugPreview::pair_device_type, true);
    preview_button(action, "Enter a code", DebugPreview::pair_device_internet, true);
    preview_button(action, "Paired", DebugPreview::pair_device_done, true);
    retro_ui_text("GENERATE PAIRING CODE");
    preview_button(action, "Our code", DebugPreview::pair_code_shown, false);
    preview_button(action, "A request", DebugPreview::pair_code_request, true);
    preview_button(action, "Unsigned build", DebugPreview::pair_code_unsigned, true);
    preview_button(action, "Paired", DebugPreview::pair_code_done, true);
    retro_ui_end_disabled();
    if (state->pair_open)
    {
        retro_ui_dim_text("PAIR is open: close it to preview it.", nullptr);
    }
    retro_ui_begin_disabled(state->guest_open);
    retro_ui_text("REMOTE PLAY");
    preview_button(action, "No link", DebugPreview::remote_no_link, false);
    preview_button(action, "Link, no guests", DebugPreview::remote_no_guests, true);
    preview_button(action, "Link and guests", DebugPreview::remote_guests, true);
    retro_ui_end_disabled();
    if (state->guest_open)
    {
        retro_ui_dim_text("REMOTE PLAY is open: close it to preview it.", nullptr);
    }
    retro_ui_text("OTHERS");
    retro_ui_begin_disabled(state->about_open);
    preview_button(action, "ABOUT", DebugPreview::about, false);
    retro_ui_end_disabled();
    // The real QUICK MENU needs a game; this one is a fake game's, its
    // slots without pictures.
    preview_button(action, "QUICK MENU, SAVE STATES open", DebugPreview::quick_menu, true);
}

void debug_window_draw(const DebugWindowState* state, RetroUiFont* pixel_font, int32_t scale, DebugWindowAction* action)
{
    *action = {};
    for (uint32_t i = 0; i < DEBUG_WINDOW_ICON_COUNT; i++)
    {
        action->icons_forced[i] = state->icons_forced[i];
    }
    action->pair_request_card = state->pair_request_card;

    retro_ui_push_theme(pixel_font, scale);
    RetroUiWindow window = {};
    window.id = "debug";
    window.title = "piccolo";
    window.x = 40.0f;
    window.y = 80.0f;
    if (retro_ui_window_begin(&window, scale, &action->closed))
    {
        draw_about(state);
        // Every section after the numbers folds away, closed at first (Andrés, testing rounds 1 and 2).
        if (retro_ui_section_header("hud", "Widgets (HUD)", false))
        {
            draw_hud_widgets(action);
        }
        if (retro_ui_section_header("windows", "Widgets (windows; a game must run)", false))
        {
            draw_window_widgets(state, action);
        }
        if (retro_ui_section_header("effects", "Screen effects (a game must run)", false))
        {
            draw_screen_effects(state, action);
        }
        if (retro_ui_section_header("previews", "Dialogs (preview)", false))
        {
            draw_previews(state, action);
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}
