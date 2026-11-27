// What connected devices are told and what they ask for: the game's state,
// the actions, controllers and devices, the speed test, the stream, the
// games list.

#include "app_net_remote.h"

#include "app_files.h"
#include "app_game.h"
#include "app_states.h"
#include "app_ui_modes.h"

// --- The game's state -------------------------------------------------------

// game_state (what runs now) to one authorized peer, or to all of them
// (NET_NO_PEER).
void net_send_game_state(App* app, uint32_t peer)
{
    size_t size = net_games_write_state(&app->net_games, &app->catalog, app->net_sent_core.c_str(),
                                        app->net_sent_content.c_str());
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        // A guest hears what runs too (net_access_send lets game_state through to it).
        NetAccessPeerState state = app->net_service.access.peers[i].state;
        if ((peer == NET_NO_PEER || peer == i) && (state == NetAccessPeerState::authorized || state == NetAccessPeerState::guest))
        {
            net_access_send(&app->net_service.access, i, NetChannel::control, app->net_games.reply.data(), size);
        }
    }
}

// Tells every device when what runs changed, however it changed (LOAD GAME,
// the network, CLOSE GAME, a core that quit). Compared every loop, copied
// only on a change.
void net_follow_game_state(App* app)
{
    bool running = game_running(app);
    const char* core = running ? app->marker_core_path.c_str() : "";
    const char* content = running ? app->marker_content_path.c_str() : "";
    if (app->net_sent_core == core && app->net_sent_content == content)
    {
        return;
    }
    app->net_sent_core = core;
    app->net_sent_content = content;
    net_send_game_state(app, NET_NO_PEER);
}

static NetRemoteGame net_remote_game(const App* app)
{
    NetRemoteGame game;
    game.running = game_running(app);
    game.slot = app->session.state_slot;
    game.pause = 0;
    if (game.running)
    {
        // The wire's bits (net_settings.h), not UiPause's: a device reads them.
        const uint32_t pause = app->ui_derived.pause;
        if (app->ui.user_paused)
        {
            game.pause |= NET_PAUSE_USER;
        }
        if ((pause & ui_pause_bit(UiPause::menu)) != 0)
        {
            game.pause |= NET_PAUSE_MENU;
        }
        if ((pause & ui_pause_bit(UiPause::focus)) != 0)
        {
            game.pause |= NET_PAUSE_FOCUS;
        }
        if ((pause & ui_pause_bit(UiPause::controllers)) != 0)
        {
            game.pause |= NET_PAUSE_CONTROLLERS;
        }
        if ((pause & ui_pause_bit(UiPause::background)) != 0)
        {
            game.pause |= NET_PAUSE_BACKGROUND;
        }
        if ((pause & ui_pause_bit(UiPause::pairing)) != 0)
        {
            game.pause |= NET_PAUSE_PAIRING;
        }
    }
    return game;
}

// What a connected device may use here now (its grants, what we offer and it uses).
static NetCaps net_peer_caps(const App* app, uint32_t peer)
{
    return net_access_peer_caps(&app->net_service.access, peer);
}

// --- GO HEADLESS ------------------------------------------------------------

// GO HEADLESS (headless.h, net_stream.h): the state, to every authorized
// device that may stream (each learns whether it's the one holding it).
void headless_tell(App* app)
{
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        if (app->net_service.access.peers[i].state != NetAccessPeerState::authorized ||
            (net_peer_caps(app, i) & NET_CAP_STREAM) == 0)
        {
            continue;
        }
        uint8_t out[4];
        size_t size = net_stream_write_headless_state(out, sizeof(out), app->headless.on,
                                                      app->headless.on && app->headless.peer == i);
        net_access_send(&app->net_service.access, i, NetChannel::control, out, size);
    }
}

// Back on piccolo's window. `pause`: the device that had the game is gone
// (or stopped streaming), so nobody plays it: paused until someone here does.
void headless_back(App* app, bool pause)
{
    if (!app->headless.on)
    {
        return;
    }
    headless_leave(&app->headless, app->shell.window);
    if (pause && game_running(app))
    {
        app->ui.user_paused = true;   // it stays paused until the player here goes on
        set_status(app, piccolo_text(&app->strings, Str::status_headless_paused));
    }
    else
    {
        set_status(app, piccolo_text(&app->strings, Str::status_headless_back));
    }
    SDL_Log("Headless: back on the window%s", pause ? " (paused)" : "");
}

// --- Actions ----------------------------------------------------------------

