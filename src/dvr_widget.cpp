#include "dvr_widget.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

#include "core_session.h"
#include "hud_view.h"
#include "imgui.h"   // the display's size, for the phone's panel (drawing only)
#include "input_bindings.h"
#include "piccolo_settings.h"
#include "piccolo_text.h"
#include "retro_ui_controls.h"
#include "settings.h"
#include "ui_toolkit.h"

// ---------------------------------------------------------------------------
// State (no GUI): the history, its controls, the thumbnails and the view
// ---------------------------------------------------------------------------

// Previews every 5 s in SCRUB (Andrés: "se ven frames cada 5 segundos").
constexpr double PREVIEW_SECONDS = 5.0;
// LEFT / RIGHT (Andrés, 2026-11-22). SCRUB: held this long, 2x while held.
// SNAPSHOTS: held, one more snapshot every SNAPSHOT_REPEAT_MS.
constexpr uint64_t SCRUB_FAST_AFTER_MS = 300;
constexpr double   SCRUB_FAST_SPEED    = 2.0;
constexpr int32_t  SCRUB_MAX_STEPS     = 8;      // per loop (a long loop doesn't jump far)
constexpr uint64_t SNAPSHOT_REPEAT_MS  = 1000;
constexpr uint64_t NO_FRAME   = ~static_cast<uint64_t>(0);
// A thumbnail that took longer than this waits THUMB_SPACING times its cost
// before the next one (at most a quarter of the time on thumbnails).
constexpr uint64_t THUMB_SLOW_NS = 5000000;
constexpr uint64_t THUMB_SPACING = 3;

static double fps_of(const DvrWidget* widget)
{
    return widget->rewind.fps > 1.0 ? widget->rewind.fps : 60.0;
}

// "-1:05" (or "-1:05.3" with tenths) for `frames` before the present.
static void format_back(char* out, size_t size, uint64_t frames, double fps, bool tenths)
{
    double seconds = static_cast<double>(frames) / fps;
    uint32_t whole = static_cast<uint32_t>(seconds);
    if (tenths)
    {
        uint32_t tenth = static_cast<uint32_t>((seconds - static_cast<double>(whole)) * 10.0);
        std::snprintf(out, size, "-%u:%02u.%u", whole / 60, whole % 60, tenth);
    }
    else
    {
        std::snprintf(out, size, "-%u:%02u", whole / 60, whole % 60);
    }
}

static RewindMode mode_from(const char* value)
{
    if (value == nullptr)
    {
        return RewindMode::automatic;
    }
    if (std::strcmp(value, "off") == 0)
    {
        return RewindMode::off;
    }
    if (std::strcmp(value, "scrub") == 0)
    {
        return RewindMode::scrub;
    }
    if (std::strcmp(value, "snapshots") == 0)
    {
        return RewindMode::snapshots;
    }
    return RewindMode::automatic;
}

static uint32_t number_of(const Settings* settings, const char* key)
{
    const char* value = settings_get(settings, key, nullptr);
    return value != nullptr ? static_cast<uint32_t>(std::strtoul(value, nullptr, 10)) : 0;
}

void dvr_widget_game_started(DvrWidget* widget, CoreSession* session, const Settings* settings)
{
    CoreRewindSettings* config = &widget->rewind.settings;
    config->mode             = mode_from(settings_get(settings, PICCOLO_REWIND, nullptr));
    config->known            = mode_from(settings_get_layer(settings, SettingLayer::game, PICCOLO_REWIND_KNOWN));
    // LENGTH for both modes; SNAPSHOTS keeps as many as LENGTH needs at its frequency.
    uint32_t length_seconds  = number_of(settings, PICCOLO_REWIND_LENGTH) * 60;
    uint32_t frequency       = number_of(settings, PICCOLO_REWIND_SNAPSHOT_FREQUENCY);
    frequency                = frequency > 0 ? frequency : 1;
    config->scrub_seconds    = length_seconds;
    config->snapshot_seconds = frequency;
    config->snapshots_kept   = length_seconds / frequency > 0 ? length_seconds / frequency : 1;
    if (config->known == RewindMode::off)
    {
        config->known = RewindMode::automatic;
    }
#if defined(SDL_PLATFORM_EMSCRIPTEN)
    // The browser: AUTO is SNAPSHOTS, never measured. The core, the page and
    // everything else share one thread there, and SCRUB's state every frame
    // was too much on a phone (Andrés, 2026-11-21).
    if (config->mode == RewindMode::automatic)
    {
        config->mode = RewindMode::snapshots;
    }
#endif

    uint64_t megabytes = number_of(settings, PICCOLO_REWIND_MEMORY);
    if (megabytes == 0)   // AUTO
    {
#if defined(SDL_PLATFORM_ANDROID)
        megabytes = 128;
#elif defined(SDL_PLATFORM_EMSCRIPTEN)
        megabytes = 32;   // the whole page has 256 MiB, games and saves included (docs/wasm.md)
#else
        megabytes = 256;
#endif
    }
    // Never more than a quarter of the device's memory (Andrés: "el techo
    // que el dispositivo permita"). After a PLAY FROM HERE the kept lane
    // takes as much again (its own history, the same size).
    int ram = SDL_GetSystemRAM();
    if (ram > 0 && megabytes > static_cast<uint64_t>(ram) / 4)
    {
        megabytes = static_cast<uint64_t>(ram) / 4;
    }
    config->memory_bytes = static_cast<size_t>(megabytes) << 20;

    session->rewind = &widget->rewind;
    if (core_session_running(session))
    {
        core_rewind_start(&widget->rewind, session->host, session->host->av_info.timing.fps);
    }
}

