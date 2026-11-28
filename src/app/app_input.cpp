// Input, once per loop: the hotkeys, gamepads driving the UI, what the
// controllers say to the core session, and SDL's events.

#include "app_input.h"

#include "app_continue.h"
#include "app_files.h"
#include "app_game.h"
#include "app_menus.h"
#include "app_picture.h"
#include "app_screenshots.h"
#include "app_share.h"
#include "app_states.h"
#include "app_ui_modes.h"

// --- Hotkeys ----------------------------------------------------------------

// The hotkeys of this frame (input_bindings: the keyboard's F1 / F2 / F3 /
// F4 / Escape, or HOTKEY ENABLE + a button on a gamepad; the keyboard's
// without Shift in MAIN MENU).
static void app_handle_hotkeys(App* app, const InputFrame* frame)
{
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_menu)])
    {
        if (app->ui.mode == UiMode::main_menu)
        {
            ui_do_back(app);   // its top window, then the game
        }
        else
        {
            app_enter_main_menu(app);
        }
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_quick_menu)])
    {
        if (quick_menu_up(app))
        {
            app_enter_game(app);
        }
        else if (game_running(app))
        {
            app_open_quick_menu(app);
        }
    }
    // DEVICES: the same hotkey closes it; from any mode it replaces what's up.
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_ports)])
    {
        if (app->ui.mode == UiMode::widget && app->ui.widget == UiWidget::ports)
        {
            app_enter_game(app);
        }
        else if (game_running(app) && app->ports_widget.port_count > 0)
        {
            app_open_widget(app, UiWidget::ports);
        }
    }
    // The DVR: a window widget (it replaces what's up); the same hotkey goes
    // back to now.
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_dvr)])
    {
        if (app->ui.mode == UiMode::widget && app->ui.widget == UiWidget::dvr)
        {
            app_enter_game(app);
        }
        else if (game_running(app))
        {
            app_open_widget(app, UiWidget::dvr);
        }
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_save_state)])
    {
        save_state_from(app, true);
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_load_state)])
    {
        load_state_from(app, true);
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_undo_save_state)])
    {
        undo_save_from(app, true);
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_undo_load_state)])
    {
        undo_load_from(app, true);
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_screenshot)])
    {
        take_screenshot(app);   // the menus stay as they are (they're not in the picture)
    }
    // RESET (testing round 3; nothing bound by default): the core's own
    // reset, said with a card (no menu shows it).
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_reset)] && soft_reset(app))
    {
        set_status(app, piccolo_text(&app->strings, Str::status_reset));
    }
    // PAUSE: the player's own pause; what's up stays up.
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_pause)] && game_running(app))
    {
        toggle_user_pause(app);
    }
}

// --- Gamepads in the UI -----------------------------------------------------

constexpr uint64_t UI_BACK_HOLD_MS = 600;   // hold BACK (or SELECT alone) this long to close a window