// The actions one authorized peer may trigger (`id` 0: sent unasked), or
// to all of them (NET_NO_PEER), each with its own level.
void net_send_actions(App* app, uint32_t peer, uint32_t id)
{
    if (!app->net_remote_ready)
    {
        return;
    }
    NetRemoteGame game = net_remote_game(app);
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        if ((peer == NET_NO_PEER || peer == i) && app->net_service.access.peers[i].state == NetAccessPeerState::authorized)
        {
            app->net_actions_caps[i] = net_peer_caps(app, i);
            size_t size = net_remote_write_actions(&app->net_remote, id, app->net_peer_language[i],
                                                   app->paths.root.c_str(), app->net_actions_caps[i], &game);
            net_access_send(&app->net_service.access, i, NetChannel::control, app->net_remote.reply.data(), size);
        }
    }
}

// An action from a device, with the same effect as its hotkey or quick
// menu item.
static void net_do_action(App* app, uint32_t peer, const std::string* key, bool pressed, const char* device)
{
    NetRemoteGame game = net_remote_game(app);
    if (!net_remote_action_allowed(key->c_str(), net_peer_caps(app, peer), &game))
    {
        return;
    }
    if (*key == "fast_forward")
    {
        app->net_fast_forward[peer] = pressed;   // held until released (or the device leaves)
        return;
    }
    if (!pressed)
    {
        return;   // the others act on the press
    }
    if (*key == "save_state")
    {
        save_state_from(app, true);
    }
    else if (*key == "load_state")
    {
        load_state_from(app, true);
    }
    else if (*key == "undo_save_state")
    {
        undo_save_from(app, true);
    }
    else if (*key == "undo_load_state")
    {
        undo_load_from(app, true);
    }
    else if (*key == "slot_next" || *key == "slot_previous")
    {
        uint32_t slot = app->session.state_slot;
        if (*key == "slot_next")
        {
            slot = slot >= QUICK_MENU_SLOT_LAST ? QUICK_MENU_SLOT_FIRST : slot + 1;
        }
        else
        {
            slot = slot <= QUICK_MENU_SLOT_FIRST ? QUICK_MENU_SLOT_LAST : slot - 1;
        }
        app->session.state_slot = slot;
        app->quick_menu.slot = slot;
        char status[64];
        std::snprintf(status, sizeof(status), "%s %u", piccolo_text(&app->strings, Str::quick_menu_slot), slot);
        set_status(app, status);
    }
    else if (*key == "pause")
    {
        // The USER pause, the same as here: only a player lifts it (this
        // device, another one, or here), even after this device leaves.
        app->ui.user_paused = game_running(app) && !app->ui.user_paused;
        if (app->ui.user_paused)
        {
            char status[256];
            std::snprintf(status, sizeof(status), "%s %s", piccolo_text(&app->strings, Str::status_paused_remotely),
                          device);
            set_status(app, status);
        }
        else
        {
            app->resumed_ms = SDL_GetTicks();   // PLAY, as here
        }
    }
    else if (*key == "reset")
    {
        if (soft_reset(app))
        {
            app_leave_main_menu(app);
            set_status(app, piccolo_text(&app->strings, Str::status_reset));   // nothing here showed it
        }
    }
    else if (*key == "close_game")
    {
        app->pending_close_game = true;   // at the top of the next loop, like the quick menu's
    }
}

// --- Controllers ------------------------------------------------------------

// The ports the core has, as the quick menu's CONTROLS shows them: as many
// as SETTINGS > INPUT > PORTS says, no more than the core describes. 0 when
// no game runs.
static uint32_t net_core_ports(const App* app)
{
    const LibretroHost* host = app->session.host;
    if (!game_running(app) || host == nullptr)
    {
        return 0;
    }
    return piccolo_settings_ports(&app->settings, host->input_info.port_count);
}

// The port a peer's pad plays on, or -1.
static int32_t net_pad_port(const App* app, uint32_t peer, uint32_t pad)
{
    const InputNetPad* entry = &app->input_net.pads[peer][pad];
    if (!entry->active)
    {
        return -1;
    }
    int32_t controller = input_first_controller(&app->input, entry->device);
    return controller < 0 ? -1 : input_controller_port(&app->input, static_cast<uint32_t>(controller));
}

// What the core expects on each port, and where this peer's pads play.
static void net_controllers_for(const App* app, uint32_t peer, NetControllers* controllers)
{
    for (uint32_t pad = 0; pad < NET_CONTROLLERS_PADS; pad++)
    {
        controllers->pad_ports[pad] = static_cast<int8_t>(net_pad_port(app, peer, pad));
    }
    controllers->ports.clear();
    uint32_t ports = net_core_ports(app);
    const LibretroHost* host = app->session.host;
    for (uint32_t p = 0; p < ports; p++)
    {
        NetPort port;
        port.device = host->port_devices[p];
        const LibretroInputInfo* info = &host->input_info;
        if (p < info->port_count)
        {
            for (uint32_t t = 0; t < info->ports[p].count; t++)
            {
                NetPortDevice device;
                device.id = info->ports[p].types[t].id;
                device.name = info->ports[p].types[t].description;
                port.devices.push_back(device);
            }
        }
        for (uint32_t d = 0; d < info->descriptor_count; d++)
        {
            const LibretroInputDescriptor* in = &info->descriptors[d];
            if (in->port != p)
            {
                continue;
            }
            NetPortDescriptor descriptor;
            descriptor.device = static_cast<uint8_t>(in->device);
            descriptor.index = static_cast<uint8_t>(in->index);
            descriptor.id = static_cast<uint8_t>(in->id);
            descriptor.name = in->description;
            port.descriptors.push_back(descriptor);
        }
        controllers->ports.push_back(port);
    }
}

