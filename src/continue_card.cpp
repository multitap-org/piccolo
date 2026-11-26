#include "continue_card.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "host_fs.h"
#include "settings_file.h"

static const char* const SECTION = "continue";

static const char* saved_name(ContinueSaved saved)
{
    switch (saved)
    {
    case ContinueSaved::state:
        return "state";
    case ContinueSaved::no_states:
        return "no_states";
    case ContinueSaved::incomplete:
        return "incomplete";
    case ContinueSaved::off:
        return "off";
    }
    return "state";
}

static ContinueSaved saved_from_name(const char* name)
{
    if (std::strcmp(name, "no_states") == 0)
    {
        return ContinueSaved::no_states;
    }
    if (std::strcmp(name, "incomplete") == 0)
    {
        return ContinueSaved::incomplete;
    }
    if (std::strcmp(name, "off") == 0)
    {
        return ContinueSaved::off;
    }
    return ContinueSaved::state;
}

static std::string text_or_empty(const SettingsFile* file, const char* key)
{
    const char* value = settings_file_get(file, SECTION, key);
    return value != nullptr ? std::string(value) : std::string();
}

static uint64_t number_or_zero(const SettingsFile* file, const char* key)
{
    const char* value = settings_file_get(file, SECTION, key);
    if (value == nullptr || value[0] == '\0')
    {
        return 0;
    }
    return std::strtoull(value, nullptr, 10);
}

bool continue_record_load(ContinueRecord* record, const char* path)
{
    *record = ContinueRecord{};
    SettingsFile file = {};
    if (!host_fs_exists(path) || !settings_file_load(&file, path))
    {
        return false;
    }
    record->core_path = text_or_empty(&file, "core");
    if (record->core_path.empty())
    {
        *record = ContinueRecord{};
        return false;
    }
    record->content_path = text_or_empty(&file, "content");
    record->game_name = text_or_empty(&file, "game");
    record->core_name = text_or_empty(&file, "core_name");
    record->system_name = text_or_empty(&file, "system");
    record->state_path = text_or_empty(&file, "state");
    record->picture_path = text_or_empty(&file, "picture");
    record->save_path = text_or_empty(&file, "save");
    uint64_t width = number_or_zero(&file, "aspect_width");
    uint64_t height = number_or_zero(&file, "aspect_height");
    // A picture is never this big; anything else is a broken file.
    if (width > 0 && height > 0 && width <= 65536 && height <= 65536)
    {
        record->aspect_width = static_cast<uint32_t>(width);
        record->aspect_height = static_cast<uint32_t>(height);
    }
    record->closed = number_or_zero(&file, "closed");
    record->saved = saved_from_name(text_or_empty(&file, "saved").c_str());
    return true;
}

bool continue_record_save(const ContinueRecord* record, const char* path)
{
    SettingsFile file = {};
    char width[16] = {};
    char height[16] = {};
    char closed[32] = {};
    std::snprintf(width, sizeof(width), "%u", record->aspect_width);
    std::snprintf(height, sizeof(height), "%u", record->aspect_height);
    std::snprintf(closed, sizeof(closed), "%llu", static_cast<unsigned long long>(record->closed));
    bool ok = settings_file_set(&file, SECTION, "core", record->core_path.c_str()) &&
              settings_file_set(&file, SECTION, "content", record->content_path.c_str()) &&
              settings_file_set(&file, SECTION, "game", record->game_name.c_str()) &&
              settings_file_set(&file, SECTION, "core_name", record->core_name.c_str()) &&
              settings_file_set(&file, SECTION, "system", record->system_name.c_str()) &&
              settings_file_set(&file, SECTION, "state", record->state_path.c_str()) &&
              settings_file_set(&file, SECTION, "picture", record->picture_path.c_str()) &&
              settings_file_set(&file, SECTION, "save", record->save_path.c_str()) &&
              settings_file_set(&file, SECTION, "aspect_width", width) &&
              settings_file_set(&file, SECTION, "aspect_height", height) &&
              settings_file_set(&file, SECTION, "closed", closed) &&
              settings_file_set(&file, SECTION, "saved", saved_name(record->saved));
    if (!ok)
    {
        return false;   // logged (e.g. a path with a line break)
    }
    return settings_file_save(&file, path, "piccolo: the last game closed, for the CONTINUE card");
}

bool continue_record_matches(const ContinueRecord* record, const RecentGame* top)
{
    return top != nullptr && !record->core_path.empty() && record->core_path == top->core_path &&
           record->content_path == top->content_path;
}

static uint64_t modified_or_zero(const std::string* path)
{
    uint64_t size = 0;
    uint64_t modified = 0;
    if (path->empty() || !host_fs_file_info(path->c_str(), &size, &modified))
    {
        return 0;
    }
    return modified > 0 ? modified : 1;   // there, whatever its date says
}

void continue_files_read(const ContinueRecord* record, ContinueFiles* files)
{
    *files = ContinueFiles{};
    files->state_modified = modified_or_zero(&record->state_path);
    files->save_modified = modified_or_zero(&record->save_path);
    files->picture = !record->picture_path.empty() && host_fs_exists(record->picture_path.c_str());
}

ContinueChoice continue_card_pick(const ContinueRecord* record, bool matches, const ContinueFiles* files)
{
    ContinueChoice choice = {};
    if (!matches)
    {
        return choice;
    }
    choice.kind = ContinueKind::start;
    switch (record->saved)
    {
    case ContinueSaved::no_states:
        choice.reason = ContinueReason::no_states;
        return choice;
    case ContinueSaved::incomplete:
        choice.reason = ContinueReason::incomplete;
        return choice;
    case ContinueSaved::off:
        choice.reason = ContinueReason::off;
        return choice;
    case ContinueSaved::state:
        break;
    }
    if (files->state_modified == 0)
    {
        choice.reason = ContinueReason::state_missing;
        return choice;
    }
    // A state carries the game's save RAM: resuming would bring the older
    // save back, so START (keeping the newer save) comes first.
    if (files->save_modified > files->state_modified + CONTINUE_SAVE_GRACE_SECONDS)
    {
        choice.kind = ContinueKind::start_or_resume;
        choice.reason = ContinueReason::newer_save;
        return choice;
    }
    choice.kind = ContinueKind::resume;
    return choice;
}

// Days since 1970-01-01 of a civil date (Howard Hinnant's days_from_civil):
// exact across months, years and leap years, no time zone involved.
static int64_t days_from_civil(int64_t year, int64_t month, int64_t day)
{
    year -= month <= 2 ? 1 : 0;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    int64_t year_of_era = year - era * 400;
    int64_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

int64_t continue_card_days_between(const ContinueDate* a, const ContinueDate* b)
{
    return days_from_civil(b->year, b->month, b->day) - days_from_civil(a->year, a->month, a->day);
}

ContinueWhen continue_card_when(const ContinueDate* played, const ContinueDate* now)
{
    int64_t days = continue_card_days_between(played, now);
    if (days <= 0)
    {
        return ContinueWhen::today;   // a clock set back counts as today
    }
    if (days == 1)
    {
        return ContinueWhen::yesterday;
    }
    if (days <= 6)
    {
        return ContinueWhen::this_week;
    }
    return ContinueWhen::earlier;
}