bool dvr_widget_take_verdict(DvrWidget* widget, RewindMode* known)
{
    if (!widget->rewind.verdict)
    {
        return false;
    }
    widget->rewind.verdict = false;
    *known = widget->rewind.state == CoreRewindState::scrub ? RewindMode::scrub : RewindMode::snapshots;
    return true;
}

static void set_hud(DvrWidget* widget, const char* text, uint64_t now_ms)
{
    std::snprintf(widget->hud, sizeof(widget->hud), "%s", text);
    widget->hud_since_ms = now_ms;
}

// The previews on the cursor's lane: every snapshot, or (SCRUB) the first
// state of each 5 s.
static void build_previews(DvrWidget* widget)
{
    const RewindBuffer* buffer = core_rewind_dvr_buffer(&widget->rewind);
    bool scrub = widget->rewind.state != CoreRewindState::snapshots;
    widget->preview_frames = scrub ? static_cast<uint64_t>(PREVIEW_SECONDS * fps_of(widget))
                                   : widget->rewind.state_every;
    if (widget->preview_frames == 0)
    {
        widget->preview_frames = 1;
    }
    widget->previews.clear();
    uint64_t next = 0;
    for (uint32_t i = 0; i < buffer->count; i++)
    {
        const RewindRecord* record = rewind_buffer_record(buffer, i);
        if (record->kind != RewindRecordKind::state || (scrub && record->frame < next))
        {
            continue;
        }
        widget->previews.push_back(record->frame);
        next = (record->frame / widget->preview_frames + 1) * widget->preview_frames;
    }
}

static void play_from_here(DvrWidget* widget, uint64_t now_ms)
{
    core_rewind_dvr_resume_here(&widget->rewind);
    widget->open = false;
    widget->hud_since_ms = 0;
    widget->played_from_ms = now_ms == 0 ? 1 : now_ms;   // the PLAY state icon
}

static bool pressed(const InputFrame* frame, InputAction action)
{
    return frame->pressed[static_cast<uint32_t>(action)];
}

static bool held(const InputFrame* frame, InputAction action)
{
    return frame->held[static_cast<uint32_t>(action)];
}

static uint64_t cursor_of(const DvrWidget* widget)
{
    CoreRewindPosition position = {};
    core_rewind_dvr_position(&widget->rewind, &position);
    return position.cursor;
}

// The newest frame of the cursor's lane (times are counted back from it).
static uint64_t lane_end(const CoreRewindPosition* position)
{
    return position->on_kept ? position->kept_newest : position->newest;
}

// The bounce at an end: a fresh press starts it again; a held repeat only
// once the last one is over.
static void bump(DvrWidget* widget, int32_t direction, uint64_t now_ms, bool repeat)
{
    uint64_t length_ms = static_cast<uint64_t>(DVR_VIEW_BUMP_SECONDS * 1000.0f);
    if (repeat && widget->bump_ms != 0 && now_ms - widget->bump_ms < length_ms)
    {
        return;
    }
    widget->bump_direction = direction;
    widget->bump_ms = now_ms == 0 ? 1 : now_ms;
}

// L / R: 5 s (SCRUB), or a snapshot (SNAPSHOTS: the previews are the
// snapshots). Past an end it lands on the end, and bounces.
static void jump(DvrWidget* widget, int32_t direction, uint64_t now_ms)
{
    CoreRewindPosition position = {};
    core_rewind_dvr_position(&widget->rewind, &position);
    uint64_t from = position.cursor;
    uint64_t target = from;
    if (direction < 0)
    {
        target = target > widget->preview_frames ? target - widget->preview_frames : 0;
    }
    else
    {
        target += widget->preview_frames;
    }
    bool edge = false;
    if (widget->rewind.state == CoreRewindState::snapshots)
    {
        edge = !core_rewind_dvr_step(&widget->rewind, direction);
    }
    else
    {
        core_rewind_dvr_seek(&widget->rewind, target);
        uint64_t every = widget->rewind.state_every > 0 ? widget->rewind.state_every : 1;
        edge = direction < 0 ? target <= position.oldest : target + every >= lane_end(&position);
    }
    uint64_t to = cursor_of(widget);
    edge = edge || to == from;
    if (edge)
    {
        bump(widget, direction, now_ms, false);
    }
    SDL_Log("DVR: %s, frame %llu -> %llu%s", direction < 0 ? "L" : "R", static_cast<unsigned long long>(from),
            static_cast<unsigned long long>(to), edge ? (direction < 0 ? " (the oldest)" : " (the newest)") : "");
}

// LEFT / RIGHT (and the touch arrows): a step; at an end, the bounce.
static void step(DvrWidget* widget, int32_t steps, uint64_t now_ms, bool repeat)
{
    uint64_t from = cursor_of(widget);
    bool moved = core_rewind_dvr_step(&widget->rewind, steps);
    if (!moved)
    {
        bump(widget, steps < 0 ? -1 : 1, now_ms, repeat);
    }
    if (!repeat)
    {
        SDL_Log("DVR: %s, frame %llu -> %llu%s", steps < 0 ? "LEFT" : "RIGHT", static_cast<unsigned long long>(from),
                static_cast<unsigned long long>(cursor_of(widget)),
                moved ? "" : (steps < 0 ? " (the oldest)" : " (the newest)"));
    }
}

