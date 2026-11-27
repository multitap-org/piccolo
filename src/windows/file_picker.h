#pragma once

// piccolo's "load game" window, in the retro style (retro_ui theme):
//
//   +- LOAD GAME ------------------------------------------+
//   | CORE   [ Snes9x 1.63                             v ] |
//   | FOLDER [ /home/user/roms                         v ] |
//   | FILTER [ mario                                     ] |
//   | +- files -----------------------+ +- folders -----+ |
//   | | Super Mario World (USA).sfc   | | ..            | |
//   | | ...                           | | Nintendo - .. | |
//   | +-------------------------------+ +---------------+ |
//   | /home/user/Downloads/...                             |
//   | [x] SHOW ALL FILES        [START WITHOUT GAME] [LOAD] |
//   +-------------------------------------------------------+
//
// The core is picked first; the file list shows only what that core says it
// can load (its own extension list, from the core catalog). FILTER narrows
// both lists to the names with its text in them. A phone held upright has
// the folders above the files (a fifth of the room) instead of beside them. Double-click a
// file or press LOAD to start it; click a folder to open it.
//
// Zip and 7z archives are always in the file list (never filtered: what's
// inside isn't known until one is opened). Selecting one peeks at its index
// and the line under the lists says what LOAD will do (content_archive.h):
//   - the core takes archives whole (arcade cores, block_extract): the archive;
//   - else, exactly one file inside the core can load: that file
//     ("a.7z#dir/Game.sfc", extracted by the core session);
//   - else: opens it as a folder, like BROWSE, which always does that.
// FOLDER jumps to one of the game folders (SETTINGS > GENERAL), HOME or (Android)
// an SD card / USB drive, and shows the one we're in. The picker isn't limited to them: they're only
// places to start (the network is limited to them, not the local user).
// Esc or the window's close button closes the picker.
//
// The same window chooses a folder (file_picker_open_folder: SETTINGS' game
// folders): CHOOSE FOLDER, no core, only folders (archives aren't folders
// here), and USE THIS FOLDER picks the folder shown.
//
// It also picks a shader preset (FilePickerMode::shader_preset, from
// SETTINGS > SHADER > CHOOSE): no core row, only presets (.slangp, or .glpreset on OpenGL ES), no archives
// (presets are always plain files), and NONE / USE instead of the game
// buttons. It can go to any folder (e.g. a full slang-shaders checkout).
//
// Split like every window: file_picker_update lists the folder when it
// changed and file_picker_state fills what the window shows (no GUI),
// file_picker_draw draws it and records what was clicked, and
// file_picker_apply acts on it (no GUI): goes to a folder, selects and peeks
// at a file, picks the core, makes the choice. The listing, the selection and
// SHOW ALL FILES stay the picker's own state.

#include <cstdint>
#include <string>
#include <vector>

#include "archive.h"
#include "core_catalog.h"
#include "localization.h"

#include "retro_ui_font.h"

constexpr size_t  FILE_PICKER_NAME_MAX    = 256;

enum class FilePickerMode
{
    game,            // LOAD GAME: a core and a file
    folder,          // CHOOSE FOLDER: a folder
    shader_preset,   // a preset for the SHADER setting (shader_extension)
};

struct FilePicker
{
    bool open = false;
    FilePickerMode mode = FilePickerMode::game;
    char directory[LIBRETRO_PATH_MAX] = {};

    int32_t core_index = -1;   // index into the catalog; -1 = none chosen yet
    std::vector<std::string> game_folders;   // FOLDER's choices (copied at open), then HOME
    std::vector<std::string> volumes;        // then SD cards / USB drives (Android), read when FOLDER opens
    bool    show_all   = false;
    char    shader_extension[16] = ".slangp";   // shader_preset mode: the files listed

    // Current folder's contents, sorted (case-insensitive). Re-read when the
    // folder, the core or "show all" changes.
    // Any number of them (a full No-Intro set is thousands); only the rows
    // in view are drawn (retro_ui_begin_clipped).
    std::vector<std::string> folders;
    std::vector<std::string> files;
    int32_t selected_file = -1;
    // FILTER: only the names with this text in them (any case) are listed;
    // cleared when the folder changes. Typed in the window (the drawing edits
    // it); file_picker_update lists the matches again when it changes.
    char filter[FILE_PICKER_NAME_MAX] = {};
    char filter_listed[FILE_PICKER_NAME_MAX] = {};    // what shown_* were made for
    char filter_folder[LIBRETRO_PATH_MAX] = {};       // the folder it was typed in
    std::vector<uint32_t> shown_folders;              // indices into folders / files
    std::vector<uint32_t> shown_files;
    bool    listing_stale = true;

