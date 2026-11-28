#include "continue_panel.h"

#include <cstdio>

#include <SDL3/SDL.h>

#include "app_shell.h"
#include "host_fs.h"
#include "piccolo_text.h"

// Room for any picture as saved (a core's cropped frame).
constexpr uint32_t PICTURE_CAPACITY = 2048;
// It scaled up: never more than this across or down.
constexpr uint32_t SCALED_CAPACITY = 4096;
// How often the files are looked at while the card may show (a save copied
// in by FILE TRANSFER changes the card).
constexpr uint64_t CHECK_EVERY_MS = 1000;

// ---------------------------------------------------------------------------
// The debug window's fake games

struct FakeGame
{
    const char* name;
    const char* core;
    uint32_t    aspect_width;
    uint32_t    aspect_height;
    bool        newer_save;     // its save file is newer than its state
    ContinueSaved saved;
    bool        recorded;       // false: in RECENT with no record (no picture)
    uint32_t    picture_width;  // the made-up picture's size, as the game's own
    uint32_t    picture_height;
    uint32_t    days_ago;
};

static const FakeGame FAKE_GAMES[] = {
    {"Super Mario World (USA)", "Snes9x", 292, 224, false, ContinueSaved::state, true, 256, 224, 0},
    {"Quake", "TyrQuake", 4, 3, false, ContinueSaved::no_states, true, 320, 240, 1},
    {"Pokemon - Crystal Version (USA)", "SameBoy", 160, 144, true, ContinueSaved::state, true, 160, 144, 3},
    {"Ikaruga", "Flycast", 3, 4, false, ContinueSaved::state, true, 240, 320, 9},   // vertical
    {"Sonic The Hedgehog (USA, Europe)", "Genesis Plus GX", 0, 0, false, ContinueSaved::state, false, 0, 0, 0},
    {"Metroid Fusion (USA)", "mGBA", 3, 2, false, ContinueSaved::state, true, 240, 160, 2},
};
constexpr uint32_t FAKE_GAME_COUNT = sizeof(FAKE_GAMES) / sizeof(FAKE_GAMES[0]);

static uint32_t fake_card_game(ContinuePreview preview)
{
    switch (preview)
    {
    case ContinuePreview::no_states:
        return 1;
    case ContinuePreview::newer_save:
        return 2;
    case ContinuePreview::vertical:
        return 3;
    case ContinuePreview::resume:
    case ContinuePreview::phone:
    case ContinuePreview::none:
        break;
    }
    return 0;
}

static ContinueChoice fake_choice(uint32_t game)
{
    const FakeGame* fake = &FAKE_GAMES[game];
    ContinueChoice choice = {};
    choice.kind = ContinueKind::resume;
    if (!fake->recorded)
    {
        choice.kind = ContinueKind::start;
    }
    else if (fake->saved == ContinueSaved::no_states)
    {
        choice.kind = ContinueKind::start;
        choice.reason = ContinueReason::no_states;
    }
    else if (fake->newer_save)
    {
        choice.kind = ContinueKind::start_or_resume;
        choice.reason = ContinueReason::newer_save;
    }
    return choice;
}

// A made-up frame for a fake card: sky, hills, ground and a few blocks, in
// the game's own resolution, so its pixels show how sharp the card draws.
// `variant` shifts the colors so the games' pictures differ.
static uint32_t fake_color(uint32_t color, uint32_t variant)
{
    uint32_t red = (color >> 16) & 0xFFu;
    uint32_t green = (color >> 8) & 0xFFu;
    uint32_t blue = color & 0xFFu;
    switch (variant % 3)
    {
    case 1:
        return 0xFF000000u | (blue << 16) | (green << 8) | red;
    case 2:
        return 0xFF000000u | (green << 16) | (blue << 8) | red;
    default:
        break;
    }
    return color;
}

