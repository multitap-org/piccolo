#include "quick_menu.h"

#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

#include "app_shell.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

// Room for any thumbnail (the picture at x1: a few hundred pixels across).
constexpr uint32_t THUMBNAIL_CAPACITY = 1024;

// The used slots' button color (0xAARRGGBB), so they stand out from empty ones.
constexpr uint32_t USED_SLOT = 0xFF3A6EA5u;

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
    menu->shown_slot = 0;   // the selected thumbnail may have changed
}

void quick_menu_state(QuickMenuState* state, const Localization* strings, const char* core_name, const char* game_name,
                      bool user_paused)
{
    state->strings = strings;
    state->core_name = core_name;
    state->game_name = game_name;
    state->user_paused = user_paused;
}

// ---------------------------------------------------------------------------
// Drawing

// The nine slot buttons; the focused one (arrows) or the clicked one is the selection.
static void draw_slots(QuickMenu* menu, float block, bool appearing)
{
    float size = 18.0f * block;
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        if (slot > LIBRETRO_STATE_SLOT_FIRST)
        {
            retro_ui_same_line();
        }
        bool selected = slot == menu->slot;
        RetroUiTile tile = {};
        tile.width = size;
        tile.height = size;
        tile.face = menu->slots[slot].used ? USED_SLOT : 0;
        tile.chosen = selected;
        tile.edge = block;
        // The focused button is the selection (arrows move it), so a slot
        // picked otherwise (opening, 1-9) takes the focus too.
        if ((appearing || menu->focus_slot) && selected)
        {
            tile.focus = true;
            menu->focus_slot = false;
        }
        char label[16] = {};
        std::snprintf(label, sizeof(label), "%u", slot);
        retro_ui_push_index(slot);
        if (retro_ui_tile("slot", label, &tile))
        {
            menu->slot = slot;
        }
        retro_ui_pop_id();
    }
    // Number keys pick a slot directly.
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        RetroUiKey key = static_cast<RetroUiKey>(static_cast<int32_t>(RetroUiKey::digit_1) + static_cast<int32_t>(slot - 1));
        if (retro_ui_window_focused() && retro_ui_key_pressed(key, false))
        {
            menu->slot = slot;
            menu->focus_slot = true;
        }
    }
}

