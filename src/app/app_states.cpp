// Savestates: the undo of a load and of a save, the AUTO state saved when
// a game closes, and the slots' actions.

#include "app_states.h"

#include "app_game.h"
#include "app_ui_modes.h"

// ---------------------------------------------------------------------------
// UNDO SAVE STATE / UNDO LOAD STATE (Andrés, 2026-11-26; design-notes.md).
// A load keeps the game as it was in memory, one level; a save moves the
// slot's old files aside on disk, one level per slot (state_undo.h).
// ---------------------------------------------------------------------------

// The game ends: its load undo goes (the buffers stay for the next game) and
// the slot saved last is forgotten. The slots' undo files stay on disk.
void drop_undos(App* app)
{
    app->undo_load_size = 0;
    app->saved_slot_known = false;
    app->undo_save_checked = UINT32_MAX;   // checked again for the next game
}

// Every state load (a slot, the quick slot, a hotkey, a device) comes
// through here: the running game goes to the spare buffer first and becomes
// the undo once the load worked. The DVR's load at its cursor (PLAY FROM
// HERE) doesn't: it has its own lane.
bool load_state_keeping_undo(App* app)
{
    bool kept = false;
    if (game_running(app))
    {
        // (0 = no savestates.) What the core really wrote is the vector's size, not the size asked.
        kept = libretro_host_state_size(app->session.host) > 0 &&
               libretro_host_serialize(app->session.host, &app->undo_load_spare);
    }
    bool loaded = core_session_load_state(&app->session);
    if (loaded && kept)
    {
        app->undo_load_size = app->undo_load_spare.size();
        app->undo_load.swap(app->undo_load_spare);
    }
    return loaded;
}

// UNDO LOAD STATE: the game back as before the last load. The state it
// replaces becomes the undo, so undoing twice is redo. False with nothing to
// undo, or when the core refused it.
bool undo_load_state(App* app)
{
    if (!game_running(app) || app->undo_load_size == 0)
    {
        return false;
    }
    LibretroHost* host = app->session.host;
    bool kept = libretro_host_state_size(host) > 0 && libretro_host_serialize(host, &app->undo_load_spare);
    // A load as the player's (the rewind history jumps here, as for a slot).
    if (!libretro_host_load_state_memory(host, app->undo_load.data(), app->undo_load_size))
    {
        return false;
    }
    audio_output_clear(&app->session.audio);   // what was queued belongs to the time before
    app->undo_load_size = kept ? app->undo_load_spare.size() : 0;   // what the core wrote, not the size asked
    app->undo_load.swap(app->undo_load_spare);
    return true;
}

// The slot GAME > UNDO SAVE STATE, its hotkey and a device undo: the one
// saved last in this game, the selected one until then. (QUICK MENU's UNDO
// SAVE undoes the slot selected there.)
uint32_t undo_save_slot(const App* app)
{
    return app->saved_slot_known ? app->saved_slot : app->session.state_slot;
}

// UNDO SAVE STATE: `slot` swapped with its undo files.
bool undo_save_state(App* app, uint32_t slot)
{
    char path[LIBRETRO_PATH_MAX] = {};
    if (!game_running(app) || !libretro_host_state_path(app->session.host, slot, path, sizeof(path)))
    {
        return false;
    }
    bool undone = state_undo_swap(path);
    quick_menu_refresh(&app->quick_menu);
    app->quick_slot_stale = true;   // the quick slot and the undo files, read again
    return undone;
}

// An undo's card, as report_session decides a save's or a load's: NOTHING TO
// UNDO always, the undo done only when `announce` (a hotkey, a device).
void report_undo_save(App* app, bool undone, uint32_t slot, bool announce)
{
    if (!undone)
    {
        set_status(app, piccolo_text(&app->strings, Str::status_undo_nothing));
    }
    else if (announce)
    {
        char text[128] = {};
        std::snprintf(text, sizeof(text), piccolo_text(&app->strings, Str::status_undo_save), slot);
        set_status(app, text);
    }
}

void report_undo_load(App* app, bool undone, bool announce)
{
    if (!undone || announce)
    {
        set_status(app, piccolo_text(&app->strings, undone ? Str::status_undo_load : Str::status_undo_nothing));
    }
}

// ---------------------------------------------------------------------------
// SAVE STATE ON EXIT (on by default, Andrés 2026-11-26): closing a game
// (CLOSE GAME, another game, QUIT, Android sending piccolo to the background,
// where it may be killed) saves its AUTO state; starting it again loads it,
// with a card, and UNDO LOAD STATE goes back to the fresh start.
// ---------------------------------------------------------------------------

