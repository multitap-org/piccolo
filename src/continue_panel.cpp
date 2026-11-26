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

void continue_panel_load(ContinuePanel* panel, const char* path)
{
    panel->path = path;
    panel->record_loaded = continue_record_load(&panel->record, path);
    panel->recheck = true;
}

void continue_panel_closed(ContinuePanel* panel, const ContinueRecord* record)
{
    panel->record = *record;
    panel->record_loaded = true;
    panel->recheck = true;
    panel->raw_path.clear();   // the same file name may hold a new picture
    if (!panel->path.empty())
    {
        continue_record_save(record, panel->path.c_str());
    }
}

// ---------------------------------------------------------------------------
// The debug window's fake cards

static const ContinueRecord* fake_record(ContinuePreview preview)
{
    static ContinueRecord resume = {};
    static ContinueRecord no_states = {};
    static ContinueRecord newer_save = {};
    resume.game_name = "Super Mario World (USA)";
    resume.core_name = "Snes9x";
    resume.system_name = "Super Nintendo";
    resume.aspect_width = 292;   // 256 x 224 at 8:7 pixels, as IMAGE SETTINGS' CORE aspect shows it
    resume.aspect_height = 224;
    no_states = resume;
    no_states.game_name = "Quake";
    no_states.core_name = "TyrQuake";
    no_states.system_name = "PC";
    no_states.aspect_width = 4;
    no_states.aspect_height = 3;
    no_states.saved = ContinueSaved::no_states;
    newer_save = resume;
    newer_save.game_name = "Pokemon - Crystal Version (USA)";
    newer_save.core_name = "SameBoy";
    newer_save.system_name = "Game Boy Color";
    newer_save.aspect_width = 160;
    newer_save.aspect_height = 144;
    switch (preview)
    {
    case ContinuePreview::no_states:
        return &no_states;
    case ContinuePreview::newer_save:
        return &newer_save;
    case ContinuePreview::resume:
    case ContinuePreview::phone:
    case ContinuePreview::none:
        break;
    }
    return &resume;
}

// A made-up frame for a fake card: sky, hills, ground and a few blocks, in
// the game's own resolution, so its pixels show how sharp the card draws.
static void fake_picture(UiPicture* picture, uint32_t width, uint32_t height)
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
            picture->pixels[static_cast<size_t>(y) * width + x] = color;
        }
    }
    ui_picture_changed(picture);
}

void continue_panel_preview(ContinuePanel* panel, ContinuePreview preview)
{
    panel->preview = preview;
    panel->raw_path.clear();
    panel->scaled_for = 0;
    if (preview == ContinuePreview::none)
    {
        ui_picture_clear(&panel->raw);
        panel->recheck = true;
        return;
    }
    if (preview == ContinuePreview::newer_save)
    {
        fake_picture(&panel->raw, 160, 144);
    }
    else if (preview == ContinuePreview::no_states)
    {
        fake_picture(&panel->raw, 320, 240);
    }
    else
    {
        fake_picture(&panel->raw, 256, 224);
    }
    panel->raw.modified = 1;
}

ContinueChoice continue_panel_choice(const ContinuePanel* panel)
{
    ContinueChoice choice = {};
    switch (panel->preview)
    {
    case ContinuePreview::none:
        return panel->choice;
    case ContinuePreview::resume:
    case ContinuePreview::phone:
        choice.kind = ContinueKind::resume;
        break;
    case ContinuePreview::no_states:
        choice.kind = ContinueKind::start;
        choice.reason = ContinueReason::no_states;
        break;
    case ContinuePreview::newer_save:
        choice.kind = ContinueKind::start_or_resume;
        choice.reason = ContinueReason::newer_save;
        break;
    }
    return choice;
}

const ContinueRecord* continue_panel_record(const ContinuePanel* panel)
{
    return panel->preview != ContinuePreview::none ? fake_record(panel->preview) : &panel->record;
}

// ---------------------------------------------------------------------------
// Update

