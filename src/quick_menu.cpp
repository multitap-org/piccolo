#include "quick_menu.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

#include "app_shell.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

// Room for any thumbnail as saved (the picture at x1: a few hundred pixels across).
constexpr uint32_t THUMBNAIL_CAPACITY = 1024;
// The big picture scaled up: never more than this across or down.
constexpr uint32_t PREVIEW_CAPACITY = 4096;

// The grid: five slots a row, the actions four a row, SAVE / LOAD / GAME DVR three.
constexpr uint32_t SLOT_COLUMNS = 5;
constexpr uint32_t ACTION_COLUMNS = 4;
constexpr uint32_t STATE_COLUMNS = 3;
constexpr uint32_t SLOT_COUNT = LIBRETRO_STATE_SLOT_LAST - LIBRETRO_STATE_SLOT_FIRST + 1;
// A slot at least this many retro pixels wide (the actions' labels usually
// make the grid wider).
constexpr float SLOT_MIN_WIDTH = 52.0f;

// ---------------------------------------------------------------------------
// State

void quick_menu_refresh(QuickMenu* menu)
{
    menu->refresh = true;
}

void quick_menu_update(QuickMenu* menu, const CoreSession* session)
{
    if (!menu->open || !menu->refresh)
    {
        return;
    }
    menu->refresh = false;
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        core_session_slot_info(session, slot, &menu->slots[slot]);
    }
}

