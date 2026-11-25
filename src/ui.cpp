// The frame's drawing (ui.h): retroui, the game view and the windows' draw
// functions only. No model here: what is shown comes in the UiView, what was
// done goes out in the UiActions.

#include "ui.h"

#include "game_view.h"

// ---------------------------------------------------------------------------
// Drawing helpers

// The game view widget: behind every window, menu bar included.
static void draw_game(const UiView* view)
{
    if (view->game_shown)
    {
        game_view_draw(&view->game_image);
    }
}

// The HUD widgets' state icons and notification cards (retroui's hud_view),
// behind every window.
static void draw_hud(const UiView* view, RetroUiFont* pixel_font, int32_t scale)
{
    hud_view_draw(view->hud, &view->hud_area, pixel_font, scale, view->hud_now_ms);
}

// INTERFACE > BACKDROP: the whole screen covered by black at that opacity,
// behind every window (the menu bar stays on top of it). The only one: drawn
// once a frame, here; widgets and modals don't dim the screen themselves.
static void draw_menu_backdrop(const UiView* view, RetroUiFont* pixel_font, int32_t scale)
{
    if (view->backdrop_alpha == 0)
    {
        return;
    }
    retro_ui_begin_layer("##menu_backdrop", pixel_font, scale, false);
    retro_ui_layer_rect(0.0f, 0.0f, view->screen_width, view->screen_height, view->backdrop_alpha << 24);
    retro_ui_end_layer();
}

// The status line: with the menu bar, or alone over a paused game (it says
// why: "PAUSED: ..."). Returns the menu bar's pick (-1: none).
static int32_t draw_status_and_menu_bar(const UiView* view, const PortsWidget* ports, RetroUiFont* pixel_font,
                                        int32_t scale)
{
    float status_left = ports_widget_tabs_width(ports);
    status_left = status_left > 4.0f ? status_left : 4.0f;
    if (view->status_alone)
    {
        retro_ui_status_line_from(pixel_font, scale, view->status, status_left);
    }
    if (!view->menu_shown)
    {
        return -1;
    }
    // The status line's window goes first, so it stays behind the others; it
    // starts past the tabs.
    retro_ui_status_line_from(pixel_font, scale, view->status, status_left);
    return retro_ui_menu_bar(pixel_font, scale, view->menus, view->menu_count, view->open_first_menu);
}

// ---------------------------------------------------------------------------
// The frame

void ui_draw(const UiView* view, UiWindows* windows, RetroUiFont* pixel_font, int32_t scale, UiActions* actions)
{
    draw_game(view);   // background, behind everything
    // The touch gamepad over the game (or its EDIT LAYOUT).
    onscreen_pad_draw(windows->pad, &view->pad, pixel_font, &actions->pad);
    // The ports' tabs, behind every window (they peek while the menu is up).
    ports_widget_draw_tabs(windows->ports, &view->ports_tabs, pixel_font, scale);
    draw_hud(view, pixel_font, scale);
    draw_menu_backdrop(view, pixel_font, scale);   // (after the tabs: behind them and every window)
    actions->menu_picked = draw_status_and_menu_bar(view, windows->ports, pixel_font, scale);

    settings_window_draw(windows->settings, windows->network, &view->settings, pixel_font, scale,
                         &actions->settings);
    // After SETTINGS, so CHOOSE FOLDER opened from it is drawn on top.
    file_picker_draw(windows->picker, &view->picker, pixel_font, scale, &actions->picker);
    quick_menu_draw(windows->quick, &view->quick, pixel_font, scale, &actions->quick);
    core_config_window_draw(windows->config, windows->options_ui, &view->config, pixel_font, scale, &actions->config);
    // CORE CONFIGURATION > SHADER > CHOOSE's picker, over that window.
    file_picker_draw(windows->shader_picker, &view->shader_picker, pixel_font, scale, &actions->shader_picker);
    ports_widget_draw_board(windows->ports, &view->ports_board, pixel_font, scale, &actions->ports);
    dvr_widget_draw(windows->dvr, &view->dvr, pixel_font, scale, &actions->dvr);
    core_options_manager_draw(windows->manager_open, windows->options_ui, &view->manager, pixel_font, scale,
                              &actions->manager);
    cores_window_draw(windows->cores, &view->cores, pixel_font, scale, &actions->cores);
    if (view->share_shown)
    {
        share_window_draw(windows->share, &view->share, pixel_font, scale, &actions->share);
    }
    // TOOLS > PAIR..., also a device asking to pair (over anything).
    if (view->pair_shown)
    {
        pair_window_draw(windows->pair, &view->pair, pixel_font, scale, &actions->pair);
    }
    if (view->guest_shown)
    {
        guest_window_draw(windows->guest, &view->guest, pixel_font, scale, &actions->guest);
    }
    input_window_draw(windows->input, &view->input, pixel_font, scale, &actions->input);
    map_window_draw(windows->map, &view->map, pixel_font, scale, &actions->map);
    if (view->safe_start_shown)
    {
        safe_start_window_draw(&view->safe_start, pixel_font, scale, &actions->safe_start);
    }
    if (view->shader_check_shown)
    {
        actions->shader_check = shader_check_view_draw_window(pixel_font, scale, &view->shader_check_text,
                                                              view->shader_check_preset.c_str(),
                                                              view->shader_check_measured_fps,
                                                              view->shader_check_target_fps,
                                                              view->shader_check_asked_ms, view->shader_check_now_ms);
    }
    if (view->text_input_shown)
    {
        text_input_window_draw(windows->text_input, &view->text_input, pixel_font, scale, &actions->text_input);
    }
    about_window_draw(windows->about, &view->about, pixel_font, scale, &actions->about);
    // The debug window's preview of a window (never up with the real one).
    debug_preview_draw(windows->previews, &view->preview, pixel_font, scale, &actions->preview);
    if (view->debug_shown)
    {
        debug_window_draw(&view->debug, pixel_font, scale, &actions->debug);
    }
    if (view->fps_shown)
    {
        fps_meter_draw(view->fps_meter, view->fps_target, pixel_font, scale, view->screen_width);
    }
    retro_ui_scrollbars(scale);
    retro_ui_cursor(scale, view->cursor_shown);   // the theme's cursor, on top of everything
}
