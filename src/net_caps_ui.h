#pragma once

// What the pairing window and SETTINGS > NETWORK > DEVICES show about
// capabilities (multitap-net's NetCaps; labels as Andrés set them on the
// pairing v3 page, 2026-11-25): their names (STREAM shows as REMOTE PLAY,
// SHARE as FILE TRANSFER), the kind chips, the PERMISSIONS checkboxes, and
// the red UNSIGNED BUILD question asked before FILE TRANSFER or SETTINGS is
// granted to a device whose build Multitap didn't sign.

#include <cstddef>
#include <cstdint>
#include <string>

#include "localization.h"
#include "net_trust.h"

// One capability's label (a single bit), or "-" for none.
const char* net_caps_ui_label(const Localization* strings, NetCaps cap);
// "FILE TRANSFER, SETTINGS", or "-" for nothing.
void net_caps_ui_list(const Localization* strings, NetCaps caps, char* out, size_t size);
// PICCOLO / PICCOLETTO / PICCOLINO.
const char* net_caps_ui_kind(const Localization* strings, NetAppKind kind);

// What a kind of device offers and uses (the model on the pairing page): a
// device's own word is only known while it's connected, so the lists show
// what its kind can use. piccolo offers everything and uses FILE TRANSFER and
// SETTINGS; piccoletto offers nothing and uses GAMEPAD, REMOTE PLAY and
// SETTINGS; piccolino offers FILE TRANSFER and SETTINGS and uses nothing.
NetCaps net_caps_ui_kind_offers(NetAppKind kind);
NetCaps net_caps_ui_kind_uses(NetAppKind kind);

// The PERMISSIONS checkboxes: one per capability in `choices`, two to a
// row, in bit order. Unticking applies at once (`*value` changes, true is
// returned). Ticking FILE TRANSFER or SETTINGS for an unsigned device
// (`signed_build` false) doesn't: `*asked` gets that bit, for the caller to
// ask with net_caps_ui_unsigned (and tick it only on GRANT ANYWAY).
bool net_caps_ui_checkboxes(const char* id, const Localization* strings, NetCaps choices, bool signed_build,
                            NetCaps* value, NetCaps* asked);

// The red question (retro_ui_danger_palette): UNSIGNED BUILD, "<NAME> RUNS AN
// UNSIGNED BUILD.", what `cap` allows, ONLY GRANT THIS PERMISSION TO PEOPLE
// YOU TRUST; GRANT ANYWAY / CANCEL. Call every frame while `ask->cap` isn't
// 0; it returns the bit on GRANT ANYWAY (and clears the ask on either answer).
struct NetCapsUnsignedAsk
{
    NetCaps     cap = 0;    // 0: nothing asked
    std::string name;       // the device's
    uint32_t    target = 0; // the caller's own note (whose ticks: a request, a card...)
    uint8_t     key[NET_KEY_SIZE] = {};   // the caller's own note (the device, by key)
};
NetCaps net_caps_ui_unsigned(NetCapsUnsignedAsk* ask, const Localization* strings);
