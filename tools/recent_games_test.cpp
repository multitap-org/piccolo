// Checks the recent games list (recent_games.h): order, moving a game back
// to the top, the limit, labels and save/load.
//
// Usage: piccolo_recent_games_test <scratch directory>
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>
#include <string>

#include "recent_games.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static const char* const SNES = "/cores/snes9x_libretro.so";
static const char* const GBA = "/cores/mgba_libretro.so";

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <scratch directory>\n", argv[0]);
        return 2;
    }

    RecentGames recent = {};
    recent_games_add(&recent, SNES, "/roms/Super Mario World (USA).sfc");
    recent_games_add(&recent, GBA, "/roms/Pokémon ポケモン.gba");
    recent_games_add(&recent, SNES, "");
    check(recent.games.size() == 3, "three games");
    check(recent.games[0].core_path == SNES && recent.games[0].content_path.empty(), "most recent first");
    check(recent.games[2].content_path == "/roms/Super Mario World (USA).sfc", "oldest last");

    recent_games_add(&recent, SNES, "/roms/Super Mario World (USA).sfc");
    check(recent.games.size() == 3, "loading one again doesn't duplicate it");
    check(recent.games[0].content_path == "/roms/Super Mario World (USA).sfc", "it moves to the top");
    check(recent.games[1].content_path.empty() && recent.games[2].core_path == GBA, "the others keep their order");

    // Same game file on another core: a different entry.
    recent_games_add(&recent, GBA, "/roms/Super Mario World (USA).sfc");
    check(recent.games.size() == 4, "same content on another core is its own entry");

    for (int i = 0; i < 20; i++)
    {
        char content[64] = {};
        std::snprintf(content, sizeof(content), "/roms/game %d.sfc", i);
        recent_games_add(&recent, SNES, content);
    }
    check(recent.games.size() == RECENT_GAMES_MAX, "never more than the limit");
    check(recent.games[0].content_path == "/roms/game 19.sfc", "the newest is first");
    check(recent.games[RECENT_GAMES_MAX - 1].content_path == "/roms/game 10.sfc", "the oldest kept is the 10th newest");

    char label[512] = {};
    RecentGame game = {};
    game.core_path = SNES;
    game.content_path = "/roms/Super Mario World (USA).sfc";
    recent_games_label(&game, "NO GAME", label, sizeof(label));
    check(std::strcmp(label, "Super Mario World (USA)  [snes9x]") == 0, "label: game and core");
    game.content_path = "";
    recent_games_label(&game, "NO GAME", label, sizeof(label));
    check(std::strcmp(label, "NO GAME  [snes9x]") == 0, "label: a core alone");
    char name[256] = {};
    char core[256] = {};
    game.content_path = "/roms/Super Mario World (USA).sfc";
    recent_games_names(&game, "NO GAME", name, sizeof(name), core, sizeof(core));
    check(std::strcmp(name, "Super Mario World (USA)") == 0 && std::strcmp(core, "snes9x") == 0, "names: game and core apart");

    std::string path = std::string(argv[1]) + "/recent.cfg";
    std::remove(path.c_str());
    RecentGames loaded = {};
    check(recent_games_load(&loaded, path.c_str()) && loaded.games.empty(), "a missing file is an empty list");

    RecentGames small = {};
    recent_games_add(&small, GBA, "/roms/Pokémon ポケモン.gba");
    recent_games_add(&small, SNES, "");
    check(recent_games_save(&small, path.c_str()), "save");
    check(recent_games_load(&loaded, path.c_str()), "load");
    check(loaded.games.size() == 2, "both entries back");
    check(loaded.games.size() == 2 && loaded.games[0].core_path == SNES && loaded.games[0].content_path.empty(),
          "order and the core-only entry kept");
    check(loaded.games.size() == 2 && loaded.games[1].content_path == "/roms/Pokémon ポケモン.gba", "UTF-8 path kept");

    // A path settings_file_set refuses (a blank at its end) doesn't stop the
    // others from being saved: it's left out, the rest numbered without a gap.
    RecentGames mixed = {};
    recent_games_add(&mixed, SNES, "/roms/last.sfc");
    recent_games_add(&mixed, SNES, "/roms/ends in a blank.sfc ");
    recent_games_add(&mixed, GBA, "/roms/first.gba");
    check(mixed.games.size() == 3, "the refused path is in the list");
    check(recent_games_save(&mixed, path.c_str()), "save with a refused path");
    RecentGames mixed_loaded = {};
    check(recent_games_load(&mixed_loaded, path.c_str()), "load after a refused path");
    check(mixed_loaded.games.size() == 2, "the refused path is left out");
    check(mixed_loaded.games.size() == 2 && mixed_loaded.games[0].content_path == "/roms/first.gba" &&
              mixed_loaded.games[1].content_path == "/roms/last.sfc",
          "the others kept, in order");

    if (g_failures == 0)
    {
        std::printf("recent games: all checks passed\n");
        return 0;
    }
    std::printf("recent games: %d check(s) failed\n", g_failures);
    return 1;
}