static void drive_ui_with_gamepads(App* app, const InputFrame* source_frame, bool ui_up)
{
    const InputFrame* frame = source_frame;
    for (size_t i = 0; i < UI_KEY_COUNT; i++)
    {
        uint32_t action = static_cast<uint32_t>(UI_KEYS[i].action);
        bool down = ui_up && frame->gamepad_held[action];
        if (down != app->ui_keys_down[i])
        {
            ui_toolkit_nav_key(UI_KEYS[i].key, down);
            app->ui_keys_down[i] = down;
        }
    }
    // BACK (RetroPad B): a tap closes an open list or popup, or backs out of
    // a view inside a window; a window closes only when BACK, or SELECT by
    // itself, is held (a tap must not close a window by accident).
    uint32_t back = static_cast<uint32_t>(InputAction::ui_back);
    bool back_down = ui_up && frame->gamepad_held[back];
    bool hold_down = ui_up && (back_down || frame->gamepad_enable_alone);
    uint64_t now = SDL_GetTicks();
    if (back_down && !app->ui_back_down)
    {
        if (ui_toolkit_has_popup())
        {
            ui_toolkit_nav_key(UiNavKey::back, true);   // a list or popup closes first
            ui_toolkit_nav_key(UiNavKey::back, false);
        }
        else if (ui_top_window(&app->ui) == UiWindow::core_config && core_config_window_sub_view(&app->core_config))
        {
            core_config_window_close_sub_view(&app->core_config);   // a view inside a window: back out of it
        }
    }
    if (!hold_down)
    {
        app->ui_back_since_ms = 0;
        app->ui_back_done = false;
    }
    else if (app->ui_back_since_ms == 0)
    {
        app->ui_back_since_ms = now;
    }
    else if (!app->ui_back_done && now - app->ui_back_since_ms >= UI_BACK_HOLD_MS)
    {
        app->ui_back_done = true;   // once per hold
        ui_do_back(app);
    }
    app->ui_back_down = back_down;

    // OPEN MENU (Y on a gamepad, START too on a RetroPad device): the menu
    // bar's first menu, when the bar is up and nothing else is open on top of it.
    // Not for the START that just joined a port.
    bool bar_alone = app->ui.mode == UiMode::main_menu && app->ui.window_count == 0 &&
                     !ui_toolkit_has_popup();
    if (bar_alone && frame->joined_port == INPUT_NO_PORT && frame->pressed[static_cast<uint32_t>(InputAction::ui_open_menu)])
    {
        app->open_first_menu = true;
    }
    // The CONTINUE card's first button (A / Enter), while nothing covers it.
    if (bar_alone && !app->phone_ui && frame->joined_port == INPUT_NO_PORT &&
        frame->pressed[static_cast<uint32_t>(InputAction::ui_accept)] && continue_card_active(app) &&
        app->continue_panel.preview != ContinuePreview::phone)
    {
        ContinueKind kind = continue_panel_choice(&app->continue_panel).kind;
        continue_card_press(app, kind == ContinueKind::resume ? ContinueViewButton::resume : ContinueViewButton::start);
    }
}

// MAP CONTROLLER offered: once per session for a controller SDL doesn't
// know (it can't be used otherwise), and the first time a known one joins
// (unless mapped or declined before). Its results go to the status line
// and input.cfg.
static void offer_mapping(App* app, const InputFrame* frame)
{
    MapWindow* map = &app->map_window;
    uint64_t now = SDL_GetTicks();
    if (!map_window_open_now(map))
    {
        for (size_t u = 0; u < app->input_sdl.unmapped.size(); u++)
        {
            uint32_t joystick = app->input_sdl.unmapped[u];
            bool offered = false;
            for (size_t o = 0; o < app->map_offered.size(); o++)
            {
                offered = offered || app->map_offered[o] == joystick;
            }
            if (!offered)
            {
                app->map_offered.push_back(joystick);
                map_window_offer(map, joystick, true, &app->layouts, &app->input, now);
                break;
            }
        }
    }
    if (!map_window_open_now(map) && frame->joined_controller >= 0)
    {
        uint32_t device_id = app->input.controllers[static_cast<size_t>(frame->joined_controller)].device_id;
        const InputDevice* device = input_devices_find_const(&app->input.devices, device_id);
        if (device != nullptr && device->kind == InputDeviceKind::gamepad)
        {
            const InputProfile* profile = input_profile(&app->input, device->guid);
            uint32_t joystick = input_sdl_joystick_of(&app->input_sdl, device_id);
            if ((profile == nullptr || profile->appearance.empty()) && joystick != 0)
            {
                map_window_offer(map, joystick, false, &app->layouts, &app->input, now);
            }
        }
    }
    if (app->input_window.map_request != 0 && !map_window_open_now(map))
    {
        map_window_open(map, app->input_window.map_request, &app->layouts, &app->input);
    }
    app->input_window.map_request = 0;
    if (!map->result.empty())
    {
        SDL_Log("Map: %s", map->result.c_str());   // (the MAP window showed it: no message)
        map->result.clear();
    }
    if (map->profiles_dirty && !app->input_config_path.empty())
    {
        save_input_config(app);
        map->profiles_dirty = false;
    }
}

