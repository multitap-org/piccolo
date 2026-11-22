#include "onscreen_pad.h"

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_internal.h"   // BringWindowToDisplayFront (EDIT LAYOUT on top)
#include "input_devices.h"
#include "pad_layout.h"
#include "retro_ui.h"
#include "touch_ui.h"

constexpr float BELOW_GAP = 4.0f;   // retro pixels between the picture and the pad (pad_layouts_place)

void onscreen_pad_init(OnscreenPad* pad, const char* settings_dir)
{
    pad_layouts_init(&pad->layouts);
    if (settings_dir[0] != '\0')
    {
        pad->path = std::string(settings_dir) + "/touch_layouts.cfg";
        settings_file_load(&pad->file, pad->path.c_str());   // (none yet: the defaults)
        pad_layouts_load_file(&pad->layouts, &pad->file);
    }
    haptics_open(&pad->haptics);
    // A touch screen (AUTO): asked once here (SDL allocates the list); a
    // finger later says so too.
    int count = 0;
    SDL_TouchID* touches = SDL_GetTouchDevices(&count);
    SDL_free(touches);
    pad->touch_screen = count > 0;
}

void onscreen_pad_shutdown(OnscreenPad* pad)
{
    haptics_close(&pad->haptics);
}

static void save_layouts(OnscreenPad* pad)
{
    if (pad->path.empty())
    {
        return;
    }
    pad_layouts_save_file(&pad->layouts, &pad->file);
    settings_file_save(&pad->file, pad->path.c_str(), "piccolo's on-screen gamepad layouts (EDIT LAYOUT)");
}

void onscreen_pad_event(OnscreenPad* pad, const SDL_Event* event, float display_width, float display_height)
{
    if (event->type == SDL_EVENT_FINGER_DOWN)
    {
        pad->touch_screen = true;
    }
    touch_fingers_event(&pad->fingers, event, display_width, display_height);
}

bool onscreen_pad_enabled(const OnscreenPad* pad, OnscreenPadShow show)
{
    if (show == OnscreenPadShow::on)
    {
        return true;
    }
    if (show == OnscreenPadShow::off)
    {
        return false;
    }
    return pad->touch_screen;   // AUTO
}

void onscreen_pad_edit_end(OnscreenPad* pad)
{
    if (pad->editor.active)
    {
        pad->editor.active = false;
        save_layouts(pad);
    }
}

// A real gamepad played (a button held): AUTO hides the pad.
static bool real_gamepad_played(const OnscreenPad* pad, const InputSystem* input)
{
    for (size_t i = 0; i < input->devices.devices.size(); i++)
    {
        const InputDevice* device = &input->devices.devices[i];
        if (device->kind != InputDeviceKind::gamepad || !device->connected || device->game_only || device->retropad ||
            (pad->plugged && device->id == pad->device))
        {
            continue;
        }
        for (uint32_t b = 0; b < INPUT_BUTTON_COUNT; b++)
        {
            if (device->held[b] != 0)
            {
                return true;
            }
        }
    }
    return false;
}

void onscreen_pad_update(OnscreenPad* pad, InputSystem* input, bool enabled, OnscreenPadShow show, bool playing)
{
    if (enabled && !pad->plugged)
    {
        pad->device = input_devices_add(&input->devices, InputDeviceKind::gamepad, "touch", "TOUCH PAD");
        InputDevice* device = input_devices_find(&input->devices, pad->device);
        if (device != nullptr)
        {
            device->retropad = true;               // hands a RetroPad (the layouts' parts)
            device->ignore = InputIgnore::no;      // (a finger held on a stick isn't a stuck receiver)
        }
        input_add_controllers_for_device(input, pad->device);
        pad->plugged = true;
        SDL_Log("Touch pad: plugged in");
    }
    else if (!enabled && pad->plugged)
    {
        input_devices_disconnect(&input->devices, pad->device);   // (plugged again later: the same device comes back)
        pad->plugged = false;
        SDL_Log("Touch pad: unplugged");
    }
    // AUTO: a real gamepad hides it, a finger on the screen brings it back.
    if (show == OnscreenPadShow::automatic)
    {
        if (real_gamepad_played(pad, input))
        {
            pad->hidden = true;
        }
        for (int32_t f = 0; f < pad->fingers.count; f++)
        {
            pad->hidden = pad->hidden && !pad->fingers.fingers[f].began;
        }
    }
    else
    {
        pad->hidden = false;
    }
    if (pad->plugged)
    {
        RetroPad none;
        const RetroPad* state = playing && !pad->hidden && !pad->editor.active ? &pad->pad : &none;
        input_devices_set_retropad(&input->devices, pad->device, state->buttons, state->analog);
    }
}

// A controller layout's labels by SDL position (InputMapControl) onto the
// RetroPad: the bottom face button is B, the right one A, and so on (the
// same places input_bindings maps a real gamepad to).
struct StyleButton
{
    InputMapControl control;
    RetroPadButton  button;
};