// A summary of what net_controllers_for would say, compared every loop
// (no allocation) to know when to send it again.
static uint64_t net_controllers_summary(const App* app, uint32_t peer)
{
    uint64_t summary = 1469598103934665603ull;   // FNV-1a over the numbers
    uint32_t values[16] = {};
    uint32_t count = 0;
    for (uint32_t pad = 0; pad < NET_CONTROLLERS_PADS; pad++)
    {
        values[count++] = static_cast<uint32_t>(net_pad_port(app, peer, pad) + 1);
    }
    uint32_t ports = net_core_ports(app);
    values[count++] = ports;
    if (ports > 0)
    {
        values[count++] = app->session.host->input_info.controller_serial;
        values[count++] = app->session.host->input_info.descriptor_serial;
        for (uint32_t p = 0; p < ports && count < 16; p++)
        {
            values[count++] = app->session.host->port_devices[p];
        }
    }
    for (uint32_t i = 0; i < count; i++)
    {
        summary = (summary ^ values[i]) * 1099511628211ull;
    }
    return summary;
}

void net_send_controllers(App* app, uint32_t peer, uint32_t id)
{
    NetControllers controllers;
    net_controllers_for(app, peer, &controllers);
    size_t size = net_settings_write_controllers(app->net_remote.reply.data(), app->net_remote.reply.size(), id,
                                                 &controllers);
    net_access_send(&app->net_service.access, peer, NetChannel::control, app->net_remote.reply.data(), size);
    app->net_controllers_sent[peer] = net_controllers_summary(app, peer);
}

// port_device: the device on the port this peer's pad plays on, as the
// quick menu's CONTROLS changes it (the core is told; kept for the core).
static void net_change_port_device(App* app, uint32_t peer, uint8_t pad, uint32_t device_id, const char* device)
{
    int32_t port = net_pad_port(app, peer, pad);
    const LibretroHost* host = app->session.host;
    if (port < 0 || static_cast<uint32_t>(port) >= net_core_ports(app) ||
        static_cast<uint32_t>(port) >= host->input_info.port_count)
    {
        return;   // not on a port, or the core offers nothing to choose there
    }
    const LibretroPortControllers* types = &host->input_info.ports[port];
    bool offered = false;
    for (uint32_t t = 0; t < types->count && !offered; t++)
    {
        offered = types->types[t].id == device_id;
    }
    if (!offered)
    {
        return;
    }
    libretro_host_set_port_device(app->session.host, static_cast<unsigned>(port), device_id);
    settings_set_int(&app->settings, SettingLayer::core, PICCOLO_INPUT_PORT_DEVICES[port],
                     static_cast<int32_t>(device_id));
    if (!app->settings_window.open)
    {
        save_settings(app);
    }
    SDL_Log("Network: %s set port %d to device %u", device, port + 1, device_id);
}

// --- Devices ----------------------------------------------------------------

// Who plays on piccolo's ports now (devices): only controllers that joined
// a port and whose device is connected. `peer` is the device asking (its own
// pads are marked).
static void net_devices_for(const App* app, uint32_t peer, std::vector<NetDevice>* devices)
{
    devices->clear();
    for (size_t c = 0; c < app->input.controllers.size(); c++)
    {
        uint32_t controller = static_cast<uint32_t>(c);
        uint8_t ports = 0;
        for (uint32_t p = 0; p < INPUT_PORT_COUNT; p++)
        {
            if (input_port_has(&app->input, p, controller))
            {
                ports = static_cast<uint8_t>(ports | (1u << p));
            }
        }
        const InputDevice* device = input_devices_find_const(&app->input.devices, app->input.controllers[c].device_id);
        if (ports == 0 || device == nullptr || !device->connected)
        {
            continue;
        }
        NetDevice entry;
        entry.controller = controller;
        entry.device = device->id;
        entry.ports = ports;
        entry.name = input_controller_label(&app->input, controller);
        entry.kind = device->kind == InputDeviceKind::keyboard ? NetDeviceKind::keyboard
                     : device->kind == InputDeviceKind::mouse  ? NetDeviceKind::mouse
                                                                : NetDeviceKind::gamepad;
        // A paired device's pad: whose, and which of its pads.
        for (uint32_t owner = 0; owner < NET_MAX_PEERS; owner++)
        {
            for (uint32_t pad = 0; pad < NET_INPUT_MAX_PADS; pad++)
            {
                const InputNetPad* net_pad = &app->input_net.pads[owner][pad];
                if (net_pad->active && net_pad->device == device->id)
                {
                    entry.kind = NetDeviceKind::network;
                    entry.owner = owner == peer ? NetDeviceOwner::yours : NetDeviceOwner::other;
                    entry.pad = static_cast<uint8_t>(pad);
                }
            }
        }
        devices->push_back(entry);
    }
}

