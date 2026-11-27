// The network service: starting and stopping it, and the loop that follows
// its events (devices arriving and leaving, pairing, what they ask to load).

#include "app_net.h"

#include "app_game.h"
#include "app_guest.h"
#include "app_net_remote.h"
#include "app_share.h"

// --- Starting and stopping --------------------------------------------------

// The listener, when SETTINGS has it on. Started at launch and restarted
// when ALLOW CONNECTIONS or PORT change (net_update).
// The network library and this device's key (made once, kept in the
// settings folder): for the listener and for SHARE's connection alike.
bool net_prepare(App* app)
{
    if (app->paths.settings.empty())
    {
        return false;   // nowhere to keep the device key: networking stays off
    }
    if (!app->net_ready)
    {
        app->net_ready = net_init();
        if (!app->net_ready)
        {
            return false;
        }
    }
    if (!app->net_has_identity)
    {
        std::string identity_path = app->paths.settings + "/net_identity.cfg";
        app->net_has_identity = net_identity_load_or_create(identity_path.c_str(), &app->net_identity);
        if (!app->net_has_identity)
        {
            return false;
        }
        net_key_fingerprint(app->net_identity.public_key, app->net_fingerprint, sizeof(app->net_fingerprint));
        SDL_Log("Network: this device is %s", app->net_fingerprint);
        // Before 2026-11-25 the devices we connect to had a list of their
        // own: it joins the paired devices once (one record per device).
        std::string devices_path = app->paths.settings + "/net_devices.cfg";
        std::string hosts_path = app->paths.settings + "/net_hosts.cfg";
        NetTrust trust;
        net_trust_file_lock();
        net_trust_load(&trust, devices_path.c_str());
        net_trust_merge_old_hosts(&trust, devices_path.c_str(), hosts_path.c_str());
        net_trust_file_unlock();
    }
    return true;
}

static void net_start(App* app)
{
    // Any failure below is remembered with the port it was tried on, so it
    // isn't retried every frame (net_update).
    app->net_port = static_cast<uint16_t>(settings_get_int(&app->settings, PICCOLO_NET_PORT));
    app->net_listen_failed = true;
    if (!net_prepare(app))
    {
        return;
    }
    app->net_service.access = NetAccess{};
    net_games_init(&app->net_games);
    if (!app->net_remote_ready)
    {
        app->net_remote_ready = net_remote_init(&app->net_remote, &app->settings);
    }
    std::string devices_path = app->paths.settings + "/net_devices.cfg";
    if (!net_service_start(&app->net_service, app->net_port, net_device_info(app), &app->net_identity,
                           devices_path.c_str()))
    {
        return;
    }
    app->net_listen_failed = false;
    app->net_running = true;
    app->net_beacon_applied.clear();   // a new service: net_update hands it the beacon again
    app->net_share_set = false;        // and its shared folders
}

// What a peer slot keeps in App: gone with the device (`closed`, net_stop), and
// cleared again when a new one takes the slot (a lost `closed` leaves it behind).
// Only plain state: what needs the lock or the network stays with the caller.
static void net_peer_forget(App* app, uint32_t peer)
{
    if (peer >= NET_MAX_PEERS)
    {
        return;
    }
    if (app->net_speed_sender.peer == peer)
    {
        app->net_speed_sender.active = false;   // its test ends with it
        app->net_speed_reporting = false;
    }
    if (app->net_speed_receiver.peer == peer)
    {
        app->net_speed_receiver.active = false;
    }
    app->stream_tuning_set[peer] = false;   // what it asked for
    app->net_fast_forward[peer] = false;    // a dropped device doesn't keep it held
    app->net_peer_language[peer][0] = '\0';
    app->net_devices_wanted[peer] = false;
    app->net_guest_name[peer][0] = '\0';
    if (app->net_load_waiting && app->net_load_peer == peer)
    {
        app->net_load_waiting = false;   // the game still starts; nobody to tell
    }
    input_net_peer_gone(&app->input_net, &app->input, peer);
}

static void net_stop(App* app)
{
    if (app->net_running)
    {
        net_service_lock(&app->net_service);
        guest_end(app, &app->net_service.access);   // guests told before the network goes
        net_service_unlock(&app->net_service);
        for (uint32_t peer = 0; peer < NET_MAX_PEERS; peer++)
        {
            net_peer_forget(app, peer);   // their pads are unplugged, their state dropped
        }
        // net_update stops looking at these once the network is off.
        app->net_streaming = false;
        app->session.capture_audio = false;
        app->stream_tuning_on = false;
        net_service_stop(&app->net_service);
        app->net_running = false;
        // A pairing request (it pauses the game) or our code's wait can't be
        // answered any more, and a restarted listener numbers its peers anew.
        pair_window_network_stopped(&app->pair_window);
    }
}

