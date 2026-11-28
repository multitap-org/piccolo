#include "continue_card.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "host_fs.h"
#include "settings_file.h"

// The card's date is seconds * SDL_NS_PER_SECOND in an int64 (continue_panel):
// anything above this (year 2255) is a broken file, not a time.
constexpr uint64_t CLOSED_MAX_SECONDS = 9000000000;

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

static std::string text_or_empty(const SettingsFile* file, const char* section, const char* key)
{
    const char* value = settings_file_get(file, section, key);
    return value != nullptr ? std::string(value) : std::string();
}

static uint64_t number_or_zero(const SettingsFile* file, const char* section, const char* key)
{
    const char* value = settings_file_get(file, section, key);
    if (value == nullptr || value[0] == '\0')
    {
        return 0;
    }
    return std::strtoull(value, nullptr, 10);
}

// One section as a record. False (record empty) without a core: not a record.
static bool record_read(ContinueRecord* record, const SettingsFile* file, const char* section)
{
    *record = ContinueRecord{};
    record->core_path = text_or_empty(file, section, "core");
    if (record->core_path.empty())
    {
        return false;
    }
    record->content_path = text_or_empty(file, section, "content");
    record->game_name = text_or_empty(file, section, "game");
    record->core_name = text_or_empty(file, section, "core_name");
    record->state_path = text_or_empty(file, section, "state");
    record->picture_path = text_or_empty(file, section, "picture");
    record->save_path = text_or_empty(file, section, "save");
    uint64_t width = number_or_zero(file, section, "aspect_width");
    uint64_t height = number_or_zero(file, section, "aspect_height");
    // A picture is never this big; anything else is a broken file.
    if (width > 0 && height > 0 && width <= 65536 && height <= 65536)
    {
        record->aspect_width = static_cast<uint32_t>(width);
        record->aspect_height = static_cast<uint32_t>(height);
    }
    record->closed = number_or_zero(file, section, "closed");
    if (record->closed > CLOSED_MAX_SECONDS)
    {
        record->closed = 0;   // no date line
    }
    record->saved = saved_from_name(text_or_empty(file, section, "saved").c_str());
    return true;
}

bool continue_records_load(std::vector<ContinueRecord>* records, const char* path)
{
    records->clear();
    SettingsFile file = {};
    if (!host_fs_exists(path) || !settings_file_load(&file, path))
    {
        return false;
    }
    // Every section is a record, whatever it is called: "1", "2", ... most
    // recent first, or the single "[continue]" the first version wrote.
    for (size_t i = 0; i < file.sections.size() && records->size() < RECENT_GAMES_MAX; i++)
    {
        const char* section = file.sections[i].name.c_str();
        if (section[0] == '\0')
        {
            continue;   // the global section: nothing of ours there
        }
        ContinueRecord record = {};
        if (record_read(&record, &file, section))
        {
            continue_records_put_last(records, &record);
        }
    }
    return true;
}

// A name as the card shows it: line breaks as blanks, none at the ends.
// settings_file_set refuses both, and a name is only ever shown, so the
// record is saved with a clean one instead of not at all.
static std::string shown_name(const std::string* name)
{
    std::string clean = *name;
    for (size_t i = 0; i < clean.size(); i++)
    {
        if (clean[i] == '\n' || clean[i] == '\r')
        {
            clean[i] = ' ';
        }
    }
    size_t first = clean.find_first_not_of(" \t");
    if (first == std::string::npos)
    {
        return std::string();
    }
    size_t last = clean.find_last_not_of(" \t");
    return clean.substr(first, last - first + 1);
}

// One record into section `section`. False when a value can't be stored
// (e.g. a path with a line break).
static bool record_write(SettingsFile* file, const char* section, const ContinueRecord* record)
{
    char width[16] = {};
    char height[16] = {};
    char closed[32] = {};
    std::snprintf(width, sizeof(width), "%u", record->aspect_width);
    std::snprintf(height, sizeof(height), "%u", record->aspect_height);
    std::snprintf(closed, sizeof(closed), "%llu", static_cast<unsigned long long>(record->closed));
    std::string game_name = shown_name(&record->game_name);
    std::string core_name = shown_name(&record->core_name);
    return settings_file_set(file, section, "core", record->core_path.c_str()) &&
           settings_file_set(file, section, "content", record->content_path.c_str()) &&
           settings_file_set(file, section, "game", game_name.c_str()) &&
           settings_file_set(file, section, "core_name", core_name.c_str()) &&
           settings_file_set(file, section, "state", record->state_path.c_str()) &&
           settings_file_set(file, section, "picture", record->picture_path.c_str()) &&
           settings_file_set(file, section, "save", record->save_path.c_str()) &&
           settings_file_set(file, section, "aspect_width", width) &&
           settings_file_set(file, section, "aspect_height", height) &&
           settings_file_set(file, section, "closed", closed) &&
           settings_file_set(file, section, "saved", saved_name(record->saved));
}

bool continue_records_save(const std::vector<ContinueRecord>* records, const char* path)
{
    SettingsFile file = {};
    uint32_t written = 0;
    for (size_t i = 0; i < records->size() && written < RECENT_GAMES_MAX; i++)
    {
        // Tried on a scratch file first, like recent.cfg: one record that
        // can't be stored must not stop the others nor leave half a section.
        SettingsFile probe = {};
        if (!record_write(&probe, "1", &(*records)[i]))
        {
            continue;   // logged by settings_file_set
        }
        written++;
        char section[16] = {};
        std::snprintf(section, sizeof(section), "%u", written);
        record_write(&file, section, &(*records)[i]);
    }
    return settings_file_save(&file, path, "piccolo: the games closed last, for the CONTINUE card (most recent first)");
}

void continue_records_put(std::vector<ContinueRecord>* records, const ContinueRecord* record)
{
    int32_t found = continue_records_find(records, record->core_path.c_str(), record->content_path.c_str());
    if (found >= 0)
    {
        records->erase(records->begin() + found);
    }
    records->insert(records->begin(), *record);
    while (records->size() > RECENT_GAMES_MAX)
    {
        records->pop_back();
    }
}

void continue_records_put_last(std::vector<ContinueRecord>* records, const ContinueRecord* record)
{
    int32_t found = continue_records_find(records, record->core_path.c_str(), record->content_path.c_str());
    if (found < 0 && records->size() < RECENT_GAMES_MAX)
    {
        records->push_back(*record);   // (a game twice in a file: its first, the most recent, stays)
    }
}

int32_t continue_records_find(const std::vector<ContinueRecord>* records, const char* core_path,
                              const char* content_path)
{
    for (size_t i = 0; i < records->size(); i++)
    {
        const ContinueRecord* record = &(*records)[i];
        if (record->core_path == core_path && record->content_path == content_path)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// The shelf's order

void continue_order_reset(std::vector<uint32_t>* order, size_t count)
{
    order->clear();
    for (size_t i = 0; i < count; i++)
    {
        order->push_back(static_cast<uint32_t>(i));
    }
}

void continue_order_select(std::vector<uint32_t>* order, size_t position)
{
    if (position == 0 || position >= order->size())
    {
        return;
    }
    uint32_t chosen = (*order)[position];
    order->erase(order->begin() + static_cast<std::ptrdiff_t>(position));
    order->insert(order->begin(), chosen);
}

uint32_t continue_shelf_pages(size_t order_size)
{
    if (order_size <= 1)
    {
        return 0;
    }
    return static_cast<uint32_t>((order_size - 1 + CONTINUE_SHELF_TILES - 1) / CONTINUE_SHELF_TILES);
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
