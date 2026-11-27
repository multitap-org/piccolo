#include "settings_window.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include <SDL3/SDL.h>

#include "game_mode.h"
#include "libretro_host.h"   // LIBRETRO_PATH_MAX

#include "piccolo_settings.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "settings_ui.h"
#include "settings_view.h"
#include "ui_toolkit.h"

// ---------------------------------------------------------------------------
// State

void settings_window_update(SettingsWindow* window, const Settings* settings, const char* core_name,
                            const char* game_name)
{
    if (!window->open && !window->image_open)
    {
        return;   // IMAGE SETTINGS is its own window and uses the scope too
    }
    bool has_core = core_name[0] != '\0';
    bool has_game = has_core && game_name[0] != '\0';
    settings_view_scope(settings, has_core, has_game, &window->scope, &window->choose_scope);
}

// A boolean setting as it resolves at `scope` ("true").
static bool true_at_scope(const Settings* settings, SettingLayer scope, const char* key)
{
    const char* value = settings_get_at(settings, scope, key, nullptr);
    return value != nullptr && std::strcmp(value, "true") == 0;
}

void settings_window_state(SettingsWindowState* state, const SettingsWindow* window, const Settings* settings,
                           const Localization* strings, const AppPaths* paths, const char* core_name,
                           const char* game_name, const SettingsWindowNotes* notes,
                           const std::vector<PiccoloThemeFile>* themes, const ImageWindowSource* image,
                           const NetWindowState* network)
{
    SettingLayer scope = window->scope;
    state->settings = settings;
    state->strings = strings;
    state->core_name = core_name;
    state->game_name = game_name;
    state->notes = *notes;
    state->folder_hints[0] = paths->cores.c_str();
    state->folder_hints[1] = paths->system.c_str();
    state->folder_hints[2] = paths->saves.c_str();
    state->folder_hints[3] = paths->states.c_str();
    state->folder_hints[4] = paths->screenshots.c_str();
    state->game_folder_count = 0;
    state->game_folder_free = PICCOLO_GAME_FOLDER_MAX;
    for (uint32_t slot = 0; slot < PICCOLO_GAME_FOLDER_MAX; slot++)
    {
        const char* folder = settings_get_text(settings, PICCOLO_PATH_GAMES[slot]);
        if (folder[0] == '\0')
        {
            if (state->game_folder_free == PICCOLO_GAME_FOLDER_MAX)
            {
                state->game_folder_free = slot;
            }
            continue;   // a gap: skipped, as piccolo_settings_game_folders does
        }
        state->game_folders[state->game_folder_count] = folder;
        state->game_folder_slots[state->game_folder_count] = slot;
        state->game_folder_count++;
    }
    state->game_mode_supported = game_mode_supported();
    state->window_fixed = settings_get_enum(settings, PICCOLO_WINDOW_MODE) == PICCOLO_WINDOW_FIXED;
    state->latency_preset = piccolo_settings_latency_preset(settings, scope);
    state->frame_delay_usable = true_at_scope(settings, scope, PICCOLO_LATENCY_WAIT_FOR_PRESENT);
    state->frame_delay_auto = true_at_scope(settings, scope, PICCOLO_LATENCY_FRAME_DELAY_AUTO);
    state->rate_control = true_at_scope(settings, scope, PICCOLO_AUDIO_RATE_CONTROL);
    state->themes = themes;
    image_window_state(&state->image, settings, strings, scope, image);
    state->network = network;
}

// ---------------------------------------------------------------------------
// Drawing

// GLOBAL / THIS CORE / THIS GAME (settings_ui_scope).
static void draw_scope(SettingsWindow* window, const SettingsWindowState* state)
{
    settings_ui_scope(state->settings, state->strings, state->core_name, state->game_name, &window->scope,
                      &window->choose_scope);
}

// GAME FOLDERS: one row per folder, then "+". Clicking a row's field or
// "+" asks piccolo for CHOOSE FOLDER (action->choose_folder); "-" removes the
// row (settings_window_apply). Global only, like the other folders (greyed out in other scopes).
// FOLDERS and GAME FOLDERS: one open at a time (opening one closes the
// other; Andrés: their rows needn't line up then). True if `section` is open.
static bool folder_section(SettingsWindow* window, FolderSection section, const char* label, const char* id)
{
    bool open = retro_ui_section_header_open(id, label, window->folder_section == section);
    if (open)
    {
        window->folder_section = section;
    }
    else if (window->folder_section == section)
    {
        window->folder_section = FolderSection::none;   // closed by its header
    }
    return open;
}