void net_shutdown_all(App* app)
{
    net_share_link_stop(&app->share_link);
    net_stop(app);
    guest_card_finish(&app->guest_window.card);
    net_keypair_wipe(&app->net_identity);
    if (app->net_ready)
    {
        net_shutdown();
        app->net_ready = false;
    }
}

// --- A device asked for a game ----------------------------------------------

// The load_result for the load_request waiting in pending_start.
void net_answer_load(App* app, NetContentStatus status)
{
    app->net_load_waiting = false;
    if (!app->net_running)
    {
        return;
    }
    uint8_t reply[16];
    size_t size = net_content_write_load_result(reply, sizeof(reply), app->net_load_id, status);
    // Called from the main loop, outside net_update: the lock is ours to take.
    net_service_lock(&app->net_service);
    net_access_send(&app->net_service.access, app->net_load_peer, NetChannel::control, reply, size);
    net_service_unlock(&app->net_service);
}

// A load_request whose start was refused (SAFE START closed without starting):
// answered failed, unless another start is on its way. Outside the lock.
void net_answer_load_refused(App* app)
{
    if (app->net_load_waiting && !app->has_pending_start)
    {
        net_answer_load(app, NetContentStatus::failed);   // not started after all
    }
}

// --- Once per loop ----------------------------------------------------------

