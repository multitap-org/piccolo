#include "app_ui_modes.h"

#include "app_game.h"
#include "app_input.h"
#include "app_shaders.h"

// ---------------------------------------------------------------------------
// UI modes (ui_mode.h): what has the screen and the input
// ---------------------------------------------------------------------------
//
// app->ui is the truth. The windows keep their own open flags (their
// modules close themselves: an X, a choice); ui_sync_windows() keeps MAIN
// MENU's stack in step with them, once per loop. Every mode change goes
// through ui_follow_changes(): held keys are let go, input grabs cancelled,
// and whatever the old mode had open closes.

// --- Windows and input grabs ------------------------------------------------

// A MAIN MENU window's own open flag, in its module.
static bool window_flag(const App* app, UiWindow window)
{
    switch (window)
    {
        case UiWindow::load_game:
            return app->picker.open && app->picker.mode != FilePickerMode::folder;
        case UiWindow::choose_folder:
            return app->picker.open && app->picker.mode == FilePickerMode::folder;
        case UiWindow::settings:
            return app->settings_window.open;
        case UiWindow::image_settings:
            return app->settings_window.image_open;
        case UiWindow::core_options:
            return app->core_options_manager;
        case UiWindow::core_config:
            return app->core_config.open;
        case UiWindow::cores:
            return app->cores_window.open;
        case UiWindow::share:
            return app->share_window.open;
        case UiWindow::input:
            return app->input_window.open;
        case UiWindow::map_controller:
            return map_window_open_now(&app->map_window);
        case UiWindow::safe_start:
            return app->safe_start_open;
        case UiWindow::debug:
            return app->show_debug_window;
        case UiWindow::about:
            return app->about_window.open;
        case UiWindow::pad_editor:
            return app->onscreen_pad.editor.active;
        case UiWindow::none:
        case UiWindow::count:
            break;
    }
    return false;
}

// Closes a MAIN MENU window through its module.
static void window_close(App* app, UiWindow window)
{
    switch (window)
    {
        case UiWindow::load_game:
        case UiWindow::choose_folder:
            app->picker.open = false;
            break;
        case UiWindow::settings:
            app->settings_window.open = false;
            break;
        case UiWindow::image_settings:
            app->settings_window.image_open = false;
            break;
        case UiWindow::core_options:
            app->core_options_manager = false;
            break;
        case UiWindow::core_config:
            app->core_config.open = false;
            app->shader_picker.open = false;   // CORE CONFIGURATION > SHADER > CHOOSE
            break;
        case UiWindow::cores:
            app->cores_window.open = false;
            break;
        case UiWindow::share:
            app->share_window.open = false;
            break;
        case UiWindow::input:
            app->input_window.open = false;
            break;
        case UiWindow::map_controller:
            map_window_close(&app->map_window, &app->input);
            break;
        case UiWindow::safe_start:
            app->safe_start_open = false;   // no answer: the game doesn't start
            // This can run with the network lock held, and answering takes it:
            // the main loop answers (net_answer_load_refused).
            app->net_load_refused = true;
            break;
        case UiWindow::debug:
            app->show_debug_window = false;
            break;
        case UiWindow::about:
            app->about_window.open = false;
            break;
        case UiWindow::pad_editor:
            onscreen_pad_edit_end(&app->onscreen_pad);   // EDIT LAYOUT: DONE
            break;
        case UiWindow::none:
        case UiWindow::count:
            break;
    }
}

// What grabs the input now, from the modules' own state.
UiGrab grab_now(const App* app)
{
    if (app->core_config.macro_record != 0)
    {
        return UiGrab::macro;
    }
    if (app->input.suspended)
    {
        return UiGrab::mapping;
    }
    if (app->input.capture.active)
    {
        return UiGrab::capture;
    }
    return UiGrab::none;
}