static void draw_game_folders(SettingsWindow* window, const SettingsUiContext* ui, const SettingsWindowState* state,
                              SettingsWindowAction* action)
{
    if (!folder_section(window, FolderSection::game_folders, piccolo_text(ui->strings, Str::section_game_folders),
                        "game_folders"))
    {
        return;
    }
    bool global = ui->scope == SettingLayer::global;
    retro_ui_begin_disabled(!global);
    uint32_t set = state->game_folder_count;
    // The field up to "-" at the right edge, lined up with the setting rows.
    const char* label = piccolo_text(ui->strings, Str::setting_path_games);
    int32_t remove = -1;
    for (uint32_t i = 0; i < set; i++)
    {
        RetroUiRow row = {};
        row.id = PICCOLO_PATH_GAMES[state->game_folder_slots[i]];
        row.label = label;
        row.help = piccolo_text(ui->strings, Str::setting_path_games_help);
        row.label_width = ui->label_width;
        row.end_button = "-";
        row.end_button_help = piccolo_text(ui->strings, Str::game_folder_remove_help);
        retro_ui_row_begin(&row);
        if (retro_ui_picker_field("value", nullptr, state->game_folders[i],
                                  piccolo_text(ui->strings, Str::game_folder_choose_help)))
        {
            action->choose_folder = PICCOLO_PATH_GAMES[state->game_folder_slots[i]];
        }
        if (retro_ui_row_end(&row))
        {
            remove = static_cast<int32_t>(state->game_folder_slots[i]);
        }
    }
    if (remove >= 0)
    {
        action->remove_game_folder = true;
        action->game_folder = static_cast<uint32_t>(remove);
    }
    // "+" in the label column, after the marker's room.
    RetroUiRow add = {};
    add.id = "add_game_folder";
    retro_ui_row_begin(&add);
    retro_ui_begin_disabled(set >= PICCOLO_GAME_FOLDER_MAX);
    if (retro_ui_button("+"))
    {
        action->choose_folder = PICCOLO_PATH_GAMES[state->game_folder_free];
    }
    retro_ui_end_disabled();
    retro_ui_item_tooltip(piccolo_text(ui->strings, Str::game_folder_add_help));
    retro_ui_row_end(&add);
    retro_ui_end_disabled();
    if (!global)
    {
        retro_ui_disabled_item_tooltip(piccolo_text(ui->strings, Str::global_only_help));
    }
}

// REWIND, folded at first. Its header is just REWIND (testing round 3); what
// runs now is the note under the rows.
static void draw_rewind(const SettingsUiContext* ui, const SettingsWindowState* state, SettingsWindowAction* action)
{
    if (!settings_ui_section("rewind", piccolo_text(ui->strings, Str::section_rewind), nullptr, false))
    {
        return;
    }
    settings_ui_row(ui, PICCOLO_REWIND, nullptr);
    settings_ui_row(ui, PICCOLO_REWIND_LENGTH, nullptr);
    settings_ui_row(ui, PICCOLO_REWIND_SNAPSHOT_FREQUENCY, nullptr);
    settings_ui_row(ui, PICCOLO_REWIND_MEMORY, nullptr);
    if (state->notes.rewind_note != nullptr && state->notes.rewind_note[0] != '\0')
    {
        retro_ui_dim_text(state->notes.rewind_note, nullptr);
    }
    if (state->notes.rewind_known)
    {
        if (retro_ui_button(piccolo_text(ui->strings, Str::rewind_measure_again)))
        {
            action->rewind_measure_again = true;
        }
        retro_ui_item_tooltip(piccolo_text(ui->strings, Str::rewind_measure_again_help));
    }
}

static void draw_general(SettingsWindow* window, const SettingsUiContext* ui, const SettingsWindowState* state,
                         SettingsWindowAction* action)
{
    settings_ui_row(ui, PICCOLO_LANGUAGE, nullptr);
    settings_ui_row(ui, PICCOLO_PAUSE_IN_MENU, nullptr);
    settings_ui_row(ui, PICCOLO_PAUSE_ON_FOCUS_LOSS, nullptr);
    settings_ui_row(ui, PICCOLO_PAUSE_ON_DISCONNECT, nullptr);
    settings_ui_row(ui, PICCOLO_SCREENSAVER_WHEN_PAUSED, nullptr);
    settings_ui_row(ui, PICCOLO_SAVESTATE_ON_EXIT, nullptr);
    if (state->game_mode_supported)   // Linux only: nothing to turn on elsewhere
    {
        settings_ui_row(ui, PICCOLO_GAME_MODE, nullptr);
        retro_ui_dim_text(state->notes.game_mode, nullptr);
    }
    settings_ui_row(ui, PICCOLO_SHOW_FPS, nullptr);
    settings_ui_row(ui, PICCOLO_SCREENSHOT_MODE, nullptr);
    draw_rewind(ui, state, action);
    // FOLDERS: rarely changed, closed until opened. A click on a field
    // opens CHOOSE FOLDER (piccolo); RESET goes back to the default shown.
    if (folder_section(window, FolderSection::folders, piccolo_text(ui->strings, Str::section_folders), "folders"))
    {
        const char* keys[SETTINGS_FOLDER_COUNT] = {PICCOLO_PATH_CORES, PICCOLO_PATH_SYSTEM, PICCOLO_PATH_SAVES,
                                                   PICCOLO_PATH_STATES, PICCOLO_PATH_SCREENSHOTS};
        // Android: no CORES row. Cores load only from the app's private
        // storage (shared storage is noexec); main.cpp ignores the setting.
        // The browser: no CORES or SYSTEM rows either (the core is linked in).
#if defined(SDL_PLATFORM_ANDROID)
        uint32_t first = 1;
#else
        uint32_t first = PICCOLO_BROWSER ? 2 : 0;
#endif
        for (uint32_t i = first; i < SETTINGS_FOLDER_COUNT; i++)
        {
            if (settings_ui_folder_row(ui, keys[i], state->folder_hints[i]))
            {
                action->choose_folder = keys[i];
            }
        }
        retro_ui_dim_text(piccolo_text(ui->strings, Str::folders_next_game), nullptr);
    }
    draw_game_folders(window, ui, state, action);
}