void quick_menu_state(QuickMenuState* state, const QuickMenu* menu, const Localization* strings, const char* core_name,
                      const char* game_name, bool user_paused)
{
    state->strings = strings;
    state->core_name = core_name;
    state->game_name = game_name;
    state->user_paused = user_paused;
    // Each slot's label: its number, and when it was saved (the time if
    // today, the day otherwise: a slot is narrow).
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    SDL_DateTime today = {};
    SDL_TimeToDateTime(now, &today, true);
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        char* label = state->labels[slot];
        const StateSlotInfo* info = &menu->slots[slot];
        if (!info->used)
        {
            std::snprintf(label, QUICK_MENU_LABEL_MAX, "%u", slot);
            continue;
        }
        SDL_DateTime date = {};
        SDL_TimeToDateTime(info->modified, &date, true);
        if (date.year == today.year && date.month == today.month && date.day == today.day)
        {
            std::snprintf(label, QUICK_MENU_LABEL_MAX, "%u \xC2\xB7 %02d:%02d", slot, date.hour, date.minute);
        }
        else
        {
            std::snprintf(label, QUICK_MENU_LABEL_MAX, "%u \xC2\xB7 %02d-%02d", slot, date.month, date.day);
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing

// The aspect the pictures are drawn at: the selected slot's, else any slot's
// (one game: they're all the same), else 4:3.
static float picture_aspect(const QuickMenu* menu)
{
    const QuickMenuPicture* selected = &menu->thumbs[menu->slot];
    if (selected->modified != 0 && selected->width > 0 && selected->height > 0)
    {
        return static_cast<float>(selected->width) / static_cast<float>(selected->height);
    }
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        const QuickMenuPicture* thumb = &menu->thumbs[slot];
        if (thumb->modified != 0 && thumb->width > 0 && thumb->height > 0)
        {
            return static_cast<float>(thumb->width) / static_cast<float>(thumb->height);
        }
    }
    return 4.0f / 3.0f;
}

// `width` across at `aspect`, no taller than `room_height` (narrower then):
// bounded horizontally, so a widescreen picture is shorter.
static void fit_picture(float width, float room_height, float aspect, float* out_width, float* out_height)
{
    float picture_width = width;
    float picture_height = width / aspect;
    if (picture_height > room_height)
    {
        picture_height = room_height;
        picture_width = room_height * aspect;
    }
    *out_width = static_cast<float>(static_cast<int>(picture_width));
    *out_height = static_cast<float>(static_cast<int>(picture_height));
}

// A slot's tile: its picture area (4:3 inside the button's padding), and
// the whole tile with its label.
static float slot_picture_height(float block, float slot_width)
{
    float pad = 2.0f * block;   // a button's padding (retro_ui_tile puts its picture inside it)
    return static_cast<float>(static_cast<int>((slot_width - 2.0f * pad) * 3.0f / 4.0f));
}

static float slot_tile_height(float block, float slot_width)
{
    return slot_picture_height(block, slot_width) + retro_ui_font_size() + 6.0f * block;
}

// The selected slot's picture, as wide as the grid: the one scaled up by a
// whole number (sharp bilinear), or its thumbnail for the frame before that
// one is up. No taller than `room_height` (narrower then, centered).
static void draw_preview(QuickMenu* menu, const QuickMenuState* state, float grid, float room_height)
{
    float aspect = picture_aspect(menu);
    float box_width = 0.0f;
    float box_height = 0.0f;
    float tallest = grid * 3.0f / 4.0f;
    tallest = room_height < tallest ? room_height : tallest;
    fit_picture(grid, tallest, aspect, &box_width, &box_height);
    retro_ui_canvas(grid, box_height);
    retro_ui_canvas_rect(0.0f, 0.0f, grid, box_height, 0xFF000000u);
    float x = static_cast<float>(static_cast<int>((grid - box_width) * 0.5f));
    menu->preview_width = box_width;
    menu->preview_height = box_height;
    const QuickMenuPicture* picture = nullptr;
    if (menu->preview.shown && menu->preview_slot == menu->slot)
    {
        picture = &menu->preview;
    }
    else if (menu->thumbs[menu->slot].shown)
    {
        picture = &menu->thumbs[menu->slot];
    }
    if (picture != nullptr)
    {
        float u = 0.0f;
        float v = 0.0f;
        video_texture_uv_max(&picture->texture, &u, &v);
        retro_ui_canvas_image_smooth(picture->texture.ui_texture, x, 0.0f, box_width, box_height, u, v, 0xFFFFFFFFu);
    }
    else if (!menu->slots[menu->slot].used)
    {
        const char* empty = piccolo_text(state->strings, Str::quick_menu_empty);
        float text_x = static_cast<float>(static_cast<int>((grid - retro_ui_text_width(empty)) * 0.5f));
        float text_y = static_cast<float>(static_cast<int>((box_height - retro_ui_font_size()) * 0.5f));
        retro_ui_canvas_text(text_x, text_y, empty, retro_ui_palette()->text_disabled);
    }
}

// The slots, five a row, each its thumbnail and label; the focused one
// (arrows) or the clicked one is the selection.
static void draw_slots(QuickMenu* menu, const QuickMenuState* state, float block, float slot_width)
{
    float pad = 2.0f * block;
    float area_width = slot_width - 2.0f * pad;
    float area_height = slot_picture_height(block, slot_width);
    // A slot picked otherwise (opening, 1-9) takes the focus, which moves
    // only a frame later: meanwhile the tile focused before still says it's
    // chosen, and must not take the selection back.
    bool focus_now = menu->focus_slot;
    menu->focus_slot = false;
    if (focus_now)
    {
        menu->focus_wait = 3;
    }
    bool waiting = menu->focus_wait > 0;
    if (waiting)
    {
        menu->focus_wait--;
    }
    float aspect = picture_aspect(menu);
    const char* empty = piccolo_text(state->strings, Str::quick_menu_empty);
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        if ((slot - LIBRETRO_STATE_SLOT_FIRST) % SLOT_COLUMNS != 0)
        {
            retro_ui_same_line();
        }
        bool selected = slot == menu->slot;
        const QuickMenuPicture* thumb = &menu->thumbs[slot];
        RetroUiTile tile = {};
        tile.width = slot_width;
        tile.picture_height = area_height;
        tile.height = slot_tile_height(block, slot_width);
        tile.chosen = selected;
        tile.edge = block;
        if (thumb->shown)
        {
            tile.texture = thumb->texture.ui_texture;
            fit_picture(area_width, area_height, aspect, &tile.image_width, &tile.image_height);
            video_texture_uv_max(&thumb->texture, &tile.u_max, &tile.v_max);
        }
        else if (!menu->slots[slot].used)
        {
            tile.empty = empty;
        }
        // The focused slot is the selection (arrows move it).
        tile.focus = focus_now && selected;
        retro_ui_push_index(slot);
        if (retro_ui_tile("slot", state->labels[slot], &tile))
        {
            if (!waiting)
            {
                menu->slot = slot;
            }
            else if (selected)
            {
                menu->focus_wait = 0;   // the focus is on it now
            }
        }
        retro_ui_pop_id();
    }
}

