#include "recent_games.h"

#include <cstdio>

#include "content_archive.h"
#include "host_log.h"
#include "libretro_host.h"   // libretro_core_short_name
#include "settings_file.h"

void recent_games_add(RecentGames* recent, const char* core_path, const char* content_path)
{
    RecentGame game = {};
    game.core_path = core_path;
    game.content_path = content_path != nullptr ? content_path : "";
    for (size_t i = 0; i < recent->games.size(); i++)
    {
        if (recent->games[i].core_path == game.core_path && recent->games[i].content_path == game.content_path)
        {
            recent->games.erase(recent->games.begin() + static_cast<ptrdiff_t>(i));
            break;
        }
    }
    recent->games.insert(recent->games.begin(), game);
    if (recent->games.size() > RECENT_GAMES_MAX)
    {
        recent->games.resize(RECENT_GAMES_MAX);
    }
}

bool recent_games_load(RecentGames* recent, const char* path)
{
    recent->games.clear();
    SettingsFile file = {};
    if (!settings_file_load(&file, path))
    {
        return false;
    }
    for (size_t i = 0; i < file.sections.size() && recent->games.size() < RECENT_GAMES_MAX; i++)
    {
        const char* section = file.sections[i].name.c_str();
        if (section[0] == '\0')
        {
            continue;   // the global section: nothing of ours there
        }
        const char* core = settings_file_get(&file, section, "core");
        const char* content = settings_file_get(&file, section, "content");
        if (core == nullptr || core[0] == '\0')
        {
            continue;
        }
        RecentGame game = {};
        game.core_path = core;
        game.content_path = content != nullptr ? content : "";
        recent->games.push_back(game);
    }
    return true;
}

bool recent_games_save(const RecentGames* recent, const char* path)
{
    SettingsFile file = {};
    uint32_t saved = 0;
    for (size_t i = 0; i < recent->games.size(); i++)
    {
        const RecentGame* game = &recent->games[i];
        // Tried on a scratch file first: settings_file_set refuses a path that
        // ends in a blank or holds a line break, and one such game must not
        // stop the others from being saved (nor leave half an entry). It stays
        // in the list and playable; only RECENT after a restart forgets it.
        SettingsFile probe = {};
        if (!settings_file_set(&probe, "1", "core", game->core_path.c_str()) ||
            !settings_file_set(&probe, "1", "content", game->content_path.c_str()))
        {
            host_log(LogLevel::warning, "Recent games: %s can't be stored in recent.cfg; left out",
                     game->content_path.c_str());
            continue;
        }
        char section[32] = {};   // room for any size_t
        std::snprintf(section, sizeof(section), "%zu", static_cast<size_t>(saved) + 1);
        settings_file_set(&file, section, "core", game->core_path.c_str());
        settings_file_set(&file, section, "content", game->content_path.c_str());
        saved++;
    }
    return settings_file_save(&file, path, "piccolo: recent games, most recent first");
}

void recent_games_names(const RecentGame* game, const char* no_game, char* name, size_t name_size, char* core,
                        size_t core_size)
{
    if (!libretro_core_short_name(game->core_path.c_str(), core, core_size))
    {
        std::snprintf(core, core_size, "?");
    }
    if (game->content_path.empty() || !content_archive_game_name(game->content_path.c_str(), name, name_size))
    {
        std::snprintf(name, name_size, "%s", no_game);
    }
}

void recent_games_label(const RecentGame* game, const char* no_game, char* out, size_t out_size)
{
    char core[256] = {};
    char content[256] = {};
    recent_games_names(game, no_game, content, sizeof(content), core, sizeof(core));
    std::snprintf(out, out_size, "%s  [%s]", content, core);
}