static void draw_picture(SettingsWindow* window, const SettingsUiContext* ui, const SettingsWindowState* state)
{
    settings_ui_row(ui, PICCOLO_VIDEO_ASPECT, nullptr);
    settings_ui_row(ui, PICCOLO_VIDEO_SCALING, nullptr);
    settings_ui_row(ui, PICCOLO_VIDEO_MAX_OFFSCREEN, nullptr);
    settings_ui_row(ui, PICCOLO_VIDEO_FILTER, nullptr);
    // The shader preset is in CORE CONFIGURATION (SHADER), next to the game.
    settings_ui_row(ui, PICCOLO_VIDEO_POSITION, nullptr);

    // The crop (and more image settings later) live in their own window.
    char crop[200] = {};
    // The crop at the scope being edited, as IMAGE SETTINGS reads it: top, bottom, left, right.
    const float* at_scope = state->image.crop;
    std::snprintf(crop, sizeof(crop), "%s: %d, %d, %d, %d", piccolo_text(ui->strings, Str::crop_summary),
                  static_cast<int32_t>(at_scope[0]), static_cast<int32_t>(at_scope[1]),
                  static_cast<int32_t>(at_scope[2]), static_cast<int32_t>(at_scope[3]));
    retro_ui_spacing();
    if (retro_ui_id_button("image", piccolo_text(ui->strings, Str::image_settings_button)))
    {
        window->image_open = true;
    }
    if (state->image.source.texture_id == 0)
    {
        retro_ui_item_tooltip(piccolo_text(ui->strings, Str::image_settings_button_help));
    }
    retro_ui_dim_text(crop, nullptr);
}

static void draw_display(const SettingsUiContext* ui, const SettingsWindowState* state)
{
    settings_ui_row(ui, PICCOLO_DISPLAY_FULLSCREEN, nullptr);
    settings_ui_row(ui, PICCOLO_DISPLAY_FULLSCREEN_TYPE, nullptr);
    // WINDOW: closed at first, like most sections (Andrés, testing round 2).
    if (!settings_ui_section("window", piccolo_text(ui->strings, Str::section_window), nullptr, false))
    {
        return;
    }
    settings_ui_row(ui, PICCOLO_WINDOW_MODE, nullptr);
    if (state->window_fixed)
    {
        settings_ui_row(ui, PICCOLO_WINDOW_ASPECT, nullptr);
        settings_ui_row(ui, PICCOLO_WINDOW_HEIGHT, nullptr);
    }
    else
    {
        settings_ui_row(ui, PICCOLO_WINDOW_SCALE, nullptr);
    }
    retro_ui_dim_text(state->notes.window_size, nullptr);
    settings_ui_row(ui, PICCOLO_WINDOW_REMEMBER, nullptr);
}

// PRESET: sets the three latency settings below at the edited scope. Not a
// setting itself: it shows the preset the values match, or CUSTOM.
static void draw_latency_preset(const SettingsUiContext* ui, const SettingsWindowState* state,
                                SettingsWindowAction* action)
{
    uint32_t current = state->latency_preset;
    RetroUiRow row = {};
    row.id = "latency_preset";
    row.label = piccolo_text(ui->strings, Str::latency_preset);
    row.help = piccolo_text(ui->strings, Str::latency_preset_help);
    row.label_width = ui->label_width;
    retro_ui_row_begin(&row);
    if (retro_ui_begin_choice("preset", piccolo_text(ui->strings, piccolo_settings_latency_preset_name(current))))
    {
        for (uint32_t i = 0; i < PICCOLO_LATENCY_PRESET_COUNT; i++)
        {
            // The preset shown, picked again, sets its values again.
            if (retro_ui_choice_item_repick(i, piccolo_text(ui->strings, piccolo_settings_latency_preset_name(i)),
                                            i == current))
            {
                action->set_latency_preset = true;
                action->latency_preset = i;
                action->latency_scope = ui->scope;
            }
        }
        retro_ui_end_choice();
    }
    retro_ui_row_end(&row);
}

