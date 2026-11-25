#pragma once

// piccolo's NETWORK tab in SETTINGS (CONFIG > NETWORK opens SETTINGS on
// it): the listener's settings, this device's key, pairing with its code,
// and the paired devices. It was a window of its own until 2026-11-19
// (Andrés: network settings belong in SETTINGS).
//
//   +- NETWORK (tab) ----------------------------------------------+
//   |   ALLOW CONNECTIONS  [x]                                      |
//   |   NAME               [LIVING ROOM               ]             |
//   |   PORT               [- 55420 +]                              |
//   |   INTERNET           [x]                                      |
//   |   BEACON SERVER      [beacon.get-piccolo.app    ]             |
//   |   INTERNET STATUS    READY: PAIRED DEVICES CAN CONNECT        |
//   |   THIS DEVICE        3fa2-91c0-7d4e-b815                       |
//   | -- PAIRED DEVICES ------------------------------------------- |
//   |   ANA'S PHONE  9c77-3332-311c-4017  CONNECTED                 |
//   |   [GAMEPAD][STREAM][SHARE][FULL]  [FORGET]                    |
//   +--------------------------------------------------------------+
//
// The settings rows edit piccolo's global settings; main.cpp restarts the
// listener when ALLOW CONNECTIONS or PORT change. The device list acts on the
// running NetAccess (net_window_apply). Pairing isn't here (Andrés,
// 2026-11-22: SETTINGS holds configuration, not actions): a device asks, and
// the pairing request dialog below (shown over anything, the game paused)
// takes the code.
//
// Split like every window, the tab, the pairing request and INVITE alike:
// *_state fills what it shows (no GUI), *_draw draws it and records what was
// done in an action, and *_apply acts on it (no GUI). The tab is drawn inside
// SETTINGS' tab (settings_window.cpp); main.cpp fills its state and applies
// its action around SETTINGS' drawing.

#include <cstdint>
#include <string>
#include <vector>

#include "localization.h"
#include "net_access.h"
#include "settings.h"
#include "settings_ui.h"

#include "retro_ui_font.h"

// The last speed test of a device, as it reported each step (net_speed.h
// speed_result): the best rate each way that lost no more than
// NET_WINDOW_SPEED_MAX_LOSS. Kept while piccolo runs, by device key.
constexpr uint16_t NET_WINDOW_SPEED_MAX_LOSS = 20;   // per mille

struct NetSpeedSummary
{
    uint8_t  key[NET_KEY_SIZE] = {};
    uint32_t down_kbps = 0;   // 0 = none clean (or not tested)
    uint32_t up_kbps = 0;
    uint32_t down_last_asked = 0;   // a step asking less than the last starts a new test
    uint32_t up_last_asked = 0;
    bool     down_tested = false;
    bool     up_tested = false;
};

// A device asking to pair (NetAccessEventType::pairing_requested): the code
// its screen shows is typed here, with the level to give it.
struct NetPairingRequest
{
    bool        active = false;
    bool        waiting = false;   // ALLOW pressed: until paired, or a new request (wrong code)
    bool        retry = false;     // the last code typed was wrong
    bool        focus = false;     // the code field takes the keyboard next frame
    uint32_t    peer = 0;
    NetPairKind kind = NetPairKind::piccolo;
    char        name[NET_DEVICE_NAME_MAX + 1] = {};
    char        code[NET_PAIR_CODE_DIGITS + 1] = {};
    uint32_t    level = 0;         // NetAccessLevel
};

struct NetPairingRequestState
{
    const Localization* strings = nullptr;
};

enum class NetPairingRequestPress
{
    none,
    allow,     // ALLOW (or Enter with a whole code): answer with the code and level typed
    refuse,    // REFUSE, or CANCEL while waiting
};

struct NetPairingRequestAction
{
    NetPairingRequestPress press = NetPairingRequestPress::none;
};

// TOOLS > INVITE: a device across the internet shows a code; typed here with
// the level, ALLOW waits in that code's pairing room on the beacon
// (net_access_invite) and answers the request that comes from there.
struct NetInvite
{
    bool     open = false;
    bool     waiting = false;      // ALLOW pressed: in the pairing room
    bool     answered = false;     // a request came and was answered with our code
    uint32_t peer = 0;             // the one answered
    char     code[NET_PAIR_CODE_DIGITS + 1] = {};
    uint32_t level = 0;            // NetAccessLevel
    bool     focus = false;
};

struct NetInviteState
{
    const Localization* strings = nullptr;
    bool                network = false;   // the network runs (else: NETWORK IS OFF)
};

struct NetInviteAction
{
    bool allow = false;   // ALLOW (or Enter with a whole code): wait in the code's pairing room
    bool close = false;   // CANCEL, or the title bar's X
};