static const StyleButton STYLE_BUTTONS[] = {
    {InputMapControl::south, RetroPadButton::b},           {InputMapControl::east, RetroPadButton::a},
    {InputMapControl::west, RetroPadButton::y},            {InputMapControl::north, RetroPadButton::x},
    {InputMapControl::left_shoulder, RetroPadButton::l},   {InputMapControl::right_shoulder, RetroPadButton::r},
    {InputMapControl::left_trigger, RetroPadButton::l2},   {InputMapControl::right_trigger, RetroPadButton::r2},
    {InputMapControl::back, RetroPadButton::select},       {InputMapControl::start, RetroPadButton::start},
    {InputMapControl::left_stick, RetroPadButton::l3},     {InputMapControl::right_stick, RetroPadButton::r3},
};

void onscreen_pad_set_style(OnscreenPad* pad, const InputLayout* layout, const char* name)
{
    pad->style_name = name;
    pad->style = PadStyle{};
    pad->styled = layout != nullptr;
    if (layout == nullptr)
    {
        return;
    }
    for (size_t i = 0; i < sizeof(STYLE_BUTTONS) / sizeof(STYLE_BUTTONS[0]); i++)
    {
        const std::string* label = &layout->labels[static_cast<int32_t>(STYLE_BUTTONS[i].control)];
        if (!label->empty())
        {
            pad->style.labels[static_cast<int32_t>(STYLE_BUTTONS[i].button)] = label->c_str();
        }
    }
    if (layout->dpad)
    {
        // The arrows stay ("": the part's own label).
        pad->style.labels[static_cast<int32_t>(RetroPadButton::up)] = "";
        pad->style.labels[static_cast<int32_t>(RetroPadButton::down)] = "";
        pad->style.labels[static_cast<int32_t>(RetroPadButton::left)] = "";
        pad->style.labels[static_cast<int32_t>(RetroPadButton::right)] = "";
    }
    pad->style.sticks = layout->sticks > 0;
    pad->style.six_face = layout->six_face;
}

void onscreen_pad_place(OnscreenPad* pad, bool playing, float aspect, float width, float height)
{
    pad->shown = pad->editor.active || (pad->plugged && playing && !pad->hidden);
    // Landscape over the picture; upright under it if it fits, else over a
    // tall picture (retroui decides, the same as piccoletto's REMOTE PLAY).
    float block = static_cast<float>(touch_ui_scale_for(width, height));
    pad->placement = pad_layouts_place(&pad->layouts, width / block, height / block, aspect, BELOW_GAP);
    pad->layout = pad->placement.id;
    pad->below = pad->shown && pad->placement.below;
    pad->picture_height = pad->below ? pad->placement.picture_height * block : height;
}

void onscreen_pad_draw(OnscreenPad* pad, ImFont* pixel_font, const char* menu_label, const char* quick_label, float opacity,
                       bool vibrate)
{
    pad->menu_pressed = false;
    pad->quick_pressed = false;
    pad->edit_done = false;
    pad->pad = RetroPad{};
    if (!pad->shown)
    {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    TouchUi ui;
    ui.font = pixel_font;
    ui.scale = touch_ui_scale_for(io.DisplaySize.x, io.DisplaySize.y);
    ui.palette = *retro_ui_palette();
    ui.fingers = &pad->fingers;
    ui.pad = &pad->pad;
    for (int32_t i = 0; i < PAD_LAYOUT_COUNT; i++)
    {
        pad_layout_set_default_alpha(&pad->layouts.layouts[i], opacity);   // (a layout's own, from EDIT LAYOUT, wins)
        pad->layouts.layouts[i].style = pad->styled ? &pad->style : nullptr;
    }
    PadLayout* layout = pad_layouts_get(&pad->layouts, pad->layout);
    layout->menu_label = menu_label;     // retroui's menu button: the MAIN MENU
    layout->action_label = quick_label;  // its action button: the QUICK MENU
    float width = touch_ui_width(&ui);
    float height = touch_ui_height(&ui);
    PadFrame frame = pad->placement.frame;
    // The overlay window takes the taps meanwhile (nothing under it reacts).
    touch_ui_begin_overlay(&ui, "##onscreen_pad");
    if (pad->editor.active)
    {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());   // EDIT LAYOUT: above everything
    }
    if (pad->below)
    {
        float top = pad->placement.picture_height;
        touch_ui_rect(&ui, 0.0f, top, width, height - top, ui.palette.button_face);   // the pad's body
    }
    if (pad->editor.active)
    {
        if (pad_editor_draw(&pad->editor, &ui, &frame, layout))
        {
            pad->editor.active = false;
            pad->edit_done = true;
            save_layouts(pad);
        }
    }
    else
    {
        pad_layout_draw(&ui, &frame, layout);
        pad->menu_pressed = layout->menu_pressed;
        pad->quick_pressed = layout->action_pressed;
        if (vibrate && pad->fingers.presses > 0)
        {
            haptics_buzz(&pad->haptics);
        }
    }
    touch_ui_end_screen();
}

void onscreen_pad_end_frame(OnscreenPad* pad)
{
    touch_fingers_end_frame(&pad->fingers);
}

void onscreen_pad_edit(OnscreenPad* pad)
{
    pad->editor = PadEditor{};
    pad->editor.active = true;
    pad->hidden = false;
}
