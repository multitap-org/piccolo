#include "menus.h"

struct MenuItem
{
    Str         label;
    MenuAction  action;
    MenuShown   shown;
};

struct Menu
{
    Str             label;
    const MenuItem* items;
    int32_t         item_count;
};

static const MenuItem GAME_ITEMS[] = {
    { Str::menu_load_game,   MenuAction::load_game,   MenuShown::always },
    { Str::menu_close_game,  MenuAction::close_game,  MenuShown::greyed_no_game },
    { Str::menu_game,        MenuAction::separator,   MenuShown::with_game },
    { Str::menu_pause,       MenuAction::pause,       MenuShown::with_game },   // reads RESUME while the player paused (main.cpp)
    { Str::menu_quick_save,  MenuAction::quick_save,  MenuShown::with_game },
    { Str::menu_quick_load,  MenuAction::quick_load,  MenuShown::with_game },
    { Str::menu_screenshot,  MenuAction::screenshot,  MenuShown::with_game },
    { Str::menu_reset,       MenuAction::reset,       MenuShown::with_game },
    { Str::menu_hard_reset,  MenuAction::hard_reset,  MenuShown::with_game },
    { Str::menu_core_config, MenuAction::core_config, MenuShown::with_game },
    { Str::menu_quit,        MenuAction::quit,        MenuShown::always },
};

// The window's tabs in order, then INPUT (it stays here: a menu of its own
// would confuse people once a game runs).
static const MenuItem CONFIG_ITEMS[] = {
    { Str::menu_general,   MenuAction::general,   MenuShown::always },
    { Str::menu_interface, MenuAction::interface, MenuShown::always },
    { Str::menu_video,     MenuAction::video,     MenuShown::always },
    { Str::menu_sound,     MenuAction::sound,     MenuShown::always },
    { Str::menu_network,   MenuAction::network,   MenuShown::always },
    { Str::menu_input,     MenuAction::input,     MenuShown::always },
};

// Tools: the game's DVR, the core tools, then the network tools. The browser
// has no CORE MANAGER: its core is linked into the page's program.
// TODO(round1): PAIR and INVITE become one PAIR... entry with the one pairing
// window (testing round 1, item 5); until then both stay.
static const MenuItem TOOLS_ITEMS[] = {
    { Str::menu_game_dvr,        MenuAction::game_dvr,        MenuShown::greyed_no_game },
#if !defined(__EMSCRIPTEN__)
    { Str::menu_cores,           MenuAction::cores,           MenuShown::always },
#endif
    { Str::menu_options_manager, MenuAction::options_manager, MenuShown::always },
    { Str::menu_tools,           MenuAction::separator,       MenuShown::always },
    { Str::menu_pair,            MenuAction::pair,            MenuShown::always },
    { Str::menu_invite,          MenuAction::invite,          MenuShown::always },
    { Str::menu_guests,          MenuAction::guests,          MenuShown::always },
    { Str::menu_share,           MenuAction::share,           MenuShown::always },
};

static const MenuItem MISC_ITEMS[] = {
    { Str::menu_debug_window, MenuAction::debug_window, MenuShown::always },
    { Str::menu_about,        MenuAction::about,        MenuShown::always },
};

static const Menu MENUS[] = {
    { Str::menu_game,   GAME_ITEMS,   static_cast<int32_t>(sizeof(GAME_ITEMS) / sizeof(GAME_ITEMS[0])) },
    { Str::menu_config, CONFIG_ITEMS, static_cast<int32_t>(sizeof(CONFIG_ITEMS) / sizeof(CONFIG_ITEMS[0])) },
    { Str::menu_tools,  TOOLS_ITEMS,  static_cast<int32_t>(sizeof(TOOLS_ITEMS) / sizeof(TOOLS_ITEMS[0])) },
    { Str::menu_misc,   MISC_ITEMS,   static_cast<int32_t>(sizeof(MISC_ITEMS) / sizeof(MISC_ITEMS[0])) },
};

constexpr int32_t MENU_COUNT = static_cast<int32_t>(sizeof(MENUS) / sizeof(MENUS[0]));

int32_t menus_count()
{
    return MENU_COUNT;
}

Str menus_label(int32_t menu)
{
    return MENUS[menu].label;
}

int32_t menus_item_count(int32_t menu)
{
    return MENUS[menu].item_count;
}

Str menus_item_label(int32_t menu, int32_t item)
{
    return MENUS[menu].items[item].label;
}

MenuAction menus_item_action(int32_t menu, int32_t item)
{
    return MENUS[menu].items[item].action;
}

MenuShown menus_item_shown(int32_t menu, int32_t item)
{
    return MENUS[menu].items[item].shown;
}

const char* menu_action_name(MenuAction action)
{
    switch (action)
    {
        case MenuAction::none:       return "NONE";
        case MenuAction::pause:      return "PAUSE";
        case MenuAction::load_game:  return "LOAD GAME";
        case MenuAction::close_game: return "CLOSE GAME";
        case MenuAction::reset:      return "SOFT RESET";
        case MenuAction::hard_reset: return "HARD RESET";
        case MenuAction::quick_save: return "QUICK SAVE";
        case MenuAction::quick_load: return "QUICK LOAD";
        case MenuAction::save_state: return "SAVE STATE";
        case MenuAction::load_state: return "LOAD STATE";
        case MenuAction::screenshot: return "TAKE SCREENSHOT";
        case MenuAction::quit:       return "QUIT";
        case MenuAction::general:    return "GENERAL";
        case MenuAction::interface:  return "INTERFACE";
        case MenuAction::video:      return "VIDEO";
        case MenuAction::sound:      return "SOUND";
        case MenuAction::input:      return "INPUT";
        case MenuAction::about:      return "ABOUT";
        case MenuAction::debug_window: return "DEBUG";
        case MenuAction::recent_game:  return "RECENT GAME";
        case MenuAction::clear_recents: return "CLEAR ALL";
        case MenuAction::game_dvr:     return "GAME DVR";
        case MenuAction::options_manager: return "CORE OPTIONS MANAGER";
        case MenuAction::core_config: return "CORE CONFIGURATION";
        case MenuAction::cores:      return "CORE MANAGER";
        case MenuAction::share:      return "FILE TRANSFER";
        case MenuAction::pair:       return "PAIR";
        case MenuAction::invite:     return "INVITE";
        case MenuAction::guests:     return "REMOTE PLAY";
        case MenuAction::network:    return "NETWORK";
        case MenuAction::separator:    return "SEPARATOR";
        default:                     return "?";
    }
}