static void fake_picture(UiPicture* picture, uint32_t width, uint32_t height, uint32_t variant)
{
    picture->width = width;
    picture->height = height;
    picture->pixels.resize(static_cast<size_t>(width) * height);
    uint32_t ground = height * 3 / 4;
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            uint32_t sky_blue = 0xA0u + (y * 0x50u) / height;
            uint32_t color = 0xFF000000u | (0x58u << 16) | (0x88u << 8) | sky_blue;
            uint32_t hill = ground - ((x / 4) % 16 < 8 ? (x / 4) % 16 : 16 - (x / 4) % 16) * 2;
            if (y >= hill)
            {
                color = 0xFF2E8B3Au;
            }
            if (y >= ground)
            {
                color = ((x / 8) + (y / 8)) % 2 == 0 ? 0xFFB8743Au : 0xFF9A5A2Au;
            }
            bool block_row = y >= height / 3 && y < height / 3 + 16;
            bool block_column = x % 48 >= 16 && x % 48 < 32;
            if (block_row && block_column)
            {
                color = (x % 16 == 0 || y == height / 3 || x % 16 == 15 || y == height / 3 + 15) ? 0xFF201000u
                                                                                                  : 0xFFE8B030u;
            }
            picture->pixels[static_cast<size_t>(y) * width + x] = fake_color(color, variant);
        }
    }
    ui_picture_changed(picture);
}

void continue_panel_preview(ContinuePanel* panel, ContinuePreview preview)
{
    panel->preview = preview;
    panel->recheck = true;
    panel->shelf_page = 0;
    panel->focus_tile = -1;
    panel->focus_second = false;
    panel->card_picture.raw_path.clear();
    panel->card_picture.scaled_for = 0;
    for (uint32_t i = 0; i < CONTINUE_VIEW_TILES_MAX; i++)
    {
        panel->tile_pictures[i].raw_path.clear();
        panel->tile_pictures[i].scaled_for = 0;
    }
    if (preview == ContinuePreview::none)
    {
        panel->fakes.clear();
        panel->order.clear();   // the real ones, from RECENT, on the next update
        panel->games.clear();
        ui_picture_clear(&panel->card_picture.raw);
        for (uint32_t i = 0; i < CONTINUE_VIEW_TILES_MAX; i++)
        {
            ui_picture_clear(&panel->tile_pictures[i].raw);
        }
        return;
    }
    // The fake games, closed a few days back from now.
    SDL_Time now_time = 0;
    SDL_GetCurrentTime(&now_time);
    uint64_t now = static_cast<uint64_t>(now_time / SDL_NS_PER_SECOND);
    panel->fakes.clear();
    for (uint32_t i = 0; i < FAKE_GAME_COUNT; i++)
    {
        const FakeGame* fake = &FAKE_GAMES[i];
        ContinueRecord record = {};
        record.core_path = "/fake/core.so";
        record.content_path = fake->name;
        record.game_name = fake->name;
        record.core_name = fake->core;
        record.aspect_width = fake->aspect_width;
        record.aspect_height = fake->aspect_height;
        record.saved = fake->saved;
        record.closed = now - static_cast<uint64_t>(fake->days_ago) * 86400u - 1800u;
        panel->fakes.push_back(record);
    }
    // The card's game first, the others in the table's order.
    uint32_t card = fake_card_game(preview);
    panel->order.clear();
    panel->order.push_back(card);
    for (uint32_t i = 0; i < FAKE_GAME_COUNT; i++)
    {
        if (i != card)
        {
            panel->order.push_back(i);
        }
    }
}

// ---------------------------------------------------------------------------
// Records and the games in the card and on the shelf

// The record of the game at `position` of the order (nullptr: none). A fake
// game that is "in RECENT with no record" has none either.
static const ContinueRecord* record_at(const ContinuePanel* panel, size_t position)
{
    if (position >= panel->order.size())
    {
        return nullptr;
    }
    uint32_t game = panel->order[position];
    if (panel->preview != ContinuePreview::none)
    {
        return FAKE_GAMES[game].recorded ? &panel->fakes[game] : nullptr;
    }
    const RecentGame* recent = &panel->games[game];
    int32_t found = continue_records_find(&panel->records, recent->core_path.c_str(), recent->content_path.c_str());
    return found >= 0 ? &panel->records[static_cast<size_t>(found)] : nullptr;
}

