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
// running NetAccess directly. Pairing isn't here (Andrés, 2026-11-22: SETTINGS
// holds configuration, not actions): a device asks, and the pairing request
// dialog below (shown over anything, the game paused) takes the code.

#include <cstdint>
#include <string>
#include <vector>

#include "localization.h"
#include "net_access.h"
#include "settings.h"

struct ImFont;

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

// What the tab keeps between frames: SHARE / FULL are given only after their
// confirmation, to the device with this key (the list may change meanwhile).
struct NetWindowState
{
    uint8_t owner_key[NET_KEY_SIZE] = {};
    bool    confirm_owner = false;
    NetAccessLevel confirm_level = NetAccessLevel::share;   // what the confirmation gives
    NetPairingRequest request;
    NetInvite         invite;
};

struct NetWindowContext
{
    NetWindowState*     state = nullptr;
    Settings*           settings = nullptr;
    const Localization* strings = nullptr;
    NetAccess*          access = nullptr;   // nullptr while networking is off
    bool                listen_failed = false;   // on, but the port couldn't be opened
    const char*         this_device = "";   // our key's fingerprint
    const std::vector<NetSpeedSummary>* speed = nullptr;   // speed test results, by device key
    bool                internet = false;   // INTERNET is on (the beacon status row shows)
    // The piccolos this one connects to (its client side, SHARE / PAIR), and
    // FORGET on one of them (the index, for main.cpp; -1 = none).
    const std::vector<std::string>* hosts = nullptr;
    int32_t*            forget_host = nullptr;
};

// The tab's contents, inside SETTINGS' tab. The rows always edit the global
// settings (networking isn't per core or game), with labels `label_width`
// wide like the other tabs'.
void net_window_draw_tab(const NetWindowContext* context, float label_width);

// The pairing request dialog (drawn every frame while one is active).
void net_pairing_request_open(NetPairingRequest* request, const NetAccessEvent* event);
void net_pairing_request_close(NetPairingRequest* request);
void net_pairing_request_draw(NetPairingRequest* request, NetAccess* access, const Localization* strings,
                             ImFont* pixel_font, int32_t scale);
// The four port LEDs lit in turn, then `label`: something being waited for.
void net_window_waiting_leds(const char* label);

// TOOLS > INVITE (drawn every frame while open). `access` nullptr = network off.
void net_invite_open(NetInvite* invite);
void net_invite_close(NetInvite* invite, NetAccess* access);
void net_invite_draw(NetInvite* invite, NetAccess* access, const Localization* strings, ImFont* pixel_font,
                     int32_t scale);
// A pairing request while waiting: answered with the typed code (true), or not ours.
bool net_invite_take_request(NetInvite* invite, NetAccess* access, const NetAccessEvent* event);
