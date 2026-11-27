// The game's picture and its timing: the window, the viewport, the frame
// delay, and what is drawn behind the windows (background, HUD, snow).

#include "app_picture.h"

#include "app_files.h"
#include "app_game.h"
#include "app_shaders.h"
#include "app_share.h"
#include "app_ui_modes.h"

// --- The window and the picture ---------------------------------------------

// F11 / Alt+Enter: the global fullscreen setting, saved on exit.
void toggle_fullscreen(App* app)
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

void update_window(App* app)
{
    ViewportInput picture = {};
    bool has_picture = game_picture(app, &picture);
    if (!app_loop_window_follows_page())
    {
        piccolo_window_apply(&app->window, app->shell.window, &app->settings, has_picture ? &picture : nullptr);
    }
}

// The game's picture for this frame: placed, its shaders and screen effect
// set, and what the game view widget shows (view->game_image), behind every
// window. The cursors learn where it is.
void prepare_game_picture(App* app, UiView* view)
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

// --- Frame delay ------------------------------------------------------------

// SEARCH AGAIN: forget this game's delay and start the search over.
void search_frame_delay_again(App* app)
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
bool stream_delay_overrides(const App* app)
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

// Low latency: in Vsync timing the loop is paced by the display, so waiting
// a few ms here (right after presenting) moves the next input read and core
// frame closer to the next refresh. Only after waiting for present: without
// it the loop blocks in acquire later, after the input read, and a delay here
// only adds latency (the setting is greyed out then).
// FRAME DELAY, or with AUTO FRAME DELAY the delay it is finding or found for
// this game (frame_delay.h). AUTO watches this loop's missed refresh
// (frame_timing) and needs a minute of uninterrupted play to lock: the menu,
// a pause, fast forward or another window having focus start that minute over.
void frame_delay(App* app)
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

// --- Behind the windows: background, HUD, snow ------------------------------

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
uint32_t background_over_black(const RetroUiPalette* palette)
{
    uint32_t transparency = palette->background_transparency > 100 ? 100 : palette->background_transparency;
    uint32_t opacity = 100u - transparency;
    uint32_t red = ((palette->background >> 16) & 0xFFu) * opacity / 100u;
    uint32_t green = ((palette->background >> 8) & 0xFFu) * opacity / 100u;
    uint32_t blue = (palette->background & 0xFFu) * opacity / 100u;
    return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

uint32_t menu_backdrop(const App* app)
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
void hud_fill(App* app, UiView* view)
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

void snow_fill(App* app, UiView* view)
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