// The name and core to show for the game at `position`: the record's, or
// what RECENT knows (a file name, the core's file name) without one.
static void names_at(const ContinuePanel* panel, size_t position, const Localization* strings, char* name,
                     size_t name_size, char* core, size_t core_size)
{
    name[0] = '\0';
    core[0] = '\0';
    if (position >= panel->order.size())
    {
        return;
    }
    uint32_t game = panel->order[position];
    if (panel->preview != ContinuePreview::none)
    {
        std::snprintf(name, name_size, "%s", FAKE_GAMES[game].name);
        std::snprintf(core, core_size, "%s", FAKE_GAMES[game].core);
        return;
    }
    char recent_name[CONTINUE_TEXT_MAX] = {};
    char recent_core[CONTINUE_TEXT_MAX] = {};
    recent_games_names(&panel->games[game], piccolo_text(strings, Str::recent_no_game), recent_name,
                       sizeof(recent_name), recent_core, sizeof(recent_core));
    const ContinueRecord* record = record_at(panel, position);
    bool named = record != nullptr && !record->game_name.empty();
    std::snprintf(name, name_size, "%s", named ? record->game_name.c_str() : recent_name);
    bool core_named = record != nullptr && !record->core_name.empty();
    std::snprintf(core, core_size, "%s", core_named ? record->core_name.c_str() : recent_core);
}

void continue_panel_load(ContinuePanel* panel, const char* path)
{
    panel->path = path;
    continue_records_load(&panel->records, path);
    panel->recheck = true;
}

void continue_panel_closed(ContinuePanel* panel, const ContinueRecord* record)
{
    continue_records_put(&panel->records, record);
    panel->recheck = true;
    // The same file name may hold a new picture.
    panel->card_picture.raw_path.clear();
    for (uint32_t i = 0; i < CONTINUE_VIEW_TILES_MAX; i++)
    {
        panel->tile_pictures[i].raw_path.clear();
    }
    if (!panel->path.empty())
    {
        continue_records_save(&panel->records, panel->path.c_str());
    }
}

ContinueChoice continue_panel_choice(const ContinuePanel* panel)
{
    if (panel->preview == ContinuePreview::none)
    {
        return panel->choice;
    }
    return fake_choice(panel->order[0]);
}

const ContinueRecord* continue_panel_record(const ContinuePanel* panel)
{
    static const ContinueRecord none = {};
    const ContinueRecord* record = record_at(panel, 0);
    return record != nullptr ? record : &none;
}

bool continue_panel_card_has_record(const ContinuePanel* panel)
{
    return record_at(panel, 0) != nullptr;
}

const RecentGame* continue_panel_card_game(const ContinuePanel* panel)
{
    if (panel->preview != ContinuePreview::none || panel->order.empty())
    {
        return nullptr;
    }
    return &panel->games[panel->order[0]];
}

// ---------------------------------------------------------------------------
// Pictures

// `picture` follows the file at `path` ("" or nullptr: nothing to show): loaded
// when it's another file or it changed.
static void picture_follow_file(ContinuePicture* picture, const std::string* path)
{
    uint64_t size = 0;
    uint64_t modified = 0;
    bool there = path != nullptr && !path->empty() && host_fs_file_info(path->c_str(), &size, &modified);
    if (!there)
    {
        ui_picture_clear(&picture->raw);
        picture->raw_path.clear();
        return;
    }
    if (*path != picture->raw_path || modified != picture->raw_modified)
    {
        picture->raw_path = *path;
        picture->raw_modified = modified;
        picture->scaled_for = 0;
        if (ui_picture_load_png(&picture->raw, path->c_str(), PICTURE_CAPACITY, "Continue card: picture"))
        {
            picture->raw.modified = 1;
        }
        else
        {
            picture->raw.modified = 0;
        }
    }
}

// The same for a fake game's made-up picture (none: nothing to show).
static void picture_follow_fake(ContinuePicture* picture, uint32_t game)
{
    const FakeGame* fake = &FAKE_GAMES[game];
    if (!fake->recorded || fake->picture_width == 0)
    {
        ui_picture_clear(&picture->raw);
        picture->raw_path.clear();
        return;
    }
    char key[32] = {};
    std::snprintf(key, sizeof(key), "fake:%u", game);
    if (picture->raw_path != key)
    {
        picture->raw_path = key;
        picture->scaled_for = 0;
        fake_picture(&picture->raw, fake->picture_width, fake->picture_height, game);
        picture->raw.modified = 1;
    }
}

// The picture scaled up by the biggest whole number that fits its size on
// screen: drawn bilinear to that size, it's sharp bilinear.
static void picture_scale(ContinuePicture* picture, float width, float height)
{
    if (picture->raw.modified == 0 || picture->raw.width == 0)
    {
        picture->scaled.shown = false;
        return;
    }
    uint32_t scale = ui_picture_whole_scale(picture->raw.width, picture->raw.height, width, height, SCALED_CAPACITY);
    if (scale != picture->scaled_for)
    {
        picture->scaled_for = scale;
        ui_picture_scale_from(&picture->scaled, &picture->raw, scale);
    }
}