// A summary of net_devices_for, compared every loop (no allocation).
static uint64_t net_devices_summary(const App* app, uint32_t peer)
{
    uint64_t summary = 1469598103934665603ull;
    for (size_t c = 0; c < app->input.controllers.size(); c++)
    {
        uint32_t controller = static_cast<uint32_t>(c);
        uint32_t ports = 0;
        for (uint32_t p = 0; p < INPUT_PORT_COUNT; p++)
        {
            ports |= input_port_has(&app->input, p, controller) ? (1u << p) : 0u;
        }
        const InputDevice* device = input_devices_find_const(&app->input.devices, app->input.controllers[c].device_id);
        if (ports == 0 || device == nullptr || !device->connected)
        {
            continue;
        }
        uint32_t values[3] = {controller, device->id, ports};
        for (uint32_t i = 0; i < 3; i++)
        {
            summary = (summary ^ values[i]) * 1099511628211ull;
        }
    }
    return (summary ^ peer) * 1099511628211ull;
}

static void net_send_devices(App* app, uint32_t peer, uint32_t id)
{
    std::vector<NetDevice> devices;
    net_devices_for(app, peer, &devices);
    size_t size = net_settings_write_devices(app->net_remote.reply.data(), app->net_remote.reply.size(), id, &devices);
    net_access_send(&app->net_service.access, peer, NetChannel::control, app->net_remote.reply.data(), size);
    app->net_devices_sent[peer] = net_devices_summary(app, peer);
}

// device_port: that controller on that port only (-1: on none), as the quick
// menu's CONTROLS does it. Nothing is saved (START joins again next time).
// A stale controller / device pair (the list changed meanwhile) is ignored:
// the summary check sends the device a fresh `devices`.
static void net_move_device(App* app, uint32_t controller, uint32_t device_id, int8_t port, const char* device)
{
    if (controller >= app->input.controllers.size() || app->input.controllers[controller].device_id != device_id ||
        port >= static_cast<int8_t>(INPUT_PORT_COUNT) || port < -1)
    {
        return;
    }
    for (uint32_t p = 0; p < INPUT_PORT_COUNT; p++)
    {
        input_port_remove(&app->input, p, controller);
    }
    if (port >= 0)
    {
        input_port_add(&app->input, static_cast<uint32_t>(port), controller);
    }
    SDL_Log("Network: %s moved %s to port %d", device, input_controller_label(&app->input, controller).c_str(), port + 1);
}

// --- Following changes, and the remote's messages ---------------------------

// Every loop: settings that changed (from anywhere) go to the devices with SETTINGS,
// and the action lists again when what they show changed.
void net_follow_remote(App* app)
{
    if (!app->net_remote_ready)
    {
        return;
    }
    size_t size = net_remote_next_change(&app->net_remote, &app->settings);
    while (size > 0)
    {
        for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
        {
            if (app->net_service.access.peers[i].state == NetAccessPeerState::authorized &&
                (net_peer_caps(app, i) & NET_CAP_SETTINGS) != 0)
            {
                net_access_send(&app->net_service.access, i, NetChannel::control, app->net_remote.reply.data(), size);
            }
        }
        size = net_remote_next_change(&app->net_remote, &app->settings);
    }
    // A device whose permissions changed (DEVICES): its new welcome went out
    // with net_access_set_grants; its action list follows at once, and what it
    // may no longer use stops: remote play and a FAST FORWARD it holds
    // (STREAM since 2026-11-20; a PAUSE it made stays: only a player lifts
    // it). Speed tests and pads are GAMEPAD.
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        if (app->net_service.access.peers[i].state != NetAccessPeerState::authorized ||
            net_peer_caps(app, i) == app->net_actions_caps[i])
        {
            continue;
        }
        net_send_actions(app, i, 0);
        if ((net_peer_caps(app, i) & NET_CAP_STREAM) == 0)
        {
            app->net_fast_forward[i] = false;
            if (app->headless.on && app->headless.peer == i)
            {
                headless_back(app, true);   // remote play (and so the game) needs STREAM
                headless_tell(app);
            }
            net_service_set_stream(&app->net_service, i, 0);   // remote play needs STREAM
            app->stream_tuning_set[i] = false;
        }
        if ((net_peer_caps(app, i) & NET_CAP_GAMEPAD) == 0)
        {
            // Pads are GAMEPAD: a device that lost it (net_send_actions above
            // already took the new caps as seen) has its pads unplugged.
            input_net_peer_gone(&app->input_net, &app->input, i);
        }
    }
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        if (app->net_devices_wanted[i] && app->net_service.access.peers[i].state == NetAccessPeerState::authorized &&
            (net_peer_caps(app, i) & NET_CAP_SETTINGS) != 0 && net_devices_summary(app, i) != app->net_devices_sent[i])
        {
            net_send_devices(app, i, 0);   // someone joined, left or moved
        }
    }
    for (uint32_t i = 0; i < NET_MAX_PEERS; i++)
    {
        if (app->net_service.access.peers[i].state == NetAccessPeerState::authorized &&
            net_controllers_summary(app, i) != app->net_controllers_sent[i])
        {
            net_send_controllers(app, i, 0);   // a pad joined or left a port, another game, another device
        }
    }
    NetRemoteGame game = net_remote_game(app);
    if (game.running != app->net_actions_sent.running || game.slot != app->net_actions_sent.slot ||
        game.pause != app->net_actions_sent.pause)
    {
        app->net_actions_sent = game;
        net_send_actions(app, NET_NO_PEER, 0);
    }
}