// The ports' cursors for the core: from the part of the frame on screen to
// the whole frame (a crop shows less of it). And the mouse grabbed while a
// port's core device is a mouse with a mouse on it (no UI up, not let go
// with MOUSE GRAB).
static void update_cursors(App* app, const InputFrame* frame, bool ui_up)
{
    for (uint32_t p = 0; p < INPUT_PORT_COUNT && p < LIBRETRO_MAX_PORTS; p++)
    {
        LibretroCursorState* cursor = &app->cursor_input.cursor[p];
        float x = (static_cast<float>(frame->cursor_x[p]) / 32767.0f + 1.0f) * 0.5f;
        float y = (static_cast<float>(frame->cursor_y[p]) / 32767.0f + 1.0f) * 0.5f;
        if (app->view_frame[0] > 0 && app->view_frame[1] > 0 && app->view_source[2] > 0 && app->view_source[3] > 0)
        {
            x = (static_cast<float>(app->view_source[0]) + x * static_cast<float>(app->view_source[2])) /
                static_cast<float>(app->view_frame[0]);
            y = (static_cast<float>(app->view_source[1]) + y * static_cast<float>(app->view_source[3])) /
                static_cast<float>(app->view_frame[1]);
        }
        cursor->x = static_cast<int16_t>(x * 65534.0f - 32767.0f);
        cursor->y = static_cast<int16_t>(y * 65534.0f - 32767.0f);
        cursor->off_screen = frame->cursor_off[p] || app->input.view_width <= 0.0f;
        cursor->mouse_dx = frame->mouse_dx[p];
        cursor->mouse_dy = frame->mouse_dy[p];
        cursor->buttons = frame->mouse_buttons[p];   // (the same bits: left, right, middle, wheel, reload, the lightgun's)
    }
    if (frame->pressed[static_cast<uint32_t>(InputAction::hotkey_grab_mouse)])
    {
        app->mouse_released = !app->mouse_released;
    }
    bool mouse_port = false;
    for (uint32_t p = 0; game_running(app) && p < INPUT_PORT_COUNT; p++)
    {
        if ((app->session.host->port_devices[p] & RETRO_DEVICE_MASK) != RETRO_DEVICE_MOUSE)
        {
            continue;
        }
        for (size_t m = 0; m < app->input.ports[p].size(); m++)
        {
            uint32_t controller = app->input.ports[p][m];
            const InputDevice* device = input_devices_find_const(&app->input.devices, app->input.controllers[controller].device_id);
            mouse_port = mouse_port || (device != nullptr && device->kind == InputDeviceKind::mouse);
        }
    }
    bool grab = mouse_port && !ui_up && !app->mouse_released && app->window_focused;
    if (grab != app->mouse_grabbed)
    {
        SDL_SetWindowRelativeMouseMode(app->shell.window, grab);
        app->mouse_grabbed = grab;
    }
}

// --- The controllers, once per loop -----------------------------------------

// Is a connected gamepad (local or remote) on any port?
bool gamepad_on_a_port(const App* app)
{
    for (size_t c = 0; c < app->input.controllers.size(); c++)
    {
        const InputDevice* device = input_devices_find_const(&app->input.devices, app->input.controllers[c].device_id);
        if (device != nullptr && device->connected && device->kind == InputDeviceKind::gamepad &&
            input_controller_port(&app->input, static_cast<uint32_t>(c)) != INPUT_NO_PORT)
        {
            return true;
        }
    }
    return false;
}

// PAUSE WHEN CONTROLLERS DISCONNECT: notes whether a local gamepad played
// last (a RetroPad input held in the game), or something that never
// disconnects that way: the keyboard, a mouse, a device's pad over the
// network, the on-screen pad (ui_derive's CONTROLLERS cause).
static void follow_what_plays(App* app)
{
    uint32_t pad_actions = static_cast<uint32_t>(InputAction::pad_turbo);   // the RetroPad's buttons and sticks
    for (size_t c = 0; c < app->input.controllers.size(); c++)
    {
        const InputController* controller = &app->input.controllers[c];
        if (controller->layer != InputLayer::game)
        {
            continue;
        }
        bool playing = false;
        for (uint32_t a = 0; a < pad_actions && !playing; a++)
        {
            playing = controller->held_last[a];
        }
        const InputDevice* device = input_devices_find_const(&app->input.devices, controller->device_id);
        if (playing && device != nullptr)
        {
            app->played_with_gamepad = device->kind == InputDeviceKind::gamepad && !device->game_only && !device->retropad;
        }
    }
}