// How many tiles the page shown has.
static uint32_t page_tile_count(const ContinuePanel* panel)
{
    size_t first = 1 + static_cast<size_t>(panel->shelf_page) * CONTINUE_SHELF_TILES;
    if (panel->order.size() <= first)
    {
        return 0;
    }
    size_t left = panel->order.size() - first;
    return static_cast<uint32_t>(left < CONTINUE_SHELF_TILES ? left : CONTINUE_SHELF_TILES);
}

// The files and pictures now: the card's choice and picture, and the
// pictures of the shelf page shown (the other pages' are not kept).
static void panel_refresh(ContinuePanel* panel, int32_t tile_scale, uint64_t now_ms)
{
    panel->recheck = false;
    panel->checked_ms = now_ms;
    bool fake = panel->preview != ContinuePreview::none;
    const ContinueRecord* card = record_at(panel, 0);
    panel->card_recorded = card != nullptr;
    if (fake)
    {
        picture_follow_fake(&panel->card_picture, panel->order[0]);
    }
    else if (card == nullptr)
    {
        // In RECENT, no record: RESTART alone, nothing else to say.
        panel->choice = ContinueChoice{};
        if (!panel->order.empty())
        {
            panel->choice.kind = ContinueKind::start;
        }
        picture_follow_file(&panel->card_picture, nullptr);
    }
    else
    {
        ContinueFiles files = {};
        continue_files_read(card, &files);
        panel->choice = continue_card_pick(card, true, &files);
        picture_follow_file(&panel->card_picture, files.picture ? &card->picture_path : nullptr);
    }
    uint32_t shown = tile_scale > 0 ? page_tile_count(panel) : 0;
    for (uint32_t i = 0; i < CONTINUE_VIEW_TILES_MAX; i++)
    {
        ContinuePicture* picture = &panel->tile_pictures[i];
        if (i >= shown)
        {
            ui_picture_clear(&picture->raw);
            picture->raw_path.clear();
            continue;
        }
        size_t position = 1 + static_cast<size_t>(panel->shelf_page) * CONTINUE_SHELF_TILES + i;
        if (fake)
        {
            picture_follow_fake(picture, panel->order[position]);
            continue;
        }
        const ContinueRecord* record = record_at(panel, position);
        picture_follow_file(picture, record != nullptr ? &record->picture_path : nullptr);
    }
}

// RECENT changed (a game played, removed, CLEAR ALL): the order is RECENT's.
static bool games_same(const std::vector<RecentGame>* a, const std::vector<RecentGame>* b)
{
    if (a->size() != b->size())
    {
        return false;
    }
    for (size_t i = 0; i < a->size(); i++)
    {
        if ((*a)[i].core_path != (*b)[i].core_path || (*a)[i].content_path != (*b)[i].content_path)
        {
            return false;
        }
    }
    return true;
}

void continue_panel_update(ContinuePanel* panel, const std::vector<RecentGame>* games, bool wanted,
                           float picture_width, float picture_height, int32_t tile_scale, bool as_list,
                           uint64_t now_ms)
{
    panel->as_list = as_list;
    if (panel->preview == ContinuePreview::none)
    {
        if (!wanted)
        {
            panel->recheck = true;   // looked at again as soon as it may show
            return;
        }
        if (!games_same(games, &panel->games))
        {
            panel->games = *games;
            continue_order_reset(&panel->order, panel->games.size());
            panel->shelf_page = 0;
            panel->focus_tile = -1;
            panel->focus_second = false;
            panel->recheck = true;
        }
    }
    if (panel->recheck || (panel->preview == ContinuePreview::none && now_ms - panel->checked_ms >= CHECK_EVERY_MS))
    {
        panel_refresh(panel, tile_scale, now_ms);
    }
    picture_scale(&panel->card_picture, picture_width, picture_height);
    for (uint32_t i = 0; i < CONTINUE_VIEW_TILES_MAX; i++)
    {
        float width = 0.0f;
        float height = 0.0f;
        size_t position = 1 + static_cast<size_t>(panel->shelf_page) * CONTINUE_SHELF_TILES + i;
        const ContinueRecord* record = tile_scale > 0 ? record_at(panel, position) : nullptr;
        uint32_t aspect_width = record != nullptr ? record->aspect_width : 0;
        uint32_t aspect_height = record != nullptr ? record->aspect_height : 0;
        if (panel->preview != ContinuePreview::none && position < panel->order.size())
        {
            aspect_width = panel->fakes[panel->order[position]].aspect_width;
            aspect_height = panel->fakes[panel->order[position]].aspect_height;
        }
        continue_view_tile_picture_size(aspect_width, aspect_height, tile_scale > 0 ? tile_scale : 1, &width, &height);
        picture_scale(&panel->tile_pictures[i], width, height);
    }
}