// Every input grab ends: a SET waiting for a key, MAP CONTROLLER (it
// closes: it can't go on without the router suspended), the macro
// recorder. The router runs again.
void cancel_grabs(App* app)
{
    if (app->core_config.macro_record != 0)
    {
        app->core_config.macro_record = 0;
    }
    if (map_window_open_now(&app->map_window) && app->input.suspended)
    {
        map_window_close(&app->map_window, &app->input);
    }
    input_capture_cancel(&app->input);
    app->input.suspended = false;
    app->input_window.capturing = false;
    app->core_config.capturing = false;
    ui_grab_end(&app->ui);
}

// What changed hands: nothing held acts in the new mode (the key that did
// it included), on the router's side and the toolkit's.
static void release_input(App* app)
{
    input_hold_until_released(&app->input);
    cancel_grabs(app);
    for (size_t i = 0; i < UI_KEY_COUNT; i++)
    {
        if (app->ui_keys_down[i])
        {
            ui_toolkit_nav_key(UI_KEYS[i].key, false);
            app->ui_keys_down[i] = false;
        }
    }
    ui_toolkit_clear_keys();
    app->ui_back_since_ms = 0;
    app->ui_back_done = false;
}

// The widget that just stopped being the mode gets its own back: the core's
// text box is cancelled (the core waits for an answer), KEEP / REVERT reverts.
static void widget_left(App* app, UiWidget widget)
{
    switch (widget)
    {
        case UiWidget::quick_menu:
            app->quick_menu.open = false;
            break;
        case UiWidget::ports:
            if (app->ports_widget.open)
            {
                ports_widget_close(&app->ports_widget, &app->input);
            }
            break;
        case UiWidget::text_input:
            if (text_input_pending(app))
            {
                libretro_host_text_input_answer(app->session.host, nullptr);
            }
            break;
        case UiWidget::shader_check:
            if (shader_check_asking(app))
            {
                shader_check_answer(app, ShaderCheckChoice::revert);
            }
            break;
        case UiWidget::dvr:
            if (app->dvr.open)
            {
                dvr_widget_close(&app->dvr);   // BACK TO NOW
            }
            break;
        case UiWidget::none:
            break;
    }
}

// --- Changing mode ----------------------------------------------------------

// After a transition: if the mode changed, input is released and what the
// old mode had open closes.
void ui_follow_changes(App* app)
{
    if (app->ui.changes == app->ui_changes_seen)
    {
        return;
    }
    app->ui_changes_seen = app->ui.changes;
    release_input(app);
    if (app->ui.mode == UiMode::game)
    {
        remap_widget_rescan(&app->remap);   // a remap may have been decided meanwhile
    }
    for (uint32_t w = 1; w < UI_WINDOW_MAX; w++)
    {
        UiWindow window = static_cast<UiWindow>(w);
        if (window_flag(app, window) && !ui_window_open(&app->ui, window))
        {
            window_close(app, window);
        }
    }
    UiWidget widget = app->ui.mode == UiMode::widget ? app->ui.widget : UiWidget::none;
    if (app->ui_widget_applied != widget)
    {
        widget_left(app, app->ui_widget_applied);
        app->ui_widget_applied = widget;
    }
}

void app_enter_game(App* app)
{
    ui_enter_game(&app->ui);
    ui_follow_changes(app);
}

// A menu bar action done (RESET, SAVE STATE...): back to the game. From a
// hotkey in QUICK MENU or a widget, what's up stays.
void app_leave_main_menu(App* app)
{
    if (app->ui.mode == UiMode::main_menu)
    {
        app_enter_game(app);
    }
}

void app_enter_main_menu(App* app)
{
    ui_enter_main_menu(&app->ui);
    ui_follow_changes(app);
}

// QUICK MENU is the window widget that's up.
bool quick_menu_up(const App* app)
{
    return app->ui.mode == UiMode::widget && app->ui.widget == UiWidget::quick_menu;
}