static bool scrubbing(const DvrWidget* widget)
{
    return widget->rewind.state != CoreRewindState::snapshots;
}

// SCRUB: plays toward `direction` (-1, 0 = still, 1) at 1x from now on.
static void set_play_direction(DvrWidget* widget, int32_t direction, uint64_t now_ms)
{
    if (direction != widget->play_direction)
    {
        SDL_Log("DVR: %s, frame %llu", direction < 0 ? "PLAY BACK" : direction > 0 ? "PLAY" : "STILL",
                static_cast<unsigned long long>(cursor_of(widget)));
    }
    widget->play_direction = direction;
    widget->play_frames = 0.0;
    widget->play_last_ms = now_ms;
    widget->play_stepping = false;
    core_rewind_dvr_set_playing(&widget->rewind, direction > 0);
}

// Y (and Space, the touch WATCH): the same as RIGHT; while it plays either
// way (SCRUB), it stops. SNAPSHOTS: WATCH plays / pauses the recording.
static void watch(DvrWidget* widget, uint64_t now_ms)
{
    if (scrubbing(widget))
    {
        set_play_direction(widget, widget->play_direction != 0 ? 0 : 1, now_ms);
        return;
    }
    SDL_Log("DVR: %s", widget->rewind.playing ? "PAUSE" : "WATCH");
    core_rewind_dvr_set_playing(&widget->rewind, !widget->rewind.playing);
}

// SCRUB, once per loop: on toward play_direction. Forward at 1x the history
// plays itself (core_session's DVR frame, with its sound); back, and 2x
// either way, step through the states at the game's rate (silent), by the
// clock so a faster display doesn't play faster. An end stops it, bouncing.
static void scrub_motion(DvrWidget* widget, bool fast, uint64_t now_ms)
{
    uint64_t elapsed_ms = now_ms > widget->play_last_ms ? now_ms - widget->play_last_ms : 0;
    widget->play_last_ms = now_ms;
    int32_t direction = widget->play_direction;
    if (direction == 0)
    {
        return;
    }
    if (direction > 0 && !fast)
    {
        if (widget->play_stepping)
        {
            widget->play_stepping = false;   // let go after a 2x: the playback again
            widget->play_frames = 0.0;
            core_rewind_dvr_set_playing(&widget->rewind, true);
        }
        else if (!widget->rewind.playing)
        {
            set_play_direction(widget, 0, now_ms);   // the history's end: the newest frame
            bump(widget, 1, now_ms, false);
        }
        return;
    }
    if (direction > 0 && !widget->play_stepping)
    {
        widget->play_stepping = true;   // 2x forward: stepped, not the playback
        core_rewind_dvr_set_playing(&widget->rewind, false);
    }
    double speed = fast ? SCRUB_FAST_SPEED : 1.0;
    double frames = widget->play_frames + static_cast<double>(elapsed_ms) * fps_of(widget) / 1000.0 * speed;
    int32_t steps = static_cast<int32_t>(frames);
    steps = steps > SCRUB_MAX_STEPS ? SCRUB_MAX_STEPS : steps;
    widget->play_frames = frames - static_cast<double>(steps);
    if (steps > 0 && !core_rewind_dvr_step(&widget->rewind, direction * steps))
    {
        set_play_direction(widget, 0, now_ms);   // an end
        bump(widget, direction, now_ms, false);
    }
}

// UP / DOWN: to the upper (kept) or the lower (recorded) lane, at the same
// frame. Any motion stops; the previews are the new lane's.
static void switch_lane(DvrWidget* widget, bool up, uint64_t now_ms)
{
    CoreRewindPosition position = {};
    core_rewind_dvr_position(&widget->rewind, &position);
    if (!position.lanes || position.on_kept == up)
    {
        return;
    }
    set_play_direction(widget, 0, now_ms);
    uint64_t from = position.cursor;
    bool switched = core_rewind_dvr_switch_lane(&widget->rewind);
    build_previews(widget);
    SDL_Log("DVR: %s, frame %llu -> %llu on the %s lane%s", up ? "UP" : "DOWN", static_cast<unsigned long long>(from),
            static_cast<unsigned long long>(cursor_of(widget)), up ? "kept" : "recorded", switched ? "" : " (failed)");
}

// The game stopping closes the history's DVR (core_session_stop): the GUI follows.
static void follow_history(DvrWidget* widget)
{
    widget->open = widget->open && widget->rewind.dvr_open;
}

