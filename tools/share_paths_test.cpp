// Checks where FILE TRANSFER's copies land (share_paths.h): MIRROR FOLDER
// STRUCTURE on (same path under the other side's shared folder of the same
// kind: the one with the same name, else the first) and off (the folder open
// on the other side), and the fallbacks.
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <string>
#include <vector>

#include "share_paths.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static void add_root(std::vector<NetShareEntry>* roots, NetShareKind kind, const char* name)
{
    NetShareEntry entry;
    entry.kind = kind;
    entry.name = name;
    roots->push_back(entry);
}

// What `from` becomes with `mirror`, copied while `open` is open on the other
// side; "-" when it can't be copied.
static std::string target(bool mirror, const std::vector<NetShareEntry>* from_roots,
                          const std::vector<NetShareEntry>* to_roots, const char* from, const char* open)
{
    std::string from_path = from;
    std::string open_folder = open;
    std::string to;
    if (!share_paths_target(mirror, from_roots, to_roots, &from_path, &open_folder, &to))
    {
        return "-";
    }
    return to;
}

int main()
{
    // This piccolo: two game folders, SAVES, STATES, SCREENSHOTS.
    std::vector<NetShareEntry> here;
    add_root(&here, NetShareKind::games, "roms");
    add_root(&here, NetShareKind::games, "arcade");
    add_root(&here, NetShareKind::saves, "SAVES");
    add_root(&here, NetShareKind::states, "STATES");
    add_root(&here, NetShareKind::screenshots, "SCREENSHOTS");
    // The other one: its game folders named otherwise (and one the same),
    // no STATES, SCREENSHOTS.
    std::vector<NetShareEntry> there;
    add_root(&there, NetShareKind::games, "ROMS");
    add_root(&there, NetShareKind::games, "arcade");
    add_root(&there, NetShareKind::saves, "SAVES");
    add_root(&there, NetShareKind::screenshots, "SCREENSHOTS");
    // An older piccolo: no SCREENSHOTS.
    std::vector<NetShareEntry> older;
    add_root(&older, NetShareKind::games, "ROMS");
    add_root(&older, NetShareKind::saves, "SAVES");

    // Mirrored: the same path under the folder of its kind, whatever is open.
    check(target(true, &here, &there, "roms/gba/Metroid Fusion.gba", "SAVES/mGBA") == "ROMS/gba/Metroid Fusion.gba",
          "game: same path in the first game folder there");
    check(target(true, &here, &there, "roms/gba/Metroid Fusion.gba", "") == "ROMS/gba/Metroid Fusion.gba",
          "game: mirrored with nothing open there");
    check(target(true, &here, &there, "arcade/neogeo/mslug.zip", "ROMS") == "arcade/neogeo/mslug.zip",
          "game: the game folder with the same name, not the first");
    check(target(true, &here, &there, "roms/Tetris.gb", "ROMS/gba") == "ROMS/Tetris.gb",
          "game at a shared folder's top stays at the top");
    check(target(true, &here, &there, "SAVES/mGBA/Metroid Fusion.srm", "ROMS") == "SAVES/mGBA/Metroid Fusion.srm",
          "save: same core folder in SAVES");
    check(target(true, &here, &there, "SCREENSHOTS/mGBA/shot-001.png", "ROMS/gba") ==
              "SCREENSHOTS/mGBA/shot-001.png",
          "screenshot: same path in SCREENSHOTS");
    check(target(true, &there, &here, "ROMS/snes/Chrono Trigger.sfc", "") == "roms/snes/Chrono Trigger.sfc",
          "the other way round: into our first game folder");

    // No folder of its kind there: into the folder open there, as when off.
    check(target(true, &here, &there, "STATES/mGBA/Metroid Fusion.state1", "SAVES/mGBA") ==
              "SAVES/mGBA/Metroid Fusion.state1",
          "no STATES there: into the open folder");
    check(target(true, &here, &older, "SCREENSHOTS/shot.png", "ROMS/gba") == "ROMS/gba/shot.png",
          "an older piccolo without SCREENSHOTS: into the open folder");
    check(target(true, &here, &older, "SCREENSHOTS/shot.png", "") == "-",
          "no folder of its kind and nothing open: nowhere");

    // Off: today's behaviour, into the folder open there.
    check(target(false, &here, &there, "roms/gba/Metroid Fusion.gba", "ROMS") == "ROMS/Metroid Fusion.gba",
          "off: into the open folder");
    check(target(false, &here, &there, "roms/gba/Metroid Fusion.gba", "") == "-",
          "off: nothing open there, nowhere");

    // Paths that aren't a file inside a known shared folder don't mirror.
    std::string to;
    std::string top = "roms";
    check(!share_paths_mirror(&here, &there, &top, &to), "a shared folder itself isn't mirrored");
    std::string unknown = "elsewhere/game.gba";
    check(!share_paths_mirror(&here, &there, &unknown, &to), "unknown shared folder isn't mirrored");

    // A kind from a newer build reads as a folder (net_share.h): it mirrors
    // only onto a root that also reads as a folder.
    std::vector<NetShareEntry> newer;
    add_root(&newer, NetShareKind::folder, "RECORDINGS");
    std::string recording = "RECORDINGS/clip.mkv";
    check(!share_paths_mirror(&newer, &there, &recording, &to), "unknown kind: no folder of it here");
    check(share_paths_mirror(&newer, &newer, &recording, &to) && to == "RECORDINGS/clip.mkv",
          "unknown kind onto the same kind");

    if (g_failures == 0)
    {
        std::printf("share paths: all checks passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
