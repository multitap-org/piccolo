// REMOTE PLAY's guest links: the link, its card on the beacon, the window.

#include "app_guest.h"

#include "app_game.h"

// --- The link and its card --------------------------------------------------

// The beacon's host name for guest links: BEACON SERVER without a UDP port
// ("beacon.get-piccolo.app:55430" -> "beacon.get-piccolo.app"), the default
// when it's empty. The link and the card live on the same name.
static std::string guest_beacon_host(const App* app)
{
    std::string name = settings_get_text(&app->settings, PICCOLO_NET_BEACON);
    if (name.empty())
    {
        name = NET_BEACON_DEFAULT_SERVER;
    }
    size_t scheme = name.find("://");
    if (scheme != std::string::npos)
    {
        name = name.substr(scheme + 3);
    }
    size_t slash = name.find('/');
    if (slash != std::string::npos)
    {
        name.resize(slash);
    }
    if (!name.empty() && name[0] != '[')
    {
        size_t colon = name.find(':');
        if (colon != std::string::npos && name.find(':', colon + 1) == std::string::npos)
        {
            name.resize(colon);
        }
    }
    return name;
}

// The frame shown now, for the card and the window's preview. Under the
// network lock it only copies the frame (guest_card_frame_copy): the PNG is
// made by the upload thread (guest_card_send). The preview's conversion
// stays here: no PNG, a plain pixel conversion. A core without a frame in
// memory (none yet): no picture, the card goes up without one (the beacon
// shows white noise).
static void guest_capture(App* app)
{
    GuestWindow* window = &app->guest_window;
    window->frame.pixels.clear();
    window->has_preview = false;   // until a new picture is uploaded: NO SCREENSHOT AVAILABLE
    if (!game_running(app) || app->session.host == nullptr)
    {
        return;
    }
    const LibretroVideoFrame* video = &app->session.host->video;
    GuestCardPixels format = GuestCardPixels::xrgb8888;
    if (video->format == RETRO_PIXEL_FORMAT_RGB565)
    {
        format = GuestCardPixels::rgb565;
    }
    else if (video->format == RETRO_PIXEL_FORMAT_0RGB1555)
    {
        format = GuestCardPixels::xrgb1555;
    }
    uint32_t pitch = video->width * video->bytes_per_pixel;
    if (!guest_card_frame_copy(&window->frame, video->pixels, video->width, video->height, pitch, format))
    {
        SDL_Log("Guest link: no picture for the card");
        return;
    }
    if (guest_card_frame_xrgb(video->pixels, video->width, video->height, pitch, format, &window->preview_pixels))
    {
        window->preview_width = video->width;
        window->preview_height = video->height;
        window->preview_pending = true;
    }
}

// The card's title: the game's name cleaned (groups in () and [] dropped).
// A name that is all groups ("[BIOS] (World)") keeps their words instead,
// and a core without content its own name: never empty, so a link can
// always be made.
static void guest_title(const App* app, char* title)
{
    net_guest_title_clean(app->game_name, title);
    if (title[0] != '\0')
    {
        return;
    }
    char words[sizeof(app->game_name)] = {};
    std::snprintf(words, sizeof(words), "%s", app->game_name);
    for (size_t i = 0; words[i] != '\0'; i++)
    {
        if (words[i] == '(' || words[i] == ')' || words[i] == '[' || words[i] == ']')
        {
            words[i] = ' ';
        }
    }
    net_guest_title_clean(words, title);
    if (title[0] == '\0')
    {
        net_guest_title_clean(app->core_name[0] != '\0' ? app->core_name : "piccolo", title);
    }
}

// The window opens with the game's name cleaned for the card and the frame
// shown now as its picture (a link already open keeps its own).
void open_guest_window(App* app)
{
    GuestWindow* window = &app->guest_window;
    if (!window->live)
    {
        guest_title(app, window->title);
        guest_capture(app);
    }
    guest_window_open(window);
}