void app_open_widget(App* app, UiWidget widget)
{
    // The widget opens first and the mode changes only if it did: a refusal
    // (the DVR with rewind off, no savestates or no history yet: its HUD says
    // why) leaves what was up, the QUICK MENU included.
    if (widget == UiWidget::dvr)
    {
        if (!game_running(app) || !dvr_widget_open(&app->dvr, &app->strings, SDL_GetTicks()))
        {
            return;
        }
    }
    if (ui_enter_widget(&app->ui, widget))
    {
        if (widget == UiWidget::ports)
        {
            ports_widget_open(&app->ports_widget, &app->input);
        }
        else if (widget == UiWidget::quick_menu)
        {
            quick_menu_open(&app->quick_menu, app->session.state_slot);
        }
    }
    ui_follow_changes(app);
}

void app_open_quick_menu(App* app)
{
    app_open_widget(app, UiWidget::quick_menu);
}

// CORE CONFIGURATION (QUICK MENU's CONFIG, GAME > CORE CONFIGURATION, HOLD
// START TO REMAP): a MAIN MENU window, so opening it closes QUICK MENU.
// `port` / `controller`: open on that controller's remap (UINT32_MAX: no).
void app_open_core_config(App* app, uint32_t port, uint32_t controller)
{
    if (!game_running(app))
    {
        return;
    }
    if (port != UINT32_MAX)
    {
        core_config_window_open_remap(&app->core_config, port, controller);
    }
    else
    {
        core_config_window_open(&app->core_config);
    }
    app->core_options_ui.choose_layer = true;   // SAVE TO: the most specific layer in use
    ui_push_window(&app->ui, UiWindow::core_config);
    ui_follow_changes(app);
}

// --- Once per loop: following what opened and closed ------------------------

// MAIN MENU's stack follows the windows' own flags: one opened by its
// module (SETTINGS' CHOOSE FOLDER, INPUT's MAP CONTROLLER, a MAP CONTROLLER
// offer during play) is pushed, entering MAIN MENU; one that closed itself
// leaves the stack.
void ui_sync_windows(App* app)
{
    for (uint32_t w = 1; w < UI_WINDOW_MAX; w++)
    {
        UiWindow window = static_cast<UiWindow>(w);
        bool open = window_flag(app, window);
        bool listed = ui_window_open(&app->ui, window);
        if (open && !listed)
        {
            ui_push_window(&app->ui, window);
        }
        else if (!open && listed)
        {
            ui_remove_window(&app->ui, window);
        }
    }
    ui_follow_changes(app);
}

// Widgets that closed themselves go back to the game; the windows the
// game asks for (a core's text box, KEEP / REVERT) open from GAME or QUICK
// MENU. In MAIN MENU they wait until it's closed: they don't take over what
// the player is doing there.
void ui_follow_widgets(App* app)
{
    if (app->ui.mode == UiMode::widget)
    {
        bool up = true;
        switch (app->ui.widget)
        {
            case UiWidget::quick_menu:
                up = app->quick_menu.open;
                break;
            case UiWidget::ports:
                up = app->ports_widget.open;
                break;
            case UiWidget::text_input:
                up = text_input_pending(app);
                break;
            case UiWidget::shader_check:
                up = shader_check_asking(app);
                break;
            case UiWidget::dvr:
                up = app->dvr.open;
                break;
            case UiWidget::none:
                break;
        }
        if (!up)
        {
            app->ui_widget_applied = UiWidget::none;   // answered: nothing more to do for it
            app_enter_game(app);
        }
    }
    if (app->ui.mode == UiMode::game || quick_menu_up(app))
    {
        if (text_input_pending(app))
        {
            app_open_widget(app, UiWidget::text_input);
        }
        else if (shader_check_asking(app))
        {
            app_open_widget(app, UiWidget::shader_check);
        }
    }
}

// A grab's start and end, as the modules report them; a grab whose owner
// is gone (its window closed some way that didn't end it) is cancelled.
void ui_follow_grabs(App* app)
{
    UiGrab now = grab_now(app);
    if (now != UiGrab::none && app->ui.grab == UiGrab::none)
    {
        ui_grab_begin(&app->ui, now);
    }
    else if (now == UiGrab::none && app->ui.grab != UiGrab::none)
    {
        ui_grab_end(&app->ui);
    }
    if (ui_grab_orphaned(&app->ui))
    {
        SDL_Log("Input grab left behind by its window: cancelled");
        cancel_grabs(app);
    }
}