void dvr_widget_update(DvrWidget* widget, const InputFrame* frame, uint64_t now_ms)
{
    follow_history(widget);
    if (!widget->open)
    {
        return;
    }
    // Which the hints name: the keyboard if a key did something, else the pads.
    for (uint32_t a = static_cast<uint32_t>(InputAction::ui_up); a <= static_cast<uint32_t>(InputAction::ui_open_menu); a++)
    {
        if (frame->keyboard_pressed[a])
        {
            widget->keyboard = true;
        }
        else if (frame->pressed[a])
        {
            widget->keyboard = false;
        }
    }
    if (pressed(frame, InputAction::ui_back))
    {
        SDL_Log("DVR: BACK TO NOW (back)");
        dvr_widget_close(widget);
        return;
    }
    if (pressed(frame, InputAction::ui_accept))
    {
        CoreRewindPosition position = {};
        core_rewind_dvr_position(&widget->rewind, &position);
        SDL_Log("DVR: PLAY FROM HERE, frame %llu%s", static_cast<unsigned long long>(position.cursor),
                position.on_kept ? " on the kept lane" : "");
        play_from_here(widget, now_ms);
        return;
    }
    if (pressed(frame, InputAction::ui_up))
    {
        switch_lane(widget, true, now_ms);
    }
    if (pressed(frame, InputAction::ui_down))
    {
        switch_lane(widget, false, now_ms);
    }
    // RetroPad Y (the keyboard's open-menu key, F1, isn't WATCH: Space is, in dvr_widget_draw).
    if (pressed(frame, InputAction::ui_open_menu) && !frame->keyboard_pressed[static_cast<uint32_t>(InputAction::ui_open_menu)])
    {
        watch(widget, now_ms);
    }
    if (pressed(frame, InputAction::ui_page_left))
    {
        jump(widget, -1, now_ms);
    }
    if (pressed(frame, InputAction::ui_page_right))
    {
        jump(widget, 1, now_ms);
    }

    // LEFT / RIGHT: the direction (SCRUB) or a snapshot (SNAPSHOTS).
    int32_t press = pressed(frame, InputAction::ui_left) ? -1 : pressed(frame, InputAction::ui_right) ? 1 : 0;
    if (press != 0)
    {
        widget->held_direction = press;
        widget->held_since_ms = now_ms;
        widget->next_step_ms = now_ms + SNAPSHOT_REPEAT_MS;
        if (scrubbing(widget))
        {
            set_play_direction(widget, press, now_ms);
        }
        else
        {
            step(widget, press, now_ms, false);
        }
    }
    bool still_held = widget->held_direction != 0 &&
                      held(frame, widget->held_direction < 0 ? InputAction::ui_left : InputAction::ui_right);
    if (!still_held)
    {
        widget->held_direction = 0;
    }
    if (scrubbing(widget))
    {
        bool fast = still_held && widget->held_direction == widget->play_direction &&
                    now_ms - widget->held_since_ms >= SCRUB_FAST_AFTER_MS;
        scrub_motion(widget, fast, now_ms);
    }
    else if (still_held && press == 0 && now_ms >= widget->next_step_ms)
    {
        step(widget, widget->held_direction, now_ms, true);   // (repeats aren't logged)
        widget->next_step_ms += SNAPSHOT_REPEAT_MS;
    }
}

// The previews shown around the cursor: [first, first + count).
static void visible_previews(const DvrWidget* widget, uint64_t cursor, uint32_t* first, uint32_t* count,
                             uint32_t* selected)
{
    uint32_t total = static_cast<uint32_t>(widget->previews.size());
    uint32_t sel = 0;
    for (uint32_t i = 0; i < total; i++)
    {
        if (widget->previews[i] <= cursor)
        {
            sel = i;
        }
    }
    uint32_t half = DVR_VIEW_MAX_THUMBS / 2;
    uint32_t start = sel > half ? sel - half : 0;
    uint32_t end = start + DVR_VIEW_MAX_THUMBS < total ? start + DVR_VIEW_MAX_THUMBS : total;
    *first = start;
    *count = end - start;
    *selected = sel - start;
}

// A thumbnail made on either lane before the fork shows on both.
static bool thumb_fits(const DvrWidget* widget, uint32_t i, uint64_t frame)
{
    const CoreRewind* rewind = &widget->rewind;
    return widget->thumb_used[i] && widget->thumb_frame[i] == frame &&
           (widget->thumb_kept[i] == rewind->on_kept || !rewind->has_kept || frame < rewind->fork_frame);
}