    // An archive's listing: the one browsed (`directory` is "a.7z#" or
    // "a.7z#dir") or peeked at, read once rather than on every change.
    std::string               archive_path;
    std::vector<ArchiveEntry> archive_entries;
    bool                      archive_readable = false;

    // The archive selected in the file list, from its listing.
    int32_t     peek_loadable = 0;   // files inside the chosen core can load
    std::string peek_single;         // the one file, when peek_loadable == 1
};

// What the user chose. CHOOSE FOLDER: the folder in content_path, no core.
// Shader preset: the preset in content_path, or empty for NONE; no core.
struct FilePickerChoice
{
    char core_path[LIBRETRO_PATH_MAX]    = {};
    char content_path[LIBRETRO_PATH_MAX] = {};   // empty = start without content
};

// Opens the window in `directory` with `core_path` preselected (either may be
// "" or not exist: then the first game folder that exists, else the home
// folder / the first usable core).
void file_picker_open(FilePicker* picker, const CoreCatalog* catalog, const char* directory, const char* core_path,
                      const std::vector<std::string>* game_folders);

// Opens CHOOSE FOLDER in `directory` (if it's a folder that exists; else
// the first game folder that does, else the home folder).
void file_picker_open_folder(FilePicker* picker, const char* directory, const std::vector<std::string>* game_folders);

// Opens the window to pick a shader preset, in `directory` (if it doesn't
// exist: the home folder).
// `extension`: the presets this GPU path takes (".slangp" on Vulkan, ".glpreset" on OpenGL ES).
void file_picker_open_shader(FilePicker* picker, const char* directory, const char* extension);

// Every frame before file_picker_state: the folder listed again when the
// folder, the core or SHOW ALL FILES changed.
void file_picker_update(FilePicker* picker, const CoreCatalog* catalog);

// What the window shows from the rest of the program (and its own lines,
// made here so the drawing only draws them).
struct FilePickerState
{
    const Localization* strings = nullptr;
    const CoreCatalog*  catalog = nullptr;   // CORE's choices (only read)
    char        core_preview[LIBRETRO_INFO_TEXT_MAX * 2 + 8] = {};   // the chosen core, its version
    const char* folder_preview = "";         // FOLDER's text (a game folder, HOME or where we are)
    bool        no_game = false;             // the core starts without a game: START WITHOUT GAME shows
    bool        selected_archive = false;    // the selected file is an archive (BROWSE)
    char        where[LIBRETRO_PATH_MAX + 300] = {};   // the line under the lists
};

enum class FilePickerPress
{
    none,
    core,          // a core from CORE (`index` into the catalog)
    game_folder,   // a game folder from FOLDER (`index`)
    home,          // HOME from FOLDER
    volume,        // an SD card / USB drive / drive from FOLDER (`index`)
    parent,        // ".."
    open_folder,   // a folder in the list (`index`)
    show_all,      // SHOW ALL FILES changed
    load,          // LOAD, or a double click on a file
    browse,        // BROWSE: the selected archive as a folder
    start_alone,   // START WITHOUT GAME
    use_folder,    // USE THIS FOLDER (CHOOSE FOLDER)
    shader_none,   // NONE (shader preset)
    shader_use,    // USE (shader preset)
};

struct FilePickerAction
{
    FilePickerPress press = FilePickerPress::none;
    int32_t index = -1;            // see FilePickerPress
    int32_t select_file = -1;      // a file clicked that wasn't selected
    bool    volumes_opened = false;   // FOLDER's list opened: the volumes read again
    bool    closed = false;        // Esc or the window's X
};

void file_picker_state(FilePickerState* state, const FilePicker* picker, const CoreCatalog* catalog,
                       const Localization* strings);

// Draws the window if open.
void file_picker_draw(FilePicker* picker, const FilePickerState* state, RetroUiFont* pixel_font, int32_t scale,
                      FilePickerAction* action);

// Returns true on the frame the user picked something; the picker closes
// itself then.
bool file_picker_apply(FilePicker* picker, const FilePickerAction* action, const CoreCatalog* catalog,
                       FilePickerChoice* choice);