// The states: slots, the selected one's thumbnail and date, SAVE / LOAD.
static void draw_states(QuickMenu* menu, const QuickMenuState* state, float block, bool appearing,
                        QuickMenuAction* action)
{
    const Localization* strings = state->strings;
    draw_slots(menu, block, appearing);

    // The thumbnail at the biggest whole-number scale that fits (sharp pixels:
    // nearest filtering, on whole pixels), the slot's details beside it. Room:
    // most of the width, the height left under the slots (less the spacing
    // below a line).
    const StateSlotInfo* info = &menu->slots[menu->slot];
    float room_x = static_cast<float>(static_cast<int>(retro_ui_width_left() * 0.6f));
    float room_y = static_cast<float>(static_cast<int>(retro_ui_height_left() - (retro_ui_line_step() - retro_ui_line_height())));
    room_x = room_x > 160.0f * block ? room_x : 160.0f * block;
    room_y = room_y > 120.0f * block ? room_y : 120.0f * block;
    bool showing = menu->has_picture && menu->shown_slot == menu->slot && menu->texture.ui_texture != 0 &&
                   menu->width > 0 && menu->height > 0;
    float box_x = room_x;   // no picture: a 4:3 box
    float box_y = room_x * 3.0f / 4.0f;
    if (box_y > room_y)
    {
        box_x = room_y * 4.0f / 3.0f;
        box_y = room_y;
    }
    if (showing)
    {
        float width = static_cast<float>(menu->width);
        float height = static_cast<float>(menu->height);
        int scale_x = static_cast<int>(room_x / width);
        int scale_y = static_cast<int>(room_y / height);
        int scale = scale_x < scale_y ? scale_x : scale_y;
        if (scale >= 1)
        {
            box_x = width * static_cast<float>(scale);
            box_y = height * static_cast<float>(scale);
        }
        else
        {
            // Not even once: shrunk to fit (it can't be whole pixels then).
            float aspect = width / height;
            box_x = room_x;
            box_y = room_x / aspect;
            if (box_y > room_y)
            {
                box_x = room_y * aspect;
                box_y = room_y;
            }
        }
    }
    box_x = static_cast<float>(static_cast<int>(box_x));
    box_y = static_cast<float>(static_cast<int>(box_y));
    // A canvas: its corner is on a whole pixel, and retro_ui_begin_window()
    // switched the window to nearest sampling.
    retro_ui_canvas(box_x, box_y);
    retro_ui_canvas_rect(0.0f, 0.0f, box_x, box_y, 0xFF000000u);
    if (showing)
    {
        float u = 0.0f;
        float v = 0.0f;
        video_texture_uv_max(&menu->texture, &u, &v);
        retro_ui_canvas_image(menu->texture.ui_texture, 0.0f, 0.0f, box_x, box_y, u, v, 0xFFFFFFFFu);
    }
    retro_ui_same_line();
    retro_ui_begin_group();
    retro_ui_text_format("%s %u", piccolo_text(strings, Str::quick_menu_slot), menu->slot);
    if (info->used)
    {
        SDL_DateTime date = {};
        SDL_TimeToDateTime(info->modified, &date, true);
        retro_ui_text_format("%04d-%02d-%02d", date.year, date.month, date.day);   // two lines: the panel is narrow
        retro_ui_text_format("%02d:%02d", date.hour, date.minute);
    }
    else
    {
        retro_ui_dim_text(piccolo_text(strings, Str::quick_menu_empty), nullptr);
    }
    retro_ui_spacing();
    bool keys = !ui_toolkit_wants_text() && retro_ui_window_focused();
    if (retro_ui_button(piccolo_text(strings, Str::quick_menu_save)) || (keys && retro_ui_key_pressed(RetroUiKey::s, false)))
    {
        action->press = QuickMenuPress::save;
    }
    retro_ui_same_line();
    retro_ui_begin_disabled(!info->used);
    if (retro_ui_button(piccolo_text(strings, Str::quick_menu_load)) ||
        (info->used && keys && retro_ui_key_pressed(RetroUiKey::l, false)))
    {
        action->press = QuickMenuPress::load;
    }
    retro_ui_end_disabled();
    retro_ui_end_group();
    retro_ui_spacing();
    retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::quick_menu_keys), 0.0f);
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
    // A window in the middle, 320 x 250 retro pixels, never bigger than the
    // screen. The same size every frame (the thumbnail takes the room left),
    // and it never scrolls.
    float width = 320.0f * block;
    float height = 250.0f * block;
    width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    height = height < screen_height - 8.0f * block ? height : screen_height - 8.0f * block;
    RetroUiWindow window = {};
    window.id = "quick_menu";
    window.title = piccolo_text(strings, Str::quick_menu_title);
    window.width = static_cast<float>(static_cast<int>(width));
    window.height = static_cast<float>(static_cast<int>(height));
    window.centered = true;
    window.focus_on_open = true;
    window.scrolls = false;
    bool closed = false;
    if (retro_ui_window_begin(&window, scale, &closed))
    {
        if (closed)
        {
            action->closed = true;
        }
        bool appearing = retro_ui_window_appearing();

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
        retro_ui_spacing();
        // The actions on the running core: PAUSE, DVR and CONFIG (CORE
        // CONFIGURATION), then RESET and CLOSE GAME, a row apart.
        float row_width = retro_ui_width_left();
        float third = static_cast<float>(static_cast<int>((row_width - 2.0f * retro_ui_item_spacing()) / 3.0f));
        float half = static_cast<float>(static_cast<int>((row_width - retro_ui_item_spacing()) / 2.0f));
        if (retro_ui_button_sized(piccolo_text(strings, state->user_paused ? Str::quick_menu_resume : Str::quick_menu_pause),
                                  third, 0.0f))
        {
            action->press = QuickMenuPress::pause;
        }
        retro_ui_same_line();
        if (retro_ui_button_sized(piccolo_text(strings, Str::quick_menu_dvr), third, 0.0f))
        {
            action->press = QuickMenuPress::dvr;   // a window widget: it replaces QUICK MENU
        }
        retro_ui_same_line();
        if (retro_ui_button_sized(piccolo_text(strings, Str::quick_menu_config), third, 0.0f))
        {
            action->press = QuickMenuPress::config;   // CORE CONFIGURATION, a MAIN MENU window
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::quick_menu_config_help));
        if (retro_ui_button_sized(piccolo_text(strings, Str::quick_menu_reset), half, 0.0f))
        {
            action->press = QuickMenuPress::reset;
        }
        retro_ui_same_line();
        if (retro_ui_button_sized(piccolo_text(strings, Str::quick_menu_close_game), half, 0.0f))
        {
            action->press = QuickMenuPress::close_game;
        }
        retro_ui_separator();
        retro_ui_spacing();
        draw_states(menu, state, block, appearing, action);
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
    action->slot_picked = menu->slot != slot_before;
}

