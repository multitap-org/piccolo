// Checks the CONTINUE card's model (continue_card.h): which card a record
// and its files make, the record written and read back, the files' dates as
// read from disk, and "today / yesterday / weekday / date".
//
// Usage: piccolo_continue_card_test <scratch directory>
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>
#include <string>

#include "continue_card.h"
#include "host_fs.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static void check_pick(const ContinueRecord* record, bool matches, uint64_t state, uint64_t save, ContinueKind kind,
                       ContinueReason reason, const char* what)
{
    ContinueFiles files = {};
    files.state_modified = state;
    files.save_modified = save;
    ContinueChoice choice = continue_card_pick(record, matches, &files);
    check(choice.kind == kind && choice.reason == reason, what);
}

static ContinueDate date(int32_t year, int32_t month, int32_t day, int32_t weekday)
{
    ContinueDate result = {};
    result.year = year;
    result.month = month;
    result.day = day;
    result.weekday = weekday;
    result.hour = 12;
    return result;
}

static void write_text(const std::string* path, const char* text)
{
    host_fs_write_file(path->c_str(), text, std::strlen(text));
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <scratch directory>\n", argv[0]);
        return 2;
    }
    std::string scratch = argv[1];
    host_fs_make_directories(scratch.c_str());

    // --- Which card -------------------------------------------------------
    ContinueRecord record = {};
    record.core_path = "/cores/snes9x_libretro.so";
    record.content_path = "/roms/Super Mario World (USA).sfc";
    record.saved = ContinueSaved::state;
    const uint64_t T = 1795600000;
    check_pick(&record, false, T, T, ContinueKind::none, ContinueReason::none, "not the top of RECENT: no card");
    check_pick(&record, true, T, 0, ContinueKind::resume, ContinueReason::none, "a state, no save file: RESUME");
    check_pick(&record, true, T, T - 3600, ContinueKind::resume, ContinueReason::none, "an older save: RESUME");
    check_pick(&record, true, T, T + 2, ContinueKind::resume, ContinueReason::none,
               "the save written as the game stopped (seconds later): still RESUME");
    check_pick(&record, true, T, T + CONTINUE_SAVE_GRACE_SECONDS, ContinueKind::resume, ContinueReason::none,
               "the save at the end of the grace: RESUME");
    check_pick(&record, true, T, T + CONTINUE_SAVE_GRACE_SECONDS + 1, ContinueKind::start_or_resume,
               ContinueReason::newer_save, "a newer save: START first, RESUME second");
    check_pick(&record, true, 0, 0, ContinueKind::start, ContinueReason::state_missing,
               "the auto state deleted since: START");
    record.saved = ContinueSaved::no_states;
    check_pick(&record, true, 0, 0, ContinueKind::start, ContinueReason::no_states, "a core without savestates: START");
    check_pick(&record, true, T, T + 9999, ContinueKind::start, ContinueReason::no_states,
               "without savestates an old state file doesn't matter: START");
    record.saved = ContinueSaved::incomplete;
    check_pick(&record, true, T, 0, ContinueKind::start, ContinueReason::incomplete, "incomplete states: START");
    record.saved = ContinueSaved::off;
    check_pick(&record, true, T, 0, ContinueKind::start, ContinueReason::off,
               "SAVE STATE ON EXIT off (an old state left): START");

    // --- Which game -------------------------------------------------------
    RecentGame top = {};
    top.core_path = record.core_path;
    top.content_path = record.content_path;
    check(continue_record_matches(&record, &top), "the record is the top of RECENT");
    check(!continue_record_matches(&record, nullptr), "RECENT empty (CLEAR ALL): no match");
    top.content_path = "/roms/Other.sfc";
    check(!continue_record_matches(&record, &top), "another game on top: no match");
    top.content_path = record.content_path;
    top.core_path = "/cores/bsnes_libretro.so";
    check(!continue_record_matches(&record, &top), "the same game on another core: no match");
    ContinueRecord empty = {};
    RecentGame no_game = {};
    check(!continue_record_matches(&empty, &no_game), "no record matches nothing");

    // --- The record on disk -----------------------------------------------
    std::string path = scratch + "/continue.cfg";
    std::string state = scratch + "/Super Mario World (USA).state.auto";
    std::string picture = state + ".png";
    ContinueRecord saved = {};
    saved.core_path = "/cores/snes9x_libretro.so";
    saved.content_path = "/roms/Super Mario World (USA).zip#Super Mario World (USA).sfc";
    saved.game_name = "Super Mario World (USA)";
    saved.core_name = "Snes9x";
    saved.system_name = "Super Nintendo";
    saved.state_path = state;
    saved.picture_path = picture;
    saved.save_path = "";
    saved.aspect_width = 292;
    saved.aspect_height = 224;
    saved.closed = T;
    saved.saved = ContinueSaved::incomplete;
    check(continue_record_save(&saved, path.c_str()), "the record is written");
    ContinueRecord loaded = {};
    check(continue_record_load(&loaded, path.c_str()), "the record is read back");
    check(loaded.core_path == saved.core_path && loaded.content_path == saved.content_path, "same game");
    check(loaded.game_name == saved.game_name && loaded.core_name == saved.core_name &&
              loaded.system_name == saved.system_name,
          "same names");
    check(loaded.state_path == state && loaded.picture_path == picture && loaded.save_path.empty(), "same files");
    check(loaded.aspect_width == 292 && loaded.aspect_height == 224, "same aspect");
    check(loaded.closed == T && loaded.saved == ContinueSaved::incomplete, "same date and state");
    std::string missing = scratch + "/nothing.cfg";
    check(!continue_record_load(&loaded, missing.c_str()) && loaded.core_path.empty(), "no file: no record");
    std::string broken = scratch + "/broken.cfg";
    write_text(&broken, "[continue]\ncore =\naspect_width = 99999999\n");
    check(!continue_record_load(&loaded, broken.c_str()), "a record without a core is no record");
    write_text(&broken, "[continue]\ncore = /c.so\naspect_width = 99999999\naspect_height = 3\nsaved = what\n");
    check(continue_record_load(&loaded, broken.c_str()) && loaded.aspect_width == 0 && loaded.aspect_height == 0 &&
              loaded.saved == ContinueSaved::state,
          "a broken aspect is unknown, an unknown state is a state");

    // --- The files ----------------------------------------------------------
    // (A run before this one left them.)
    host_fs_remove_file(state.c_str());
    host_fs_remove_file(picture.c_str());
    host_fs_remove_file((scratch + "/Super Mario World (USA).srm").c_str());
    ContinueFiles files = {};
    continue_files_read(&saved, &files);
    check(files.state_modified == 0 && !files.picture, "no files yet");
    write_text(&state, "state");
    write_text(&picture, "png");
    continue_files_read(&saved, &files);
    check(files.state_modified > 0 && files.picture && files.save_modified == 0, "the state and picture are seen");
    std::string save = scratch + "/Super Mario World (USA).srm";
    write_text(&save, "save");
    saved.save_path = save;
    host_fs_set_mtime(save.c_str(), files.state_modified + 3600);
    continue_files_read(&saved, &files);
    saved.saved = ContinueSaved::state;
    ContinueChoice choice = continue_card_pick(&saved, true, &files);
    check(choice.kind == ContinueKind::start_or_resume, "a save copied in an hour later: START / RESUME");
    host_fs_set_mtime(save.c_str(), files.state_modified + 1);
    continue_files_read(&saved, &files);
    choice = continue_card_pick(&saved, true, &files);
    check(choice.kind == ContinueKind::resume, "the save written right after the state: RESUME");

    // --- When ---------------------------------------------------------------
    ContinueDate thursday = date(2026, 11, 26, 4);
    ContinueDate same = date(2026, 11, 26, 4);
    check(continue_card_when(&same, &thursday) == ContinueWhen::today, "today");
    ContinueDate wednesday = date(2026, 11, 25, 3);
    check(continue_card_when(&wednesday, &thursday) == ContinueWhen::yesterday, "yesterday");
    ContinueDate monday = date(2026, 11, 23, 1);
    check(continue_card_when(&monday, &thursday) == ContinueWhen::this_week, "three days ago: its weekday");
    ContinueDate friday = date(2026, 11, 20, 5);
    check(continue_card_when(&friday, &thursday) == ContinueWhen::this_week, "six days ago: its weekday");
    ContinueDate week = date(2026, 11, 19, 4);
    check(continue_card_when(&week, &thursday) == ContinueWhen::earlier, "a week ago: the date");
    ContinueDate new_year = date(2027, 1, 1, 5);
    ContinueDate new_years_eve = date(2026, 12, 31, 4);
    check(continue_card_when(&new_years_eve, &new_year) == ContinueWhen::yesterday, "yesterday across a year");
    ContinueDate march = date(2028, 3, 1, 3);
    ContinueDate leap = date(2028, 2, 28, 1);
    check(continue_card_days_between(&leap, &march) == 2, "a leap year's February 29 counts");
    ContinueDate tomorrow = date(2026, 11, 27, 5);
    check(continue_card_when(&tomorrow, &thursday) == ContinueWhen::today, "a clock set back: today");

    if (g_failures == 0)
    {
        std::printf("continue card: all checks passed\n");
        return 0;
    }
    return 1;
}