static void net_update_locked(App* app)
{
    guest_update_locked(app);
    // GO HEADLESS ends from the tray, or when no game runs any more.
    // (Without a tray the taskbar restoring the window asks as well.)
    if (app->headless.on &&
        (app->headless.back_asked || !game_running(app) || headless_window_restored(&app->headless, app->shell.window)))
    {
        headless_back(app, false);
        headless_tell(app);
    }
    // A new NAME is used at once (discovery and pairing show it).
    const char* name = settings_get_text(&app->settings, PICCOLO_NET_NAME);
    if (app->net_service.access.name != name)
    {
        app->net_service.access.name = name;
        app->net_service.access.session.host.name = name;
    }

    NetAccessEvent event;
    while (net_service_next(&app->net_service, &event))
    {
        char address[64];
        net_address_to_text(event.address, address, sizeof(address));
        // The device as the list is now, by its key: event.device was an index when the event was
        // made on the network thread, and the list may have shrunk since (an unpair, FORGET).
        int32_t device_now = event.device >= 0 ? net_trust_find(&app->net_service.access.trust, event.key) : -1;
        const char* device = device_now >= 0
                                 ? app->net_service.access.trust.devices[static_cast<size_t>(device_now)].name.c_str()
                                 : "?";
        const NetAccessPeer* guest = event.guest ? net_access_guest(&app->net_service.access, event.peer) : nullptr;
        if (guest != nullptr)
        {
            device = guest->guest_name;
        }
        else if (event.guest && event.peer < NET_MAX_PEERS && app->net_guest_name[event.peer][0] != '\0')
        {
            device = app->net_guest_name[event.peer];   // already reset: the name kept at guest_joined
        }
        switch (event.type)
        {
        case NetAccessEventType::authorized:
            // First: a lost `closed` must not leave the previous device's state to this one.
            net_peer_forget(app, event.peer);
            net_service_set_stream(&app->net_service, event.peer, 0);
            SDL_Log("Network: \"%s\" connected from %s", device, address);
            net_send_game_state(app, event.peer);   // what runs, from the start
            net_send_actions(app, event.peer, 0);
            net_send_controllers(app, event.peer, 0);
            break;
        case NetAccessEventType::paired:
        {
            net_peer_forget(app, event.peer);   // as at `authorized`: this slot's new device
            net_service_set_stream(&app->net_service, event.peer, 0);
            // PAIRED WITH: the window says it when it's open; a card only
            // when it's closed (the player went back to the game meanwhile).
            if (!app->pair_window.open)
            {
                char status[256];
                std::snprintf(status, sizeof(status), "%s %s", piccolo_text(&app->strings, Str::status_net_paired),
                              device);
                set_status(app, status);
            }
            net_send_game_state(app, event.peer);
            net_send_actions(app, event.peer, 0);
            net_send_controllers(app, event.peer, 0);
            // GENERATE PAIRING CODE, if it waited for it: PAIRED WITH.
            pair_window_paired(&app->pair_window, &event, device);
            break;
        }
        case NetAccessEventType::closed:
            if (event.device < 0)
            {
                // It cancelled, or left.
                pair_window_request_gone(&app->pair_window, event.peer);
            }
            if (app->headless.on && app->headless.peer == event.peer)
            {
                headless_back(app, true);   // the device that had the game left
                headless_tell(app);
            }
            net_service_set_stream(&app->net_service, event.peer, 0);   // its stream ends with it
            SDL_Log("Network: \"%s\" disconnected", device);
            if (event.guest)
            {
                char status[128];
                std::snprintf(status, sizeof(status), piccolo_text(&app->strings, Str::status_guest_left),
                              std::strcmp(device, "?") != 0 ? device : event.info.name);
                set_status(app, status);
            }
            net_peer_forget(app, event.peer);
            break;
        case NetAccessEventType::pairing_failed:
            // PAIRING FAILED is the pairing window's own notice (in it, or its card in the game).
            SDL_Log("Network: pairing failed (too many wrong codes)");
            pair_window_request_gone(&app->pair_window, event.peer);
            pair_window_failed(&app->pair_window, SDL_GetTicks());
            break;
        case NetAccessEventType::pairing_requested:
            // A device asks (Andrés, 2026-11-25: the one asked has the code):
            // TOOLS > PAIR... opens on GENERATE PAIRING CODE showing it, with
            // the permissions to tick; the game pauses while it's up. One at a
            // time: another waits until this one is done (it asks again on its
            // own if refused).
            pair_window_take_request(&app->pair_window, &event);
            if (app->pair_window.request.active && app->pair_window.request.peer == event.peer)
            {
                SDL_Log("Network: \"%s\" asks to pair%s", event.info.name, event.retry ? " (new code)" : "");
            }
            break;
        case NetAccessEventType::code_expired:
            pair_window_code_expired(&app->pair_window, SDL_GetTicks());
            break;
        case NetAccessEventType::guest_joined:
        {
            // A guest from the link: what runs at once (it starts the stream itself).
            // First: a lost `closed` must not leave the previous device's state to this guest.
            net_peer_forget(app, event.peer);
            net_service_set_stream(&app->net_service, event.peer, 0);
            if (event.peer < NET_MAX_PEERS)
            {
                // "%.*s": a hello's name may be longer than a guest's; cut, not a warning.
                std::snprintf(app->net_guest_name[event.peer], sizeof(app->net_guest_name[event.peer]), "%.*s",
                              static_cast<int>(NET_GUEST_MAX_NAME), guest != nullptr ? guest->guest_name : event.info.name);
            }
            SDL_Log("Network: guest \"%s\" joined from %s", event.info.name, address);
            char status[128];
            std::snprintf(status, sizeof(status), piccolo_text(&app->strings, Str::status_guest_joined), event.info.name);
            set_status(app, status);
            net_send_game_state(app, event.peer);
            break;
        }
        case NetAccessEventType::guest_full:
            SDL_Log("Network: guest \"%s\" turned away: every seat is taken", event.info.name);
            break;
        case NetAccessEventType::granted:
            // A paired device says what we may use on it (stored as granted):
            // the DONE screen shows it, SETTINGS > NETWORK > DEVICES too.
            SDL_Log("Network: \"%s\" lets us use: %s", device, net_caps_name(event.caps).c_str());
            pair_window_granted(&app->pair_window, &event);
            break;
        case NetAccessEventType::unpaired:
        {
            // The device forgot this piccolo, so it's gone from PAIRED DEVICES
            // too (a card only while the pairing window is closed, as PAIRED WITH).
            if (!app->pair_window.open)
            {
                char status[256];
                std::snprintf(status, sizeof(status), "%s %s", piccolo_text(&app->strings, Str::status_net_unpaired),
                              event.info.name);
                set_status(app, status);
            }
            SDL_Log("Network: \"%s\" unpaired itself", event.info.name);
            break;
        }
        case NetAccessEventType::message:
            if (event.channel == NetChannel::input && event.size > 0 && event.data[0] == NET_INPUT_TYPE_SPEED)
            {
                // A speed test's packet (up), from the device running the test.
                if (event.peer == app->net_speed_receiver.peer)
                {
                    net_speed_receive(&app->net_speed_receiver, event.data, event.size, net_seconds());
                }
            }
            else if (event.channel == NetChannel::input && guest != nullptr)
            {
                // A PLAY guest's pad (net_access let it through only at PLAY):
                // game only, never the game's hotkeys; START joins a free port.
                input_net_set_hotkeys(&app->input_net, &app->input, event.peer, false);
                input_net_receive(&app->input_net, &app->input, event.peer, guest->key, guest->guest_name, event.data,
                                  event.size);
            }
            else if (event.channel == NetChannel::input && event.device >= 0)
            {
                // A paired device's pad needs GAMEPAD (net_access drops the rest too: this is the
                // frontend's own guard), and the device must still be paired: a packet from one
                // forgotten meanwhile is dropped, never plugged in under another device's key.
                if (device_now >= 0 && (event.caps & NET_CAP_GAMEPAD) != 0)
                {
                    const NetTrustedDevice* paired = &app->net_service.access.trust.devices[static_cast<size_t>(device_now)];
                    input_net_set_hotkeys(&app->input_net, &app->input, event.peer,
                                          (event.caps & NET_CAP_STREAM) != 0);
                    input_net_receive(&app->input_net, &app->input, event.peer, paired->key, paired->name.c_str(),
                                      event.data, event.size);
                }
            }
            else if (event.channel == NetChannel::control)
            {
                if (event.size > 0 && event.data[0] == NET_SPEED_REQUEST)
                {
                    net_speed_request(app, &event);
                }
                if (event.size > 0 && event.data[0] == NET_SPEED_RESULT)
                {
                    net_speed_result(app, &event, device_now);
                }
                net_stream_message(app, &event, device);
                net_games_message(app, &event, device);   // net_access let through only what its level covers
                net_remote_message(app, &event, device);
            }
            break;
        case NetAccessEventType::none:
            break;
        }
    }
    net_follow_game_state(app);
    net_follow_remote(app);
    net_speed_update(app);
    // Remote play: frames and audio are handed over only while someone streams.
    app->net_streaming = net_service_streaming(&app->net_service);
    if (app->net_streaming && !app->session.capture_audio)
    {
        app->session.captured_audio.reserve(16384);   // once: a few frames' worth
    }
    app->session.capture_audio = app->net_streaming;
    net_follow_stream_tuning(app);
}