// ---------------------------------------------------------------------------
// Focus: the keys on the panel

void continue_panel_buttons(ContinueKind kind, ContinueViewButton* first, ContinueViewButton* second)
{
    *first = ContinueViewButton::resume;
    *second = ContinueViewButton::start;   // RESTART beside RESUME
    switch (kind)
    {
    case ContinueKind::none:
    case ContinueKind::resume:
        break;
    case ContinueKind::start:
        *first = ContinueViewButton::start;
        *second = ContinueViewButton::none;
        break;
    case ContinueKind::start_or_resume:
        *first = ContinueViewButton::start;
        *second = ContinueViewButton::resume;
        break;
    }
}

ContinueViewButton continue_panel_focused_button(const ContinuePanel* panel)
{
    if (panel->focus_tile >= 0)
    {
        return ContinueViewButton::none;
    }
    ContinueViewButton first = ContinueViewButton::none;
    ContinueViewButton second = ContinueViewButton::none;
    continue_panel_buttons(continue_panel_choice(panel).kind, &first, &second);
    if (panel->focus_second && second != ContinueViewButton::none)
    {
        return second;
    }
    return first;
}

// Along the page's tiles or rows: `forward` the next one, over the page's end
// to the next page; `back` the other way, a full page before.
static void focus_step(ContinuePanel* panel, bool forward)
{
    uint32_t tiles = page_tile_count(panel);
    uint32_t pages = continue_shelf_pages(panel->order.size());
    uint32_t tile = static_cast<uint32_t>(panel->focus_tile);
    if (forward && tile + 1 < tiles)
    {
        panel->focus_tile = static_cast<int32_t>(tile + 1);
    }
    else if (forward && panel->shelf_page + 1 < pages)
    {
        panel->shelf_page++;
        panel->focus_tile = 0;
        panel->recheck = true;
    }
    else if (!forward && tile > 0)
    {
        panel->focus_tile = static_cast<int32_t>(tile - 1);
    }
    else if (!forward && panel->shelf_page > 0)
    {
        panel->shelf_page--;
        panel->focus_tile = static_cast<int32_t>(CONTINUE_SHELF_TILES - 1);   // (a page back is a full one)
        panel->recheck = true;
    }
}

void continue_panel_key(ContinuePanel* panel, ContinueKey key)
{
    uint32_t tiles = page_tile_count(panel);
    ContinueViewButton first = ContinueViewButton::none;
    ContinueViewButton second = ContinueViewButton::none;
    continue_panel_buttons(continue_panel_choice(panel).kind, &first, &second);
    bool two = second != ContinueViewButton::none;
    if (panel->focus_tile < 0)
    {
        if (key == ContinueKey::right && two && !panel->focus_second)
        {
            panel->focus_second = true;
        }
        else if (key == ContinueKey::right && panel->as_list && tiles > 0)
        {
            panel->focus_tile = 0;   // past the last button: into the list
        }
        else if (key == ContinueKey::left)
        {
            panel->focus_second = false;
        }
        else if (key == ContinueKey::down && !panel->as_list && tiles > 0)
        {
            panel->focus_tile = 0;
        }
        return;
    }
    if (panel->as_list)
    {
        if (key == ContinueKey::left)
        {
            panel->focus_tile = -1;
            panel->focus_second = two;   // back where it came from: the last button
        }
        else if (key == ContinueKey::down || key == ContinueKey::up)
        {
            focus_step(panel, key == ContinueKey::down);
        }
        return;
    }
    if (key == ContinueKey::up)
    {
        panel->focus_tile = -1;
        panel->focus_second = false;
    }
    else if (key == ContinueKey::right || key == ContinueKey::left)
    {
        focus_step(panel, key == ContinueKey::right);
    }
}