// Settings and actions messages from a device (net_remote.h).
void net_remote_message(App* app, const NetAccessEvent* event, const char* device)
{
    if (!app->net_remote_ready)
    {
        return;
    }
    size_t size = net_remote_answer_settings(&app->net_remote, &app->settings, app->paths.root.c_str(),
                                             &app->audio_devices, event->data, event->size);
    if (size > 0)
    {
        net_access_send(&app->net_service.access, event->peer, NetChannel::control, app->net_remote.reply.data(), size);
        uint32_t request_id = 0;
        uint32_t start = 0;
        std::string language;
        if (event->peer < NET_MAX_PEERS &&
            net_settings_read_request(event->data, event->size, &request_id, &start, &language))
        {
            std::snprintf(app->net_peer_language[event->peer], sizeof(app->net_peer_language[event->peer]), "%s",
                          language.c_str());
        }
        bool changed = event->data[0] == static_cast<uint8_t>(NetSettingsMessage::setting_set);
        if (changed && !app->settings_window.open)
        {
            save_settings(app);   // nobody will close SETTINGS to save it
        }
        return;
    }
    uint32_t id = 0;
    std::string language;
    if (net_settings_read_actions_request(event->data, event->size, &id, &language))
    {
        if (event->peer < NET_MAX_PEERS)
        {
            std::snprintf(app->net_peer_language[event->peer], sizeof(app->net_peer_language[event->peer]), "%s",
                          language.c_str());
        }
        NetRemoteGame game = net_remote_game(app);
        size = net_remote_write_actions(&app->net_remote, id, language.c_str(), app->paths.root.c_str(),
                                        net_peer_caps(app, event->peer), &game);
        net_access_send(&app->net_service.access, event->peer, NetChannel::control, app->net_remote.reply.data(), size);
        return;
    }
    std::string key;
    bool pressed = false;
    if (net_settings_read_action(event->data, event->size, &key, &pressed))
    {
        net_do_action(app, event->peer, &key, pressed, device);
        return;
    }
    if (event->peer >= NET_MAX_PEERS)
    {
        return;
    }
    if (net_settings_read_controllers_request(event->data, event->size, &id))
    {
        net_send_controllers(app, event->peer, id);
        return;
    }
    uint8_t pad = 0;
    uint32_t device_id = 0;
    if (net_settings_read_port_device(event->data, event->size, &pad, &device_id))
    {
        net_change_port_device(app, event->peer, pad, device_id, device);
        return;
    }
    // devices / device_port: SETTINGS (net_access let them through only then);
    // devices_watch, the same list read only, at any capability (the ports' tabs).
    if (net_settings_read_devices_request(event->data, event->size, &id) ||
        net_settings_read_devices_watch(event->data, event->size, &id))
    {
        app->net_devices_wanted[event->peer] = true;
        net_send_devices(app, event->peer, id);
        return;
    }
    uint32_t controller = 0;
    int8_t port = 0;
    if (net_settings_read_device_port(event->data, event->size, &controller, &device_id, &port))
    {
        net_move_device(app, controller, device_id, port, device);
    }
}

// --- Speed test -------------------------------------------------------------