// The AUTO state's file: "<game>.state.auto", next to the slots. host-libretro
// names a slot ".state<N>": slot 0's name without its digit, plus ".auto".
// It's no slot, so the slot list, QUICK MENU and FILE TRANSFER's companions
// never see it.
static bool auto_state_path(const App* app, char* out, size_t out_size)
{
    char slot_path[LIBRETRO_PATH_MAX] = {};
    if (!game_running(app) || !libretro_host_state_path(app->session.host, 0, slot_path, sizeof(slot_path)))
    {
        return false;
    }
    size_t length = std::strlen(slot_path);
    if (length == 0 || slot_path[length - 1] != '0')
    {
        return false;
    }
    slot_path[length - 1] = '\0';
    int written = std::snprintf(out, out_size, "%s.auto", slot_path);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

// The picture of the game as shown, at x1 (IMAGE SETTINGS' crop and aspect):
// a slot's thumbnail is made at that size; `aspect_*` gets it too (the
// CONTINUE card draws the cropped frame at that shape itself).
static StateThumbnail picture_thumbnail(const App* app, uint32_t* aspect_width, uint32_t* aspect_height)
{
    StateThumbnail thumbnail = {};
    *aspect_width = 0;
    *aspect_height = 0;
    const LibretroVideoFrame* video = &app->session.host->video;
    if (game_running(app) && video->width > 0 && video->height > 0)
    {
        ViewportInput input = {};
        input.frame_width = video->width;
        input.frame_height = video->height;
        input.core_aspect = static_cast<double>(app->session.host->av_info.geometry.aspect_ratio);
        piccolo_settings_viewport(&app->settings, &input);
        thumbnail.crop = input.crop;
        if (!viewport_picture_size(&input, 1, &thumbnail.width, &thumbnail.height))
        {
            thumbnail.width = 0;
            thumbnail.height = 0;
        }
        *aspect_width = thumbnail.width;
        *aspect_height = thumbnail.height;
    }
    return thumbnail;
}

// Before the running game stops (between frames): its AUTO state (SAVE
// STATE ON EXIT), and for the CONTINUE card its last frame and record
// (continue_card.h), even when there's no state to save. No state for a core
// without savestates, nor one whose states are incomplete (the card says
// START for both; an incomplete state's old file goes, so it never loads).
void save_state_on_exit(App* app)
{
    char path[LIBRETRO_PATH_MAX + 8] = {};
    if (!auto_state_path(app, path, sizeof(path)))
    {
        return;
    }
    LibretroHost* host = app->session.host;
    ContinueRecord record = {};
    record.saved = ContinueSaved::off;
    if (settings_get_bool(&app->settings, PICCOLO_SAVESTATE_ON_EXIT))
    {
        size_t size = libretro_host_state_size(host);
        std::vector<uint8_t> state;   // once, as the game closes: not a hot path
        if ((host->serialization_quirks & RETRO_SERIALIZATION_QUIRK_INCOMPLETE) != 0)
        {
            record.saved = ContinueSaved::incomplete;
            if (host_fs_exists(path))
            {
                host_fs_remove_file(path);
            }
        }
        else if (size == 0 || !libretro_host_serialize(host, &state))
        {
            record.saved = ContinueSaved::no_states;
        }
        else if (host_fs_write_file(path, state.data(), state.size()))   // what the host filled, not the size asked before
        {
            record.saved = ContinueSaved::state;
            SDL_Log("Save state on exit: %s (%zu bytes)", path, state.size());
        }
        else
        {
            record.saved = ContinueSaved::no_states;   // (logged) nothing to resume
        }
    }

    // The card's picture: the cropped frame, not scaled (sharp later).
    std::string picture = std::string(path) + ".png";
    StateThumbnail thumbnail = picture_thumbnail(app, &record.aspect_width, &record.aspect_height);
    thumbnail.native = true;
    if (thumbnail.width == 0 || !core_session_save_picture(&app->session, &thumbnail, picture.c_str()))
    {
        if (host_fs_exists(picture.c_str()))
        {
            host_fs_remove_file(picture.c_str());   // never an older game's picture
        }
    }
    record.core_path = app->marker_core_path;
    record.content_path = app->marker_content_path;
    char game[LIBRETRO_PATH_MAX] = {};
    if (record.content_path.empty() ||
        !content_archive_game_name(record.content_path.c_str(), game, sizeof(game)))
    {
        std::snprintf(game, sizeof(game), "%s", host->game_name);
    }
    record.game_name = game;
    record.core_name = host->system_info.library_name != nullptr ? host->system_info.library_name : "";
    record.state_path = path;
    record.picture_path = picture;
    // The save file a newer copy of would make resuming go back (SRAM).
    for (size_t i = 0; i < host->memory_files.size(); i++)
    {
        if (host->memory_files[i].type == RETRO_MEMORY_SAVE_RAM)
        {
            record.save_path = host->memory_files[i].path;
            break;
        }
    }
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    record.closed = static_cast<uint64_t>(now / SDL_NS_PER_SECOND);
    continue_panel_closed(&app->continue_panel, &record);
}

// Right after a game started: its AUTO state, if it has one. The fresh start
// becomes UNDO LOAD STATE's. Said with a card (nothing else shows it).
void resume_state_on_start(App* app)
{
    char path[LIBRETRO_PATH_MAX + 8] = {};
    if (!settings_get_bool(&app->settings, PICCOLO_SAVESTATE_ON_EXIT) || !auto_state_path(app, path, sizeof(path)) ||
        !host_fs_exists(path))
    {
        return;
    }
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!host_fs_read_file(path, &data, &size))
    {
        set_status(app, piccolo_text(&app->strings, Str::status_resume_failed));
        return;
    }
    LibretroHost* host = app->session.host;
    bool kept = libretro_host_state_size(host) > 0 && libretro_host_serialize(host, &app->undo_load_spare);
    bool loaded = size > 0 && libretro_host_load_state_memory(host, data, size);
    delete[] data;
    if (!loaded)
    {
        set_status(app, piccolo_text(&app->strings, Str::status_resume_failed));
        return;
    }
    audio_output_clear(&app->session.audio);
    if (kept)
    {
        app->undo_load_size = app->undo_load_spare.size();   // what the core wrote, not the size asked
        app->undo_load.swap(app->undo_load_spare);
    }
    set_status(app, piccolo_text(&app->strings, Str::status_resumed));
}