// Once per loop, before the input is read: follow the settings, then the
// network's events. Paired devices are let in and their pads join the input
// router (input_net); everyone else needs the pairing code.
void net_update(App* app)
{
    bool enabled = settings_get_bool(&app->settings, PICCOLO_NET_ENABLED);
    uint16_t port = static_cast<uint16_t>(settings_get_int(&app->settings, PICCOLO_NET_PORT));
    if (app->net_running && (!enabled || port != app->net_port))
    {
        net_stop(app);
        app->net_listen_failed = false;
    }
    // After a failure (port in use) it's tried again only with another port.
    if (enabled && !app->net_running && (!app->net_listen_failed || port != app->net_port))
    {
        net_start(app);
    }
    if (!enabled)
    {
        app->net_listen_failed = false;
    }
    if (!app->net_running)
    {
        headless_back(app, false);   // networking went off: nobody streams
        return;
    }
    // INTERNET and BEACON SERVER: the service resolves the name on its own
    // thread. Only when they change (an empty name is off too).
    // Compared as text: a std::string is built only when the name changed (not every frame).
    const char* beacon = PICCOLO_BROWSER || settings_get_bool(&app->settings, PICCOLO_NET_INTERNET)
                             ? settings_get_text(&app->settings, PICCOLO_NET_BEACON)
                             : "";
    if (std::strcmp(beacon, app->net_beacon_applied.c_str()) != 0)
    {
        app->net_beacon_applied = beacon;
        net_service_set_beacon(&app->net_service, beacon);
    }
    // The shared folders the devices with FILE TRANSFER reach (piccolo-share): looked at
    // again only when a setting changed, handed over only when they did.
    bool share_check = !app->net_share_set || app->settings.revision != app->net_share_revision;
    NetShareFolderPaths share_paths;
    std::string share_key;
    if (share_check)
    {
        app->net_share_revision = app->settings.revision;
        share_folder_paths(app, &share_paths);
        share_key = share_paths.saves + "\n" + share_paths.states + "\n" + share_paths.screenshots + "\n" +
                    share_paths.config;
        for (size_t i = 0; i < share_paths.games.size(); i++)
        {
            share_key += "\n" + share_paths.games[i];
        }
    }
    // The network thread polls; this loop handles what it queued, holding
    // the lock for all of it (the NetAccess, its peers and lists).
    net_service_lock(&app->net_service);
    if (share_check && (!app->net_share_set || share_key != app->net_share_applied))
    {
        app->net_share_applied = share_key;
        app->net_share_set = true;
        net_service_set_share_folders(&app->net_service, &share_paths);
    }
    net_update_locked(app);
    net_service_unlock(&app->net_service);
}
