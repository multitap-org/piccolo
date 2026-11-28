// The menu bar: its data (the menus in the current language, RECENT) and
// what picking an item does.

#include "app_menus.h"

#include "app_game.h"
#include "app_guest.h"
#include "app_screenshots.h"
#include "app_settings.h"
#include "app_share.h"
#include "app_states.h"
#include "app_ui_modes.h"

// --- The menu bar's data and RECENT -----------------------------------------

static bool menu_item_listed(int32_t menu, int32_t item, bool running)
{
    return running || menus_item_shown(menu, item) != MenuShown::with_game;
}

// piccolo's menus (menus.h) as retroui's menu bar takes them: RECENT first
// (the hamburger glyph: the games, then CLEAR ALL), then the menus in the
// current language, with the items that show while a game runs (or not).
// Built when the labels change, the pause and whether a game runs, never
// per frame.
void build_menus(App* app)
{
    app->menu_texts.clear();
    app->menu_items.clear();
    app->menus.clear();
    app->menus_built_paused = app->ui.user_paused;
    bool running = game_running(app);
    app->menus_built_running = running;
    app->menus_built_quick_used = app->quick_slot_used;
    app->menus_built_undo_save = app->undo_save_ready;
    app->menus_built_undo_load = app->undo_load_size > 0;
    // The labels first: the items point into them (push_back may move them).
    for (int32_t menu = 0; menu < menus_count(); menu++)
    {
        app->menu_texts.push_back(piccolo_text(&app->strings, menus_label(menu)));
        for (int32_t item = 0; item < menus_item_count(menu); item++)
        {
            if (!menu_item_listed(menu, item, running))
            {
                continue;
            }
            Str label = menus_item_label(menu, item);
            if (menus_item_action(menu, item) == MenuAction::pause && app->ui.user_paused)
            {
                label = Str::menu_resume;   // GAME > PAUSE reads RESUME while the player paused
            }
            app->menu_texts.push_back(piccolo_text(&app->strings, label));
        }
    }
    // The items, every menu's back to back (the menus point into them).
    for (size_t i = 0; i < app->recent_label_pointers.size(); i++)
    {
        app->menu_items.push_back(RetroUiMenuItem{app->recent_label_pointers[i], MENU_ID_RECENT + static_cast<int32_t>(i)});
    }
    if (!app->recent_label_pointers.empty())
    {
        app->menu_items.push_back(RetroUiMenuItem{"", RETRO_UI_MENU_SEPARATOR});
        app->menu_items.push_back(RetroUiMenuItem{piccolo_text(&app->strings, Str::menu_clear_recents),
                                                  static_cast<int32_t>(MenuAction::clear_recents)});
    }
    int32_t recent_count = static_cast<int32_t>(app->menu_items.size());
    std::vector<int32_t> counts;
    size_t text = 0;
    for (int32_t menu = 0; menu < menus_count(); menu++)
    {
        text++;   // (the menu's own label)
        int32_t count = 0;
        for (int32_t item = 0; item < menus_item_count(menu); item++)
        {
            if (!menu_item_listed(menu, item, running))
            {
                continue;
            }
            MenuAction action = menus_item_action(menu, item);
            int32_t id = action == MenuAction::separator ? RETRO_UI_MENU_SEPARATOR : static_cast<int32_t>(action);
            bool enabled = running || menus_item_shown(menu, item) != MenuShown::greyed_no_game;
            if (action == MenuAction::quick_load && !app->quick_slot_used)
            {
                enabled = false;   // as QUICK MENU's QUICK LOAD and LOAD: an empty slot has nothing to load
            }
            if ((action == MenuAction::undo_save_state && !app->undo_save_ready) ||
                (action == MenuAction::undo_load_state && app->undo_load_size == 0))
            {
                enabled = false;   // nothing to undo
            }
            app->menu_items.push_back(RetroUiMenuItem{app->menu_texts[text].c_str(), id, enabled});
            text++;
            count++;
        }
        counts.push_back(count);
    }
    app->menus.push_back(RetroUiMenu{PIXEL_FONT_UTF8_MENU, app->menu_items.data(), recent_count,
                                     piccolo_text(&app->strings, Str::menu_recent_empty)});
    size_t first_item = static_cast<size_t>(recent_count);
    text = 0;
    for (int32_t menu = 0; menu < menus_count(); menu++)
    {
        int32_t count = counts[static_cast<size_t>(menu)];
        app->menus.push_back(RetroUiMenu{app->menu_texts[text].c_str(), app->menu_items.data() + first_item, count, nullptr});
        first_item += static_cast<size_t>(count);
        text += static_cast<size_t>(count) + 1;
    }
}