// --- The slots (GAME menu, QUICK MENU, hotkeys) -----------------------------

// Saves the session's slot with a thumbnail of the picture as shown: the
// Picture settings' crop and aspect, at x1 (core_session.h).
bool save_state_with_thumbnail(App* app)
{
    uint32_t aspect_width = 0;
    uint32_t aspect_height = 0;
    StateThumbnail thumbnail = picture_thumbnail(app, &aspect_width, &aspect_height);
    // UNDO SAVE STATE: the slot's old files moved aside first (state_undo.h).
    char path[LIBRETRO_PATH_MAX] = {};
    bool kept = game_running(app) &&
                libretro_host_state_path(app->session.host, app->session.state_slot, path, sizeof(path)) &&
                state_undo_keep(path);
    bool saved = core_session_save_state(&app->session, &thumbnail);
    if (!saved && kept)
    {
        state_undo_cancel(path);   // the slot as it was
    }
    if (saved)
    {
        app->saved_slot_known = true;
        app->saved_slot = app->session.state_slot;
    }
    quick_menu_refresh(&app->quick_menu);
    app->quick_slot_stale = true;
    return saved;
}

// QUICK SAVE / QUICK LOAD: the quick slot, whatever slot is selected.
bool quick_save(App* app)
{
    if (!game_running(app))
    {
        return false;   // report_session would show the session's last message
    }
    uint32_t selected = app->session.state_slot;
    app->session.state_slot = QUICK_SLOT;
    bool saved = save_state_with_thumbnail(app);
    app->session.state_slot = selected;
    report_session(app, saved, false);
    return saved;
}

bool quick_load(App* app)
{
    if (!game_running(app))
    {
        return false;
    }
    uint32_t selected = app->session.state_slot;
    app->session.state_slot = QUICK_SLOT;
    bool loaded = load_state_keeping_undo(app);
    app->session.state_slot = selected;
    report_session(app, loaded, false);
    return loaded;
}

// SAVE STATE / LOAD STATE in the selected slot (GAME menu, hotkeys, another
// device): back to the game when it worked. `announce`: say so when it
// worked too (a hotkey's or a device's; the menus show it themselves).
void save_state_from(App* app, bool announce)
{
    if (!game_running(app))
    {
        return;   // nothing to save; the session's last message is not about this
    }
    bool saved = save_state_with_thumbnail(app);
    if (saved)
    {
        app_leave_main_menu(app);
    }
    report_session(app, saved, announce);
}

void load_state_from(App* app, bool announce)
{
    if (!game_running(app))
    {
        return;   // as save_state_from
    }
    bool loaded = load_state_keeping_undo(app);
    if (loaded)
    {
        app_leave_main_menu(app);
    }
    report_session(app, loaded, announce);
}

// UNDO SAVE STATE / UNDO LOAD STATE from the GAME menu, the hotkeys or
// another device, as SAVE / LOAD STATE: back to the game when it worked.
void undo_save_from(App* app, bool announce)
{
    uint32_t slot = undo_save_slot(app);
    bool undone = undo_save_state(app, slot);
    if (undone)
    {
        app_leave_main_menu(app);
    }
    report_undo_save(app, undone, slot, announce);
}

void undo_load_from(App* app, bool announce)
{
    bool undone = undo_load_state(app);
    if (undone)
    {
        app_leave_main_menu(app);
    }
    report_undo_load(app, undone, announce);
}