// SAVE STATES, below the actions: the big picture, the slots, SAVE / LOAD /
// GAME DVR, the keys.
// `screen_room`: how tall the window may be (from its top, its bottom
// padding taken off).
static void draw_states(QuickMenu* menu, const QuickMenuState* state, float block, float grid, float slot_width,
                        float screen_room, bool appearing, QuickMenuAction* action)
{
    const Localization* strings = state->strings;
    // The whole window fits the screen (640 x 360 retro pixels at least)
    // with SAVE / LOAD / GAME DVR in view: the big picture takes the height
    // the rest leaves.
    const char* keys = piccolo_text(strings, Str::quick_menu_keys);
    float row_gap = retro_ui_line_step() - retro_ui_line_height();
    uint32_t slot_rows = (SLOT_COUNT + SLOT_COLUMNS - 1) / SLOT_COLUMNS;
    float key_lines = std::ceil(retro_ui_text_width(keys) / (grid > 1.0f ? grid : 1.0f));
    float below = (slot_tile_height(block, slot_width) + row_gap) * static_cast<float>(slot_rows) +
                  retro_ui_line_step() + retro_ui_text_line_step() * key_lines;
    float room = screen_room - retro_ui_cursor_y() - row_gap - below;
    float least = retro_ui_font_size() * 3.0f;
    draw_preview(menu, state, grid, room > least ? room : least);
    if (appearing)
    {
        menu->focus_slot = true;
    }
    draw_slots(menu, state, block, slot_width);
    const StateSlotInfo* info = &menu->slots[menu->slot];
    RetroUiRowButton buttons[STATE_COLUMNS] = {};
    buttons[0].id = "save";
    buttons[0].label = piccolo_text(strings, Str::quick_menu_save);
    buttons[1].id = "load";
    buttons[1].label = piccolo_text(strings, Str::quick_menu_load);
    buttons[1].enabled = info->used;
    buttons[2].id = "dvr";
    buttons[2].label = piccolo_text(strings, Str::quick_menu_dvr);
    int32_t pressed = retro_ui_equal_buttons(buttons, STATE_COLUMNS, grid);
    if (pressed == 0)
    {
        action->press = QuickMenuPress::save;
    }
    else if (pressed == 1)
    {
        action->press = QuickMenuPress::load;
    }
    else if (pressed == 2)
    {
        action->press = QuickMenuPress::dvr;   // a window widget: it replaces QUICK MENU
    }
    retro_ui_dim_text_wrapped_at(keys, 0.0f);
}

// The keys that act without the section open: 1-9 pick a slot, S saves, L
// loads (as before the section folded away).
static void handle_keys(QuickMenu* menu, QuickMenuAction* action)
{
    if (!retro_ui_window_focused() || ui_toolkit_wants_text())
    {
        return;
    }
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        RetroUiKey key = static_cast<RetroUiKey>(static_cast<int32_t>(RetroUiKey::digit_1) + static_cast<int32_t>(slot - 1));
        if (retro_ui_key_pressed(key, false))
        {
            menu->slot = slot;
            menu->focus_slot = true;   // and the focus follows (draw_slots: the key wins this frame)
        }
    }
    if (retro_ui_key_pressed(RetroUiKey::s, false))
    {
        action->press = QuickMenuPress::save;
    }
    if (menu->slots[menu->slot].used && retro_ui_key_pressed(RetroUiKey::l, false))
    {
        action->press = QuickMenuPress::load;
    }
}