// A speed test request (net_speed.h; net_access let it through only for
// STREAM devices). One at a time: a request during another is ignored.
void net_speed_request(App* app, const NetAccessEvent* event)
{
    uint32_t id = 0;
    NetSpeedDirection direction = NetSpeedDirection::down;
    uint32_t rate = 0;
    uint32_t duration = 0;
    if (!net_speed_read_request(event->data, event->size, &id, &direction, &rate, &duration) ||
        app->net_speed_sender.active || app->net_speed_reporting || app->net_speed_receiver.active)
    {
        return;
    }
    double now = net_seconds();
    if (direction == NetSpeedDirection::down)
    {
        net_speed_sender_start(&app->net_speed_sender, id, event->peer, rate, duration, now);
    }
    else
    {
        net_speed_receiver_start(&app->net_speed_receiver, id, event->peer,
                                 now + static_cast<double>(duration) / 1000.0 + NET_SPEED_GRACE_SECONDS);
    }
}

// speed_result: one step of a device's speed test, as it measured it.
// `device_now`: the device's index in the list as it is now (net_trust_find), -1 when it is gone.
void net_speed_result(App* app, const NetAccessEvent* event, int32_t device_now)
{
    NetSpeedResult result;
    if (device_now < 0 || !net_speed_read_result(event->data, event->size, &result))
    {
        return;
    }
    const uint8_t* key = app->net_service.access.trust.devices[static_cast<size_t>(device_now)].key;
    NetSpeedSummary* summary = nullptr;
    for (size_t i = 0; i < app->net_speed_results.size() && summary == nullptr; i++)
    {
        if (std::memcmp(app->net_speed_results[i].key, key, NET_KEY_SIZE) == 0)
        {
            summary = &app->net_speed_results[i];
        }
    }
    if (summary == nullptr)
    {
        NetSpeedSummary entry;
        std::memcpy(entry.key, key, NET_KEY_SIZE);
        app->net_speed_results.push_back(entry);
        summary = &app->net_speed_results.back();
    }
    bool down = result.direction == NetSpeedDirection::down;
    uint32_t* best = down ? &summary->down_kbps : &summary->up_kbps;
    uint32_t* last_asked = down ? &summary->down_last_asked : &summary->up_last_asked;
    bool* tested = down ? &summary->down_tested : &summary->up_tested;
    if (result.asked_kbps <= *last_asked)
    {
        *best = 0;   // a new test (its first, lowest step)
    }
    *last_asked = result.asked_kbps;
    *tested = true;
    if (result.loss_per_mille <= NET_WINDOW_SPEED_MAX_LOSS && result.got_kbps > *best)
    {
        *best = result.got_kbps;
    }
}

// Every loop: the down test's packets that are due, and the reports.
void net_speed_update(App* app)
{
    double now = net_seconds();
    NetSpeedSender* sender = &app->net_speed_sender;
    if (sender->active)
    {
        uint8_t packet[NET_SPEED_PACKET_SIZE];
        uint32_t due = net_speed_sender_due(sender, now);
        for (uint32_t i = 0; i < due; i++)
        {
            size_t size = net_speed_write_data(packet, sizeof(packet), sender->id, sender->sent);
            net_access_send(&app->net_service.access, sender->peer, NetChannel::input, packet, size);
            net_speed_sender_sent(sender, size);   // counted as sent even if ENet dropped it here: that's loss too
        }
        app->net_speed_reporting = !sender->active;
    }
    uint8_t out[64];
    if (app->net_speed_reporting)
    {
        NetSpeedReport report;
        report.id = sender->id;
        report.direction = NetSpeedDirection::down;
        report.packets = sender->sent;
        report.bytes = sender->sent_bytes;
        report.seconds = sender->duration;
        size_t size = net_speed_write_report(out, sizeof(out), &report);
        net_access_send(&app->net_service.access, sender->peer, NetChannel::control, out, size);
        app->net_speed_reporting = false;
    }
    NetSpeedReceiver* receiver = &app->net_speed_receiver;
    if (receiver->active && now >= receiver->until)
    {
        NetSpeedReport report;
        net_speed_receiver_report(receiver, NetSpeedDirection::up, &report);
        size_t size = net_speed_write_report(out, sizeof(out), &report);
        net_access_send(&app->net_service.access, receiver->peer, NetChannel::control, out, size);
        receiver->active = false;
    }
}

// --- The stream -------------------------------------------------------------

