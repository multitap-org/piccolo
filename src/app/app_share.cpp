// FILE TRANSFER and TOOLS > PAIR: what this device shares and says about
// itself, the connection both windows use, and their glue.

#include "app_share.h"

#include "app_files.h"
#include "app_net.h"
#include "app_ui_modes.h"

// --- This device, and the connection both windows use -----------------------

// What this piccolo shares with devices granted FILE TRANSFER (and lists
// on FILE TRANSFER's own side): the game folders, SAVES, STATES and
// SCREENSHOTS as the settings say now, and CONFIG, the settings folder
// without what belongs to this device (Andrés, 2026-11-25): the global
// settings, the recent games and this machine's own files (multitap-net
// refuses the identity, the paired devices and cache/ itself). No RECORDINGS
// folder yet: piccolo has none of its own.
void share_folder_paths(const App* app, NetShareFolderPaths* paths)
{
    *paths = {};
    piccolo_settings_game_folders(&app->settings, &paths->games);
    paths->saves = folder_setting(app, PICCOLO_PATH_SAVES, app->paths.saves);
    paths->states = folder_setting(app, PICCOLO_PATH_STATES, app->paths.states);
    paths->screenshots = folder_setting(app, PICCOLO_PATH_SCREENSHOTS, app->paths.screenshots);
    paths->config = app->paths.settings;
    paths->config_refused = {"piccolo.cfg",        "recent.cfg",      "last_used.cfg",   "imgui.ini",   "continue.cfg",
                             "audio_latency.cfg",  "build_proof.cfg", "build_proof.cfg.sig", "running.cfg",
                             "frame_delay.cfg"};
    // The default screenshots folder is made by the first screenshot; made
    // here too, so SCREENSHOTS is offered (and can receive) before that, like
    // SAVES and STATES. A folder of the user's that isn't there (a drive not
    // mounted) is left alone and not offered.
    if (paths->screenshots == app->paths.screenshots && !paths->screenshots.empty())
    {
        host_fs_make_directories(paths->screenshots.c_str());
    }
}

// This device as its hello tells it (net_pairing.h): its name (read again
// each time: SETTINGS may have changed it), piccolo, what it offers and
// uses, and its build proof when this is a signed release (net_build.h; only
// tags are signed). A proof not kept yet is fetched on a thread of its own
// and used from the next launch.
const NetDeviceInfo* net_device_info(App* app)
{
    NetDeviceInfo* info = &app->net_info;
    std::snprintf(info->name, sizeof(info->name), "%s", settings_get_text(&app->settings, PICCOLO_NET_NAME));
    info->kind = NetAppKind::piccolo;
    info->offers = net_caps_ui_kind_offers(NetAppKind::piccolo);
    info->uses = net_caps_ui_kind_uses(NetAppKind::piccolo);
    NetBuildIdentity build;
    // (In the browser the page may still be hashing the program: asked again next time.)
    if (!app->net_proof_tried && !app->paths.settings.empty() && net_build_find_program("piccolo", &build))
    {
        app->net_proof_tried = true;
        build.app = "piccolo";
        build.version = PICCOLO_VERSION;
        build.target = build_target();   // as the release names its assets
        if (!net_build_proof_load(&build, app->paths.settings.c_str(), false, info))
        {
            net_build_proof_fetch_later(&build, app->paths.settings.c_str());
        }
        SDL_Log("Network: this build is %s", info->record_size > 0 ? "signed" : "unsigned");
    }
    return info;
}

// SHARE's connection to other piccolos, started on first use (TOOLS > SHARE
// and TOOLS > PAIR use the same one).
bool start_share_link(App* app)
{
    if (!net_share_link_started(&app->share_link) && net_prepare(app))
    {
        std::string devices_path = app->paths.settings + "/net_devices.cfg";
        net_share_link_start(&app->share_link, &app->net_identity, net_device_info(app), devices_path.c_str());
    }
    return net_share_link_started(&app->share_link);
}

// --- TOOLS > PAIR and pairing requests --------------------------------------