void refresh_recent_labels(App* app)
{
    app->recent_labels.clear();
    app->recent_label_pointers.clear();
    app->recent_shown.clear();
    app->recent_names.clear();
    app->recent_cores.clear();
    app->recent_rows.clear();
    const char* no_game = piccolo_text(&app->strings, Str::recent_no_game);
    // The browser: one page per core over one shared folder, so only the
    // games of the core this page has (the others can't start here).
    std::string linked_core = bundled_files_linked_core();
    for (size_t i = 0; i < app->recent.games.size(); i++)
    {
        const std::string* core = &app->recent.games[i].core_path;
        if (!linked_core.empty() &&
            (core->size() < linked_core.size() ||
             core->compare(core->size() - linked_core.size(), linked_core.size(), linked_core) != 0))
        {
            continue;
        }
        app->recent_shown.push_back(i);
        char label[600] = {};
        recent_games_label(&app->recent.games[i], no_game, label, sizeof(label));
        app->recent_labels.push_back(label);
        char name[300] = {};
        char core_name[300] = {};
        recent_games_names(&app->recent.games[i], no_game, name, sizeof(name), core_name, sizeof(core_name));
        app->recent_names.push_back(name);
        app->recent_cores.push_back(core_name);
    }
    // Pointers after all the strings exist (push_back may move them).
    for (size_t i = 0; i < app->recent_labels.size(); i++)
    {
        app->recent_label_pointers.push_back(app->recent_labels[i].c_str());
        app->recent_rows.push_back(MobileGameRow{app->recent_names[i].c_str(), app->recent_cores[i].c_str()});
    }
    build_menus(app);
}

void remember_recent(App* app, const char* core_path, const char* content_path)
{
    recent_games_add(&app->recent, core_path, content_path);
    // recent.cfg unreadable at start: the list is empty, saving it would replace the user's.
    if (!app->recent_path.empty() && !app->recent_unreadable)
    {
        recent_games_save(&app->recent, app->recent_path.c_str());
    }
    refresh_recent_labels(app);
}

// --- What a pick does -------------------------------------------------------