// Which streaming device's stream_tuning applies (the newest request of the
// ones still streaming), and what it gives: told to the devices that asked,
// again whenever it changes (AUTO coming down).
void net_follow_stream_tuning(App* app)
{
    int32_t from = -1;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        if (app->stream_tuning_set[peer] && app->net_service.stream.flags[peer] != 0 &&
            (from < 0 || app->stream_tuning_order[peer] > app->stream_tuning_order[from]))
        {
            from = static_cast<int32_t>(peer);
        }
    }
    bool changed = from != app->stream_tuning_from ||
                   (from >= 0 && (app->stream_tuning_asked[from].delay != app->stream_tuning.delay ||
                                  app->stream_tuning_asked[from].delay_ms != app->stream_tuning.delay_ms));
    if (changed)
    {
        app->stream_auto_delay = AutoFrameDelay{};   // a new request searches from the top
        app->stream_delay_last_ns = 0;
    }
    app->stream_tuning_from = from;
    app->stream_tuning_on = from >= 0;
    if (from >= 0)
    {
        app->stream_tuning = app->stream_tuning_asked[from];
    }
    // What applies, as each asking device should hear it.
    NetStreamTuning result;
    result.delay = app->stream_tuning_on ? app->stream_tuning.delay : NetStreamDelay::host;
    result.delay_ms = static_cast<uint8_t>(app->frame_delay_ms < 255 ? app->frame_delay_ms : 255);
    // What the running core really does (only cores with their own, for now).
    result.run_ahead = app->session.host != nullptr && game_running(app)
                           ? static_cast<uint8_t>(libretro_host_run_ahead_applied(app->session.host))
                           : 0;
    result.flags = app_shell_wait_for_present(&app->shell) ? NET_STREAM_TUNING_WAITS_FOR_PRESENT : 0;
    for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
    {
        if (!app->stream_tuning_set[peer])
        {
            continue;
        }
        const NetStreamTuning* sent = &app->stream_tuning_sent[peer];
        if (app->stream_tuning_told[peer] && sent->delay == result.delay && sent->delay_ms == result.delay_ms &&
            sent->run_ahead == result.run_ahead && sent->flags == result.flags)
        {
            continue;
        }
        uint8_t out[8];
        size_t size = net_stream_write_tuning(out, sizeof(out), NET_STREAM_TUNING_RESULT, &result);
        net_access_send(&app->net_service.access, peer, NetChannel::control, out, size);
        app->stream_tuning_sent[peer] = result;
        app->stream_tuning_told[peer] = true;
    }
}

// stream_start / stream_stop / keyframe_request (net_stream.h; net_access let
// them through only for STREAM devices).
void net_stream_message(App* app, const NetAccessEvent* event, const char* device)
{
    if (event->size == 0)
    {
        return;
    }
    uint8_t type = event->data[0];
    uint8_t flags = 0;
    bool headless_on = false;
    if (net_stream_read_start(event->data, event->size, &flags))
    {
        net_service_set_stream(&app->net_service, event->peer, flags);
        app->stream_frame_owed = true;
        SDL_Log("Network: %s started remote play%s%s", device, (flags & NET_STREAM_WANT_VIDEO) != 0 ? " (video)" : "",
                (flags & NET_STREAM_WANT_AUDIO) != 0 ? " (audio)" : "");
    }
    else if (type == NET_STREAM_STOP && event->size == 1)
    {
        if (app->headless.on && app->headless.peer == event->peer)
        {
            headless_back(app, true);   // it no longer sees the game
            headless_tell(app);
        }
        net_service_set_stream(&app->net_service, event->peer, 0);
        if (event->peer < NET_MAX_PEERS)
        {
            app->stream_tuning_set[event->peer] = false;
        }
        SDL_Log("Network: %s stopped remote play", device);
    }
    else if (type == NET_STREAM_KEYFRAME_REQUEST && event->size == 1)
    {
        net_service_request_keyframe(&app->net_service, event->peer);
        app->stream_frame_owed = true;
    }
    else if (net_stream_read_go_headless(event->data, event->size, &headless_on))
    {
        // Taken only by a device streaming a running game, and only if no
        // other device holds it; given back only by the one holding it.
        bool streaming = event->peer < NET_MAX_PEERS && app->net_service.stream.flags[event->peer] != 0;
        if (headless_on && !app->headless.on && game_running(app) && streaming)
        {
            headless_enter(&app->headless, app->shell.window, event->peer, &app_icon_images[2],
                           piccolo_text(&app->strings, Str::headless_tooltip),
                           piccolo_text(&app->strings, Str::headless_show),
                           piccolo_text(&app->strings, Str::headless_play_here));
            SDL_Log("Headless: %s took the game", device);
        }
        else if (!headless_on && app->headless.on && app->headless.peer == event->peer)
        {
            headless_back(app, false);
        }
        headless_tell(app);   // the asker learns the result either way
    }
    else if (event->peer < NET_MAX_PEERS &&
             net_stream_read_tuning(event->data, event->size, NET_STREAM_TUNING, &app->stream_tuning_asked[event->peer]))
    {
        app->stream_tuning_set[event->peer] = true;
        app->stream_tuning_order[event->peer] = ++app->stream_tuning_count;
        app->stream_tuning_told[event->peer] = false;   // answered with what applies, below
        SDL_Log("Network: %s asks for frame delay mode %u (%u ms), run-ahead %u", device,
                static_cast<unsigned>(app->stream_tuning_asked[event->peer].delay),
                static_cast<unsigned>(app->stream_tuning_asked[event->peer].delay_ms),
                static_cast<unsigned>(app->stream_tuning_asked[event->peer].run_ahead));
    }
}