// TOOLS > PAIR, before drawing: the connection's status, which SHARE reads
// itself while open (pair_window.h).
void pair_update(App* app)
{
    if (net_share_link_started(&app->share_link) && app->pair_window.open && !app->share_window.open)
    {
        net_share_link_status(&app->share_link, &app->share_status);
    }
}

// TOOLS > PAIR...: what was pressed. PAIR A DEVICE on FILE TRANSFER's
// connection; GENERATE PAIRING CODE (and a device's request) on the
// NetAccess, so under the network lock (the network thread waits meanwhile).
void pair_apply(App* app, const PairWindowAction* action)
{
    pair_window_apply_device(&app->pair_window, action, &app->share_link, &app->share_status,
                             settings_get_text(&app->settings, PICCOLO_NET_BEACON), app->share_window.open);
    if (app->net_running)
    {
        net_service_lock(&app->net_service);
        pair_window_apply_code(&app->pair_window, action, &app->net_service.access);
        net_service_unlock(&app->net_service);
    }
    else
    {
        pair_window_apply_code(&app->pair_window, action, nullptr);
    }
}

// TOOLS > PAIR...'s waiting steps, every frame (also closed: a request in
// the game waits as a card): 5 minutes at most (pair_window.h). Our own
// code's wait and a request are the NetAccess's, so under the network lock.
void pair_expire(App* app)
{
    uint64_t now_ms = SDL_GetTicks();
    // (The lock only when a NetAccess step waits: not taken every frame for nothing.)
    if (app->net_running && (app->pair_window.request.active || app->pair_window.shown.active))
    {
        net_service_lock(&app->net_service);
        pair_window_expire(&app->pair_window, &app->share_link, &app->net_service.access, now_ms);
        net_service_unlock(&app->net_service);
    }
    else
    {
        pair_window_expire(&app->pair_window, &app->share_link, nullptr, now_ms);
    }
}

// A pairing request shows as a card, not its window (testing round 1): the
// player is in the game with no window over it (pair_window.h).
bool pair_request_in_game(const App* app)
{
    return game_has_keyboard(app) && !app->pair_window.open && !app->guest_window.open;
}

// START held on any controller, for the pairing request's card: REMAP on a
// gamepad (START by default, as HOLD START TO REMAP), the keyboard player's
// START (Enter by default: the keyboard has no REMAP key).
bool pair_accept_held(const App* app)
{
    for (size_t i = 0; i < app->input.controllers.size(); i++)
    {
        uint32_t controller = static_cast<uint32_t>(i);
        const InputDevice* device = input_devices_find_const(&app->input.devices, app->input.controllers[i].device_id);
        if (device == nullptr)
        {
            continue;
        }
        InputAction action = device->kind == InputDeviceKind::keyboard ? InputAction::pad_start : InputAction::hotkey_remap;
        if (input_controller_action_held(&app->input, controller, action))
        {
            return true;
        }
    }
    return false;
}

// --- FILE TRANSFER ----------------------------------------------------------

void open_share_window(App* app)
{
    NetShareFolderPaths paths;
    share_folder_paths(app, &paths);
    net_share_set_folders(&app->share_folders, &paths);
    if (start_share_link(app))
    {
        // The paired piccolos' ONLINE / OFFLINE is asked through BEACON SERVER.
        net_share_link_set_beacon(&app->share_link, settings_get_text(&app->settings, PICCOLO_NET_BEACON));
        net_share_link_set_folders(&app->share_link, &paths);
    }
    share_window_open(&app->share_window);
}

// What a copy that didn't go well says on SHARE's bottom line.
static Str share_done_text(NetShareStatus status)
{
    switch (status)
    {
    case NetShareStatus::exists:
        return Str::share_done_exists;
    case NetShareStatus::same:
        return Str::share_done_same;
    case NetShareStatus::no_space:
        return Str::share_done_no_space;
    case NetShareStatus::mismatch:
        return Str::share_done_mismatch;
    case NetShareStatus::refused:
    case NetShareStatus::not_found:
        return Str::share_done_refused;
    case NetShareStatus::busy:
        return Str::share_done_busy;
    default:
        return Str::share_done_failed;
    }
}