// ---------------------------------------------------------------------------
// Apply

void quick_menu_open(QuickMenu* menu, uint32_t slot)
{
    menu->open = true;
    menu->slot = slot >= LIBRETRO_STATE_SLOT_FIRST && slot <= LIBRETRO_STATE_SLOT_LAST ? slot : LIBRETRO_STATE_SLOT_FIRST;
    menu->refresh = true;
}

// Loads the selected slot's PNG into `pixels` (B8G8R8A8, opaque) when it isn't the one shown.
static void load_thumbnail(QuickMenu* menu)
{
    const StateSlotInfo* info = &menu->slots[menu->slot];
    if (!info->used || !info->has_thumbnail)
    {
        menu->has_picture = false;
        menu->shown_slot = 0;
        return;
    }
    if (menu->shown_slot == menu->slot && menu->shown_modified == info->modified)
    {
        return;
    }
    menu->shown_slot = menu->slot;
    menu->shown_modified = info->modified;
    menu->has_picture = false;
    SDL_Surface* png = SDL_LoadPNG(info->thumbnail_path);
    SDL_Surface* converted = png != nullptr ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_ARGB8888) : nullptr;
    if (converted != nullptr && converted->w > 0 && converted->h > 0 &&
        static_cast<uint32_t>(converted->w) <= THUMBNAIL_CAPACITY && static_cast<uint32_t>(converted->h) <= THUMBNAIL_CAPACITY)
    {
        menu->width = static_cast<uint32_t>(converted->w);
        menu->height = static_cast<uint32_t>(converted->h);
        menu->pixels.resize(static_cast<size_t>(menu->width) * menu->height);
        for (uint32_t y = 0; y < menu->height; y++)
        {
            const uint8_t* row = static_cast<const uint8_t*>(converted->pixels) + static_cast<size_t>(y) * static_cast<size_t>(converted->pitch);
            std::memcpy(&menu->pixels[static_cast<size_t>(y) * menu->width], row, static_cast<size_t>(menu->width) * 4);
        }
        menu->upload_pending = true;
    }
    else
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Quick menu: could not read thumbnail %s: %s", info->thumbnail_path,
                    SDL_GetError());
    }
    SDL_DestroySurface(converted);
    SDL_DestroySurface(png);
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
        load_thumbnail(menu);   // the slot picked this frame's, uploaded later in the frame
    }
}

// The thumbnail's texture (the renderer's, outside the toolkit).
void quick_menu_upload(QuickMenu* menu, AppShell* shell)
{
    if (!menu->upload_pending)
    {
        return;
    }
    if (!video_texture_created(&menu->texture))
    {
        if (!video_texture_create(&menu->texture, shell, THUMBNAIL_CAPACITY, THUMBNAIL_CAPACITY,
                                  FramePixelFormat::xrgb8888, false))
        {
            menu->upload_pending = false;
            return;
        }
    }
    if (!app_shell_acquire(shell))
    {
        return;   // try on the next frame
    }
    if (video_texture_upload(&menu->texture, shell, menu->pixels.data(), menu->width, menu->height))
    {
        menu->has_picture = true;
    }
    menu->upload_pending = false;
}

void quick_menu_destroy(QuickMenu* menu, AppShell* shell)
{
    video_texture_destroy(&menu->texture, shell);   // waits for the GPU
}