// CREATE LINK (or REFRESH LINK: a fresh link, the guests in stay). Under the network lock.
void guest_make_link(App* app)
{
    GuestWindow* window = &app->guest_window;
#if defined(PICCOLO_GUEST_CARD)
    // HTTP for the card, started here on the main thread (curl's setup isn't
    // thread safe): before, only the core downloader started it, so on
    // Windows (WinHTTP needs its session) every card failed until CORES had
    // run (Andrés's log: "WinHTTP is not initialized", 12172).
    net_http_init("piccolo/" PICCOLO_VERSION);
#endif
    NetGuestLink link;
    if (!net_guest_link_make(&link))
    {
        SDL_Log("Guest link: no randomness for a link");
        return;
    }
    std::string beacon = guest_beacon_host(app);
    net_access_guest_open(&app->net_service.access, &link, static_cast<uint32_t>(window->seats),
                          static_cast<NetGuestLevel>(window->level));
    window->link = link;
    net_guest_link_url(&link, beacon.c_str(), window->link_text, sizeof(window->link_text));
    net_guest_link_wipe(&link);
    if (!window->live)
    {
        window->core_path = app->marker_core_path;   // the game started with these (start_game)
        window->content_path = app->marker_content_path;
    }
    window->live = true;
    SDL_Log("Guest link: open for \"%s\", %d seats, %s", window->title, window->seats,
            window->level == static_cast<uint32_t>(NetGuestLevel::play) ? "watch + play" : "watch");
    // The link shows once its room is on the beacon and its card is sent
    // (guest_update_locked): pasted anywhere, it has its card at once.
    window->preparing = true;
    window->prepare_sent = false;
    window->prepare_started_ms = SDL_GetTicks();
}

// The session ends: STOP REMOTE PLAY, the game closed or changed, networking off, quitting.
// Under the network lock (or with the network stopped: `access` nullptr).
void guest_end(App* app, NetAccess* access)
{
    if (!app->guest_window.live)
    {
        return;
    }
    guest_window_end(&app->guest_window, access);
    set_status(app, piccolo_text(&app->strings, Str::status_guests_ended));
    SDL_Log("Guest link: the session ended");
}

// Once a frame under the network lock: the session follows its game, and
// each guest learns its port. Another game (or core) ends it, a guest can't
// control what's shown; a hard reset starts the same files again and keeps
// it (Andrés, 2026-11-25).
void guest_update_locked(App* app)
{
    GuestWindow* window = &app->guest_window;
    bool same_game = app->marker_core_path == window->core_path && app->marker_content_path == window->content_path;
    if (window->live && (!game_running(app) || !same_game))
    {
        guest_end(app, &app->net_service.access);
    }
    // A new link: its card goes up once its room is joined (or after
    // GUEST_PREPARE_MS: no WebRTC here, or a beacon that's slow), and the link
    // shows when that send is done, sent or not.
    if (window->live && window->preparing)
    {
        uint64_t now = SDL_GetTicks();
        bool ready = net_access_guest_ready(&app->net_service.access) ||
                     now - window->prepare_started_ms >= GUEST_PREPARE_MS;
        GuestCardState state = guest_card_state(&window->card);
        if (!window->prepare_sent && ready && state != GuestCardState::sending)
        {
            std::string beacon = guest_beacon_host(app);
            // The frame taken when the window opened was handed to the last
            // send (a REFRESH LINK sends again): take it again then.
            if (window->frame.pixels.empty())
            {
                guest_capture(app);
            }
            guest_card_send(&window->card, beacon.c_str(), &window->link, window->title, &window->frame);
            window->prepare_sent = true;
            window->next_refresh_ms = now + GUEST_CARD_REFRESH_MS;
        }
        else if (window->prepare_sent && state != GuestCardState::sending)
        {
            window->preparing = false;
            SDL_Log("Guest link: ready (%s)", state == GuestCardState::sent ? "its card is up" : "no card");
            guest_window_link_ready(window, now);   // CREATE / REFRESH LINK: copied now, and the card says so
        }
        return;
    }
    // A live link's picture follows the game (Andrés): taken again and sent
    // every GUEST_CARD_REFRESH_MS, unless the last send is still going.
    if (window->live && SDL_GetTicks() >= window->next_refresh_ms &&
        guest_card_state(&window->card) != GuestCardState::sending)
    {
        window->next_refresh_ms = SDL_GetTicks() + GUEST_CARD_REFRESH_MS;
        guest_capture(app);
        std::string beacon = guest_beacon_host(app);
        guest_card_send(&window->card, beacon.c_str(), &window->link, window->title, &window->frame);
    }
    guest_window_tell_ports(window, &app->net_service.access, &app->input_net, &app->input);
}
