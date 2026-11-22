#include "quick_menu.h"

#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

#include "imgui.h"

#include "app_shell.h"
#include "piccolo_text.h"
#include "retro_ui.h"

// Room for any thumbnail (the picture at x1: a few hundred pixels across).
constexpr uint32_t THUMBNAIL_CAPACITY = 1024;

// The used slots' button color (0xAARRGGBB), so they stand out from empty ones.
constexpr uint32_t USED_SLOT = 0xFF3A6EA5u;

void quick_menu_open(QuickMenu* menu, uint32_t slot)
{
    menu->open = true;
    menu->slot = slot >= LIBRETRO_STATE_SLOT_FIRST && slot <= LIBRETRO_STATE_SLOT_LAST ? slot : LIBRETRO_STATE_SLOT_FIRST;
    menu->refresh = true;
}

void quick_menu_refresh(QuickMenu* menu)
{
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

// The nine slot buttons; the focused one (arrows) or the clicked one is the selection.
static void draw_slots(QuickMenu* menu, float block, bool appearing)
{
    float size = 18.0f * block;
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        if (slot > LIBRETRO_STATE_SLOT_FIRST)
        {
            ImGui::SameLine();
        }
        bool used = menu->slots[slot].used;
        bool selected = slot == menu->slot;
        if (used)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, retro_ui_color_vec4(USED_SLOT));
        }
        if (selected)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, block);
        }
        // The focused button is the selection (arrows move it), so a slot
        // picked otherwise (opening, 1-9) takes the focus too.
        if ((appearing || menu->focus_slot) && selected)
        {
            ImGui::SetKeyboardFocusHere();
            menu->focus_slot = false;
        }
        char label[16] = {};
        std::snprintf(label, sizeof(label), "%u", slot);
        if (retro_ui_button_sized(label, size, size) || ImGui::IsItemFocused())
        {
            menu->slot = slot;
        }
        if (selected)
        {
            ImGui::PopStyleVar();
        }
        if (used)
        {
            ImGui::PopStyleColor();
        }
    }
    // Number keys pick a slot directly.
    for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
    {
        ImGuiKey key = static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_1) + static_cast<int>(slot - 1));
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(key, false))
        {
            menu->slot = slot;
            menu->focus_slot = true;
        }
    }
}