// The facts ui_derive() needs, and what it says for this loop.
UiDerived app_derive(const App* app)
{
    UiFacts facts = {};
    facts.pause_in_menu = settings_get_bool(&app->settings, PICCOLO_PAUSE_IN_MENU);
    facts.pause_on_focus_loss = settings_get_bool(&app->settings, PICCOLO_PAUSE_ON_FOCUS_LOSS);
    facts.focused = app->window_focused;
    facts.streaming = app->net_streaming;
    facts.headless = app->headless.on;
    facts.pause_on_disconnect = settings_get_bool(&app->settings, PICCOLO_PAUSE_ON_DISCONNECT);
    facts.played_with_gamepad = app->played_with_gamepad;
    facts.gamepad_on_port = gamepad_on_a_port(app);
    facts.background = app->android_background;
    // A request pauses once its window shows (in the game it's a card first:
    // no pause until START held opens the window).
    facts.pairing_request = app->pair_window.request.active && app->pair_window.open;
    return ui_derive(&app->ui, &facts);
}

// --- Who has the input, and BACK --------------------------------------------

// The game has the keyboard and the controllers (GAME mode): its keys go
// to the core. Anything else up (QUICK MENU, a window widget, MAIN MENU)
// stops the core's input; HUD widgets don't.
bool game_has_keyboard(const App* app)
{
    return game_running(app) && app->ui.mode == UiMode::game;
}

bool ui_is_up(const App* app)
{
    return !game_has_keyboard(app);
}

// Escape / B: back, the same in every mode (ui_back): a grab is cancelled,
// CORE CONFIGURATION's view, the widget or MAIN MENU's top window closes,
// then the game.
void ui_do_back(App* app)
{
    // The debug window's preview first: it's on top, and not a MAIN MENU window.
    if (debug_preview_shown(&app->debug_previews))
    {
        debug_preview_end(&app->debug_previews);
        return;
    }
    // A view inside CORE CONFIGURATION (its shader picker, the remap view)
    // closes before the window, while it's the top one.
    bool sub_view = ui_top_window(&app->ui) == UiWindow::core_config &&
                    (app->shader_picker.open || core_config_window_sub_view(&app->core_config));
    // The phone's ☰: from a menu's items back to the names; then over HOME
    // it closes (over a game, back goes to the game below).
    if (app->phone_ui && app->ui.window_count == 0 && app->mobile_home.submenu > 0)
    {
        app->mobile_home.submenu = 0;
        app->mobile_home.menu_scroll = 0.0f;
        return;
    }
    if (app->phone_ui && app->mobile_home.menu_open && app->ui.window_count == 0)
    {
        app->mobile_home.menu_open = false;
        return;
    }
    switch (ui_back(&app->ui, sub_view))
    {
        case UiBack::none:
            break;
        case UiBack::cancel_grab:
            cancel_grabs(app);
            break;
        case UiBack::close_sub_view:
            if (app->shader_picker.open)
            {
                app->shader_picker.open = false;
            }
            else
            {
                core_config_window_close_sub_view(&app->core_config);
            }
            break;
        case UiBack::close_widget:
        case UiBack::to_game:
            app_enter_game(app);   // (a widget's own back runs as it stops being the mode)
            break;
        case UiBack::close_window:
        {
            UiWindow top = ui_top_window(&app->ui);
            if (top == UiWindow::share && !app->share_window.conflicts.empty())
            {
                break;   // its REPLACE? question takes Escape
            }
            window_close(app, top);
            ui_remove_window(&app->ui, top);
            break;
        }
    }
}

// GAME > RESUME: back to the game whatever is up, every grab cancelled
// (the way out when something holds the input).
void resume_game(App* app)
{
    if (!game_running(app))
    {
        return;
    }
    cancel_grabs(app);
    app_enter_game(app);
}