// The remote pads' ping in the ports' tabs and DEVICES: read from the
// network (ENet's round trip) twice a second while they show.
constexpr uint64_t PORT_PING_EVERY_MS = 500;

static void update_port_pings(App* app)
{
    uint64_t now = SDL_GetTicks();
    if (!ports_widget_showing(&app->ports_widget) || !app->net_running || now - app->ports_ping_ms < PORT_PING_EVERY_MS)
    {
        return;
    }
    app->ports_ping_ms = now;
    bool known[NET_MAX_PEERS] = {};
    uint32_t ping_ms[NET_MAX_PEERS] = {};
    net_service_lock(&app->net_service);
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        NetLinkStats stats;
        known[peer] = net_access_peer_stats(&app->net_service.access, peer, &stats);
        ping_ms[peer] = stats.round_trip_ms;
    }
    net_service_unlock(&app->net_service);
    ports_widget_set_pings(&app->ports_widget, &app->input, &app->input_net, known, ping_ms);
}

// A controller layout by its shown name (touch_pad_style keeps the name),
// or nullptr (RETROPAD, AUTO's "" or a layout that's gone: the RetroPad).
const InputLayout* find_layout_by_name(const InputLayouts* layouts, const char* name)
{
    for (size_t i = 0; name[0] != '\0' && i < layouts->layouts.size(); i++)
    {
        if (layouts->layouts[i].name == name)
        {
            return &layouts->layouts[i];
        }
    }
    return nullptr;
}

// STYLE's AUTO ("", the default): the layout listing the running game's
// extension (layouts/*.cfg extensions =; "Game.sfc", or the file inside an
// archive, "snes.7z#USA/Game.sfc"), or nullptr (the RetroPad).
const InputLayout* auto_layout(const App* app)
{
    if (!core_session_running(&app->session))
    {
        return nullptr;
    }
    std::string path = !app->marker_content_path.empty() ? app->marker_content_path : app->session.host->content_path;
    size_t cut = path.find_last_of("/\\#");
    std::string file = cut == std::string::npos ? path : path.substr(cut + 1);
    size_t dot = file.find_last_of('.');
    if (dot == std::string::npos)
    {
        return nullptr;
    }
    std::string extension = file.substr(dot + 1);
    for (size_t i = 0; i < extension.size(); i++)
    {
        extension[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(extension[i])));
    }
    return input_layouts_for_extension(&app->layouts, extension.c_str());
}