static void draw_sync(const SettingsUiContext* ui, const SettingsWindowState* state, SettingsWindowAction* action)
{
    settings_ui_row(ui, PICCOLO_VIDEO_TIMING, nullptr);
    settings_ui_row(ui, PICCOLO_VIDEO_CATCH_UP, nullptr);
    // LATENCY: folded at first (the presets are for those who look for them).
    if (!settings_ui_section("latency", piccolo_text(ui->strings, Str::section_latency), nullptr, false))
    {
        return;
    }
    if (PICCOLO_BROWSER)
    {
        // The browser paces presents itself and never waits: frames in
        // flight, waiting for present and frame delay mean nothing there.
        settings_ui_row(ui, PICCOLO_LATENCY_RUN_AHEAD, nullptr);
        retro_ui_dim_text(state->notes.run_ahead, nullptr);
        return;
    }
    draw_latency_preset(ui, state, action);
    settings_ui_row(ui, PICCOLO_LATENCY_FRAMES_IN_FLIGHT, nullptr);
    settings_ui_row(ui, PICCOLO_LATENCY_WAIT_FOR_PRESENT, nullptr);

    // Frame delay only helps after waiting for present (see frame_delay() in
    // main.cpp): greyed out without it, the stored value kept.
    bool usable = state->frame_delay_usable;
    // AUTO ignores the manual value: greyed out then (kept for turning AUTO off).
    bool is_automatic = state->frame_delay_auto;
    retro_ui_begin_disabled(!usable);
    retro_ui_begin_disabled(is_automatic);
    settings_ui_row(ui, PICCOLO_LATENCY_FRAME_DELAY, nullptr);
    retro_ui_end_disabled();
    if (!usable)
    {
        retro_ui_disabled_item_tooltip(piccolo_text(ui->strings, Str::frame_delay_needs_wait));
    }
    settings_ui_row(ui, PICCOLO_LATENCY_FRAME_DELAY_AUTO, nullptr);
    retro_ui_end_disabled();
    if (usable && is_automatic)
    {
        retro_ui_dim_text(state->notes.frame_delay, nullptr);
        if (state->notes.frame_delay_found)
        {
            if (retro_ui_id_button("search_delay", piccolo_text(ui->strings, Str::frame_delay_search_again)))
            {
                action->search_frame_delay = true;
            }
            retro_ui_item_tooltip(piccolo_text(ui->strings, Str::frame_delay_search_again_help));
        }
    }
    retro_ui_dim_text(state->notes.present_wait, nullptr);
    settings_ui_row(ui, PICCOLO_LATENCY_RUN_AHEAD, nullptr);
    retro_ui_dim_text(state->notes.run_ahead, nullptr);
}

static void draw_sound(const SettingsUiContext* ui, const SettingsWindowState* state)
{
    settings_ui_text_choice_row(ui, PICCOLO_AUDIO_DEVICE, state->notes.audio_devices, state->notes.audio_device_count,
                                piccolo_text(ui->strings, Str::audio_device_default),
                                piccolo_text(ui->strings, Str::audio_device_missing));
    settings_ui_row(ui, PICCOLO_AUDIO_VOLUME, nullptr);
    settings_ui_row(ui, PICCOLO_AUDIO_MUTE, nullptr);
    settings_ui_row(ui, PICCOLO_AUDIO_LATENCY, nullptr);
    retro_ui_dim_text(state->notes.audio_latency, nullptr);

    // ADVANCED: folded at first.
    if (!settings_ui_section("advanced", piccolo_text(ui->strings, Str::section_advanced), nullptr, false))
    {
        return;
    }
    settings_ui_row(ui, PICCOLO_AUDIO_RATE_CONTROL, nullptr);
    // The limit only matters with rate control on: greyed out without it, the stored value kept.
    retro_ui_begin_disabled(!state->rate_control);
    settings_ui_row(ui, PICCOLO_AUDIO_MAX_DEVIATION, nullptr);
    retro_ui_end_disabled();
    settings_ui_row(ui, PICCOLO_AUDIO_QUALITY, nullptr);
}

// One entry of THEME's or CUSTOM THEME's list.
struct ThemeChoice
{
    std::string           label;
    std::string           id;        // ui_theme's value
    const RetroUiPalette* palette = nullptr;
};

static bool theme_choice_before(const ThemeChoice& a, const ThemeChoice& b)
{
    return a.label < b.label;
}

// The theme in `choices` the colors match: `preferred` (the one last picked
// or saved) when it does, since two themes may have the same colors, else
// the first. -1 if none.
static int32_t matching_theme(const Settings* settings, const std::vector<ThemeChoice>& choices, const std::string& preferred)
{
    int32_t found = -1;
    for (size_t i = 0; i < choices.size(); i++)
    {
        if (!piccolo_settings_ui_palette_is(settings, choices[i].palette))
        {
            continue;
        }
        if (choices[i].label == preferred)
        {
            return static_cast<int32_t>(i);
        }
        if (found < 0)
        {
            found = static_cast<int32_t>(i);
        }
    }
    return found;
}

// A theme row: its label lined up with the setting rows, its list after it.
static RetroUiRow theme_row(const SettingsUiContext* ui, const char* id, Str label, Str help)
{
    RetroUiRow row = {};
    row.id = id;
    row.label = piccolo_text(ui->strings, label);
    row.help = piccolo_text(ui->strings, help);
    row.label_width = ui->label_width;
    return row;
}

