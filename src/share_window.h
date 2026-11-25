#pragma once

// TOOLS > FILE TRANSFER (was SHARE): copies games, saves, states and
// screenshots between this piccolo and another one that gave us OWNER access
// (piccolo-share), in the retro style:
//
//   +- FILE TRANSFER ------------------------------------------------+
//   | THIS PICCOLO                        LIVING ROOM PC *            |
//   | [ roms                        v ]   [ ROMS                  v ] |
//   | roms/gba                            ROMS/gba                    |
//   | +- folders -------------------+     +- folders --------------+  |
//   | | ..                          |     | ..                     |  |
//   | +-----------------------------+ [>] +------------------------+  |
//   | +- files ---------------------+ [<] +- files ----------------+  |
//   | | Metroid Fusion.gba    8 MIB |     | Zero Mission.gba 8 MIB |  |
//   | +-----------------------------+     +------------------------+  |
//   | SENDING METROID FUSION.GBA [####    ] 3.1 / 8.0 MIB   [CANCEL] |
//   +-----------------------------------------------------------------+
//
// Each side: its shared folders in the combo (game folders, SAVES, STATES,
// SCREENSHOTS), where we are, folders on top and files below. The arrows in
// the middle (retro_ui_arrow_button) copy the selected file to the same path
// in the other side's shared folder of its kind (MIRROR FOLDER STRUCTURE, on
// by default: `mirror_folders`), or into the folder open on the other side
// (share_paths.h has the rule). MIRROR FOLDER STRUCTURE is a checkbox above
// the bottom line; it's a global setting (share_mirror_folders) that main.cpp
// hands in and writes back (`mirror_folders_set`). This side is this piccolo's own shared folders
// (net_share_folders.h, read here directly); the other side is what its
// list_results say, asked for and handed in by the caller
// (share_window_remote_wants_list / share_window_set_remote_list). Copies
// asked for go into the action's `copies`, which share_window_apply hands to
// the connection; the line at the bottom shows the one running
// (ShareTransferLine).
//
// A game selected in a game folder is looked up in that side's SAVES and
// STATES (<core>/<game stem>.*: what the core writes); "WITH ITS SAVE AND N
// STATES" copies them along, into the other side's SAVES and STATES under
// the same core folder, so the game goes on where it was. A file in grey is
// on the other side already with the same name, size and date (copies keep
// the date). A copy that finds another file with its name asks before
// replacing it (`conflicts`, handed in by the caller).
//
// Split like every window: share_window_update lists this side and works
// out the grey marks (no GUI), share_window_state fills what the window
// shows from the rest of the program, share_window_draw draws it and records
// what was pressed (CONNECT, PAIR, DISCONNECT, CANCEL, the copies, a file
// picked), and share_window_apply acts on it (no GUI): it looks up a picked
// game's saves and states, and connects, copies or cancels on SHARE's
// connection. PAIR is main.cpp's: it opens TOOLS > PAIR.

#include <cstdint>
#include <string>
#include <vector>

#include "localization.h"
#include "net_share.h"
#include "net_share_folders.h"
#include "net_share_link.h"

#include "retro_ui_font.h"

struct SharePane
{
    std::string                path;   // virtual: "" = the shared folders, "ROMS/gba" = inside one
    std::vector<NetShareEntry> entries;
    NetShareStatus             status = NetShareStatus::ok;
    bool                       stale = true;     // list again
    bool                       waiting = false;  // the other side: asked, no answer yet
    int32_t                    selected = -1;    // a file in `entries`
    std::vector<uint8_t>       same;             // per entry: 1 = on the other side too (grey)
};

// A save or state that goes with a game (virtual path on its side).
struct ShareCompanion
{
    std::string path;    // "SAVES/mGBA/Metroid Fusion.srm"
    uint64_t    size = 0;
    uint64_t    mtime = 0;
};

// The selected game's saves and states, looked up on its side: this side
// at once, the other one a listing at a time (SAVES, then each core folder).
struct ShareCompanions
{
    bool        remote = false;          // whose game
    std::string game;                    // its virtual path ("" = no game selected)
    std::string stem;                    // its name without the extension
    std::vector<std::string> folders;    // still to list
    bool        waiting = false;         // the other side: folders[0] asked
    std::vector<ShareCompanion> files;
    uint32_t    saves = 0;
    uint32_t    states = 0;              // .stateN files (their .png pictures go along, not counted)
};

// One copy asked for, in the window's words: from a virtual path on one
// side into a folder on the other.
struct ShareCopy
{
    bool        to_remote = true;   // this piccolo -> the other one
    std::string from;               // "roms/gba/Metroid Fusion.gba"
    std::string to;                 // "ROMS/gba/Metroid Fusion.gba"
    uint64_t    size = 0;
    uint64_t    mtime = 0;
    bool        replace = false;    // another file there may be replaced (asked first)
};