// What the tab keeps between frames: SHARE / FULL are given only after their
// confirmation, to the device with this key (the list may change meanwhile).
struct NetWindow
{
    uint8_t owner_key[NET_KEY_SIZE] = {};
    bool    confirm_owner = false;
    NetAccessLevel confirm_level = NetAccessLevel::share;   // what the confirmation gives
    NetPairingRequest request;
    NetInvite         invite;
};

// One connection of the NetAccess, for CONNECTED and the devices' CONNECTED.
struct NetWindowPeer
{
    bool     connected = false;    // authorized, with a device in the list
    uint32_t device = 0;           // its index in `devices`
    bool     has_address = false;
    char     address[64] = {};
    bool     has_stats = false;
    NetLinkStats stats;
    const NetSpeedSummary* speed = nullptr;   // its last speed test, nullptr = none
};

struct NetWindowState
{
    const Settings*     settings = nullptr;   // the rows (global settings)
    const Localization* strings = nullptr;
    bool                network = false;      // the NetAccess runs (the lists show)
    bool                listen_failed = false;   // on, but the port couldn't be opened
    const char*         this_device = "";     // our key's fingerprint
    bool                internet = false;     // INTERNET is on (the beacon status row shows)
    NetBeaconStatus     beacon = NetBeaconStatus::off;   // INTERNET STATUS
    // The paired devices, read only (drawn under the network lock).
    const std::vector<NetTrustedDevice>* devices = nullptr;
    NetWindowPeer       peers[NET_MAX_PEERS];
    // The piccolos this one connects to (its client side, SHARE / PAIR);
    // nullptr: unknown (SHARE's connection doesn't run).
    const std::vector<std::string>* hosts = nullptr;
    const std::vector<uint8_t>*     hosts_online = nullptr;   // per host: on the beacon now; empty = unknown
};

struct NetWindowAction
{
    SettingsUiEdit setting;            // a settings row changed
    bool           set_level = false;  // ACCESS: `level` for device `level_device`, no confirmation needed
    uint32_t       level_device = 0;
    NetAccessLevel level = NetAccessLevel::gamepad;
    bool           forget = false;     // FORGET device `forget_device`
    uint32_t       forget_device = 0;
    bool           owner_yes = false;  // the confirmation said YES: confirm_level to owner_key
    bool           forget_host = false;   // FORGET host `forget_host_index` (main.cpp: SHARE's connection)
    uint32_t       forget_host_index = 0;
};

// The tab's state. `access` nullptr while networking is off; `speed`: the
// speed test results, by device key; `hosts` / `hosts_online` nullptr when
// SHARE's connection doesn't run. Under the network lock.
void net_window_state(NetWindowState* state, const Settings* settings, const Localization* strings,
                      const NetAccess* access, bool listen_failed, const char* this_device,
                      const std::vector<NetSpeedSummary>* speed, bool internet,
                      const std::vector<std::string>* hosts, const std::vector<uint8_t>* hosts_online);
// The tab's contents, inside SETTINGS' tab. The rows always edit the global
// settings (networking isn't per core or game), with labels `label_width`
// wide like the other tabs'.
void net_window_draw_tab(NetWindow* window, const NetWindowState* state, float label_width,
                         NetWindowAction* action);
// The settings row's edit, ACCESS, FORGET and the confirmation's YES, on
// `access` (nullptr while networking is off). Under the network lock. True
// when the settings changed (SETTINGS saves when it closes). FORGET on a
// host is main.cpp's (SHARE's connection).
bool net_window_apply(NetWindow* window, const NetWindowAction* action, Settings* settings, NetAccess* access);

// The four port LEDs lit in turn, then `label`: something being waited for.
void net_window_waiting_leds(const char* label);

// The pairing request dialog (drawn every frame while one is active, under
// the network lock).
void net_pairing_request_open(NetPairingRequest* request, const NetAccessEvent* event);
void net_pairing_request_close(NetPairingRequest* request);
void net_pairing_request_state(NetPairingRequestState* state, const Localization* strings);
void net_pairing_request_draw(NetPairingRequest* request, const NetPairingRequestState* state,
                              RetroUiFont* pixel_font, int32_t scale, NetPairingRequestAction* action);
void net_pairing_request_apply(NetPairingRequest* request, const NetPairingRequestAction* action, NetAccess* access);

// TOOLS > INVITE (drawn every frame while open). `access` nullptr = network off.
void net_invite_open(NetInvite* invite);
void net_invite_close(NetInvite* invite, NetAccess* access);
void net_invite_state(NetInviteState* state, const Localization* strings, const NetAccess* access);
void net_invite_draw(NetInvite* invite, const NetInviteState* state, RetroUiFont* pixel_font, int32_t scale,
                     NetInviteAction* action);
void net_invite_apply(NetInvite* invite, const NetInviteAction* action, NetAccess* access);
// A pairing request while waiting: answered with the typed code (true), or not ours.
bool net_invite_take_request(NetInvite* invite, NetAccess* access, const NetAccessEvent* event);