// THEME and CUSTOM THEME: they set every interface color. Not settings
// themselves: they show the theme the colors match.
//   THEME          the built-in themes and the ones that ship in themes/,
//                  sorted by name, then CUSTOM: CUSTOM when picked, or when
//                  the colors match none of them.
//   CUSTOM THEME   the user's own (themes/*-custom.cfg, SAVE THEME), only
//                  while THEME is CUSTOM: the one the colors match, or
//                  UNSAVED.
// The interface is global only.
static void draw_theme_preset(SettingsWindow* window, const SettingsUiContext* ui,
                              const std::vector<PiccoloThemeFile>* files, SettingsWindowAction* action)
{
    const Localization* strings = ui->strings;
    std::vector<ThemeChoice> shipped;
    std::vector<ThemeChoice> custom;
    for (uint32_t i = 0; i < static_cast<uint32_t>(RetroUiTheme::count); i++)
    {
        ThemeChoice choice = {};
        choice.label = piccolo_text(strings, piccolo_settings_ui_theme_name(static_cast<RetroUiTheme>(i)));
        choice.id = piccolo_ui_theme_id(static_cast<RetroUiTheme>(i));
        choice.palette = retro_ui_theme_palette(static_cast<RetroUiTheme>(i));
        shipped.push_back(choice);
    }
    for (size_t i = 0; files != nullptr && i < files->size(); i++)
    {
        ThemeChoice choice = {};
        choice.label = (*files)[i].name;
        choice.id = piccolo_ui_theme_file_id(&(*files)[i]);
        choice.palette = &(*files)[i].palette;
        if ((*files)[i].custom)
        {
            custom.push_back(choice);
        }
        else
        {
            shipped.push_back(choice);
        }
    }
    std::stable_sort(shipped.begin(), shipped.end(), theme_choice_before);
    std::stable_sort(custom.begin(), custom.end(), theme_choice_before);

    int32_t shipped_current = matching_theme(ui->settings, shipped, window->theme_choice);
    int32_t custom_current = matching_theme(ui->settings, custom, window->theme_choice);
    bool on_custom = window->theme_custom || shipped_current < 0;
    const char* custom_label = piccolo_text(strings, Str::preset_custom);

    bool global = ui->scope == SettingLayer::global;
    retro_ui_push_id("ui_theme");
    retro_ui_begin_disabled(!global);

    // A theme picked again applies its colors again (they may have been changed since).
    RetroUiRow row = theme_row(ui, "theme", Str::ui_theme, Str::ui_theme_help);
    retro_ui_row_begin(&row);
    const char* theme_label = on_custom ? custom_label : shipped[static_cast<size_t>(shipped_current)].label.c_str();
    if (retro_ui_begin_choice("theme", theme_label))
    {
        for (size_t i = 0; i < shipped.size(); i++)
        {
            if (retro_ui_choice_item_repick(static_cast<uint32_t>(i), shipped[i].label.c_str(),
                                            !on_custom && static_cast<int32_t>(i) == shipped_current))
            {
                action->set_palette = true;
                action->palette = *shipped[i].palette;
                action->theme_id = shipped[i].id;
                window->theme_choice = shipped[i].label;
                window->theme_custom = false;
            }
        }
        if (retro_ui_choice_item_repick(static_cast<uint32_t>(shipped.size()), custom_label, on_custom))
        {
            window->theme_custom = true;   // the colors stay; CUSTOM THEME picks one
        }
        retro_ui_end_choice();
    }
    retro_ui_row_end(&row);

    row = theme_row(ui, "custom_theme", Str::ui_custom_theme, Str::ui_custom_theme_help);
    retro_ui_row_begin(&row);
    const char* custom_theme_label = "-";
    if (on_custom)
    {
        custom_theme_label = custom_current >= 0 ? custom[static_cast<size_t>(custom_current)].label.c_str()
                             : custom.empty()    ? piccolo_text(strings, Str::ui_custom_theme_none)
                                                 : piccolo_text(strings, Str::ui_custom_theme_unsaved);
    }
    retro_ui_begin_disabled(!on_custom || custom.empty());
    if (retro_ui_begin_choice("custom_theme", custom_theme_label))
    {
        for (size_t i = 0; i < custom.size(); i++)
        {
            if (retro_ui_choice_item_repick(static_cast<uint32_t>(i), custom[i].label.c_str(),
                                            static_cast<int32_t>(i) == custom_current))
            {
                action->set_palette = true;
                action->palette = *custom[i].palette;
                action->theme_id = custom[i].id;
                window->theme_choice = custom[i].label;
                window->theme_custom = true;
            }
        }
        retro_ui_end_choice();
    }
    retro_ui_end_disabled();
    retro_ui_row_end(&row);

    retro_ui_end_disabled();
    if (!global)
    {
        retro_ui_disabled_item_tooltip(piccolo_text(strings, Str::global_only_help));
    }
    retro_ui_pop_id();
}