static int32_t thumb_for(const DvrWidget* widget, uint64_t frame)
{
    for (uint32_t i = 0; i < DVR_VIEW_MAX_THUMBS; i++)
    {
        if (thumb_fits(widget, i, frame))
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void dvr_widget_upload(DvrWidget* widget, CoreSession* session, AppShell* shell)
{
    follow_history(widget);
    // Only when the core may be moved and put back: not while WATCHING, and
    // not after it (the core is then the game at the cursor, kept for B).
    if (!widget->open || widget->rewind.playing || widget->rewind.synced)
    {
        return;
    }
    CoreRewindPosition position = {};
    core_rewind_dvr_position(&widget->rewind, &position);
    uint32_t first = 0;
    uint32_t count = 0;
    uint32_t selected = 0;
    visible_previews(widget, position.cursor, &first, &count, &selected);

    // The missing one nearest the cursor.
    int32_t want = -1;
    for (uint32_t distance = 0; distance < DVR_VIEW_MAX_THUMBS && want < 0; distance++)
    {
        for (int32_t sign = -1; sign <= 1 && want < 0; sign += 2)
        {
            int64_t i = static_cast<int64_t>(selected) + sign * static_cast<int64_t>(distance);
            if (i >= 0 && i < static_cast<int64_t>(count) && thumb_for(widget, widget->previews[first + static_cast<uint32_t>(i)]) < 0)
            {
                want = static_cast<int32_t>(i);
            }
        }
    }
    if (want < 0)
    {
        return;
    }
    // A free texture, or one holding a preview not shown now.
    int32_t slot = -1;
    for (uint32_t t = 0; t < DVR_VIEW_MAX_THUMBS && slot < 0; t++)
    {
        bool shown = false;
        for (uint32_t i = 0; i < count && widget->thumb_used[t]; i++)
        {
            shown = shown || thumb_fits(widget, t, widget->previews[first + i]);
        }
        if (!shown)
        {
            slot = static_cast<int32_t>(t);
        }
    }
    if (slot < 0)
    {
        return;
    }
    uint64_t now_ms = SDL_GetTicks();
    if (now_ms < widget->next_thumb_ms)
    {
        return;
    }
    // Its picture: the core goes there and comes back to the cursor.
    uint64_t started = SDL_GetTicksNS();
    uint64_t frame = widget->previews[first + static_cast<uint32_t>(want)];
    if (core_rewind_dvr_seek(&widget->rewind, frame) &&
        core_session_copy_picture(session, shell, &widget->thumbs[slot]))
    {
        widget->thumb_used[slot] = true;
        widget->thumb_frame[slot] = frame;
        widget->thumb_kept[slot] = widget->rewind.on_kept;
    }
    core_rewind_dvr_seek(&widget->rewind, position.cursor);
    uint64_t cost = SDL_GetTicksNS() - started;
    widget->next_thumb_ms = cost > THUMB_SLOW_NS ? now_ms + cost * THUMB_SPACING / 1000000 : 0;
}

float dvr_widget_strip_height(const DvrWidget* widget, int32_t scale)
{
    return widget->open && widget->rewind.dvr_open ? dvr_view_height(&widget->view, scale) : 0.0f;
}

// `frame` as a place along the track: the visible preview at or before it
// plus the way to the next; past either end at the previews' spacing (both
// lanes count the same frames, so the other lane's frames place too).
static float track_place(const DvrWidget* widget, uint32_t first, uint32_t count, uint64_t frame)
{
    if (count == 0)
    {
        return 0.0f;
    }
    double spacing = static_cast<double>(widget->preview_frames);
    uint64_t start = widget->previews[first];
    if (frame <= start)
    {
        return static_cast<float>(-static_cast<double>(start - frame) / spacing);
    }
    for (uint32_t i = 0; i + 1 < count; i++)
    {
        uint64_t here = widget->previews[first + i];
        uint64_t next = widget->previews[first + i + 1];
        if (frame < next)
        {
            return static_cast<float>(i) +
                   static_cast<float>(static_cast<double>(frame - here) / static_cast<double>(next - here));
        }
    }
    uint64_t last = widget->previews[first + count - 1];
    return static_cast<float>(count - 1) + static_cast<float>(static_cast<double>(frame - last) / spacing);
}

// The view from where the cursor is now (the game stopping closes it
// first: the history's DVR closed with it).
void dvr_widget_prepare(DvrWidget* widget, const Localization* strings, bool touch, uint64_t now_ms)
{
    follow_history(widget);
    if (!widget->open)
    {
        return;
    }
    DvrView* view = &widget->view;
    CoreRewindPosition position = {};
    core_rewind_dvr_position(&widget->rewind, &position);
    double fps = fps_of(widget);
    uint32_t first = 0;
    uint32_t count = 0;
    uint32_t selected = 0;
    visible_previews(widget, position.cursor, &first, &count, &selected);

    view->touch = touch;
    view->count = count;
    view->selected = selected;
    bool moving = scrubbing(widget) ? widget->play_direction != 0 : widget->rewind.playing;
    view->playing = moving;
    view->bump_direction = 0;
    if (widget->bump_ms != 0 && now_ms >= widget->bump_ms)
    {
        float since = static_cast<float>(now_ms - widget->bump_ms) / 1000.0f;
        if (since < DVR_VIEW_BUMP_SECONDS)
        {
            view->bump_direction = widget->bump_direction;
            view->bump_seconds = since;
        }
    }
    view->between = 0.0f;
    if (selected + 1 < count)
    {
        uint64_t here = widget->previews[first + selected];
        uint64_t next = widget->previews[first + selected + 1];
        view->between = static_cast<float>(position.cursor - here) / static_cast<float>(next - here);
    }
    for (uint32_t i = 0; i < count; i++)
    {
        DvrViewThumb* thumb = &view->thumbs[i];
        uint64_t frame = widget->previews[first + i];
        format_back(thumb->label, sizeof(thumb->label), lane_end(&position) - frame, fps, false);
        thumb->after = frame > position.cursor;
        int32_t t = thumb_for(widget, frame);
        thumb->texture = t >= 0 ? widget->thumbs[t].ui_texture : 0;
        if (t >= 0)
        {
            video_texture_uv_max(&widget->thumbs[t], &thumb->u_max, &thumb->v_max);
            if (widget->thumbs[t].content_height > 0)
            {
                view->aspect = static_cast<float>(widget->thumbs[t].content_width) /
                               static_cast<float>(widget->thumbs[t].content_height);
            }
        }
    }

    // The lanes along the track, in thumbnails (dvr_view's places).
    view->lanes = position.lanes;
    view->on_upper = position.on_kept;
    view->fork_at = track_place(widget, first, count, position.fork);
    view->upper_end = track_place(widget, first, count, position.kept_newest);
    view->lower_end = track_place(widget, first, count, position.newest);

    format_back(widget->readout, sizeof(widget->readout), lane_end(&position) - position.cursor, fps, true);
    char span[16] = {};
    format_back(span, sizeof(span), position.newest - position.oldest, fps, false);
    std::snprintf(widget->kept, sizeof(widget->kept), "%s %s", piccolo_text(strings, Str::dvr_kept), span + 1);   // no "-"
    bool scrub = widget->rewind.state != CoreRewindState::snapshots;
    if (scrub)
    {
        std::snprintf(widget->mode, sizeof(widget->mode), "%s", piccolo_text(strings, Str::dvr_mode_scrub));
    }
    else
    {
        std::snprintf(widget->mode, sizeof(widget->mode), "%s %u S", piccolo_text(strings, Str::dvr_mode_snapshots),
                      widget->rewind.settings.snapshot_seconds);
    }

    // The hints (touch: the same as buttons, with arrows for the steps).
    uint32_t n = 0;
    if (touch)
    {
        view->hints[n++] = {"", "\xE2\x97\x80", false, DvrViewAction::back};   // ◀
    }
    else
    {
        view->hints[n++] = {"\xE2\x97\x80\xE2\x96\xB6", piccolo_text(strings, scrub ? Str::dvr_hint_play : Str::dvr_hint_snapshot),
                            false, DvrViewAction::none};
        view->hints[n++] = {widget->keyboard ? "PG UP DN" : "L R",
                            piccolo_text(strings, scrub ? Str::dvr_hint_jump : Str::dvr_hint_snapshot), false,
                            DvrViewAction::none};
        if (position.lanes)
        {
            view->hints[n++] = {"\xE2\x96\xB2\xE2\x96\xBC", piccolo_text(strings, Str::dvr_hint_lane), false,   // ▲▼
                                DvrViewAction::other_lane};
        }
    }
    // The keys' names: the RetroPad's, or the keyboard's when it was used last.
    bool keys = widget->keyboard && !touch;
    view->hints[n++] = {keys ? "ENTER" : "B", piccolo_text(strings, Str::dvr_play_here), touch, DvrViewAction::play_here};
    view->hints[n++] = {keys ? "SPACE" : "Y", piccolo_text(strings, moving ? Str::dvr_pause : Str::dvr_watch),
                        moving, DvrViewAction::watch};
    view->hints[n++] = {keys ? "ESC" : "A", piccolo_text(strings, Str::dvr_back_to_now), false, DvrViewAction::back_to_now};
    if (touch)
    {
        view->hints[n++] = {"", "\xE2\x96\xB6", false, DvrViewAction::forward};   // ▶
        if (position.lanes)
        {
            view->hints[n++] = {"", piccolo_text(strings, Str::dvr_hint_lane), false, DvrViewAction::other_lane};
        }
    }
    view->hint_count = n;
}

void dvr_widget_state(DvrWidgetState* state, const DvrWidget* widget, const Localization* strings, bool phone,
                      float game_aspect)
{
    state->phone = phone;
    state->game_aspect = game_aspect;
    Str title = Str::dvr_title;
    if (scrubbing(widget) ? widget->play_direction > 0 : widget->rewind.playing)
    {
        title = Str::dvr_watching;
    }
    else if (scrubbing(widget) && widget->play_direction < 0)
    {
        title = Str::dvr_playing_back;
    }
    state->text.title = piccolo_text(strings, title);
    state->text.mode = widget->mode;
    state->text.kept = widget->kept;
    state->text.readout = widget->readout;
    state->text.exit = piccolo_text(strings, Str::dvr_back_to_now);
    state->text.lane = widget->view.on_upper ? piccolo_text(strings, Str::dvr_lane_kept) : "";
    // The phone's buttons. SNAPSHOTS: ◀◀ / ▶▶ go to the oldest / newest.
    bool moving = scrubbing(widget) ? widget->play_direction != 0 : widget->rewind.playing;
    state->text.play_here = piccolo_text(strings, Str::dvr_play_here);
    state->text.watch = piccolo_text(strings, moving ? Str::dvr_pause : Str::dvr_watch);
    state->text.jump_back = scrubbing(widget) ? piccolo_text(strings, Str::dvr_jump_back) : "\xE2\x97\x80\xE2\x97\x80";
    state->text.jump_forward = scrubbing(widget) ? piccolo_text(strings, Str::dvr_jump_forward) : "\xE2\x96\xB6\xE2\x96\xB6";
    state->text.lane_recorded = piccolo_text(strings, Str::dvr_tab_recorded);
    state->text.lane_kept = piccolo_text(strings, Str::dvr_tab_kept);
}

bool dvr_widget_phone_game_area(const DvrWidget* widget, float screen_width, float screen_height, int32_t scale,
                                float game_aspect, float* width, float* height)
{
    if (!widget->open || !widget->rewind.dvr_open || scale <= 0)
    {
        return false;
    }
    float block = static_cast<float>(scale);
    DvrPhoneAreas areas;
    dvr_view_phone_areas(&widget->view, screen_width / block, screen_height / block, game_aspect, &areas);
    *width = std::floor(areas.game_width * block);
    *height = std::floor(areas.game_height * block);
    return true;
}

constexpr uint64_t HUD_CARD_MS = 2500;      // the card stays this long...
constexpr uint64_t HUD_CARD_FADE_MS = 500;  // ...then fades out

void dvr_widget_add_hud(const DvrWidget* widget, HudStack* stack, uint64_t now_ms)
{
    uint64_t since = widget->hud_since_ms;
    if (since == 0 || now_ms < since || now_ms - since >= HUD_CARD_MS + HUD_CARD_FADE_MS)
    {
        return;
    }
    HudCard card = {};
    hud_view_card_text(&card, widget->hud);   // across two lines when it needs them
    card.since_ms = since;
    if (now_ms - since > HUD_CARD_MS)
    {
        card.alpha = 1.0f - static_cast<float>(now_ms - since - HUD_CARD_MS) / static_cast<float>(HUD_CARD_FADE_MS);
    }
    hud_view_add_card(stack, &card);
}

void dvr_widget_note(const DvrWidget* widget, const Localization* strings, char* out, size_t out_size)
{
    out[0] = '\0';
    switch (widget->rewind.state)
    {
    case CoreRewindState::measuring:
        std::snprintf(out, out_size, "%s", piccolo_text(strings, Str::rewind_note_measuring));
        break;
    case CoreRewindState::unsupported:
        std::snprintf(out, out_size, "%s", piccolo_text(strings, Str::rewind_note_unsupported));
        break;
    case CoreRewindState::scrub:
    case CoreRewindState::snapshots:
        break;   // (the caller says CURRENT MODE: AUTO's choice is that mode)
    case CoreRewindState::off:
        break;
    }
}

void dvr_widget_destroy(DvrWidget* widget, AppShell* shell)
{
    for (uint32_t i = 0; i < DVR_VIEW_MAX_THUMBS; i++)
    {
        video_texture_destroy(&widget->thumbs[i], shell);
        widget->thumb_used[i] = false;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void dvr_widget_draw(DvrWidget* widget, const DvrWidgetState* state, RetroUiFont* pixel_font, int32_t scale,
                     DvrWidgetAction* action)
{
    if (!widget->open)
    {
        return;
    }
    DvrViewInput input = {};
    if (state->phone)
    {
        // The phone: the panel at touch size (the game is fitted into its
        // room by the frontend: dvr_widget_phone_game_area).
        float block = static_cast<float>(scale > 0 ? scale : 1);
        const ImGuiIO& io = ImGui::GetIO();
        DvrPhoneAreas areas;
        dvr_view_phone_areas(&widget->view, io.DisplaySize.x / block, io.DisplaySize.y / block, state->game_aspect, &areas);
        dvr_view_draw_phone(&widget->view, pixel_font, scale, &state->text, &areas, &input);
        action->dragging = input.dragging;
        action->drag_from = input.drag_from;
        action->drag_moved = input.drag_moved;
    }
    else
    {
        dvr_view_draw(&widget->view, pixel_font, scale, &state->text, &input);
    }
    // WATCH on the keyboard: Space (not an interface action of the router).
    if (input.action == DvrViewAction::none && !ui_toolkit_wants_text() && retro_ui_key_pressed(RetroUiKey::space, false))
    {
        input.action = DvrViewAction::watch;
        action->keyboard = true;
    }
    action->press = input.action;
    action->clicked = input.clicked;
}

// ---------------------------------------------------------------------------
// Apply (no GUI)
// ---------------------------------------------------------------------------

bool dvr_widget_open(DvrWidget* widget, const Localization* strings, uint64_t now_ms)
{
    if (widget->open)
    {
        return true;
    }
    CoreRewindState state = widget->rewind.state;
    if (state == CoreRewindState::off)
    {
        set_hud(widget, piccolo_text(strings, Str::dvr_hud_off), now_ms);
        return false;
    }
    if (state == CoreRewindState::unsupported)
    {
        set_hud(widget, piccolo_text(strings, Str::dvr_hud_unsupported), now_ms);
        return false;
    }
    if (!core_rewind_dvr_open(&widget->rewind))
    {
        set_hud(widget, piccolo_text(strings, Str::dvr_hud_nothing), now_ms);
        return false;
    }
    widget->open = true;
    widget->hud_since_ms = 0;
    widget->held_direction = 0;
    widget->play_direction = 0;
    widget->play_frames = 0.0;
    widget->play_stepping = false;
    widget->bump_ms = 0;
    CoreRewindPosition position = {};
    core_rewind_dvr_position(&widget->rewind, &position);
    widget->view.lanes = position.lanes;   // the strip's height, before its first draw
    SDL_Log("DVR: open, frames %llu..%llu, %s", static_cast<unsigned long long>(position.oldest),
            static_cast<unsigned long long>(position.newest), state == CoreRewindState::snapshots ? "SNAPSHOTS" : "SCRUB");
    if (position.lanes)
    {
        SDL_Log("DVR: a kept lane from frame %llu to %llu", static_cast<unsigned long long>(position.fork),
                static_cast<unsigned long long>(position.kept_newest));
    }
    for (uint32_t i = 0; i < DVR_VIEW_MAX_THUMBS; i++)
    {
        widget->thumb_used[i] = false;   // the history moved on since: made again
    }
    build_previews(widget);
    return true;
}

void dvr_widget_close(DvrWidget* widget)
{
    if (widget->open)
    {
        core_rewind_dvr_close(&widget->rewind);
        widget->open = false;
    }
}

// The phone's -5 S / +5 S (SCRUB: as L / R), or ◀◀ / ▶▶ (SNAPSHOTS: the
// oldest / the newest snapshot of the cursor's lane).
static void jump_or_end(DvrWidget* widget, int32_t direction, uint64_t now_ms)
{
    if (scrubbing(widget))
    {
        jump(widget, direction, now_ms);
        return;
    }
    CoreRewindPosition position = {};
    core_rewind_dvr_position(&widget->rewind, &position);
    uint64_t from = position.cursor;
    core_rewind_dvr_seek(&widget->rewind, direction < 0 ? position.oldest : lane_end(&position));
    if (cursor_of(widget) == from)
    {
        bump(widget, direction, now_ms, false);
    }
    SDL_Log("DVR: %s, frame %llu -> %llu", direction < 0 ? "OLDEST" : "NEWEST", static_cast<unsigned long long>(from),
            static_cast<unsigned long long>(cursor_of(widget)));
}

// Past this (in pictures) from where the finger went down, SCRUB moves; at
// one picture away it moves DRAG_SPEED times real time, more further out
// (Andrés, 2026-11-26: slow near the start, faster the further you drag).
constexpr float  DRAG_DEAD  = 0.15f;
constexpr double DRAG_SPEED = 4.0;

// The phone's strip held by a finger: the finger going right goes back in
// time (the pictures follow it).
static void drag(DvrWidget* widget, const DvrWidgetAction* action, uint64_t now_ms)
{
    if (!action->dragging)
    {
        widget->dragging = false;
        return;
    }
    if (!widget->dragging)
    {
        widget->dragging = true;
        widget->drag_last_ms = now_ms;
        widget->drag_frames = 0.0;
        if (scrubbing(widget))
        {
            set_play_direction(widget, 0, now_ms);   // a held finger takes over from WATCH
        }
        else if (widget->rewind.playing)
        {
            watch(widget, now_ms);
        }
        SDL_Log("DVR: drag, frame %llu", static_cast<unsigned long long>(cursor_of(widget)));
    }
    double seconds = static_cast<double>(now_ms - widget->drag_last_ms) / 1000.0;
    widget->drag_last_ms = now_ms;
    if (scrubbing(widget))
    {
        float away = std::fabs(action->drag_from) - DRAG_DEAD;
        if (away <= 0.0f)
        {
            return;
        }
        double speed = DRAG_SPEED * static_cast<double>(away) * static_cast<double>(away + DRAG_DEAD);   // grows with distance
        widget->drag_frames += speed * fps_of(widget) * seconds;
        int32_t frames = static_cast<int32_t>(widget->drag_frames);
        if (frames > 0)
        {
            widget->drag_frames -= frames;
            step(widget, action->drag_from > 0.0f ? -frames : frames, now_ms, true);
        }
        return;
    }
    // SNAPSHOTS: a snapshot per picture the strip slides.
    widget->drag_frames += static_cast<double>(action->drag_moved);
    while (widget->drag_frames >= 1.0)
    {
        widget->drag_frames -= 1.0;
        step(widget, -1, now_ms, true);
    }
    while (widget->drag_frames <= -1.0)
    {
        widget->drag_frames += 1.0;
        step(widget, 1, now_ms, true);
    }
}

void dvr_widget_apply(DvrWidget* widget, const DvrWidgetAction* action, uint64_t now_ms)
{
    if (action->keyboard)
    {
        widget->keyboard = true;
    }
    // A click / tap: as the buttons do.
    if (action->clicked >= 0)
    {
        CoreRewindPosition position = {};
        core_rewind_dvr_position(&widget->rewind, &position);
        uint32_t first = 0;
        uint32_t count = 0;
        uint32_t selected = 0;
        visible_previews(widget, position.cursor, &first, &count, &selected);
        if (static_cast<uint32_t>(action->clicked) < count)
        {
            core_rewind_dvr_seek(&widget->rewind, widget->previews[first + static_cast<uint32_t>(action->clicked)]);
        }
    }
    drag(widget, action, now_ms);
    switch (action->press)
    {
    case DvrViewAction::jump_back:
    case DvrViewAction::jump_forward:
        jump_or_end(widget, action->press == DvrViewAction::jump_back ? -1 : 1, now_ms);
        break;
    case DvrViewAction::back:
    case DvrViewAction::forward:
    {
        int32_t direction = action->press == DvrViewAction::back ? -1 : 1;   // a press of LEFT / RIGHT
        if (scrubbing(widget))
        {
            set_play_direction(widget, direction, now_ms);
        }
        else
        {
            step(widget, direction, now_ms, false);
        }
        break;
    }
    case DvrViewAction::play_here:
        play_from_here(widget, now_ms);
        break;
    case DvrViewAction::watch:
        watch(widget, now_ms);
        break;
    case DvrViewAction::back_to_now:
        dvr_widget_close(widget);
        break;
    case DvrViewAction::other_lane:
        switch_lane(widget, !widget->view.on_upper, now_ms);
        break;
    case DvrViewAction::none:
        break;
    }
}
