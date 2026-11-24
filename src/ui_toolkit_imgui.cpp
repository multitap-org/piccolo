// ui_toolkit.h answered by ImGui.

#include "ui_toolkit.h"

#include "imgui.h"

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