// SAVE THEME...: a button under CUSTOM THEME, then a popup: THEME picks
// which of the user's own themes to replace, or NEW, which asks for a NAME.
// SAVE leaves the name in action->save_theme_name for piccolo to write the
// file (a custom theme: it never replaces a shipped one).
static void draw_save_theme(SettingsWindow* window, const SettingsUiContext* ui, const std::vector<PiccoloThemeFile>* files,
                            SettingsWindowAction* action)
{
    const Localization* strings = ui->strings;
    bool global = ui->scope == SettingLayer::global;

    // The user's own themes, sorted by name.
    std::vector<std::string> custom;
    for (size_t i = 0; files != nullptr && i < files->size(); i++)
    {
        if ((*files)[i].custom)
        {
            custom.push_back((*files)[i].name);
        }
    }
    std::sort(custom.begin(), custom.end());

    retro_ui_push_id("ui_theme_save");
    retro_ui_space(retro_ui_text_width("C"), retro_ui_font_size());   // the marker column
    retro_ui_same_line();
    retro_ui_same_line_at(retro_ui_cursor_x() + ui->label_width);   // in the value column
    retro_ui_begin_disabled(!global);
    if (retro_ui_button(piccolo_text(strings, Str::ui_theme_save)))
    {
        window->save_theme_open = true;
        window->save_theme_name[0] = '\0';
        // Start on the custom theme in use, else NEW.
        window->save_theme_target.clear();
        for (size_t i = 0; window->theme_custom && i < custom.size(); i++)
        {
            if (custom[i] == window->theme_choice)
            {
                window->save_theme_target = custom[i];
            }
        }
    }
    retro_ui_end_disabled();
    retro_ui_item_tooltip(piccolo_text(strings, Str::ui_theme_save_help));

    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    // One size, tall enough for everything it may show: the title, THEME,
    // NAME, the REPLACES line and the buttons.
    float chrome_width = 0.0f;
    float chrome_height = 0.0f;
    retro_ui_window_chrome(&chrome_width, &chrome_height);
    float height = chrome_height + retro_ui_line_step() * 4.5f;   // half a row to spare for the edges
    RetroUiModal modal = {};
    modal.id = "save_theme";
    modal.title = piccolo_text(strings, Str::ui_theme_save_title);
    modal.width = static_cast<float>(static_cast<int>(screen_width * 0.4f));
    modal.height = static_cast<float>(static_cast<int>(height));
    if (retro_ui_begin_modal(&modal, window->save_theme_open))
    {
        if (retro_ui_key_pressed(RetroUiKey::escape, true))
        {
            window->save_theme_open = false;   // (B on a gamepad sends Escape while a popup is up)
        }
        // A target that is gone (deleted from the folder) is NEW again.
        bool target_found = window->save_theme_target.empty();
        for (size_t i = 0; i < custom.size(); i++)
        {
            target_found = target_found || custom[i] == window->save_theme_target;
        }
        if (!target_found)
        {
            window->save_theme_target.clear();
        }
        bool is_new = window->save_theme_target.empty();
        const char* new_label = piccolo_text(strings, Str::ui_theme_save_new);
        const char* theme_label = piccolo_text(strings, Str::ui_theme);
        const char* name_label = piccolo_text(strings, Str::ui_theme_save_name);
        float label_width =
            retro_ui_label_column_width(std::max(retro_ui_text_width(theme_label), retro_ui_text_width(name_label)));

        retro_ui_label(theme_label);
        retro_ui_same_line_at(label_width);
        if (retro_ui_begin_choice("target", is_new ? new_label : window->save_theme_target.c_str()))
        {
            // NEW first (index 0), then the user's themes.
            if (retro_ui_choice_item(0, new_label, is_new))
            {
                window->save_theme_target.clear();
            }
            for (size_t i = 0; i < custom.size(); i++)
            {
                if (retro_ui_choice_item(static_cast<uint32_t>(i + 1), custom[i].c_str(),
                                         custom[i] == window->save_theme_target))
                {
                    window->save_theme_target = custom[i];
                }
            }
            retro_ui_end_choice();
        }
        is_new = window->save_theme_target.empty();

        std::string name = window->save_theme_target;
        bool enter = false;
        if (is_new)
        {
            retro_ui_label(name_label);
            retro_ui_same_line_at(label_width);
            RetroUiEditField field = {};
            field.uppercase = true;
            field.focus = retro_ui_window_appearing();   // typing starts at once
            enter = retro_ui_edit_field("name", window->save_theme_name, sizeof(window->save_theme_name), &field);
            // The name without spaces around it.
            name = window->save_theme_name;
            size_t first = name.find_first_not_of(' ');
            size_t last = name.find_last_not_of(' ');
            name = first != std::string::npos ? name.substr(first, last - first + 1) : std::string();
        }
        bool replaces = !is_new;
        for (size_t i = 0; i < custom.size(); i++)
        {
            replaces = replaces || name == custom[i];
        }
        if (replaces)
        {
            retro_ui_dim_text(piccolo_text(strings, Str::ui_theme_save_replaces), nullptr);
        }

        bool can_save = !name.empty();
        retro_ui_begin_disabled(!can_save);
        if ((retro_ui_button(piccolo_text(strings, Str::ui_theme_save_button)) || enter) && can_save)
        {
            action->save_theme = true;
            action->save_theme_name = name;
            window->save_theme_open = false;
        }
        retro_ui_end_disabled();
        retro_ui_same_line();
        if (retro_ui_button(piccolo_text(strings, Str::map_cancel)))
        {
            window->save_theme_open = false;
        }
        if (!window->save_theme_open)
        {
            retro_ui_close_modal();
        }
        retro_ui_end_modal();
    }
    retro_ui_pop_id();
}