void quick_menu_draw(QuickMenu* menu, const QuickMenuState* state, RetroUiFont* pixel_font, int32_t scale,
                     QuickMenuAction* action)
{
    *action = {};
    if (!menu->open)
    {
        return;
    }
    const Localization* strings = state->strings;
    uint32_t slot_before = menu->slot;
    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    retro_ui_push_theme(pixel_font, scale);

    // The actions, eight on two rows.
    RetroUiRowButton actions[2 * ACTION_COLUMNS] = {};
    actions[0].id = "pause";
    actions[0].label = piccolo_text(strings, state->user_paused ? Str::quick_menu_resume : Str::quick_menu_pause);
    actions[1].id = "quick_save";
    actions[1].label = piccolo_text(strings, Str::quick_menu_quick_save);
    actions[2].id = "quick_load";
    actions[2].label = piccolo_text(strings, Str::quick_menu_quick_load);
    actions[2].enabled = menu->slots[QUICK_SLOT].used;   // as LOAD: nothing to load from an empty slot
    actions[3].id = "screenshot";
    actions[3].label = piccolo_text(strings, Str::quick_menu_screenshot);
    actions[4].id = "soft_reset";
    actions[4].label = piccolo_text(strings, Str::quick_menu_reset);
    actions[5].id = "hard_reset";
    actions[5].label = piccolo_text(strings, Str::quick_menu_hard_reset);
    actions[6].id = "config";
    actions[6].label = piccolo_text(strings, Str::quick_menu_config);
    actions[6].help = piccolo_text(strings, Str::quick_menu_config_help);
    actions[7].id = "close_game";
    actions[7].label = piccolo_text(strings, Str::quick_menu_close_game);
    const QuickMenuPress presses[2 * ACTION_COLUMNS] = {
        QuickMenuPress::pause,      QuickMenuPress::quick_save, QuickMenuPress::quick_load, QuickMenuPress::screenshot,
        QuickMenuPress::soft_reset, QuickMenuPress::hard_reset, QuickMenuPress::config,     QuickMenuPress::close_game,
    };

    // The grid: wide enough for the widest action in a quarter of it, and
    // five slots of at least SLOT_MIN_WIDTH; whole pixels, five equal slots.
    float spacing = retro_ui_item_spacing();
    float grid = retro_ui_equal_buttons_width(actions, 2 * ACTION_COLUMNS, ACTION_COLUMNS);
    float slots_min = SLOT_MIN_WIDTH * block * static_cast<float>(SLOT_COLUMNS) + spacing * static_cast<float>(SLOT_COLUMNS - 1);
    grid = grid > slots_min ? grid : slots_min;
    float slot_width = static_cast<float>(static_cast<int>((grid - spacing * static_cast<float>(SLOT_COLUMNS - 1)) /
                                                            static_cast<float>(SLOT_COLUMNS) + 0.999f));
    grid = slot_width * static_cast<float>(SLOT_COLUMNS) + spacing * static_cast<float>(SLOT_COLUMNS - 1);

    // A window in the middle as wide as the grid, as tall as its contents
    // (it scrolls on a screen too short for SAVE STATES).
    float chrome_width = 0.0f;
    float chrome_height = 0.0f;
    retro_ui_window_chrome(&chrome_width, &chrome_height);
    float width = grid + chrome_width;
    width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    RetroUiWindow window = {};
    window.id = "quick_menu";
    window.title = piccolo_text(strings, Str::quick_menu_title);
    window.width = static_cast<float>(static_cast<int>(width));
    window.centered = true;
    window.focus_on_open = true;
    bool closed = false;
    if (retro_ui_window_begin(&window, scale, &closed))
    {
        if (closed)
        {
            action->closed = true;
        }
        bool appearing = retro_ui_window_appearing();
        // The rows as wide as the room actually there (a narrow screen, or a
        // scroll bar), never the grid's width over it.
        float room_width = retro_ui_width_left();
        if (room_width < grid)
        {
            slot_width = std::floor((room_width - spacing * static_cast<float>(SLOT_COLUMNS - 1)) /
                                    static_cast<float>(SLOT_COLUMNS));
            grid = slot_width * static_cast<float>(SLOT_COLUMNS) + spacing * static_cast<float>(SLOT_COLUMNS - 1);
        }
        float bottom_padding = (chrome_height - retro_ui_line_height()) * 0.5f;
        float screen_room = screen_height - 8.0f * block - bottom_padding;

        // What runs (the pixel font draws it in capitals).
        char running[600] = {};
        if (state->game_name[0] != '\0')
        {
            std::snprintf(running, sizeof(running), "%s - %s", state->game_name, state->core_name);
        }
        else
        {
            std::snprintf(running, sizeof(running), "%s", state->core_name);
        }
        retro_ui_dim_text(running, nullptr);
        for (uint32_t row = 0; row < 2; row++)
        {
            int32_t pressed = retro_ui_equal_buttons(&actions[row * ACTION_COLUMNS], ACTION_COLUMNS, grid);
            if (pressed >= 0)
            {
                action->press = presses[row * ACTION_COLUMNS + static_cast<uint32_t>(pressed)];
            }
        }
        handle_keys(menu, action);
        // Collapsed at first (Andrés): the actions are what's used most.
        if (retro_ui_section_header("states", piccolo_text(strings, Str::quick_menu_tab_states), false))
        {
            draw_states(menu, state, block, grid, slot_width, screen_room, appearing, action);
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
    action->slot_picked = menu->slot != slot_before;
}

// ---------------------------------------------------------------------------
// Apply

static void picture_changed(QuickMenuPicture* picture);

void quick_menu_open(QuickMenu* menu, uint32_t slot)
{
    menu->open = true;
    menu->slot = slot >= LIBRETRO_STATE_SLOT_FIRST && slot <= LIBRETRO_STATE_SLOT_LAST ? slot : LIBRETRO_STATE_SLOT_FIRST;
    menu->refresh = true;
}

// A slot's PNG into its thumbnail's pixels (B8G8R8A8), when it changed.
static void load_thumbnail(QuickMenu* menu, uint32_t slot)
{
    const StateSlotInfo* info = &menu->slots[slot];
    QuickMenuPicture* thumb = &menu->thumbs[slot];
    if (!info->used || !info->has_thumbnail)
    {
        thumb->modified = 0;
        thumb->shown = false;
        return;
    }
    if (thumb->modified == info->modified)
    {
        return;
    }
    thumb->modified = info->modified;
    SDL_Surface* png = SDL_LoadPNG(info->thumbnail_path);
    SDL_Surface* converted = png != nullptr ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_ARGB8888) : nullptr;
    if (converted != nullptr && converted->w > 0 && converted->h > 0 &&
        static_cast<uint32_t>(converted->w) <= THUMBNAIL_CAPACITY && static_cast<uint32_t>(converted->h) <= THUMBNAIL_CAPACITY)
    {
        thumb->width = static_cast<uint32_t>(converted->w);
        thumb->height = static_cast<uint32_t>(converted->h);
        thumb->pixels.resize(static_cast<size_t>(thumb->width) * thumb->height);
        for (uint32_t y = 0; y < thumb->height; y++)
        {
            const uint8_t* row = static_cast<const uint8_t*>(converted->pixels) + static_cast<size_t>(y) * static_cast<size_t>(converted->pitch);
            std::memcpy(&thumb->pixels[static_cast<size_t>(y) * thumb->width], row, static_cast<size_t>(thumb->width) * 4);
        }
        picture_changed(thumb);
    }
    else
    {
        // Nothing to show (kept as this state's, so it's tried and logged once).
        thumb->width = 0;
        thumb->height = 0;
        thumb->shown = false;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Quick menu: could not read thumbnail %s: %s", info->thumbnail_path,
                    SDL_GetError());
    }
    SDL_DestroySurface(converted);
    SDL_DestroySurface(png);
}