// The states: slots, the selected one's thumbnail and date, SAVE / LOAD.
static QuickMenuAction draw_states(QuickMenu* menu, const QuickMenuContext* context, float block, bool appearing)
{
    const Localization* strings = context->strings;
    const ImGuiIO& io = ImGui::GetIO();
    QuickMenuAction action = QuickMenuAction::none;
    draw_slots(menu, block, appearing);
    load_thumbnail(menu);

    // The thumbnail at the biggest whole-number scale that fits (sharp pixels:
    // nearest filtering, on whole pixels), the slot's details beside it. Room:
    // most of the width, the height left under the slots.
    const StateSlotInfo* info = &menu->slots[menu->slot];
    ImVec2 room = ImGui::GetContentRegionAvail();
    room.x = static_cast<float>(static_cast<int>(room.x * 0.6f));
    room.y = static_cast<float>(static_cast<int>(room.y - ImGui::GetStyle().ItemSpacing.y));
    room.x = room.x > 160.0f * block ? room.x : 160.0f * block;
    room.y = room.y > 120.0f * block ? room.y : 120.0f * block;
    bool showing = menu->has_picture && menu->shown_slot == menu->slot && menu->texture.imgui_id != 0 &&
                   menu->width > 0 && menu->height > 0;
    ImVec2 box(room.x, room.x * 3.0f / 4.0f);   // no picture: a 4:3 box
    if (box.y > room.y)
    {
        box = ImVec2(room.y * 4.0f / 3.0f, room.y);
    }
    if (showing)
    {
        float width = static_cast<float>(menu->width);
        float height = static_cast<float>(menu->height);
        int scale_x = static_cast<int>(room.x / width);
        int scale_y = static_cast<int>(room.y / height);
        int scale = scale_x < scale_y ? scale_x : scale_y;
        if (scale >= 1)
        {
            box = ImVec2(width * static_cast<float>(scale), height * static_cast<float>(scale));
        }
        else
        {
            // Not even once: shrunk to fit (it can't be whole pixels then).
            float aspect = width / height;
            box = ImVec2(room.x, room.x / aspect);
            if (box.y > room.y)
            {
                box = ImVec2(room.y * aspect, room.y);
            }
        }
    }
    box = ImVec2(static_cast<float>(static_cast<int>(box.x)), static_cast<float>(static_cast<int>(box.y)));
    ImVec2 corner = ImGui::GetCursorScreenPos();
    corner = ImVec2(static_cast<float>(static_cast<int>(corner.x)), static_cast<float>(static_cast<int>(corner.y)));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(corner, ImVec2(corner.x + box.x, corner.y + box.y), IM_COL32(0, 0, 0, 255));
    if (showing)
    {
        float u = 0.0f;
        float v = 0.0f;
        video_texture_uv_max(&menu->texture, &u, &v);
        const ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        draw_list->AddCallback(platform_io.DrawCallback_SetSamplerNearest, nullptr);
        draw_list->AddImage(ImTextureRef(static_cast<ImTextureID>(menu->texture.imgui_id)), corner,
                            ImVec2(corner.x + box.x, corner.y + box.y), ImVec2(0.0f, 0.0f), ImVec2(u, v));
        draw_list->AddCallback(platform_io.DrawCallback_SetSamplerLinear, nullptr);
    }
    ImGui::Dummy(box);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("%s %u", piccolo_text(strings, Str::quick_menu_slot), menu->slot);
    if (info->used)
    {
        SDL_DateTime date = {};
        SDL_TimeToDateTime(info->modified, &date, true);
        ImGui::Text("%04d-%02d-%02d", date.year, date.month, date.day);   // two lines: the panel is narrow
        ImGui::Text("%02d:%02d", date.hour, date.minute);
    }
    else
    {
        ImGui::TextDisabled("%s", piccolo_text(strings, Str::quick_menu_empty));
    }
    ImGui::Spacing();
    bool keys = !io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (retro_ui_button(piccolo_text(strings, Str::quick_menu_save)) || (keys && ImGui::IsKeyPressed(ImGuiKey_S, false)))
    {
        action = QuickMenuAction::save;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!info->used);
    if (retro_ui_button(piccolo_text(strings, Str::quick_menu_load)) ||
        (info->used && keys && ImGui::IsKeyPressed(ImGuiKey_L, false)))
    {
        action = QuickMenuAction::load;
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", piccolo_text(strings, Str::quick_menu_keys));
    ImGui::PopTextWrapPos();
    return action;
}

QuickMenuAction quick_menu_draw(QuickMenu* menu, const QuickMenuContext* context, ImFont* pixel_font, int32_t scale)
{
    if (!menu->open)
    {
        return QuickMenuAction::none;
    }
    const Localization* strings = context->strings;
    if (menu->refresh)
    {
        menu->refresh = false;
        for (uint32_t slot = LIBRETRO_STATE_SLOT_FIRST; slot <= LIBRETRO_STATE_SLOT_LAST; slot++)
        {
            core_session_slot_info(context->session, slot, &menu->slots[slot]);
        }
        menu->shown_slot = 0;   // the selected thumbnail may have changed
    }

    QuickMenuAction action = QuickMenuAction::none;
    float block = static_cast<float>(scale);
    const ImGuiIO& io = ImGui::GetIO();
    retro_ui_push_theme(pixel_font, scale);
    // A window in the middle, 320 x 250 retro pixels, never bigger than the
    // screen. The same size every frame (the thumbnail takes the room left).
    ImVec2 size(320.0f * block, 250.0f * block);
    size.x = size.x < io.DisplaySize.x - 8.0f * block ? size.x : io.DisplaySize.x - 8.0f * block;
    size.y = size.y < io.DisplaySize.y - 8.0f * block ? size.y : io.DisplaySize.y - 8.0f * block;
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(static_cast<int>(size.x)), static_cast<float>(static_cast<int>(size.y))),
                             ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    char title[200] = {};
    std::snprintf(title, sizeof(title), "%s###quick_menu", piccolo_text(strings, Str::quick_menu_title));
    if (ImGui::Begin(title, nullptr, flags))
    {
        retro_ui_begin_window(scale);
        if (retro_ui_close_button())
        {
            menu->open = false;
        }
        bool appearing = ImGui::IsWindowAppearing();
        if (appearing)
        {
            ImGui::SetWindowFocus();
        }
        const ImGuiStyle& style = ImGui::GetStyle();

        // What runs (the pixel font draws it in capitals).
        if (context->game_name[0] != '\0')
        {
            ImGui::TextDisabled("%s - %s", context->game_name, context->core_name);
        }
        else
        {
            ImGui::TextDisabled("%s", context->core_name);
        }
        ImGui::Spacing();
        // The actions on the running core: PAUSE, DVR and CONFIG (CORE
        // CONFIGURATION), then RESET and CLOSE GAME, a row apart.
        float width = ImGui::GetContentRegionAvail().x;
        float third = static_cast<float>(static_cast<int>((width - 2.0f * style.ItemSpacing.x) / 3.0f));
        float half = static_cast<float>(static_cast<int>((width - style.ItemSpacing.x) / 2.0f));
        if (retro_ui_button_sized(piccolo_text(strings, context->user_paused ? Str::quick_menu_resume : Str::quick_menu_pause),
                                  third, 0.0f))
        {
            action = QuickMenuAction::pause;
        }
        ImGui::SameLine();
        if (retro_ui_button_sized(piccolo_text(strings, Str::quick_menu_dvr), third, 0.0f))
        {
            action = QuickMenuAction::dvr;   // a window widget: it replaces QUICK MENU
        }
        ImGui::SameLine();
        if (retro_ui_button_sized(piccolo_text(strings, Str::quick_menu_config), third, 0.0f))
        {
            action = QuickMenuAction::config;   // CORE CONFIGURATION, a MAIN MENU window
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::quick_menu_config_help));
        if (retro_ui_button_sized(piccolo_text(strings, Str::quick_menu_reset), half, 0.0f))
        {
            action = QuickMenuAction::reset;
        }
        ImGui::SameLine();
        if (retro_ui_button_sized(piccolo_text(strings, Str::quick_menu_close_game), half, 0.0f))
        {
            action = QuickMenuAction::close_game;
        }
        ImGui::Separator();
        ImGui::Spacing();
        QuickMenuAction states = draw_states(menu, context, block, appearing);
        action = states != QuickMenuAction::none ? states : action;
        retro_ui_end_window();
    }
    ImGui::End();
    retro_ui_pop_theme();
    return action;
}
