#pragma once

// piccolo's NETWORK tab in SETTINGS (CONFIG > NETWORK opens SETTINGS on
// it): the listener's settings, this device's key, pairing with its code,
// and the paired devices. It was a window of its own until 2026-11-19
// (Andrés: network settings belong in SETTINGS).
//
//   +- NETWORK (tab) ----------------------------------------------+
//   |   [x] ENABLE NETWORKING                                       |
//   |   DEVICE NAME        [LIVING ROOM                           ] |
//   |   PORT               [- 55420 +]                              |
//   | v INTERNET                                                    |
//   |   [x] ENABLE CONNECTIONS OVER INTERNET                        |
//   |   BEACON SERVER      [beacon.get-piccolo.app                ] |
//   |   DEVICE ID          [3fa2-91c0-7d4e-b815                   ] |
//   |   STATUS             * READY: PAIRED DEVICES CAN CONNECT      |
//   | v DEVICES                                                     |
//   |   CONNECTED NOW        PAIRED DEVICES                         |
//   |   (a card each)        (a card each: both directions)         |
//   +--------------------------------------------------------------+
//
// The settings rows edit piccolo's global settings; main.cpp restarts the
// listener when ENABLE NETWORKING or PORT change. Pairing isn't here
// (Andrés, 2026-11-22: SETTINGS holds configuration, not actions): TOOLS >
// PAIR... (pair_window.h) does it.
//
// DEVICES (Andrés, 2026-11-25: one record per device, pairing v3 page):
// CONNECTED NOW lists the connections, ours to a device (FILE TRANSFER's)
// and theirs to us; PAIRED DEVICES has one card per paired device with both
// directions: PERMISSIONS GRANTED HERE (checkboxes, only what that device
// uses; FILE TRANSFER and SETTINGS for an unsigned build only after the red
// question, net_caps_ui.h) and PERMISSIONS GRANTED ON <NAME> (read only, the
// other device decides; the last it said). CONNECT opens FILE TRANSFER
// connected to it; FORGET unpairs. Every action names its device by key.
//
// Split like every window: *_state fills what it shows (no GUI), *_draw
// draws it and records what was done in an action, and *_apply acts on it
// (no GUI). The tab is drawn inside SETTINGS' tab (settings_window.cpp);
// main.cpp fills its state and applies its action around SETTINGS' drawing.

#include <cstdint>
#include <string>
#include <vector>

#include "localization.h"
#include "net_access.h"
#include "net_caps_ui.h"
#include "net_share_link.h"
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

// What the tab keeps between frames: the red question asked for a card's
// checkbox (the device by key: the list may change meanwhile).
struct NetWindow
{
    NetCapsUnsignedAsk ask;
};

// One connection, for CONNECTED NOW.
struct NetWindowPeer
{
    bool     connected = false;    // authorized, with a paired device
    uint8_t  key[NET_KEY_SIZE] = {};
    std::string name;
    NetCaps  caps = 0;             // what it uses here now
    bool     has_address = false;
    char     address[64] = {};
    bool     has_stats = false;
    NetLinkStats stats;
    const NetSpeedSummary* speed = nullptr;   // its last speed test, nullptr = none
};

// One paired device's card.
struct NetWindowDevice
{
    NetTrustedDevice record;       // both directions, as last saved
    bool             connected = false;   // connected now, either way (GRANTED ON is current)
    int32_t          online = -1;  // on the beacon now: 1 / 0, -1 unknown
};

struct NetWindowState
{
    const Settings*     settings = nullptr;   // the rows (global settings)
    const Localization* strings = nullptr;
    bool                network = false;      // the NetAccess runs
    bool                listen_failed = false;   // on, but the port couldn't be opened
    const char*         this_device = "";     // our key's fingerprint
    bool                internet = false;     // INTERNET is on (the beacon status row shows)
    NetBeaconStatus     beacon = NetBeaconStatus::off;   // INTERNET STATUS
    NetCaps             offers = 0;           // what this device offers (GRANTED HERE's choices)
    NetCaps             uses = 0;             // what it uses (CONNECT greyed without any there)
    bool                devices_known = false;   // the list below means something (NetAccess or FILE TRANSFER runs)
    bool                link_known = false;   // FILE TRANSFER's connection runs (CONNECT works)
    std::vector<NetWindowDevice> devices;     // copied: drawn without the network lock
    NetWindowPeer       peers[NET_MAX_PEERS]; // their connections to us
    // Ours (FILE TRANSFER's), when ready.
    bool                link_connected = false;
    std::string         link_name;
    NetCaps             link_caps = 0;
};

struct NetWindowAction
{
    SettingsUiEdit setting;              // a settings row changed
    bool           set_grants = false;   // GRANTED HERE: `grants` for the device with `key`
    NetCaps        grants = 0;
    bool           forget = false;       // FORGET the device with `key`
    bool           connect = false;      // CONNECT to the device with `key` (main.cpp: FILE TRANSFER)
    uint8_t        key[NET_KEY_SIZE] = {};
    bool           disconnect = false;   // DISCONNECT connection `disconnect_peer`, if it's still the
    uint32_t       disconnect_peer = 0;  // device with `disconnect_key`
    uint8_t        disconnect_key[NET_KEY_SIZE] = {};
    bool           disconnect_link = false;   // DISCONNECT our own (FILE TRANSFER's)
};

// The tab's state. `access` nullptr while networking is off; `link`
// FILE TRANSFER's connection's status, nullptr when it doesn't run; `speed`:
// the speed test results, by device key. Under the network lock.
void net_window_state(NetWindowState* state, const Settings* settings, const Localization* strings,
                      const NetAccess* access, const NetShareLinkStatus* link, bool listen_failed,
                      const char* this_device, const std::vector<NetSpeedSummary>* speed, bool internet,
                      NetCaps offers, NetCaps uses);
// The tab's contents, inside SETTINGS' tab. The rows always edit the global
// settings (networking isn't per core or game), with labels `label_width`
// wide like the other tabs'.
void net_window_draw_tab(NetWindow* window, const NetWindowState* state, float label_width,
                         NetWindowAction* action);
// The settings row's edit, GRANTED HERE, FORGET and DISCONNECT: on `access`
// (under the network lock) while networking is on, else through FILE
// TRANSFER's connection `link` (its status `link_status`; either may be
// nullptr). True when the settings changed (SETTINGS saves when it closes).
// CONNECT is main.cpp's (it opens FILE TRANSFER).
bool net_window_apply(const NetWindowAction* action, Settings* settings, NetAccess* access, NetShareLink* link,
                      const NetShareLinkStatus* link_status);

// The four port LEDs lit in turn, then `label`: something being waited for.
void net_window_waiting_leds(const char* label);