// What the bottom line shows (from the caller: the copy running).
struct ShareTransferLine
{
    bool        active = false;
    bool        sending = true;   // this piccolo sends (or receives)
    bool        paused = false;   // someone plays remotely: copies wait
    std::string name;
    uint64_t    done = 0;
    uint64_t    size = 0;
    uint32_t    rate_kbps = 0;
    uint32_t    queued = 0;       // copies after this one
};

struct ShareWindow
{
    bool      open = false;
    SharePane local;
    SharePane remote;
    std::vector<NetShareEntry> remote_roots;   // the other side's shared folders (its root listing)
    std::vector<NetShareEntry> local_roots;    // ours, read with this side's listing
    std::string message;             // what the last copy said, when it didn't go well

    // Connecting (the other side, while no piccolo is connected with OWNER).
    int32_t found_selected = -1;     // in the link's found list
    int32_t paired_selected = -1;    // in the link's paired list: across the internet (through the beacon)
    bool    invite_selected = false; // pair across the internet: our code, typed in the other's INVITE

    ShareCompanions companions;
    bool      with_companions = true;      // WITH ITS SAVE AND N STATES
    bool      mirror_folders = true;       // MIRROR FOLDER STRUCTURE (the caller's, from the setting)
    std::vector<ShareCopy> conflicts;      // found another file there: replace? (from the caller)
    bool      remote_asked = false;        // a listing of the other side is on its way
    bool      same_stale = true;           // the grey marks need working out again
    bool      any_same = false;            // some file is grey (the legend shows)
};

struct ShareWindowState
{
    const Localization*       strings = nullptr;
    const NetShareLinkStatus* link = nullptr;   // our connection to another piccolo (nullptr: none)
    const ShareTransferLine*  line = nullptr;
    bool                      connected = false;   // ready, at OWNER (SHARE) level: the other side shows
};

enum class ShareWindowPress
{
    none,
    connect,         // CONNECT: to found / paired_selected, or INVITE's code across the internet
    pair,            // PAIR on a piccolo we aren't paired with: TOOLS > PAIR on it
    pair_internet,   // PAIR on "another network": TOOLS > PAIR across the internet
    disconnect,      // DISCONNECT
    cancel,          // CANCEL: stop the copy running
};

struct ShareWindowAction
{
    ShareWindowPress       press = ShareWindowPress::none;
    bool                   picked_local = false;    // a file selected on this side: look up its saves and states
    bool                   picked_remote = false;   // the same on the other side
    std::vector<ShareCopy> copies;                  // asked for (the arrows, REPLACE)
    bool                   mirror_folders_set = false;   // MIRROR FOLDER STRUCTURE clicked: the setting
    bool                   mirror_folders = true;        // becomes this (main.cpp writes it, global)
};

// What main.cpp does itself after share_window_apply.
enum class ShareWindowRequest
{
    none,
    pair,            // open TOOLS > PAIR on found_selected (on the LAN)
    pair_internet,   // open TOOLS > PAIR across the internet
};

// Before the state, every frame the window is open: this side listed again
// when it's stale, and the grey marks worked out (`link` nullptr: none).
void share_window_update(ShareWindow* window, const NetShareFolders* local_folders, const NetShareLinkStatus* link);
void share_window_state(ShareWindowState* state, const Localization* strings, const NetShareLinkStatus* link,
                        const ShareTransferLine* line);
void share_window_draw(ShareWindow* window, const ShareWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                       ShareWindowAction* action);
void share_window_open(ShareWindow* window);
// `local_folders`: this piccolo's shared folders; `link` not started: only
// the window changes; `status`: the connection's last status (the found
// list); `beacon`: BEACON SERVER.
ShareWindowRequest share_window_apply(ShareWindow* window, const ShareWindowAction* action,
                                      const NetShareFolders* local_folders, NetShareLink* link,
                                      const NetShareLinkStatus* status, const char* beacon);

// The other side's folder to list, one at a time: the folder open there, or
// one where the selected game's saves and states may be (the caller sends a
// list_request for it and hands the answer to share_window_set_remote_list).
bool share_window_remote_wants_list(ShareWindow* window, std::string* path);
void share_window_set_remote_list(ShareWindow* window, const char* path, NetShareStatus status,
                                  const std::vector<NetShareEntry>* entries);
// The other piccolo left (or a new one came): its side starts over.
void share_window_remote_reset(ShareWindow* window);
// A copy ended: `exists` asks whether to replace (the rest are the caller's).
void share_window_add_conflict(ShareWindow* window, const ShareCopy* copy);