void handle_menu_action(App* app, MenuAction action)
{
    switch (action)
    {
        case MenuAction::none:
            return;
        case MenuAction::load_game:
        {
            core_catalog_scan(&app->catalog, game_running(app) ? app->session.host : nullptr);
            std::vector<std::string> game_folders;
            piccolo_settings_game_folders(&app->settings, &game_folders);
            file_picker_open(&app->picker, &app->catalog, app->last_folder, app->last_core, &game_folders);
            break;
        }
        case MenuAction::pause:
            // PAUSE: the player's pause, back to the (paused) game; RESUME
            // lifts it. Either way everything closes and any input grab is
            // cancelled: the way out when something holds the input.
            toggle_user_pause(app);
            resume_game(app);
            break;
        case MenuAction::close_game:
            close_game(app);
            break;
        case MenuAction::reset:
            if (soft_reset(app))
            {
                app_leave_main_menu(app);
            }
            break;
        case MenuAction::hard_reset:
            hard_reset(app);   // the game starts again straight into play
            break;
        case MenuAction::quick_save:
            if (quick_save(app))
            {
                app_leave_main_menu(app);
            }
            break;
        case MenuAction::quick_load:
            if (quick_load(app))
            {
                app_leave_main_menu(app);
            }
            break;
        case MenuAction::game_dvr:
            open_game_dvr(app);
            break;
        case MenuAction::clear_recents:
            app->recent.games.clear();
            if (!app->recent_path.empty() && !app->recent_unreadable)   // see remember_recent
            {
                recent_games_save(&app->recent, app->recent_path.c_str());
            }
            refresh_recent_labels(app);
            break;
        case MenuAction::about:
            about_window_open(&app->about_window);
            break;
        case MenuAction::save_state:
            save_state_from(app, false);
            break;
        case MenuAction::screenshot:
            if (game_running(app))
            {
                app_leave_main_menu(app);   // back to the game: the shutter plays on it
            }
            take_screenshot(app);
            break;
        case MenuAction::load_state:
            load_state_from(app, false);
            break;
        case MenuAction::undo_save_state:
            undo_save_from(app, false);
            break;
        case MenuAction::undo_load_state:
            undo_load_from(app, false);
            break;
        case MenuAction::quit:
            app->running = false;
            break;
        case MenuAction::recent_game:
            break;   // handled in app_apply_ui (it needs the index)
        case MenuAction::general:
            settings_window_open(&app->settings_window, SettingsTab::general);
            break;
        case MenuAction::input:
            input_window_open(&app->input_window);
            break;
        case MenuAction::interface:
            settings_window_open(&app->settings_window, SettingsTab::interface);
            break;
        case MenuAction::video:
            settings_window_open(&app->settings_window, SettingsTab::video);
            break;
        case MenuAction::sound:
            settings_window_open(&app->settings_window, SettingsTab::sound);
            break;
        case MenuAction::options_manager:
            core_options_manager_open(&app->core_options_manager, &app->core_options_ui);
            break;
        case MenuAction::core_config:
            app_open_core_config(app, UINT32_MAX, 0);
            break;
        case MenuAction::cores:
            open_cores_window(app);
            break;
        case MenuAction::share:
            open_share_window(app);
            break;
        case MenuAction::pair:
            pair_window_open(&app->pair_window, nullptr);   // app_ui_update starts FILE TRANSFER's connection
            break;
        case MenuAction::guests:
            open_guest_window(app);
            break;
        case MenuAction::network:
            settings_window_open(&app->settings_window, SettingsTab::network);
            break;
        case MenuAction::debug_window:
            app->show_debug_window = !app->show_debug_window;
            break;
        default:
            SDL_Log("Menu: action %d isn't there yet", static_cast<int>(action));
            break;
    }
}

// A pick on the menu bar, or the phone's HOME / ☰ (the same ids): a recent
// game starts, any other item is its MenuAction. RETRO_UI_MENU_NONE: nothing.
void apply_menu_pick(App* app, int32_t picked)
{
    int32_t recent_picked = -1;
    MenuAction action = MenuAction::none;
    if (picked >= MENU_ID_RECENT)
    {
        action = MenuAction::recent_game;
        recent_picked = picked - MENU_ID_RECENT;
    }
    else if (picked >= 0)
    {
        action = static_cast<MenuAction>(picked);
    }
    if (action == MenuAction::recent_game && recent_picked >= 0)
    {
        // Started at the top of the next loop, like LOAD GAME (see pending_start).
        const RecentGame* game = &app->recent.games[app->recent_shown[static_cast<size_t>(recent_picked)]];
        app->pending_start = FilePickerChoice{};
        std::snprintf(app->pending_start.core_path, sizeof(app->pending_start.core_path), "%s",
                      game->core_path.c_str());
        std::snprintf(app->pending_start.content_path, sizeof(app->pending_start.content_path), "%s",
                      game->content_path.c_str());
        app->has_pending_start = true;
    }
    handle_menu_action(app, action);
}