void continue_panel_choose(ContinuePanel* panel, uint32_t tile)
{
    if (tile >= page_tile_count(panel))
    {
        return;
    }
    size_t position = 1 + static_cast<size_t>(panel->shelf_page) * CONTINUE_SHELF_TILES + tile;
    continue_order_select(&panel->order, position);
    panel->shelf_page = 0;   // the card's old game is the first tile of the first page
    panel->focus_tile = -1;
    panel->focus_second = false;
    panel->recheck = true;
}

void continue_panel_page(ContinuePanel* panel, int32_t delta)
{
    uint32_t pages = continue_shelf_pages(panel->order.size());
    int32_t page = static_cast<int32_t>(panel->shelf_page) + delta;
    if (page < 0 || page >= static_cast<int32_t>(pages))
    {
        return;
    }
    panel->shelf_page = static_cast<uint32_t>(page);
    if (panel->focus_tile >= 0)
    {
        uint32_t tiles = page_tile_count(panel);
        panel->focus_tile = tiles > 0 && static_cast<uint32_t>(panel->focus_tile) >= tiles
                                ? static_cast<int32_t>(tiles - 1) : panel->focus_tile;
    }
    panel->recheck = true;
}

// ---------------------------------------------------------------------------
// The card's words

static Str weekday_text(int32_t weekday)
{
    switch (weekday)
    {
    case 1:
        return Str::continue_monday;
    case 2:
        return Str::continue_tuesday;
    case 3:
        return Str::continue_wednesday;
    case 4:
        return Str::continue_thursday;
    case 5:
        return Str::continue_friday;
    case 6:
        return Str::continue_saturday;
    default:
        break;
    }
    return Str::continue_sunday;
}

// When the record was closed, as a local date (false: no date known).
static bool played_date(const ContinueRecord* record, ContinueDate* played)
{
    if (record == nullptr || record->closed == 0)
    {
        return false;
    }
    SDL_DateTime date = {};
    SDL_TimeToDateTime(static_cast<SDL_Time>(record->closed) * SDL_NS_PER_SECOND, &date, true);
    *played = ContinueDate{};
    played->year = date.year;
    played->month = date.month;
    played->day = date.day;
    played->weekday = date.day_of_week;
    played->hour = date.hour;
    played->minute = date.minute;
    return true;
}

// "PLAYED TODAY AT 21:40", "PLAYED ON MONDAY AT 18:20", "PLAYED ON 2026-11-02".
static void when_text(char* out, size_t out_size, const ContinueRecord* record, const Localization* strings,
                      const ContinueDate* now)
{
    ContinueDate played = {};
    if (!played_date(record, &played))
    {
        out[0] = '\0';
        return;
    }
    switch (continue_card_when(&played, now))
    {
    case ContinueWhen::today:
        std::snprintf(out, out_size, "%s %02d:%02d", piccolo_text(strings, Str::continue_played_today), played.hour,
                      played.minute);
        break;
    case ContinueWhen::yesterday:
        std::snprintf(out, out_size, "%s %02d:%02d", piccolo_text(strings, Str::continue_played_yesterday),
                      played.hour, played.minute);
        break;
    case ContinueWhen::this_week:
        std::snprintf(out, out_size, "%s %s %s %02d:%02d", piccolo_text(strings, Str::continue_played_on),
                      piccolo_text(strings, weekday_text(played.weekday)), piccolo_text(strings, Str::continue_at),
                      played.hour, played.minute);
        break;
    case ContinueWhen::earlier:
        std::snprintf(out, out_size, "%s %04d-%02d-%02d", piccolo_text(strings, Str::continue_played_on), played.year,
                      played.month, played.day);
        break;
    }
}