// The interface settings are global only, so this tab edits the global
// layer whatever the scope (on a core or game scope everything here would
// be disabled).
static void draw_interface(SettingsWindow* window, const SettingsUiContext* context, const SettingsWindowState* state,
                           SettingsWindowAction* action)
{
    SettingsUiContext global = *context;
    global.scope = SettingLayer::global;
    const SettingsUiContext* ui = &global;
    draw_theme_preset(window, ui, state->themes, action);
    draw_save_theme(window, ui, state->themes, action);
    settings_ui_row(ui, PICCOLO_UI_MENU_BOXES, nullptr);
    settings_ui_row(ui, PICCOLO_UI_SHADOWS, nullptr);
    settings_ui_row(ui, PICCOLO_UI_THEME_CURSOR, nullptr);
    settings_ui_row(ui, PICCOLO_UI_BACKGROUND_TRANSPARENCY, nullptr);
    // COLORS, then EFFECTS (Andrés, testing round 1). COLORS is a long list
    // THEME already sets: folded at first. EFFECTS is one row: open.
    if (settings_ui_section("colors", piccolo_text(ui->strings, Str::section_colors), nullptr, false))
    {
        for (uint32_t i = 0; i < piccolo_settings_ui_color_count(); i++)
        {
            settings_ui_row(ui, piccolo_settings_ui_color_key(i), nullptr);
        }
        // The touch gamepad's starting opacities (part of the theme).
        settings_ui_row(ui, PICCOLO_UI_PAD_FILL, nullptr);
        settings_ui_row(ui, PICCOLO_UI_PAD_BORDER, nullptr);
        settings_ui_row(ui, PICCOLO_UI_PAD_LABELS, nullptr);
        settings_ui_row(ui, PICCOLO_UI_PAD_BACKGROUND, nullptr);
    }
    if (settings_ui_section("effects", piccolo_text(ui->strings, Str::section_effects), nullptr, false))
    {
        settings_ui_row(ui, PICCOLO_SCREEN_EFFECTS, nullptr);
        // Each effect, under the switch for all of them (greyed out while it's off).
        retro_ui_begin_disabled(!settings_get_bool(ui->settings, PICCOLO_SCREEN_EFFECTS));
        settings_ui_row(ui, PICCOLO_SCREEN_EFFECT_REWIND, nullptr);
        settings_ui_row(ui, PICCOLO_SCREEN_EFFECT_SCREENSHOT, nullptr);
        retro_ui_end_disabled();
    }
}

// A tab's contents scroll on their own, under the scope and tabs, which stay
// put (the window itself doesn't scroll: a scrolling window with this layout
// jumped back to the top, as the core options list in the quick menu did).
static void begin_tab_content()
{
    retro_ui_begin_scroll_area("content", 0.0f, 0.0f);
}

static void end_tab_content()
{
    retro_ui_end_scroll_area();
}