// SHARE, once per loop while the window is open or the connection runs,
// before drawing: the connection's answers go to the window (its clicks go
// to the connection in share_window_apply, after drawing).
void share_update(App* app, ShareTransferLine* line)
{
    ShareWindow* window = &app->share_window;
    NetShareLink* link = &app->share_link;
    if (!net_share_link_started(link))
    {
        return;
    }
    if (window->open)
    {
        net_share_link_status(link, &app->share_status);
    }
    bool connected = app->share_status.state == NetShareLinkState::ready &&
                     (app->share_status.caps & NET_CAP_SHARE) != 0;
    if (connected != app->share_connected)
    {
        app->share_connected = connected;
        share_window_remote_reset(window);   // a new piccolo, or none: its side starts over
    }
    std::string path;
    if (connected && share_window_remote_wants_list(window, &path))
    {
        net_share_link_list(link, path.c_str());
    }
    NetShareStatus status = NetShareStatus::ok;
    std::vector<NetShareEntry> entries;
    if (connected && net_share_link_listing(link, &path, &status, &entries))
    {
        share_window_set_remote_list(window, path.c_str(), status, &entries);
    }
    net_share_link_take_done(link, &app->share_done);
    for (size_t i = 0; i < app->share_done.size(); i++)
    {
        const NetShareClientDone* done = &app->share_done[i];
        if (done->status == NetShareStatus::ok)
        {
            window->local.stale = true;   // a new file on one side
            window->remote.stale = true;
            continue;
        }
        if (done->status == NetShareStatus::exists)
        {
            // Another file with that name there: SHARE asks whether to replace it.
            ShareCopy conflict;
            conflict.to_remote = done->copy.to_remote;
            conflict.from = done->copy.to_remote ? done->copy.local : done->copy.remote;
            conflict.to = done->copy.to_remote ? done->copy.remote : done->copy.local;
            conflict.size = done->copy.size;
            conflict.mtime = done->copy.mtime;
            share_window_add_conflict(window, &conflict);
            continue;
        }
        const std::string* name = done->copy.to_remote ? &done->copy.local : &done->copy.remote;
        window->message = std::string(piccolo_text(&app->strings, share_done_text(done->status))) + ": " + *name;
    }

    // The bottom line, with the rate over about a second.
    NetShareProgress progress;
    NetShareClientCopy copy;
    uint32_t queued = 0;
    net_share_link_progress(link, &progress, &copy, &queued);
    line->active = progress.role != NetShareRole::none;
    if (line->active)
    {
        uint64_t now = SDL_GetTicks();
        if (progress.id != app->share_rate_id)
        {
            app->share_rate_id = progress.id;
            app->share_rate_done = progress.done;
            app->share_rate_ticks = now;
            app->share_rate_kbps = 0;
        }
        else if (now - app->share_rate_ticks >= 1000)
        {
            uint64_t bytes = progress.done - app->share_rate_done;
            app->share_rate_kbps = static_cast<uint32_t>(bytes * 8 / (now - app->share_rate_ticks));
            app->share_rate_done = progress.done;
            app->share_rate_ticks = now;
        }
        size_t slash = copy.remote.rfind('/');
        line->name = slash == std::string::npos ? copy.remote : copy.remote.substr(slash + 1);
        line->sending = copy.to_remote;
        line->done = progress.done;
        line->size = progress.size;
        line->rate_kbps = app->share_rate_kbps;
        line->queued = queued;
    }
}

// SHARE's PAIR: pairing lives in TOOLS > PAIR... only: it opens on PAIR A
// DEVICE with that piccolo (or ENTER A CODE) and starts at once (unless the
// window is in the middle of something else). Once paired and closed, SHARE
// stays connected.
void share_pair(App* app, bool internet)
{
    pair_window_open(&app->pair_window, nullptr);
    if (app->pair_window.mode != PairMode::device || app->pair_window.phase != PairDevicePhase::list)
    {
        return;
    }
    app->pair_window.selected = internet ? -1 : app->share_window.found_selected;
    app->pair_window.enter_selected = internet;
    PairWindowAction pair_action = {};
    pair_action.press = PairWindowPress::pair;
    pair_action.mode = PairMode::device;
    pair_apply(app, &pair_action);
}