// Once per loop, after the events: what the controllers say (ports,
// hotkeys), handed to the core session before it runs frames.
void app_update_input(App* app)
{
    // The mode first (windows that opened or closed themselves, widgets,
    // grabs), then what it means for the router.
    if (!game_running(app))
    {
        ui_game_stopped(&app->ui);
    }
    ui_sync_windows(app);
    ui_follow_widgets(app);
    ui_follow_grabs(app);
    app->ui_derived = app_derive(app);
    bool ui_up = app->ui_derived.interface_layer;
    piccolo_settings_input(&app->settings, &app->input.settings);   // SETTINGS > INPUT, per core / game too
    app->input.settings.ui_active = ui_up;   // the INTERFACE layer: the core gets no input
    app->input.settings.keyboard_hotkeys_free = app->ui_derived.keyboard_hotkeys_free;   // MAIN MENU: no Shift
    text_input_set_osk(&app->shell.text_input, piccolo_settings_osk(&app->settings) ? TextInputOsk::on : TextInputOsk::off);
    // The core and the device on each port: a controller's bindings for them.
    app->input.core_name = game_running(app) ? app->core_name : "";
    app->input.game_name = game_running(app) ? app->game_name : "";   // a game's own remaps
    for (unsigned port = 0; port < LIBRETRO_MAX_PORTS; port++)
    {
        app->input.port_devices[port] = app->session.host->port_devices[port];
    }
    // The touch gamepad: a controller while SHOW allows it; what its parts
    // held (drawn last frame) goes to the router while a game plays.
    OnscreenPadShow show = static_cast<OnscreenPadShow>(piccolo_settings_touch_pad(&app->settings));
    // The browser's page pad (web_pad.h) takes its place while it shows.
    bool onscreen_on = onscreen_pad_enabled(&app->onscreen_pad, show) && !web_pad_active();
    onscreen_pad_update(&app->onscreen_pad, &app->input, onscreen_on, show, app->ui_derived.overlay_active);
    InputFrame frame = {};
    input_update(&app->input, SDL_GetTicks(), &frame);
    offer_mapping(app, &frame);
    update_cursors(app, &frame, ui_up);
    if (frame.profiles_changed && !app->input_config_path.empty())
    {
        save_input_config(app);   // a turbo's speed, by taps (in the game)
    }
    // Joins and disconnects: the ports' tabs show them (no status line); logged.
    char message[300] = {};
    if (frame.joined_port != INPUT_NO_PORT && frame.joined_controller >= 0)
    {
        std::snprintf(message, sizeof(message), piccolo_text(&app->strings, Str::input_joined),
                      input_controller_label(&app->input, static_cast<uint32_t>(frame.joined_controller)).c_str(), frame.joined_port + 1);
        SDL_Log("%s", message);
    }
    if (frame.left_port != INPUT_NO_PORT && frame.left_controller >= 0)
    {
        std::snprintf(message, sizeof(message), piccolo_text(&app->strings, Str::input_left),
                      input_controller_label(&app->input, static_cast<uint32_t>(frame.left_controller)).c_str(), frame.left_port + 1);
        SDL_Log("%s", message);
        // The ports' tabs show who left; the game goes on (joining again is
        // just START). Only when that was the last gamepad and nobody was
        // playing with the keyboard or the on-screen pad (played_with_gamepad)
        // does it pause (the setting) and CONTROLS (the ports' board) open,
        // where a controller joins again (Andrés, testing round 3; no message).
        // (ui_derive's CONTROLLERS cause; it lifts by itself when one is back.)
        if (game_running(app) && settings_get_bool(&app->settings, PICCOLO_PAUSE_ON_DISCONNECT) &&
            app->played_with_gamepad && !gamepad_on_a_port(app))
        {
            SDL_Log("Paused: the last gamepad playing went away");
            if (app->ui.mode == UiMode::game && app->ports_widget.port_count > 0)
            {
                app_open_widget(app, UiWidget::ports);
            }
        }
    }
    follow_what_plays(app);
    // The ports' tabs and DEVICES: its moves first (each controller reads
    // its own buttons), then who is where.
    bool board_was_open = app->ports_widget.open;
    ports_widget_board_update(&app->ports_widget, &app->input);
    // Without a game, the PORTS setting's: a pad joining at the menu (START)
    // makes the tabs rise too, as in a game (Andrés: that's what shows a join).
    uint32_t shown_ports = game_running(app) ? piccolo_settings_ports(&app->settings, app->session.host->input_info.port_count)
                                             : piccolo_settings_ports(&app->settings, 0);
    ports_widget_update(&app->ports_widget, &app->input, &app->input_net, &frame, shown_ports, SDL_GetTicks());
    // HOLD START TO REMAP: its cards watch REMAP held (in GAME); holding it
    // opens QUICK MENU on that controller's remap.
    RemapWidgetResult remap = remap_widget_update(&app->remap, &app->input, &app->input_sdl, &app->layouts,
                                                  game_running(app), app->ui.mode == UiMode::game, app->headless.on,
                                                  SDL_GetTicks());
    if (remap.save && !app->input_config_path.empty())
    {
        save_input_config(app);   // a RetroPad remembered as decided
    }
    if (remap.open_remap)
    {
        app_open_core_config(app, remap.port, remap.controller);   // CONTROLS, that controller's remap
    }
    // A pairing request's card (in GAME): START held opens its window (the
    // game pauses then, as for a request that opens it by itself).
    if (pair_window_hold_accept(&app->pair_window, pair_request_in_game(app), pair_accept_held(app), SDL_GetTicks()))
    {
        SDL_Log("Network: pairing request accepted from the game: its window opens");
    }
    // The debug window's fake request card: the same hold, nothing accepted.
    debug_preview_hold_request(&app->debug_previews, game_has_keyboard(app), pair_accept_held(app), SDL_GetTicks());
    update_port_pings(app);
    // The DVR reads its buttons itself (before the hotkeys: the one that
    // opens it doesn't act in it the same frame).
    bool dvr_was_open = app->dvr.open;
    dvr_widget_update(&app->dvr, &frame, SDL_GetTicks());
    app_handle_hotkeys(app, &frame);
    // DEVICES reads the pads itself: they don't drive the menus meanwhile, and
    // its BACK (Escape too) only closes it. The DVR the same.
    bool board = board_was_open || app->ports_widget.open || dvr_was_open || app->dvr.open;
    drive_ui_with_gamepads(app, &frame, ui_up && !board);
    // Escape in the UI: back, at once (an open list or popup is the toolkit's:
    // it closes on the same key).
    if (frame.keyboard_pressed[static_cast<uint32_t>(InputAction::ui_back)] && !board_was_open && !dvr_was_open &&
        !ui_toolkit_has_popup())
    {
        ui_do_back(app);
    }
    // PAGE LEFT / RIGHT change tabs (not while typing in a field).
    int page = 0;
    if (ui_up && !ui_toolkit_wants_text())
    {
        page = frame.pressed[static_cast<uint32_t>(InputAction::ui_page_right)] ? 1
             : frame.pressed[static_cast<uint32_t>(InputAction::ui_page_left)]  ? -1 : 0;
    }
    ui_toolkit_page_request(page);
    core_session_set_pads(&app->session, frame.pad_buttons, frame.analog);
    bool net_fast_forward = false;
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        net_fast_forward = net_fast_forward || app->net_fast_forward[i];
    }
    app->session.fast_forward_held =
        game_running(app) && (frame.held[static_cast<uint32_t>(InputAction::hotkey_fast_forward)] || net_fast_forward);
    if (app->session.host != nullptr)
    {
        input_sdl_rumble(&app->input_sdl, &app->input, app->session.host->rumble, LIBRETRO_MAX_PORTS);
    }
}