static void draw_tabs(SettingsWindow* window, NetWindow* network_window, const SettingsUiContext* ui,
                      const SettingsWindowState* state, SettingsWindowAction* action)
{
    const Localization* strings = state->strings;
    if (!retro_ui_begin_tabs("tabs"))
    {
        return;
    }
    int target = retro_ui_tab_switch(static_cast<int>(window->tab), 5);
    if (target >= 0)
    {
        window->tab = static_cast<SettingsTab>(target);
        window->select_tab = true;
    }
    // Which tab is chosen now, decided before any tab is drawn (a tab shown
    // becomes window->tab).
    bool select_general = window->select_tab && window->tab == SettingsTab::general;
    bool select_interface = window->select_tab && window->tab == SettingsTab::interface;
    bool select_video = window->select_tab && window->tab == SettingsTab::video;
    bool select_sound = window->select_tab && window->tab == SettingsTab::sound;
    bool select_network = window->select_tab && window->tab == SettingsTab::network;
    window->select_tab = false;
    if (retro_ui_begin_tab("general", piccolo_text(strings, Str::tab_general), select_general))
    {
        window->tab = SettingsTab::general;
        begin_tab_content();
        draw_general(window, ui, state, action);
        end_tab_content();
        retro_ui_end_tab();
    }
    if (retro_ui_begin_tab("interface", piccolo_text(strings, Str::tab_interface), select_interface))
    {
        window->tab = SettingsTab::interface;
        begin_tab_content();
        draw_interface(window, ui, state, action);
        end_tab_content();
        retro_ui_end_tab();
    }
    if (retro_ui_begin_tab("video", piccolo_text(strings, Str::tab_video), select_video))
    {
        window->tab = SettingsTab::video;
        if (retro_ui_begin_tabs("video_tabs"))
        {
            if (retro_ui_begin_tab("picture", piccolo_text(strings, Str::tab_picture), false))
            {
                begin_tab_content();
                draw_picture(window, ui, state);
                end_tab_content();
                retro_ui_end_tab();
            }
            // DISPLAY (full screen, the window): the browser's page owns them;
            // a phone is always full screen.
            if (!PICCOLO_BROWSER && !PICCOLO_MOBILE && retro_ui_begin_tab("display", piccolo_text(strings, Str::tab_display), false))
            {
                begin_tab_content();
                draw_display(ui, state);
                end_tab_content();
                retro_ui_end_tab();
            }
            if (retro_ui_begin_tab("sync", piccolo_text(strings, Str::tab_sync), false))
            {
                begin_tab_content();
                draw_sync(ui, state, action);
                end_tab_content();
                retro_ui_end_tab();
            }
            retro_ui_end_tabs();
        }
        retro_ui_end_tab();
    }
    if (retro_ui_begin_tab("sound", piccolo_text(strings, Str::tab_sound), select_sound))
    {
        window->tab = SettingsTab::sound;
        begin_tab_content();
        draw_sound(ui, state);
        end_tab_content();
        retro_ui_end_tab();
    }
    if (state->network != nullptr &&
        retro_ui_begin_tab("network", piccolo_text(strings, Str::net_title), select_network))
    {
        window->tab = SettingsTab::network;
        begin_tab_content();
        net_window_draw_tab(network_window, state->network, ui->label_width, &action->network);
        end_tab_content();
        retro_ui_end_tab();
    }
    retro_ui_end_tabs();
}

void settings_window_draw(SettingsWindow* window, NetWindow* network_window, const SettingsWindowState* state,
                          RetroUiFont* pixel_font, int32_t scale, SettingsWindowAction* action)
{
    if (!window->open && !window->image_open)
    {
        return;
    }

    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    if (window->open)
    {
        retro_ui_push_theme(pixel_font, scale);
        // 360 x 240 retro pixels, never bigger than the window.
        float width = 360.0f * block;
        float height = 240.0f * block;
        width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
        height = height < screen_height - 24.0f * block ? height : screen_height - 24.0f * block;
        // The window itself never scrolls: each tab's contents do.
        RetroUiWindow frame = {};
        frame.id = "settings";
        frame.title = piccolo_text(state->strings, Str::settings_title);
        frame.width = width;
        frame.height = height;
        frame.centered_on_open = true;
        frame.scrolls = false;
        bool closed = false;
        if (retro_ui_window_begin(&frame, scale, &closed))
        {
            if (closed)
            {
                window->open = false;
            }
            draw_scope(window, state);

            // The rows hand their edit out (settings_window_apply).
            SettingsUiContext ui = {};
            ui.settings = state->settings;
            ui.strings = state->strings;
            ui.scope = window->scope;
            ui.label_width = settings_ui_label_width(state->settings, state->strings);
            ui.edit = &action->setting;
            draw_tabs(window, network_window, &ui, state, action);
        }
        retro_ui_window_end();
        retro_ui_pop_theme();
    }

    if (window->image_open)
    {
        image_window_draw(&window->image_open, &state->image, pixel_font, scale, &action->image);
    }
}

// ---------------------------------------------------------------------------
// Apply

void settings_window_open(SettingsWindow* window, SettingsTab tab)
{
    if (!window->open)
    {
        window->choose_scope = true;   // needs the settings: done on the next update
    }
    window->open = true;
    window->tab = tab;
    window->select_tab = true;
}

bool settings_window_apply(const SettingsWindowAction* action, Settings* settings)
{
    SettingsUiEdit edit = action->setting;
    bool changed = settings_ui_apply_edit(settings, &edit);
    changed = image_window_apply(&action->image, settings) || changed;
    if (action->remove_game_folder)
    {
        changed = piccolo_settings_remove_game_folder(settings, action->game_folder) || changed;
    }
    // The preset shown, picked again, sets its values again.
    if (action->set_latency_preset)
    {
        changed = piccolo_settings_apply_latency_preset(settings, action->latency_scope, action->latency_preset) ||
                  changed;
    }
    // A theme picked again applies its colors again (they may have been changed since).
    if (action->set_palette)
    {
        changed = piccolo_settings_apply_ui_palette(settings, &action->palette) || changed;
        if (!action->theme_id.empty())
        {
            changed = settings_set(settings, SettingLayer::global, PICCOLO_UI_THEME, action->theme_id.c_str()) || changed;
        }
    }
    return changed;
}
