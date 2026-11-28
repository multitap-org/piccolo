// The frame's drawing (ui.h): retroui, the game view and the windows' draw
// functions only. No model here: what is shown comes in the UiView, what was
// done goes out in the UiActions.

#include "ui.h"

#include "continue_view.h"
#include "game_view.h"
#include "touch_ui.h"

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

// INTERFACE > BACKGROUND TRANSPARENCY: the whole screen covered by the
// theme's BACKGROUND color at that opacity, behind every window (the menu bar
// stays on top of it). The only one: drawn once a frame, here; widgets and
// modals don't dim the screen themselves.
static void draw_menu_backdrop(const UiView* view, RetroUiFont* pixel_font, int32_t scale)
{
    if ((view->backdrop >> 24) == 0)
    {
        return;
    }
    retro_ui_begin_layer("##menu_backdrop", pixel_font, scale, false);
    retro_ui_layer_rect(0.0f, 0.0f, view->screen_width, view->screen_height, view->backdrop);
    retro_ui_end_layer();
}

// The menu bar, while it shows (no status line: messages, and why a game
// is paused, are HUD cards). Returns its pick (-1: none).
static int32_t draw_menu_bar(const UiView* view, RetroUiFont* pixel_font, int32_t scale)
{
    if (!view->menu_shown)
    {
        return -1;
    }
    return retro_ui_menu_bar(pixel_font, scale, view->menus, view->menu_count, view->open_first_menu);
}

// The CONTINUE card on the empty screen: a panel behind every window and
// menu (drawn back before the backdrop, which goes further back), under the
// menu bar. Returns what was pressed, chosen or paged.
static ContinueViewAction draw_continue_panel(const UiView* view, RetroUiFont* pixel_font, int32_t scale)
{
    if (!view->continue_card.shown || view->continue_card.phone)
    {
        return ContinueViewAction{};
    }
    float bar = view->menu_shown ? retro_ui_menu_bar_height(scale) : 0.0f;
    return continue_view_draw_panel(pixel_font, scale, &view->continue_card.card, bar, view->screen_width,
                                    view->screen_height);
}

// The debug window's preview of the phone's HOME card: a phone-wide card in
// the touch look, over everything, with the screen dimmed behind it. Any
// button ends the preview (app_continue.cpp).
static ContinueViewButton draw_continue_phone(const UiView* view, RetroUiFont* pixel_font)
{
    if (!view->continue_card.shown || !view->continue_card.phone)
    {
        return ContinueViewButton::none;
    }
    TouchUi ui = {};
    ui.font = pixel_font;
    ui.scale = touch_ui_scale_for(view->screen_width, view->screen_height);
    ui.palette = *retro_ui_palette();
    touch_ui_begin_overlay(&ui, "##continue_phone");
    float screen_width = touch_ui_width(&ui);
    float screen_height = touch_ui_height(&ui);
    touch_ui_rect(&ui, 0.0f, 0.0f, screen_width, screen_height, 0x99000000u);
    float phone = screen_width < 195.0f ? screen_width : 195.0f;   // a phone's width (touch_ui.h)
    float x = static_cast<float>(static_cast<int32_t>((screen_width - phone) * 0.5f));
    touch_ui_rect(&ui, x, 0.0f, phone, screen_height, ui.palette.background);
    float height = 0.0f;
    ContinueViewButton pressed = continue_view_draw_card(&ui, &view->continue_card.card, x + 8.0f, 16.0f,
                                                         phone - 16.0f, &height);
    touch_ui_end_screen();
    return pressed;
}

// ---------------------------------------------------------------------------
// The frame

void ui_draw(const UiView* view, UiWindows* windows, RetroUiFont* pixel_font, int32_t scale, UiActions* actions)
{
    draw_game(view);   // background, behind everything
    // The menu snow, on the same background list after the game: over the
    // picture, behind every window and the menu bar.
    if (view->snow != nullptr)
    {
        snow_view_draw(view->snow, 0.0f, 0.0f, scale);
    }
    // The touch gamepad over the game (or its EDIT LAYOUT).
    onscreen_pad_draw(windows->pad, &view->pad, pixel_font, &actions->pad);
    // The ports' tabs, behind every window (they peek while the menu is up).
    ports_widget_draw_tabs(windows->ports, &view->ports_tabs, pixel_font, scale);
    draw_hud(view, pixel_font, scale);
    ContinueViewAction continue_action = draw_continue_panel(view, pixel_font, scale);
    actions->continue_pressed = continue_action.pressed;
    actions->continue_tile = continue_action.tile_chosen;
    actions->continue_page = continue_action.page_delta;
    draw_menu_backdrop(view, pixel_font, scale);   // (after the tabs and the card: behind them and every window)
    actions->menu_picked = draw_menu_bar(view, pixel_font, scale);
    // The phone: HOME behind every window (a page covers it), the ☰ sidebar
    // over it (never up with a window).
    if (view->phone)
    {
        mobile_home_draw(windows->mobile_home, &view->mobile_home, pixel_font, scale, &actions->mobile_home);
    }

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
    if (view->settings_error_shown)
    {
        settings_error_window_draw(&view->settings_error, pixel_font, scale, &actions->settings_error);
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
    ContinueViewButton phone = draw_continue_phone(view, pixel_font);
    if (phone != ContinueViewButton::none)
    {
        actions->continue_pressed = phone;
    }
    if (view->fps_shown)
    {
        fps_meter_draw(view->fps_meter, view->fps_target, pixel_font, scale, view->screen_width);
    }
    retro_ui_scrollbars(scale);
    retro_ui_cursor(scale, view->cursor_shown);   // the theme's cursor, on top of everything
}