// After the frames of this loop ran: the frame shown and the audio heard go
// to the network thread (it codes and sends; this loop never waits for it).
// With run-ahead (later) this stays the shown frame and the real timeline's
// audio, numbered by this loop.
// Also called when no frame ran but one is owed (a stream started or asked
// for a keyframe while paused): the network thread codes keyframes only
// from a frame handed over after the request, so the last shown frame goes
// again, or a paused game would show that device nothing until it resumes.
void net_stream_frame(App* app)
{
    bool owed = app->stream_frame_owed;
    app->stream_frame_owed = false;
    if (!app->net_streaming || !game_running(app))
    {
        app->session.captured_audio.clear();
        return;
    }
    if (owed)
    {
        app->stream_serial = 0;   // the same core frame again (serials start at 1)
    }
    app->stream_loop++;
    const LibretroHost* host = app->session.host;
    if (!app->session.captured_audio.empty())
    {
        net_service_submit_audio(&app->net_service, app->session.captured_audio.data(),
                                 app->session.captured_audio.size() / 2, app->stream_loop);
        app->session.captured_audio.clear();
    }
    const LibretroVideoFrame* video = &host->video;
    FramePixelFormat format = FramePixelFormat::xrgb8888;
    if (video->format == RETRO_PIXEL_FORMAT_RGB565)
    {
        format = FramePixelFormat::rgb565;
    }
    else if (video->format == RETRO_PIXEL_FORMAT_0RGB1555)
    {
        format = FramePixelFormat::xrgb1555;
    }
    else if (video->format != RETRO_PIXEL_FORMAT_XRGB8888)
    {
        return;   // the HDR formats aren't streamed
    }
    if (video->pixels == nullptr || video->width == 0 || video->height == 0 || video->serial == app->stream_serial)
    {
        return;   // nothing new to show
    }
    app->stream_serial = video->serial;
    NetStreamInfo info;
    info.width = video->width;
    info.height = video->height;
    info.format = format;
    info.aspect = static_cast<double>(host->av_info.geometry.aspect_ratio);
    info.fps = host->av_info.timing.fps;
    info.sample_rate = host->av_info.timing.sample_rate;
    net_service_submit_frame(&app->net_service, video->pixels, video->width, video->height, format, app->stream_loop,
                             &info);
}

// --- Games ------------------------------------------------------------------

// A control message from a paired device (net_games.h). Folders and cores
// are read when asked, never every frame.
void net_games_message(App* app, const NetAccessEvent* event, const char* device)
{
    uint8_t type = event->size > 0 ? event->data[0] : 0;
    if (type < static_cast<uint8_t>(NetContentMessage::cores_request) ||
        type > static_cast<uint8_t>(NetContentMessage::load_result))
    {
        return;   // not ours (yet)
    }
    std::vector<std::string> game_folders;
    piccolo_settings_game_folders(&app->settings, &game_folders);
    net_games_set_folders(&app->net_games, &game_folders);

    size_t size = net_games_answer(&app->net_games, &app->catalog, event->data, event->size);
    if (size > 0)
    {
        net_access_send(&app->net_service.access, event->peer, NetChannel::control, app->net_games.reply.data(), size);
        return;
    }
    uint32_t id = 0;
    NetContentStatus status = NetContentStatus::failed;
    FilePickerChoice choice;
    if (!net_games_load(&app->net_games, &app->catalog, event->data, event->size, &id, &status, &choice))
    {
        return;   // a result type, or a damaged request: nothing to answer
    }
    if (status != NetContentStatus::ok || app->has_pending_start || app->net_load_waiting)
    {
        if (status == NetContentStatus::ok)
        {
            status = NetContentStatus::failed;   // another start is already on its way
        }
        uint8_t reply[16];
        size_t reply_size = net_content_write_load_result(reply, sizeof(reply), id, status);
        net_access_send(&app->net_service.access, event->peer, NetChannel::control, reply, reply_size);
        return;
    }
    SDL_Log("Network: %s starts %s %s", device, choice.core_path, choice.content_path);
    app->pending_start = choice;   // started at the top of the next loop, like LOAD GAME's
    app->has_pending_start = true;
    app->picker.open = false;      // what was being picked here is overtaken
    app->net_load_waiting = true;
    app->net_load_peer = event->peer;
    app->net_load_id = id;
}