// The selected slot's thumbnail scaled up by the biggest whole number that
// fits the big picture's size, with nearest: drawn bilinear to that size,
// it's sharp bilinear (sharp pixels, soft only where a pixel falls between
// two screen pixels).
static void build_preview(QuickMenu* menu)
{
    const QuickMenuPicture* thumb = &menu->thumbs[menu->slot];
    if (thumb->modified == 0 || thumb->width == 0 || thumb->height == 0 || menu->preview_width <= 0.0f)
    {
        menu->preview.shown = false;
        menu->preview.modified = 0;
        return;
    }
    uint32_t scale_x = static_cast<uint32_t>(menu->preview_width) / thumb->width;
    uint32_t scale_y = static_cast<uint32_t>(menu->preview_height) / thumb->height;
    uint32_t scale = scale_x < scale_y ? scale_x : scale_y;
    scale = scale >= 1 ? scale : 1;
    while (scale > 1 && (thumb->width * scale > PREVIEW_CAPACITY || thumb->height * scale > PREVIEW_CAPACITY))
    {
        scale--;
    }
    QuickMenuPicture* preview = &menu->preview;
    if (menu->preview_slot == menu->slot && preview->modified == thumb->modified && menu->preview_scale == scale)
    {
        return;
    }
    menu->preview_slot = menu->slot;
    menu->preview_scale = scale;
    preview->modified = thumb->modified;
    preview->width = thumb->width * scale;
    preview->height = thumb->height * scale;
    preview->pixels.resize(static_cast<size_t>(preview->width) * preview->height);
    for (uint32_t y = 0; y < preview->height; y++)
    {
        const uint32_t* source = &thumb->pixels[static_cast<size_t>(y / scale) * thumb->width];
        uint32_t* row = &preview->pixels[static_cast<size_t>(y) * preview->width];
        for (uint32_t x = 0; x < preview->width; x++)
        {
            row[x] = source[x / scale];
        }
    }
    picture_changed(preview);
}