// A tile's "when": TODAY / YESTERDAY / MONDAY / 2026-11-02 ("" without a date).
static void short_when_text(char* out, size_t out_size, const ContinueRecord* record, const Localization* strings,
                            const ContinueDate* now)
{
    ContinueDate played = {};
    if (!played_date(record, &played))
    {
        out[0] = '\0';
        return;
    }
    switch (continue_card_when(&played, now))
    {
    case ContinueWhen::today:
        std::snprintf(out, out_size, "%s", piccolo_text(strings, Str::continue_short_today));
        break;
    case ContinueWhen::yesterday:
        std::snprintf(out, out_size, "%s", piccolo_text(strings, Str::continue_short_yesterday));
        break;
    case ContinueWhen::this_week:
        std::snprintf(out, out_size, "%s", piccolo_text(strings, weekday_text(played.weekday)));
        break;
    case ContinueWhen::earlier:
        std::snprintf(out, out_size, "%04d-%02d-%02d", played.year, played.month, played.day);
        break;
    }
}

static const char* reason_text(ContinueReason reason, const Localization* strings)
{
    switch (reason)
    {
    case ContinueReason::none:
        return "";
    case ContinueReason::no_states:
        return piccolo_text(strings, Str::continue_why_no_states);
    case ContinueReason::incomplete:
        return piccolo_text(strings, Str::continue_why_incomplete);
    case ContinueReason::off:
        return piccolo_text(strings, Str::continue_why_off);
    case ContinueReason::state_missing:
        return piccolo_text(strings, Str::continue_why_missing);
    case ContinueReason::newer_save:
        return piccolo_text(strings, Str::continue_why_newer_save);
    }
    return "";
}

// "A / ENTER: RESUME · v: OTHER GAMES" on a button, "A / ENTER: SHOW <NAME>
// HERE · ^: BACK" on a tile (the pieces are separate strings: a language
// puts the name where it needs it).
static void hint_text(ContinuePanelView* view, const ContinuePanel* panel, const Localization* strings,
                      const char* focused_label, const char* focused_name)
{
    bool list = panel->as_list;
    const char* games = piccolo_text(strings, list ? Str::continue_hint_games_list : Str::continue_hint_games);
    const char* back = piccolo_text(strings, list ? Str::continue_hint_back_list : Str::continue_hint_back);
    const char* enter = piccolo_text(strings, Str::continue_hint_enter);
    if (panel->focus_tile >= 0)
    {
        std::snprintf(view->hint, sizeof(view->hint), "%s %s %s %s · %s", enter,
                      piccolo_text(strings, Str::continue_hint_show), focused_name,
                      piccolo_text(strings, Str::continue_hint_here), back);
    }
    else if (view->card.shelf.tile_count > 0)
    {
        std::snprintf(view->hint, sizeof(view->hint), "%s %s · %s", enter, focused_label, games);
    }
    else
    {
        std::snprintf(view->hint, sizeof(view->hint), "%s %s", enter, focused_label);
    }
    view->card.hint = view->hint;
}