// --- Events -----------------------------------------------------------------

// piccolo:// links (app_links.h; Andrés, 2026-11-22: for links to come, such
// as joining a game from the lobby: piccolo://join/<room> is kept for that).
// Nothing opens one yet: the window comes forward and says so.
void handle_links(App* app)
{
    std::string link;
    while (app_links_next(&app->links, &link))
    {
        SDL_RaiseWindow(app->shell.window);
        // A guest link (piccolo://join/...): guests play in piccoletto; piccolo
        // has no player of its own yet (it may get one), so it only says so
        // (Andrés, testing round 3: no more handing it to the browser).
        NetGuestLink guest;
        char beacon[NET_GUEST_MAX_BEACON];
        if (net_guest_link_parse(link.c_str(), &guest, beacon, sizeof(beacon)))
        {
            net_guest_link_wipe(&guest);
            set_status(app, piccolo_text(&app->strings, Str::status_guest_link));
            continue;
        }
        // A join link that didn't parse may still hold a token: only its
        // scheme and host are shown or logged. Any other link is logged cut at
        // the query / fragment and at a sane length.
        std::string shown = link;
        bool join_link = link.compare(0, 14, "piccolo://join") == 0;
        if (join_link)
        {
            shown = link.substr(0, 14);   // "piccolo://join"
        }
        else
        {
            size_t cut = shown.find_first_of("?#");
            if (cut != std::string::npos)
            {
                shown.resize(cut);
            }
            if (shown.size() > 200)
            {
                shown.resize(200);
            }
        }
        SDL_Log("Link: %s", shown.c_str());
        std::string text = std::string(piccolo_text(&app->strings, Str::status_link_unknown)) + " " +
                           (join_link ? shown : link);
        set_status(app, text.c_str());
    }
}