void quick_menu_apply(QuickMenu* menu, const QuickMenuAction* action, CoreSession* session)
{
    if (action->closed)
    {
        menu->open = false;
    }
    if (action->slot_picked || menu->open)
    {
        session->state_slot = menu->slot;   // F2 / F4 follow the picked slot
    }
    if (menu->open)
    {
        // The pictures this frame asks for, uploaded later in the frame.
        for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
        {
            load_thumbnail(menu, slot);
        }
        build_preview(menu);
    }
}

// New pixels in `picture` (apply): uploaded later in the frame. A texture of
// another size is made again, but not in the frame that may still draw the
// old one: the picture is hidden first and the new texture made a frame
// later (see upload_picture).
static void picture_changed(QuickMenuPicture* picture)
{
    const VideoTexture* texture = &picture->texture;
    picture->upload_pending = true;
    picture->shown = false;
    picture->replace_wait = video_texture_created(texture) &&
                            (texture->capacity_width != picture->width || texture->capacity_height != picture->height);
}

// One picture's texture (the renderer's, outside the toolkit), made the
// picture's size: bilinear sampling would blend in what lies past a
// smaller upload's edge. Before app_shell_acquire.
static void prepare_picture(QuickMenuPicture* picture, AppShell* shell)
{
    if (!picture->upload_pending || picture->replace_wait)
    {
        return;
    }
    VideoTexture* texture = &picture->texture;
    if (video_texture_created(texture) &&
        (texture->capacity_width != picture->width || texture->capacity_height != picture->height))
    {
        video_texture_destroy(texture, shell);   // waits for the GPU; nothing drew it this frame (hidden)
    }
    if (!video_texture_created(texture) &&
        !video_texture_create(texture, shell, picture->width, picture->height, FramePixelFormat::xrgb8888, false))
    {
        picture->upload_pending = false;
    }
}

static void upload_picture(QuickMenuPicture* picture, AppShell* shell)
{
    if (picture->replace_wait)
    {
        picture->replace_wait = false;   // this frame drew the old texture: made again next frame
        return;
    }
    if (!picture->upload_pending)
    {
        return;
    }
    if (video_texture_upload(&picture->texture, shell, picture->pixels.data(), picture->width, picture->height))
    {
        picture->shown = true;
    }
    picture->upload_pending = false;
}

void quick_menu_upload(QuickMenu* menu, AppShell* shell)
{
    bool pending = menu->preview.upload_pending;
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        pending = pending || menu->thumbs[slot].upload_pending;
    }
    if (!pending)
    {
        return;
    }
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        prepare_picture(&menu->thumbs[slot], shell);
    }
    prepare_picture(&menu->preview, shell);
    if (!app_shell_acquire(shell))
    {
        return;   // try on the next frame
    }
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        upload_picture(&menu->thumbs[slot], shell);
    }
    upload_picture(&menu->preview, shell);
}

void quick_menu_destroy(QuickMenu* menu, AppShell* shell)
{
    for (uint32_t slot = 0; slot < QUICK_MENU_SLOTS; slot++)
    {
        video_texture_destroy(&menu->thumbs[slot].texture, shell);   // waits for the GPU
    }
    video_texture_destroy(&menu->preview.texture, shell);
}