void continue_panel_state(ContinuePanelView* view, const ContinuePanel* panel, const Localization* strings,
                          const ContinueDate* now, bool active, bool phone)
{
    const ContinueRecord* record = record_at(panel, 0);
    ContinueChoice choice = continue_panel_choice(panel);
    ContinueViewCard* card = &view->card;
    *card = ContinueViewCard{};
    view->phone = panel->preview == ContinuePreview::phone;
    names_at(panel, 0, strings, view->game, sizeof(view->game), view->core, sizeof(view->core));
    when_text(view->when, sizeof(view->when), record, strings, now);
    card->game = view->game;
    card->core = view->core;
    card->when = view->when;
    // The desktop says why only when it matters: a newer save file. Nothing
    // to resume is just START (the phone still says why).
    bool say_why = phone || view->phone || choice.reason == ContinueReason::newer_save;
    card->reason = say_why ? reason_text(choice.reason, strings) : "";
    const char* resume = piccolo_text(strings, Str::continue_resume);
    const char* start = piccolo_text(strings, Str::continue_start);
    const char* restart = piccolo_text(strings, Str::continue_restart);
    bool beside = !phone && !view->phone;   // RESTART beside RESUME is the desktop's
    switch (choice.kind)
    {
    case ContinueKind::none:
    case ContinueKind::resume:
        card->first_label = resume;
        if (beside)
        {
            card->second_label = restart;
        }
        break;
    case ContinueKind::start:
        card->first_label = start;
        break;
    case ContinueKind::start_or_resume:
        card->first_label = beside ? restart : start;
        card->second_label = resume;
        break;
    }
    continue_panel_buttons(choice.kind, &card->first, &card->second);
    if (card->second_label[0] == '\0')
    {
        card->second = ContinueViewButton::none;
    }
    card->aspect_width = record != nullptr ? record->aspect_width : 0;
    card->aspect_height = record != nullptr ? record->aspect_height : 0;
    if (panel->card_picture.scaled.shown)
    {
        card->texture = panel->card_picture.scaled.texture.ui_texture;
        video_texture_uv_max(&panel->card_picture.scaled.texture, &card->u_max, &card->v_max);
    }
    card->active = active;
    if (phone || view->phone)
    {
        return;
    }

    // The shelf: the page shown.
    ContinueViewShelf* shelf = &card->shelf;
    shelf->header = piccolo_text(strings, Str::continue_recent);
    shelf->tile_count = page_tile_count(panel);
    shelf->page = panel->shelf_page;
    shelf->page_count = continue_shelf_pages(panel->order.size());
    for (uint32_t i = 0; i < shelf->tile_count; i++)
    {
        size_t position = 1 + static_cast<size_t>(panel->shelf_page) * CONTINUE_SHELF_TILES + i;
        const ContinueRecord* tile_record = record_at(panel, position);
        char short_when[CONTINUE_TEXT_MAX] = {};
        names_at(panel, position, strings, view->tile_game[i], CONTINUE_TEXT_MAX, view->tile_core[i],
                 CONTINUE_TEXT_MAX);
        short_when_text(short_when, sizeof(short_when), tile_record, strings, now);
        if (short_when[0] != '\0')
        {
            std::snprintf(view->tile_line[i], CONTINUE_TEXT_MAX, "%.140s · %.140s", view->tile_core[i], short_when);
        }
        else
        {
            std::snprintf(view->tile_line[i], CONTINUE_TEXT_MAX, "%s", view->tile_core[i]);
        }
        ContinueViewTile* tile = &shelf->tiles[i];
        tile->game = view->tile_game[i];
        tile->line = view->tile_line[i];
        tile->core = view->tile_core[i];
        tile->aspect_width = tile_record != nullptr ? tile_record->aspect_width : 0;
        tile->aspect_height = tile_record != nullptr ? tile_record->aspect_height : 0;
        const ContinuePicture* picture = &panel->tile_pictures[i];
        if (picture->scaled.shown && picture->raw.modified != 0)
        {
            tile->texture = picture->scaled.texture.ui_texture;
            video_texture_uv_max(&picture->scaled.texture, &tile->u_max, &tile->v_max);
        }
    }
    card->focus_tile = panel->focus_tile;
    card->focus_second = panel->focus_second && card->second_label[0] != '\0';
    const char* focused = card->focus_second ? card->second_label : card->first_label;
    const char* tile_name = "";
    if (panel->focus_tile >= 0 && static_cast<uint32_t>(panel->focus_tile) < shelf->tile_count)
    {
        tile_name = shelf->tiles[panel->focus_tile].game;
    }
    hint_text(view, panel, strings, focused, tile_name);
}

// ---------------------------------------------------------------------------
// Upload

static ContinuePicture* picture_slot(ContinuePanel* panel, uint32_t index)
{
    return index == 0 ? &panel->card_picture : &panel->tile_pictures[index - 1];
}

void continue_panel_upload(ContinuePanel* panel, AppShell* shell)
{
    constexpr uint32_t SLOTS = 1 + CONTINUE_VIEW_TILES_MAX;
    bool pending = false;
    for (uint32_t i = 0; i < SLOTS; i++)
    {
        UiPicture* scaled = &picture_slot(panel, i)->scaled;
        if (scaled->upload_pending || scaled->replace_wait)
        {
            ui_picture_prepare(scaled, shell);
            pending = true;
        }
    }
    if (!pending || !app_shell_acquire(shell))
    {
        return;   // try on the next frame
    }
    for (uint32_t i = 0; i < SLOTS; i++)
    {
        ui_picture_upload(&picture_slot(panel, i)->scaled, shell);
    }
}

void continue_panel_destroy(ContinuePanel* panel, AppShell* shell)
{
    constexpr uint32_t SLOTS = 1 + CONTINUE_VIEW_TILES_MAX;
    for (uint32_t i = 0; i < SLOTS; i++)
    {
        ui_picture_destroy(&picture_slot(panel, i)->scaled, shell);
        ui_picture_destroy(&picture_slot(panel, i)->raw, shell);   // (never uploaded: nothing to wait for)
    }
}