// The language setting -> the UI text and what cores are told (GET_LANGUAGE,
// from the next core start). Only redone when the setting changes: "auto"
// asks the system, which allocates.
void update_language(App* app)
{
    uint32_t setting = settings_get_enum(&app->settings, PICCOLO_LANGUAGE);
    if (setting == app->language_setting)
    {
        return;
    }
    app->language_setting = setting;
    uint32_t language = piccolo_settings_language(&app->settings);
    app->session.language = piccolo_language_retro(language);
    piccolo_text_load_language(&app->strings, language, app->paths.root.c_str());
    web_page_set_language(piccolo_language_code(language));
    refresh_recent_labels(app);
}

void refresh_audio_devices(App* app)
{
    audio_output_list_devices(&app->audio_devices);
    app->audio_device_pointers.clear();
    for (size_t i = 0; i < app->audio_devices.size(); i++)
    {
        app->audio_device_pointers.push_back(app->audio_devices[i].c_str());
    }
}

void app_handle_events(App* app)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        // A link dropped on the window, or Android's (PiccoloActivity hands a
        // link to the running app as a dropped file).
        if ((event.type == SDL_EVENT_DROP_TEXT || event.type == SDL_EVENT_DROP_FILE) && event.drop.data != nullptr)
        {
            app_links_push(&app->links, event.drop.data);
        }
        // The keyboard's Escape always cancels an input grab (SET, MAP
        // CONTROLLER, the macro recorder), before anything else sees it:
        // the router is off or reading raw controls meanwhile.
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE && grab_now(app) != UiGrab::none)
        {
            SDL_Log("Escape: input grab cancelled");
            cancel_grabs(app);
            continue;
        }
        // Android's back button is the app's, before a core that has the keyboard.
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_AC_BACK)
        {
            if (!event.key.repeat)
            {
                SDL_Log("Back button");
                app_back_button(app);
            }
            continue;
        }
        if (event.type == SDL_EVENT_KEY_UP && event.key.scancode == SDL_SCANCODE_AC_BACK)
        {
            continue;
        }
        if (core_session_handle_event(&app->session, &app->shell, &event, game_has_keyboard(app)))
        {
            continue;
        }
        // While INPUT waits for a key (SET), keys are the binding's, not the menus'.
        bool key_event = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_TEXT_INPUT;
        if (!(key_event && (app->input_window.capturing || app->core_config.capturing)))
        {
            app_shell_process_event(&app->shell, &event);
        }
        input_sdl_handle_event(&app->input_sdl, &app->input, &event);
        float screen_width = 0.0f;
        float screen_height = 0.0f;
        ui_toolkit_screen_size(&screen_width, &screen_height);
        onscreen_pad_event(&app->onscreen_pad, &event, screen_width, screen_height);

        if ((event.type == SDL_EVENT_AUDIO_DEVICE_ADDED || event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED) &&
            !event.adevice.recording)
        {
            refresh_audio_devices(app);
        }
        if (event.type == SDL_EVENT_QUIT)
        {
            app->running = false;
        }
        // Android: the BACKGROUND pause cause (SDL keeps running a little while
        // the app is behind another one).
        if (event.type == SDL_EVENT_DID_ENTER_BACKGROUND)
        {
            app->android_background = true;
            if (game_running(app))
            {
                // Android may kill piccolo back there without a quit: the
                // game saved, and that end counts as a proper one (no SAFE
                // START, the AUTO state loads next time).
                save_state_on_exit(app);
                remove_running_marker(app);
            }
        }
        if (event.type == SDL_EVENT_WILL_ENTER_FOREGROUND || event.type == SDL_EVENT_DID_ENTER_FOREGROUND)
        {
            if (app->android_background && game_running(app))
            {
                write_running_marker(app);   // running again
            }
            app->android_background = false;
        }
        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            app->window_focused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(app->shell.window))
        {
            app->running = false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            switch (event.key.key)
            {
                case SDLK_F11:
                    toggle_fullscreen(app);
                    break;
                case SDLK_RETURN:
                    if (event.key.mod & SDL_KMOD_ALT)
                    {
                        toggle_fullscreen(app);
                    }
                    break;
                default:
                    break;
            }
        }
    }
}
