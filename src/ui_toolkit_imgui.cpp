// ui_toolkit.h answered by ImGui.

#include "ui_toolkit.h"

#include <string>

#include "imgui.h"

#include "debug_ui.h"
#include "pixel_font_imgui.h"
#include "retro_ui.h"

static int32_t g_scale = 1;
static std::string g_layout_file;   // ImGui keeps a pointer to it

// The theme's text shadows, added to what ImGui is about to draw.
static void add_text_shadows(ImDrawData* draw_data, void* user)
{
    static_cast<void>(user);
    retro_ui_text_shadows(draw_data, g_scale);
}

ImFont* ui_toolkit_start()
{
    debug_ui_set_draw_data_hook(add_text_shadows, nullptr);
    return pixel_font_imgui_add();
}

void ui_toolkit_set_layout_file(const char* path)
{
    if (path == nullptr)
    {
        ImGui::GetIO().IniFilename = nullptr;
        return;
    }
    g_layout_file = path;
    ImGui::GetIO().IniFilename = g_layout_file.c_str();
}

void ui_toolkit_set_scale(int32_t scale)
{
    g_scale = scale;
}

static ImGuiKey imgui_key(UiNavKey key)
{
    switch (key)
    {
    case UiNavKey::up:
        return ImGuiKey_UpArrow;
    case UiNavKey::down:
        return ImGuiKey_DownArrow;
    case UiNavKey::left:
        return ImGuiKey_LeftArrow;
    case UiNavKey::right:
        return ImGuiKey_RightArrow;
    case UiNavKey::accept:
        return ImGuiKey_Space;
    case UiNavKey::back:
        return ImGuiKey_Escape;
    }
    return ImGuiKey_None;
}

bool ui_toolkit_has_popup()
{
    return ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
}

bool ui_toolkit_wants_text()
{
    return ImGui::GetIO().WantTextInput;
}

void ui_toolkit_nav_key(UiNavKey key, bool down)
{
    ImGui::GetIO().AddKeyEvent(imgui_key(key), down);
}

void ui_toolkit_clear_keys()
{
    ImGui::GetIO().ClearInputKeys();
}

void ui_toolkit_screen_size(float* width, float* height)
{
    const ImGuiIO& io = ImGui::GetIO();
    *width = io.DisplaySize.x;
    *height = io.DisplaySize.y;
}

void ui_toolkit_demo_window(bool* open)
{
    ImGui::ShowDemoWindow(open);
}
