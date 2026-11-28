#pragma once

// The CONTINUE card's model (Andrés, 2026-11-26, mockup v2: docs/mockups/
// continue/ in piccolo-agents): with no game running, the last game played,
// its picture and RESUME. No GUI here (tools/continue_card_test.cpp); the
// card is drawn by retroui's continue_view, main.cpp wires it.
//
// Closing a game (CLOSE GAME, another game, QUIT, Android to the background:
// where SAVE STATE ON EXIT saves "<game>.state.auto") also writes:
//   - "<game>.state.auto.png", the last frame cropped like on screen but not
//     scaled (a slot's thumbnail is already scaled to the aspect, smoothly;
//     the card scales this one itself, sharp bilinear). Written even when no
//     state could be saved (a core without savestates): it's the picture.
//   - <settings>/continue.cfg, this record: which game (its RECENT entry),
//     the files, the aspect it was shown at, what the state is.
//
//   [continue]
//   core = /home/ana/.local/share/piccolo/cores/snes9x_libretro.so
//   content = /home/ana/roms/Super Mario World (USA).sfc
//   game = Super Mario World (USA)
//   core_name = Snes9x
//   state = /home/ana/.local/share/piccolo/states/snes9x/Super Mario World (USA).state.auto
//   picture = ....state.auto.png
//   save = /home/ana/.local/share/piccolo/saves/snes9x/Super Mario World (USA).srm
//   aspect_width = 292              (the picture's size as shown at x1: IMAGE SETTINGS'
//   aspect_height = 224              aspect, after their crop, which the PNG already has)
//   closed = 1795671814             (seconds since 1970)
//   saved = state                   (state / no_states / incomplete / off)
//
// One record per game, like recent.cfg: sections "1", "2", ... most recent
// first, at most RECENT_GAMES_MAX, each with the keys above. Closing a game
// replaces its record (same core and content) and moves it to the front. The
// first version wrote a single "[continue]": it reads as the one record.
// A game of RECENT without a record still gets a card (RESTART alone, no
// picture); a record whose game left RECENT is just never looked up.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "recent_games.h"

// What closing the game left to resume.
enum class ContinueSaved
{
    state,        // "<game>.state.auto" was written
    no_states,    // the core can't save states (size 0, or serializing failed)
    incomplete,   // the core says its states are incomplete (RETRO_SERIALIZATION_QUIRK_INCOMPLETE)
    off,          // SAVE STATE ON EXIT is off for this game
};

struct ContinueRecord
{
    std::string   core_path;
    std::string   content_path;   // "" = the core without content (as RECENT keeps it)
    std::string   game_name;
    std::string   core_name;
    std::string   state_path;
    std::string   picture_path;
    std::string   save_path;      // "" = the core keeps no save file
    uint32_t      aspect_width = 0;    // 0 = unknown: the picture's own shape
    uint32_t      aspect_height = 0;
    uint64_t      closed = 0;     // seconds since 1970
    ContinueSaved saved = ContinueSaved::state;
};

// A missing or unreadable file: false, `records` empty. Sections without a
// core are skipped.
bool continue_records_load(std::vector<ContinueRecord>* records, const char* path);
bool continue_records_save(const std::vector<ContinueRecord>* records, const char* path);
// This game's record replaces the one of the same game (core + content) and
// goes to the front; the oldest beyond RECENT_GAMES_MAX go.
void continue_records_put(std::vector<ContinueRecord>* records, const ContinueRecord* record);
// Appended at the end unless that game is there already (loading a file).
void continue_records_put_last(std::vector<ContinueRecord>* records, const ContinueRecord* record);
// The record of this game: its index, -1 when it has none.
int32_t continue_records_find(const std::vector<ContinueRecord>* records, const char* core_path,
                              const char* content_path);

// The desktop panel shows the game in its card and a shelf of the others
// (Andrés, 2026-11-27). The order is only what's on screen, in memory:
// RECENT's indices, [0] in the card, [1...] on the shelf, 4 to a page.
// Choosing a shelf game moves it to the front, the card's game becomes the
// shelf's first. Reset to RECENT's order when RECENT changes.
constexpr uint32_t CONTINUE_SHELF_TILES = 4;
void continue_order_reset(std::vector<uint32_t>* order, size_t count);
void continue_order_select(std::vector<uint32_t>* order, size_t position);
// How many shelf pages `order_size` games make (the card's game isn't on the
// shelf): 0 when there is nothing but the card.
uint32_t continue_shelf_pages(size_t order_size);

// The record is the game at the top of RECENT (nullptr: RECENT is empty).
bool continue_record_matches(const ContinueRecord* record, const RecentGame* top);

// The files on disk now (seconds since 1970; 0 = not there).
struct ContinueFiles
{
    uint64_t state_modified = 0;
    uint64_t save_modified = 0;
    bool     picture = false;
};
void continue_files_read(const ContinueRecord* record, ContinueFiles* files);

// A save file written this long after the auto state is still the same
// session's: the core's save RAM is written as the game stops, right after
// SAVE STATE ON EXIT. Later than that, it came from elsewhere (FILE TRANSFER,
// another machine, played without the state).
constexpr uint64_t CONTINUE_SAVE_GRACE_SECONDS = 30;

// What the card offers.
enum class ContinueKind
{
    none,              // no card
    resume,            // RESUME
    start,             // RESTART only (nothing to resume)
    start_or_resume,   // RESTART first, RESUME second (the save file is newer)
};

// Why it says RESTART alone (the card's line).
enum class ContinueReason
{
    none,
    no_states,        // the core can't save states
    incomplete,       // its states are incomplete
    off,              // SAVE STATE ON EXIT is off
    state_missing,    // the auto state was deleted since
    newer_save,       // the save file is newer than the auto state
};

struct ContinueChoice
{
    ContinueKind   kind = ContinueKind::none;
    ContinueReason reason = ContinueReason::none;
};

// Which card, if any: the record must be the top of RECENT (`matches`).
ContinueChoice continue_card_pick(const ContinueRecord* record, bool matches, const ContinueFiles* files);

// A local date and time, broken down (the caller converts: SDL_TimeToDateTime).
struct ContinueDate
{
    int32_t year = 0;
    int32_t month = 0;     // 1-12
    int32_t day = 0;       // 1-31
    int32_t weekday = 0;   // 0 = Sunday
    int32_t hour = 0;
    int32_t minute = 0;
};

// When it was played, relative to today.
enum class ContinueWhen
{
    today,
    yesterday,
    this_week,   // 2-6 days ago: its weekday
    earlier,     // the date
};

ContinueWhen continue_card_when(const ContinueDate* played, const ContinueDate* now);
// Days between two dates (b - a), calendar days.
int64_t continue_card_days_between(const ContinueDate* a, const ContinueDate* b);
