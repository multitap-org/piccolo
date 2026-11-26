#include "file_picker.h"

#include <algorithm>   // std::sort (the names)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

#include "content_archive.h"
#include "host_fs.h"

#include "app_paths.h"

#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"


// ---------------------------------------------------------------------------
// State (no GUI)
// ---------------------------------------------------------------------------

// Paths.

// archive_split_path (the text: ".zip#" / ".7z#") and the archive part is a
// real file: a folder that happens to be named "pack.zip#old" is a folder.
static bool split_archive_path(const char* path, std::string* archive, std::string* folder)
{
    if (!archive_split_path(path, archive, folder))
    {
        return false;
    }
    SDL_PathInfo info = {};
    return SDL_GetPathInfo(archive->c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

// "a.7z#" (an archive's top folder) takes names right after the '#'.
static bool join_path(const char* directory, const char* name, char* out, size_t out_size)
{
    size_t length = std::strlen(directory);
    std::string archive;
    std::string folder;
    bool archive_top = split_archive_path(directory, &archive, &folder) && folder.empty();
    bool has_separator = archive_top || (length > 0 && (directory[length - 1] == '/' || directory[length - 1] == '\\'));
    int written = std::snprintf(out, out_size, "%s%s%s", directory, has_separator ? "" : "/", name);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

static bool is_separator(char c)
{
    return c == '/' || c == '\\';
}

// "/a/b/c" -> "/a/b", "/a" -> "/". Stays "/" at the root.
// Windows drives: "C:/a" -> "C:/" (not "C:", which means the drive's current
// folder), and it stays at "C:/".
// Archives: "/r/a.7z#d/e" -> "/r/a.7z#d" -> "/r/a.7z#" -> "/r".
static void go_to_parent(char* directory)
{
    std::string archive;
    std::string folder;
    if (split_archive_path(directory, &archive, &folder))
    {
        // Either way the result is shorter than `directory`: copied in place.
        if (!folder.empty())
        {
            size_t slash = folder.rfind('/');
            std::string parent = archive + "#" + (slash == std::string::npos ? std::string() : folder.substr(0, slash));
            std::memcpy(directory, parent.c_str(), parent.size() + 1);
            return;
        }
        std::memcpy(directory, archive.c_str(), archive.size() + 1);   // then its folder's parent, below
    }
    size_t length = std::strlen(directory);
    bool drive = length >= 2 && directory[1] == ':' &&
                 ((directory[0] >= 'A' && directory[0] <= 'Z') || (directory[0] >= 'a' && directory[0] <= 'z'));
    if (drive)
    {
        size_t last_separator = 0;
        for (size_t i = 2; i + 1 < length; i++)   // a trailing separator doesn't count
        {
            if (is_separator(directory[i]))
            {
                last_separator = i;
            }
        }
        if (last_separator <= 2)
        {
            directory[2] = '/';   // the drive's root
            directory[3] = '\0';
            return;
        }
    }
    // Ignore trailing separators ("/a/b/" is "/a/b").
    while (length > 1 && is_separator(directory[length - 1]))
    {
        length--;
    }
    // Find the separator before the last component.
    size_t last = length;
    while (last > 0 && !is_separator(directory[last - 1]))
    {
        last--;
    }
    if (last <= 1)
    {
        std::snprintf(directory, 2, "/");   // the parent is the root
        return;
    }
    directory[last - 1] = '\0';
}

// Listing.

// Names in order, case-insensitive (std::sort's comparison).
static bool name_before(const std::string& a, const std::string& b)
{
    return SDL_strcasecmp(a.c_str(), b.c_str()) < 0;
}

static void sort_names(std::vector<std::string>* names)
{
    std::sort(names->begin(), names->end(), name_before);
}

static const LibretroCoreInfo* selected_core(const FilePicker* picker, const CoreCatalog* catalog)
{
    if (picker->core_index < 0 || picker->core_index >= catalog->count)
    {
        return nullptr;
    }
    return &catalog->entries[picker->core_index].info;
}

static void add_folder(FilePicker* picker, const char* name)
{
    if (std::strlen(name) < FILE_PICKER_NAME_MAX)
    {
        picker->folders.push_back(name);
    }
}

static void add_file(FilePicker* picker, const char* name)
{
    if (std::strlen(name) < FILE_PICKER_NAME_MAX)
    {
        picker->files.push_back(name);
    }
}

// Archives list folders once per file inside them ("d/a", "d/b"): added once.
static void add_folder_once(FilePicker* picker, const char* name)
{
    for (size_t i = 0; i < picker->folders.size(); i++)
    {
        if (picker->folders[i] == name)
        {
            return;
        }
    }
    add_folder(picker, name);
}


// The archive's listing, unless it's the one already read.
static void read_archive(FilePicker* picker, const std::string* archive)
{
    if (picker->archive_path != *archive)
    {
        picker->archive_readable = archive_list(archive->c_str(), &picker->archive_entries);   // failure: logged
        picker->archive_path = *archive;
    }
}


// The folder `folder` ("" = the top) inside the archive: its subfolders and
// the files the core can load.
static void list_archive(FilePicker* picker, const LibretroCoreInfo* core, const std::string* archive,
                         const std::string* folder)
{
    read_archive(picker, archive);
    std::string prefix = folder->empty() ? std::string() : *folder + "/";
    for (size_t i = 0; i < picker->archive_entries.size(); i++)
    {
        const ArchiveEntry* entry = &picker->archive_entries[i];
        if (entry->name.size() <= prefix.size() || entry->name.compare(0, prefix.size(), prefix) != 0)
        {
            continue;   // not inside this folder (or the folder itself)
        }
        std::string rest = entry->name.substr(prefix.size());
        size_t slash = rest.find('/');
        std::string first = slash == std::string::npos ? rest : rest.substr(0, slash);
        if (first.empty())
        {
            continue;   // a name starting with '/': no folder to list it in
        }
        if (content_archive_hidden_name(first.c_str()))
        {
            continue;
        }
        if (slash != std::string::npos || entry->is_directory)
        {
            add_folder_once(picker, first.c_str());
        }
        else if (picker->show_all || core == nullptr || libretro_core_info_accepts(core, rest.c_str()))
        {
            add_file(picker, rest.c_str());
        }
    }
}

// A shader preset: "*<extension>" (".slangp" / ".glpreset"), any case.
static bool is_shader_preset(const char* name, const char* extension)
{
    size_t length = std::strlen(name);
    size_t extension_length = std::strlen(extension);
    return length > extension_length && SDL_strcasecmp(name + length - extension_length, extension) == 0;
}

// Whether a file goes in the list.
static bool listed_file(const FilePicker* picker, const LibretroCoreInfo* core, const char* name)
{
    if (picker->mode == FilePickerMode::folder)
    {
        return false;   // folders only
    }
    if (picker->mode == FilePickerMode::shader_preset)
    {
        return is_shader_preset(name, picker->shader_extension);
    }
    // Only what the chosen core says it loads (its own extension list), and
    // every archive: what's inside is only known later.
    return picker->show_all || core == nullptr || libretro_core_info_accepts(core, name) ||
           archive_format(name) != ArchiveFormat::none;
}

// FILTER: `name` has `filter` in it, any case (ASCII letters; other
// characters as they are). An empty filter matches everything.
static bool name_matches(const char* name, const char* filter)
{
    if (filter[0] == '\0')
    {
        return true;
    }
    for (size_t start = 0; name[start] != '\0'; start++)
    {
        size_t i = 0;
        while (filter[i] != '\0' && name[start + i] != '\0' &&
               SDL_tolower(static_cast<unsigned char>(name[start + i])) == SDL_tolower(static_cast<unsigned char>(filter[i])))
        {
            i++;
        }
        if (filter[i] == '\0')
        {
            return true;
        }
    }
    return false;
}

// The rows listed: the names that match FILTER. A selected file it hides
// is let go (LOAD mustn't start what isn't shown).
static void apply_filter(FilePicker* picker)
{
    std::snprintf(picker->filter_listed, sizeof(picker->filter_listed), "%s", picker->filter);
    picker->shown_folders.clear();
    picker->shown_files.clear();
    for (size_t i = 0; i < picker->folders.size(); i++)
    {
        if (name_matches(picker->folders[i].c_str(), picker->filter))
        {
            picker->shown_folders.push_back(static_cast<uint32_t>(i));
        }
    }
    bool selected_shown = false;
    for (size_t i = 0; i < picker->files.size(); i++)
    {
        if (name_matches(picker->files[i].c_str(), picker->filter))
        {
            picker->shown_files.push_back(static_cast<uint32_t>(i));
            selected_shown = selected_shown || static_cast<int32_t>(i) == picker->selected_file;
        }
    }
    if (!selected_shown)
    {
        picker->selected_file = -1;
    }
}

static void list_folder(FilePicker* picker, const CoreCatalog* catalog)
{
    // Another folder: FILTER starts empty there.
    if (std::strcmp(picker->filter_folder, picker->directory) != 0)
    {
        picker->filter[0] = '\0';
        std::snprintf(picker->filter_folder, sizeof(picker->filter_folder), "%s", picker->directory);
    }
    picker->folders.clear();
    picker->files.clear();
    picker->selected_file = -1;
    picker->listing_stale = false;

    const LibretroCoreInfo* core = selected_core(picker, catalog);
    std::string archive;
    std::string folder;
    if (split_archive_path(picker->directory, &archive, &folder))
    {
        list_archive(picker, core, &archive, &folder);
        sort_names(&picker->folders);
        sort_names(&picker->files);
        return;
    }

    // One listing with the types in it (host_fs_list_entries): no question
    // per file, which on a network share (a NAS) is a round trip each.
    std::vector<HostFsEntry> entries;
    host_fs_list_entries(picker->directory, &entries);   // (a failure is logged: an empty list)
    for (size_t i = 0; i < entries.size(); i++)
    {
        const char* name = entries[i].name.c_str();
        if (name[0] == '.' || entries[i].name.size() >= FILE_PICKER_NAME_MAX)
        {
            continue;   // hidden files and folders, and names we can't hold
        }
        if (entries[i].is_directory)
        {
            add_folder(picker, name);
        }
        else if (listed_file(picker, core, name))
        {
            add_file(picker, name);
        }
    }
    sort_names(&picker->folders);
    sort_names(&picker->files);
}

// Is `directory` `folder` or inside it ("/roms" holds "/roms/snes" and
// "/roms/a.7z#x", not "/roms2")? Forward slashes on both.
static bool folder_holds(const std::string* folder, const char* directory)
{
    size_t length = folder->size();
    while (length > 1 && (*folder)[length - 1] == '/')
    {
        length--;
    }
    if (std::strncmp(directory, folder->c_str(), length) != 0)
    {
        return false;
    }
    char next = directory[length];
    return next == '\0' || next == '/' || (length > 0 && (*folder)[length - 1] == '/');
}

// FOLDER's text: the game folder we're in (the longest, if they nest),
// else HOME when we're right there, else where we are.
static const char* folder_preview(const FilePicker* picker, const Localization* strings)
{
    int32_t best = -1;
    for (size_t i = 0; i < picker->game_folders.size(); i++)
    {
        if (folder_holds(&picker->game_folders[i], picker->directory) &&
            (best < 0 || picker->game_folders[i].size() > picker->game_folders[static_cast<size_t>(best)].size()))
        {
            best = static_cast<int32_t>(i);
        }
    }
    if (best >= 0)
    {
        return picker->game_folders[static_cast<size_t>(best)].c_str();
    }
    const char* home = app_paths_home_folder();   // /sdcard on Android
    if (home != nullptr)
    {
        char home_path[LIBRETRO_PATH_MAX] = {};
        std::snprintf(home_path, sizeof(home_path), "%s", home);
        host_fs_forward_slashes(home_path);
        std::string home_folder = home_path;
        // Both without trailing separators: SDL_GetUserFolder ends the home
        // with one, so a bare length check took a one-character subfolder for it.
        size_t home_length = home_folder.size();
        while (home_length > 1 && is_separator(home_folder[home_length - 1]))
        {
            home_length--;
        }
        size_t directory_length = std::strlen(picker->directory);
        while (directory_length > 1 && is_separator(picker->directory[directory_length - 1]))
        {
            directory_length--;
        }
        if (folder_holds(&home_folder, picker->directory) && directory_length == home_length)
        {
            return piccolo_text(strings, Str::picker_home);   // the home folder itself (with or without its trailing '/')
        }
    }
    return picker->directory;
}

// The selected file, if it's an archive in a real folder (archives inside
// archives are plain files: they aren't browsed). Only in LOAD GAME.
static bool selected_archive(const FilePicker* picker)
{
    std::string archive;
    std::string folder;
    return picker->mode == FilePickerMode::game && picker->selected_file >= 0 &&
           archive_format(picker->files[static_cast<size_t>(picker->selected_file)].c_str()) != ArchiveFormat::none &&
           !split_archive_path(picker->directory, &archive, &folder);
}

// Every frame before the state: the folder's contents again when the
// folder, the core or "show all" changed.
void file_picker_update(FilePicker* picker, const CoreCatalog* catalog)
{
    if (picker->open && picker->listing_stale)
    {
        list_folder(picker, catalog);
        apply_filter(picker);
    }
    else if (picker->open && std::strcmp(picker->filter, picker->filter_listed) != 0)
    {
        apply_filter(picker);
    }
}

// The line under the lists: where we are, or what LOAD does with the
// selected archive.
static void where_text(const FilePicker* picker, const LibretroCoreInfo* core, const Localization* strings, char* where,
                       size_t where_size)
{
    if (selected_archive(picker))
    {
        const char* name = picker->files[static_cast<size_t>(picker->selected_file)].c_str();
        if (content_archive_core_takes_whole(core, name))
        {
            std::snprintf(where, where_size, "%s: %s", name, piccolo_text(strings, Str::picker_peek_whole));
        }
        else if (!picker->archive_readable)
        {
            std::snprintf(where, where_size, "%s: %s", name, piccolo_text(strings, Str::picker_peek_unreadable));
        }
        else if (picker->peek_loadable == 1)
        {
            std::snprintf(where, where_size, "%s: %s %s", name, piccolo_text(strings, Str::picker_peek_loads),
                          picker->peek_single.c_str());
        }
        else if (picker->peek_loadable == 0)
        {
            std::snprintf(where, where_size, "%s: %s", name, piccolo_text(strings, Str::picker_peek_none));
        }
        else
        {
            std::snprintf(where, where_size, "%s: %s %d", name, piccolo_text(strings, Str::picker_peek_folder),
                          static_cast<int>(picker->peek_loadable));
        }
    }
    else
    {
        std::snprintf(where, where_size, "%s", picker->directory);
    }
}

void file_picker_state(FilePickerState* state, const FilePicker* picker, const CoreCatalog* catalog,
                       const Localization* strings)
{
    state->strings = strings;
    state->catalog = catalog;
    if (!picker->open)
    {
        return;   // nothing draws a closed picker: no work (and no stat) per frame
    }
    const LibretroCoreInfo* core = selected_core(picker, catalog);
    state->no_game = core != nullptr && core->supports_no_game;
    std::snprintf(state->core_preview, sizeof(state->core_preview), "%s %s",
                  core != nullptr ? core->name : piccolo_text(strings, Str::picker_no_cores), core != nullptr ? core->version : "");
    state->folder_preview = folder_preview(picker, strings);
    state->selected_archive = selected_archive(picker);
    where_text(picker, core, strings, state->where, sizeof(state->where));
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// CHOOSE FOLDER's bottom row: USE THIS FOLDER (the folder shown) at the right.
static void draw_folder_buttons(const Localization* strings, FilePickerAction* action)
{
    RetroUiEndButton use = {};
    use.id = "use_folder";
    use.label = piccolo_text(strings, Str::picker_use_folder);
    if (retro_ui_end_buttons(&use, 1, false) == 0)
    {
        action->press = FilePickerPress::use_folder;
    }
}

// The shader picker's bottom row: NONE and USE at the right.
static void draw_shader_buttons(const FilePicker* picker, const Localization* strings, FilePickerAction* action)
{
    RetroUiEndButton buttons[2] = {};
    buttons[0].id = "none";
    buttons[0].label = piccolo_text(strings, Str::shader_preset_none);
    buttons[1].id = "use";
    buttons[1].label = piccolo_text(strings, Str::shader_picker_use);
    buttons[1].enabled = picker->selected_file >= 0;
    int32_t pressed = retro_ui_end_buttons(buttons, 2, false);
    if (pressed == 0)
    {
        action->press = FilePickerPress::shader_none;
    }
    else if (pressed == 1)
    {
        action->press = FilePickerPress::shader_use;
    }
}

// LOAD GAME's bottom row: SHOW ALL FILES, then START WITHOUT GAME, BROWSE and
// LOAD at the right.
static void draw_game_buttons(FilePicker* picker, const FilePickerState* state, FilePickerAction* action)
{
    const Localization* strings = state->strings;
    // (SHOW ALL FILES is the picker's own switch: apply lists the folder again.)
    if (retro_ui_check("show_all", piccolo_text(strings, Str::picker_show_all), &picker->show_all))
    {
        action->press = FilePickerPress::show_all;
    }
    RetroUiEndButton buttons[3] = {};
    buttons[0].id = "alone";
    buttons[0].label = piccolo_text(strings, Str::picker_start_without_game);
    buttons[0].shown = state->no_game;
    // BROWSE: the selected archive as a folder, whatever the core.
    buttons[1].id = "browse";
    buttons[1].label = piccolo_text(strings, Str::picker_browse);
    buttons[1].enabled = state->selected_archive;
    buttons[2].id = "load";
    buttons[2].label = piccolo_text(strings, Str::picker_load);
    buttons[2].enabled = picker->selected_file >= 0;
    int32_t pressed = retro_ui_end_buttons(buttons, 3, true);
    if (pressed == 0)
    {
        action->press = FilePickerPress::start_alone;
    }
    else if (pressed == 1)
    {
        action->press = FilePickerPress::browse;
    }
    else if (pressed == 2)
    {
        action->press = FilePickerPress::load;
    }
}

// The files that match FILTER (`width` 0: the whole width). Only the rows in
// view are drawn (thousands of files stay smooth; keyboard / gamepad
// navigation still reaches them all).
static void draw_files(const FilePicker* picker, const Localization* strings, float width, float height,
                       FilePickerAction* action)
{
    retro_ui_begin_list("files", width, height);
    uint32_t first = 0;
    uint32_t end = 0;
    retro_ui_begin_clipped(static_cast<uint32_t>(picker->shown_files.size()));
    while (retro_ui_clipped_step(&first, &end))
    {
        for (uint32_t row = first; row < end; row++)
        {
            uint32_t i = picker->shown_files[row];
            int32_t index = static_cast<int32_t>(i);
            RetroUiClick click = retro_ui_list_item_click(i, picker->files[i].c_str(), index == picker->selected_file);
            if (click != RetroUiClick::none && picker->selected_file != index)
            {
                action->select_file = index;
            }
            if (click == RetroUiClick::twice)
            {
                action->press = FilePickerPress::load;
            }
        }
    }
    if (picker->shown_files.empty())
    {
        retro_ui_dim_text(piccolo_text(strings, Str::picker_nothing_here), nullptr);
    }
    retro_ui_end_list();
}

// ".." and the folders that match FILTER.
static void draw_folders(const FilePicker* picker, float width, float height, FilePickerAction* action)
{
    retro_ui_begin_list("folders", width, height);
    retro_ui_push_id("parent");
    if (retro_ui_list_item(0, "..", false, nullptr))
    {
        action->press = FilePickerPress::parent;
    }
    retro_ui_pop_id();
    uint32_t first = 0;
    uint32_t end = 0;
    retro_ui_begin_clipped(static_cast<uint32_t>(picker->shown_folders.size()));
    while (retro_ui_clipped_step(&first, &end))
    {
        for (uint32_t row = first; row < end; row++)
        {
            uint32_t i = picker->shown_folders[row];
            if (retro_ui_list_item(i, picker->folders[i].c_str(), false, nullptr))
            {
                action->press = FilePickerPress::open_folder;
                action->index = static_cast<int32_t>(i);
            }
        }
    }
    retro_ui_end_list();
}

void file_picker_draw(FilePicker* picker, const FilePickerState* state, RetroUiFont* pixel_font, int32_t scale,
                      FilePickerAction* action)
{
    if (!picker->open)
    {
        return;
    }
    const Localization* strings = state->strings;
    const CoreCatalog* catalog = state->catalog;

    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);

    retro_ui_push_theme(pixel_font, scale);
    // 400 x 250 retro pixels, but never bigger than the window.
    float width = 400.0f * block;
    float height = 250.0f * block;
    RetroUiWindow window = {};
    window.width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    window.height = height < screen_height - 24.0f * block ? height : screen_height - 24.0f * block;
    window.centered_on_open = true;

    bool choosing_folder = picker->mode == FilePickerMode::folder;
    bool shader = picker->mode == FilePickerMode::shader_preset;
    if (choosing_folder)
    {
        window.id = "choose_folder";
        window.title = piccolo_text(strings, Str::picker_folder_title);
    }
    else if (shader)
    {
        window.id = "shader_preset";
        window.title = piccolo_text(strings, Str::shader_picker_title);
    }
    else
    {
        window.id = "load_game";
        window.title = piccolo_text(strings, Str::picker_title);
    }
    bool closed = false;
    if (retro_ui_window_begin(&window, scale, &closed))
    {
        // Core first: it decides which files are shown. Neither the core nor
        // the game folders matter when picking a shader preset.
        bool core_row = !choosing_folder && !shader;
        const char* core_label = piccolo_text(strings, Str::picker_core);
        const char* folder_label = piccolo_text(strings, Str::picker_folder);
        // One label column for CORE and FOLDER, so the choices line up.
        float label_width = core_row ? retro_ui_text_width(core_label) : 0.0f;
        float folder_label_width = retro_ui_text_width(folder_label);
        label_width = (label_width > folder_label_width ? label_width : folder_label_width) + retro_ui_item_spacing();
        if (core_row)
        {
            RetroUiRow row = {};
            row.id = "core";
            row.marker_column = false;
            row.label = core_label;
            row.label_width = label_width;
            retro_ui_row_begin(&row);
            if (retro_ui_begin_choice("choice", state->core_preview))
            {
                for (int32_t i = 0; i < catalog->count; i++)
                {
                    const CoreCatalogEntry* entry = &catalog->entries[i];
                    if (!entry->info.usable)
                    {
                        continue;
                    }
                    char label[LIBRETRO_INFO_TEXT_MAX * 2 + 8] = {};
                    std::snprintf(label, sizeof(label), "%s %s", entry->info.name, entry->info.version);
                    if (retro_ui_choice_item_repick(static_cast<uint32_t>(i), label, i == picker->core_index))
                    {
                        action->press = FilePickerPress::core;
                        action->index = i;
                    }
                }
                retro_ui_end_choice();
            }
            retro_ui_row_end(&row);
        }

        // Where to start: a game folder or HOME.
        RetroUiRow folder_row = {};
        folder_row.id = "folder";
        folder_row.marker_column = false;
        folder_row.label = folder_label;
        folder_row.label_width = label_width;
        retro_ui_row_begin(&folder_row);
        if (retro_ui_begin_choice("choice", state->folder_preview))
        {
            if (retro_ui_choice_opened())
            {
                action->volumes_opened = true;   // read again every time the list opens
            }
            // Items: the game folders, HOME, then the volumes (none shows as
            // selected: picking the folder shown reads it again).
            uint32_t item = 0;
            for (size_t i = 0; i < picker->game_folders.size(); i++)
            {
                if (retro_ui_choice_item(item++, picker->game_folders[i].c_str(), false))
                {
                    action->press = FilePickerPress::game_folder;
                    action->index = static_cast<int32_t>(i);
                }
            }
            if (retro_ui_choice_item(item++, piccolo_text(strings, Str::picker_home), false))
            {
                action->press = FilePickerPress::home;
            }
            // SD cards and USB drives (Android), the drives (Windows), as they are while the list is open.
            for (size_t i = 0; i < picker->volumes.size(); i++)
            {
                if (retro_ui_choice_item(item++, picker->volumes[i].c_str(), false))
                {
                    action->press = FilePickerPress::volume;
                    action->index = static_cast<int32_t>(i);
                }
            }
            retro_ui_end_choice();
        }
        retro_ui_row_end(&folder_row);

        // FILTER: both lists narrowed to the names with this text in them.
        RetroUiRow filter_row = {};
        filter_row.id = "filter";
        filter_row.marker_column = false;
        filter_row.label = piccolo_text(strings, Str::picker_filter);
        filter_row.label_width = label_width;
        retro_ui_row_begin(&filter_row);
        RetroUiEditField filter_field = {};
        filter_field.hint = piccolo_text(strings, Str::picker_filter_hint);
        retro_ui_edit_field("field", picker->filter, sizeof(picker->filter), &filter_field);
        retro_ui_row_end(&filter_row);

        // Files (left) and folders (right), leaving room for two rows below.
        // A phone held upright: the folders above the files, each the whole
        // width, the folders a fifth of the room (Andrés, 2026-11-25).
        float row = retro_ui_line_step();
        float list_height = retro_ui_height_left() - row * 2.0f;
        if (choosing_folder)   // folders only: the whole room
        {
            draw_folders(picker, 0.0f, list_height, action);
        }
        else if (retro_ui_page_upright())
        {
            // (A row's height goes to the gap between them.)
            float folders_height = std::floor((list_height - row) * 0.2f);
            draw_folders(picker, 0.0f, folders_height, action);
            draw_files(picker, strings, 0.0f, list_height - row - folders_height, action);
        }
        else
        {
            draw_files(picker, strings, retro_ui_width_left() * 0.62f, list_height, action);
            retro_ui_same_line();
            draw_folders(picker, 0.0f, list_height, action);
        }

        // Where we are, or what LOAD does with the selected archive.
        retro_ui_dim_text(state->where, nullptr);

        // Bottom row. (A double click above may have picked already.)
        if (choosing_folder)
        {
            draw_folder_buttons(strings, action);
        }
        else if (shader)
        {
            draw_shader_buttons(picker, strings, action);
        }
        else
        {
            draw_game_buttons(picker, state, action);
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();

    action->closed = closed;
}

// ---------------------------------------------------------------------------
// Apply (no GUI)
// ---------------------------------------------------------------------------

static bool is_directory(const char* path)
{
    SDL_PathInfo info = {};
    return path[0] != '\0' && SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

static void go_home(FilePicker* picker)
{
    const char* home = app_paths_home_folder();   // /sdcard on Android
    std::snprintf(picker->directory, sizeof(picker->directory), "%s", home != nullptr ? home : "/");
    host_fs_forward_slashes(picker->directory);
}

// The first game folder that exists, else the home folder.
static void go_to_first_game_folder(FilePicker* picker)
{
    go_home(picker);
    for (size_t i = 0; i < picker->game_folders.size(); i++)
    {
        if (is_directory(picker->game_folders[i].c_str()))
        {
            std::snprintf(picker->directory, sizeof(picker->directory), "%s", picker->game_folders[i].c_str());
            return;
        }
    }
}

void file_picker_open_folder(FilePicker* picker, const char* directory, const std::vector<std::string>* game_folders)
{
    picker->open = true;
    picker->mode = FilePickerMode::folder;
    picker->game_folders = *game_folders;
    picker->listing_stale = true;
    picker->selected_file = -1;
    picker->archive_path.clear();
    picker->archive_entries.clear();
    if (directory != nullptr && is_directory(directory))
    {
        std::snprintf(picker->directory, sizeof(picker->directory), "%s", directory);
        host_fs_forward_slashes(picker->directory);
    }
    else
    {
        go_to_first_game_folder(picker);
    }
}

void file_picker_open_shader(FilePicker* picker, const char* directory, const char* extension)
{
    std::snprintf(picker->shader_extension, sizeof(picker->shader_extension), "%s", extension);
    picker->open = true;
    picker->mode = FilePickerMode::shader_preset;
    picker->core_index = -1;
    picker->show_all = false;
    picker->game_folders.clear();
    picker->listing_stale = true;
    picker->selected_file = -1;
    picker->archive_path.clear();
    picker->archive_entries.clear();
    if (directory != nullptr && is_directory(directory))
    {
        std::snprintf(picker->directory, sizeof(picker->directory), "%s", directory);
        host_fs_forward_slashes(picker->directory);   // so the presets picked from it don't mix separators
    }
    else
    {
        go_home(picker);
    }
}

void file_picker_open(FilePicker* picker, const CoreCatalog* catalog, const char* directory, const char* core_path,
                      const std::vector<std::string>* game_folders)
{
    picker->open = true;
    picker->mode = FilePickerMode::game;
    picker->game_folders = *game_folders;
    picker->listing_stale = true;

    picker->archive_path.clear();   // the archive may have changed since: read it again
    picker->archive_entries.clear();

    // A folder, or a folder inside an archive that still exists.
    SDL_PathInfo info = {};
    std::string archive;
    std::string folder;
    bool in_archive = directory != nullptr && split_archive_path(directory, &archive, &folder);
    bool usable = in_archive ? true
                             : directory != nullptr && directory[0] != '\0' && SDL_GetPathInfo(directory, &info) &&
                                   info.type == SDL_PATHTYPE_DIRECTORY;
    if (usable)
    {
        std::snprintf(picker->directory, sizeof(picker->directory), "%s", directory);
        host_fs_forward_slashes(picker->directory);   // so the files picked from it don't mix separators
    }
    else
    {
        go_to_first_game_folder(picker);
    }

    // The given core if it's in the catalog, else the first usable one that
    // needs content (the catalog lists usable cores first).
    picker->core_index = -1;
    for (int32_t i = 0; i < catalog->count; i++)
    {
        char path[LIBRETRO_PATH_MAX] = {};
        if (core_path != nullptr && core_catalog_path(catalog, &catalog->entries[i], path, sizeof(path)) &&
            std::strcmp(path, core_path) == 0 && catalog->entries[i].info.usable)
        {
            picker->core_index = i;
            return;
        }
    }
    for (int32_t i = 0; i < catalog->count && picker->core_index < 0; i++)
    {
        if (catalog->entries[i].info.usable)
        {
            picker->core_index = i;
        }
    }
}

// What's in the selected archive for the chosen core (see file_picker.h).
static void peek_selected(FilePicker* picker, const CoreCatalog* catalog)
{
    picker->peek_loadable = 0;
    picker->peek_single.clear();
    if (!selected_archive(picker))
    {
        return;
    }
    char path[LIBRETRO_PATH_MAX] = {};
    if (!join_path(picker->directory, picker->files[static_cast<size_t>(picker->selected_file)].c_str(), path, sizeof(path)))
    {
        return;
    }
    std::string archive = path;
    read_archive(picker, &archive);
    picker->peek_loadable = content_archive_loadable(selected_core(picker, catalog), &picker->archive_entries,
                                                     &picker->peek_single);
}

// Opens the selected archive as a folder ("a.7z#").
static void browse_selected(FilePicker* picker)
{
    char next[LIBRETRO_PATH_MAX] = {};
    if (join_path(picker->directory, picker->files[static_cast<size_t>(picker->selected_file)].c_str(), next, sizeof(next)) &&
        std::strlen(next) + 1 < sizeof(next))
    {
        std::strcat(next, "#");
        std::snprintf(picker->directory, sizeof(picker->directory), "%s", next);
        picker->listing_stale = true;
    }
}

// Fills `choice` with the selected core and `file_name` (nullptr = no content).
static bool choose(FilePicker* picker, const CoreCatalog* catalog, const char* file_name, FilePickerChoice* choice)
{
    *choice = FilePickerChoice{};
    if (picker->core_index < 0 ||
        !core_catalog_path(catalog, &catalog->entries[picker->core_index], choice->core_path, sizeof(choice->core_path)))
    {
        return false;
    }
    if (file_name != nullptr && !join_path(picker->directory, file_name, choice->content_path, sizeof(choice->content_path)))
    {
        return false;
    }
    picker->open = false;
    return true;
}

// A shader preset (`file_name`), or NONE (nullptr).
static bool choose_shader(FilePicker* picker, const char* file_name, FilePickerChoice* choice)
{
    *choice = FilePickerChoice{};
    if (file_name != nullptr && !join_path(picker->directory, file_name, choice->content_path, sizeof(choice->content_path)))
    {
        return false;
    }
    picker->open = false;
    return true;
}

// LOAD (or a double click) on the selected file: an archive follows the
// rules in file_picker.h. True if something was picked.
static bool load_selected(FilePicker* picker, const CoreCatalog* catalog, FilePickerChoice* choice)
{
    const char* name = picker->files[static_cast<size_t>(picker->selected_file)].c_str();
    if (picker->mode == FilePickerMode::shader_preset)
    {
        return choose_shader(picker, name, choice);
    }
    if (!selected_archive(picker) || content_archive_core_takes_whole(selected_core(picker, catalog), name))
    {
        return choose(picker, catalog, name, choice);
    }
    if (picker->peek_loadable == 1)
    {
        std::string inner = std::string(name) + "#" + picker->peek_single;
        return choose(picker, catalog, inner.c_str(), choice);
    }
    browse_selected(picker);
    return false;
}

// The folder picked (`directory`): listed next frame.
static void go_to(FilePicker* picker, const char* directory)
{
    std::snprintf(picker->directory, sizeof(picker->directory), "%s", directory);
    picker->listing_stale = true;
}

bool file_picker_apply(FilePicker* picker, const FilePickerAction* action, const CoreCatalog* catalog,
                       FilePickerChoice* choice)
{
    bool picked = false;
    // A file clicked: selected, and an archive peeked at (before a double
    // click's LOAD, which uses it).
    if (action->select_file >= 0 && static_cast<size_t>(action->select_file) < picker->files.size())
    {
        picker->selected_file = action->select_file;
        peek_selected(picker, catalog);
    }
    int32_t index = action->index;
    switch (action->press)
    {
    case FilePickerPress::none:
        break;
    case FilePickerPress::core:
        picker->core_index = index;
        picker->listing_stale = true;
        break;
    case FilePickerPress::game_folder:
        if (index >= 0 && static_cast<size_t>(index) < picker->game_folders.size())
        {
            go_to(picker, picker->game_folders[static_cast<size_t>(index)].c_str());
        }
        break;
    case FilePickerPress::home:
        go_home(picker);
        picker->listing_stale = true;
        break;
    case FilePickerPress::volume:
        if (index >= 0 && static_cast<size_t>(index) < picker->volumes.size())
        {
            go_to(picker, picker->volumes[static_cast<size_t>(index)].c_str());
        }
        break;
    case FilePickerPress::parent:
        go_to_parent(picker->directory);
        picker->listing_stale = true;
        break;
    case FilePickerPress::open_folder:
        if (index >= 0 && static_cast<size_t>(index) < picker->folders.size())
        {
            char next[LIBRETRO_PATH_MAX] = {};
            if (join_path(picker->directory, picker->folders[static_cast<size_t>(index)].c_str(), next, sizeof(next)))
            {
                go_to(picker, next);
            }
        }
        break;
    case FilePickerPress::show_all:
        picker->listing_stale = true;
        break;
    case FilePickerPress::load:
        if (picker->selected_file >= 0)
        {
            picked = load_selected(picker, catalog, choice);
        }
        break;
    case FilePickerPress::browse:
        if (selected_archive(picker))
        {
            browse_selected(picker);
        }
        break;
    case FilePickerPress::start_alone:
        picked = choose(picker, catalog, nullptr, choice);
        break;
    case FilePickerPress::use_folder:
        // CHOOSE FOLDER: the folder shown.
        *choice = FilePickerChoice{};
        std::snprintf(choice->content_path, sizeof(choice->content_path), "%s", picker->directory);
        picker->open = false;
        picked = true;
        break;
    case FilePickerPress::shader_none:
        picked = choose_shader(picker, nullptr, choice);
        break;
    case FilePickerPress::shader_use:
        if (picker->selected_file >= 0)
        {
            picked = choose_shader(picker, picker->files[static_cast<size_t>(picker->selected_file)].c_str(), choice);
        }
        break;
    }
    if (action->volumes_opened)
    {
        app_paths_storage_volumes(&picker->volumes);   // SD cards / USB drives, as they are now
    }
    if (action->closed)
    {
        picker->open = false;
    }
    return picked;
}