void continue_panel_update(ContinuePanel* panel, const RecentGame* top, bool wanted, float picture_width,
                           float picture_height, uint64_t now_ms)
{
    if (panel->preview == ContinuePreview::none)
    {
        if (!wanted)
        {
            panel->recheck = true;   // looked at again as soon as it may show
            return;
        }
        if (panel->recheck || now_ms - panel->checked_ms >= CHECK_EVERY_MS)
        {
            panel->recheck = false;
            panel->checked_ms = now_ms;
            ContinueFiles files = {};
            bool matches = panel->record_loaded && continue_record_matches(&panel->record, top);
            if (matches)
            {
                continue_files_read(&panel->record, &files);
            }
            panel->choice = continue_card_pick(&panel->record, matches, &files);
            // The picture, when it's another file or it changed.
            uint64_t size = 0;
            uint64_t modified = 0;
            const std::string* path = &panel->record.picture_path;
            bool there = matches && files.picture && host_fs_file_info(path->c_str(), &size, &modified);
            if (!there)
            {
                ui_picture_clear(&panel->raw);
                panel->raw_path.clear();
            }
            else if (*path != panel->raw_path || modified != panel->raw_modified)
            {
                panel->raw_path = *path;
                panel->raw_modified = modified;
                panel->scaled_for = 0;
                if (ui_picture_load_png(&panel->raw, path->c_str(), PICTURE_CAPACITY, "Continue card: picture"))
                {
                    panel->raw.modified = 1;
                }
                else
                {
                    panel->raw.modified = 0;
                }
            }
        }
    }
    // The picture scaled up by the biggest whole number that fits its size
    // on screen: drawn bilinear to that size, it's sharp bilinear.
    if (panel->raw.modified == 0 || panel->raw.width == 0)
    {
        panel->scaled.shown = false;
        return;
    }
    uint32_t scale = ui_picture_whole_scale(panel->raw.width, panel->raw.height, picture_width, picture_height,
                                            SCALED_CAPACITY);
    if (scale != panel->scaled_for)
    {
        panel->scaled_for = scale;
        ui_picture_scale_from(&panel->scaled, &panel->raw, scale);
    }
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

// "PLAYED TODAY AT 21:40", "PLAYED ON MONDAY AT 18:20", "PLAYED ON 2026-11-02".
static void when_text(char* out, size_t out_size, const ContinueRecord* record, const Localization* strings,
                      const ContinueDate* now, bool fake)
{
    ContinueDate played = *now;
    if (!fake)
    {
        if (record->closed == 0)
        {
            out[0] = '\0';
            return;
        }
        SDL_DateTime date = {};
        SDL_TimeToDateTime(static_cast<SDL_Time>(record->closed) * SDL_NS_PER_SECOND, &date, true);
        played.year = date.year;
        played.month = date.month;
        played.day = date.day;
        played.weekday = date.day_of_week;
        played.hour = date.hour;
        played.minute = date.minute;
    }
    else
    {
        played.hour = 21;
        played.minute = 40;
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

void continue_panel_state(ContinuePanelView* view, const ContinuePanel* panel, const Localization* strings,
                          const ContinueDate* now, bool active)
{
    const ContinueRecord* record = continue_panel_record(panel);
    ContinueChoice choice = continue_panel_choice(panel);
    ContinueViewCard* card = &view->card;
    *card = ContinueViewCard{};
    view->phone = panel->preview == ContinuePreview::phone;
    std::snprintf(view->game, sizeof(view->game), "%s", record->game_name.c_str());
    std::snprintf(view->core, sizeof(view->core), "%s", record->core_name.c_str());
    std::snprintf(view->system, sizeof(view->system), "%s", record->system_name.c_str());
    when_text(view->when, sizeof(view->when), record, strings, now, panel->preview != ContinuePreview::none);
    card->game = view->game;
    card->core = view->core;
    card->system = view->system;
    card->when = view->when;
    card->reason = reason_text(choice.reason, strings);
    const char* resume = piccolo_text(strings, Str::continue_resume);
    const char* start = piccolo_text(strings, Str::continue_start);
    switch (choice.kind)
    {
    case ContinueKind::none:
    case ContinueKind::resume:
        card->first_label = resume;
        card->first = ContinueViewButton::resume;
        card->hint = piccolo_text(strings, Str::continue_hint);
        break;
    case ContinueKind::start:
        card->first_label = start;
        card->first = ContinueViewButton::start;
        break;
    case ContinueKind::start_or_resume:
        card->first_label = start;
        card->first = ContinueViewButton::start;
        card->second_label = resume;
        card->second = ContinueViewButton::resume;
        break;
    }
    card->aspect_width = record->aspect_width;
    card->aspect_height = record->aspect_height;
    if (panel->scaled.shown)
    {
        card->texture = panel->scaled.texture.ui_texture;
        video_texture_uv_max(&panel->scaled.texture, &card->u_max, &card->v_max);
    }
    card->active = active;
}

// ---------------------------------------------------------------------------
// Upload

void continue_panel_upload(ContinuePanel* panel, AppShell* shell)
{
    if (!panel->scaled.upload_pending && !panel->scaled.replace_wait)
    {
        return;
    }
    ui_picture_prepare(&panel->scaled, shell);
    if (!app_shell_acquire(shell))
    {
        return;   // try on the next frame
    }
    ui_picture_upload(&panel->scaled, shell);
}

void continue_panel_destroy(ContinuePanel* panel, AppShell* shell)
{
    ui_picture_destroy(&panel->scaled, shell);
    ui_picture_destroy(&panel->raw, shell);   // (never uploaded: nothing to wait for)
}
