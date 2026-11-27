#include "onscreen_pad.h"

#include <cmath>
#include <cstdio>

#include <SDL3/SDL.h>

#include "input_devices.h"
#include "pad_layout.h"
#include "pixel_font_imgui.h"
#include "piccolo_settings.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "touch_ui.h"
#include "ui_toolkit.h"

constexpr float BELOW_GAP = 4.0f;   // retro pixels between the picture and the pad (pad_layouts_place)

// The burger menu's pop-up (retro pixels): its items, stacked.
constexpr float MENU_ITEM_WIDTH = 120.0f;
constexpr float MENU_ITEM_HEIGHT = 28.0f;
constexpr float MENU_ITEM_GAP = 4.0f;
constexpr int32_t MENU_ITEM_COUNT = 5;
constexpr float MENU_HELD_FILL_MIN = 0.5f;   // a held item shows at least this solid, as a held part does

// ---------------------------------------------------------------------------
// State

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

// A real gamepad played (a button held): AUTO hides the pad.
// Whether two devices' controllers share a port (any port either is on).
static bool share_a_port(const InputSystem* input, uint32_t device_a, uint32_t device_b)
{
    for (uint32_t p = 0; p < INPUT_PORT_COUNT; p++)
    {
        bool has_a = false;
        bool has_b = false;
        for (size_t m = 0; m < input->ports[p].size(); m++)
        {
            uint32_t controller = input->ports[p][m];
            if (controller < input->controllers.size())
            {
                has_a = has_a || input->controllers[controller].device_id == device_a;
                has_b = has_b || input->controllers[controller].device_id == device_b;
            }
        }
        if (has_a && has_b)
        {
            return true;
        }
    }
    return false;
}

// A real gamepad on the touch pad's port pressed something: the player there
// picked up a controller (Andrés: a gamepad on another port, another
// player, leaves the touch pad alone).
static bool real_gamepad_played(const OnscreenPad* pad, const InputSystem* input)
{
    if (!pad->plugged)
    {
        return false;
    }
    for (size_t i = 0; i < input->devices.devices.size(); i++)
    {
        const InputDevice* device = &input->devices.devices[i];
        if (device->kind != InputDeviceKind::gamepad || !device->connected || device->game_only || device->retropad ||
            device->id == pad->device || !share_a_port(input, device->id, pad->device))
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
            device->shares_port1 = true;           // on port 1 at once, a gamepad's START still takes port 1
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

// The theme's levels (ui_pad_*, in the settings with the rest of the palette)
// or the user's own (touch_pad_*), whichever is in force (touch_pad_look_set).
PadLook onscreen_pad_look(const Settings* settings)
{
    const char* fill = PICCOLO_UI_PAD_FILL;
    const char* border = PICCOLO_UI_PAD_BORDER;
    const char* labels = PICCOLO_UI_PAD_LABELS;
    const char* background = PICCOLO_UI_PAD_BACKGROUND;
    if (settings_get_bool(settings, PICCOLO_TOUCH_PAD_LOOK_SET))
    {
        fill = PICCOLO_TOUCH_PAD_OPACITY;
        border = PICCOLO_TOUCH_PAD_BORDER_OPACITY;
        labels = PICCOLO_TOUCH_PAD_LABEL_OPACITY;
        background = PICCOLO_TOUCH_PAD_BACKGROUND_OPACITY;
    }
    PadLook look;
    look.fill = static_cast<float>(settings_get_int(settings, fill)) / 100.0f;
    look.border = static_cast<float>(settings_get_int(settings, border)) / 100.0f;
    look.labels = static_cast<float>(settings_get_int(settings, labels)) / 100.0f;
    look.background = static_cast<float>(settings_get_int(settings, background)) / 100.0f;
    return look;
}

void onscreen_pad_state(OnscreenPadState* state, const Settings* settings, const Localization* strings)
{
    state->menu_label = PIXEL_FONT_UTF8_MENU;
    state->main_menu_label = piccolo_text(strings, Str::map_role_menu);
    state->quick_menu_label = piccolo_text(strings, Str::quick_menu_title);
    state->game_dvr_label = piccolo_text(strings, Str::quick_menu_dvr);
    state->controllers_label = piccolo_text(strings, Str::touch_pad_controllers);
    state->edit_layout_label = piccolo_text(strings, Str::input_touch_pad_edit);
    state->look = onscreen_pad_look(settings);
    state->vibrate = settings_get_bool(settings, PICCOLO_TOUCH_PAD_VIBRATE);
}

void onscreen_pad_end_frame(OnscreenPad* pad)
{
    touch_fingers_end_frame(&pad->fingers);
}

// ---------------------------------------------------------------------------
// Drawing

// The space the picture leaves empty: the theme's pad_background at the look's
// BACKGROUND opacity over black (so the same drawn opaque, no game behind it
// to show through). `width` x `height` is the screen in retro pixels.
static void draw_empty_space(const TouchUi* ui, const OnscreenPadState* state, float width, float height)
{
    uint32_t background = ui->palette.pad_background;
    float alpha = state->look.background;
    uint32_t red = static_cast<uint32_t>(static_cast<float>((background >> 16) & 0xFFu) * alpha + 0.5f);
    uint32_t green = static_cast<uint32_t>(static_cast<float>((background >> 8) & 0xFFu) * alpha + 0.5f);
    uint32_t blue = static_cast<uint32_t>(static_cast<float>(background & 0xFFu) * alpha + 0.5f);
    uint32_t over_black = 0xFF000000u | (red << 16) | (green << 8) | blue;
    const OnscreenPadPicture* picture = &state->picture;
    if (!picture->shown)
    {
        touch_ui_rect(ui, 0.0f, 0.0f, width, height, over_black);
        return;
    }
    // The picture's edges in retro pixels, half a window pixel in so the
    // floor of the toolkit's rounding lands on the pixel (no seam).
    float scale = static_cast<float>(ui->scale);
    float left = (picture->x + 0.5f) / scale;
    float top = (picture->y + 0.5f) / scale;
    float right = (picture->x + picture->width + 0.5f) / scale;
    float bottom = (picture->y + picture->height + 0.5f) / scale;
    touch_ui_rect(ui, 0.0f, 0.0f, width, top, over_black);
    touch_ui_rect(ui, 0.0f, bottom, width, height - bottom, over_black);
    touch_ui_rect(ui, 0.0f, top, left, bottom - top, over_black);
    touch_ui_rect(ui, right, top, width - right, bottom - top, over_black);
}

// The burger menu's pop-up: centered over the picture (the screen without
// one), the items stacked, in the pad_center colors at the look's opacities
// (fill for the faces, labels for the text). A tap anywhere else closes it.
static OnscreenPadMenuPress draw_menu(const TouchUi* ui, const OnscreenPadState* state)
{
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    float scale = static_cast<float>(ui->scale);
    OnscreenPadMenuPress press = OnscreenPadMenuPress::none;
    float area_x = 0.0f;
    float area_y = 0.0f;
    float area_width = width;
    float area_height = height;
    if (state->picture.shown)
    {
        area_x = state->picture.x / scale;
        area_y = state->picture.y / scale;
        area_width = state->picture.width / scale;
        area_height = state->picture.height / scale;
    }
    float total = static_cast<float>(MENU_ITEM_COUNT) * MENU_ITEM_HEIGHT + static_cast<float>(MENU_ITEM_COUNT - 1) * MENU_ITEM_GAP;
    float x = std::floor(area_x + (area_width - MENU_ITEM_WIDTH) * 0.5f);
    float y = std::floor(area_y + (area_height - total) * 0.5f);
    x = std::fmax(0.0f, std::fmin(x, width - MENU_ITEM_WIDTH));
    y = std::fmax(0.0f, std::fmin(y, height - total));
    const char* labels[MENU_ITEM_COUNT] = {state->main_menu_label, state->quick_menu_label, state->game_dvr_label,
                                           state->controllers_label, state->edit_layout_label};
    const OnscreenPadMenuPress picks[MENU_ITEM_COUNT] = {
        OnscreenPadMenuPress::main_menu, OnscreenPadMenuPress::quick_menu, OnscreenPadMenuPress::game_dvr,
        OnscreenPadMenuPress::controllers, OnscreenPadMenuPress::edit_layout};
    const RetroUiPalette* palette = &ui->palette;
    for (int32_t i = 0; i < MENU_ITEM_COUNT; i++)
    {
        float item_y = y + static_cast<float>(i) * (MENU_ITEM_HEIGHT + MENU_ITEM_GAP);
        char id[32];
        std::snprintf(id, sizeof(id), "##pad_menu_%d", static_cast<int>(i));
        bool item_held = false;
        if (touch_ui_area(ui, id, x, item_y, MENU_ITEM_WIDTH, MENU_ITEM_HEIGHT, &item_held, TouchNav::reachable))
        {
            press = picks[i];
        }
        float fill = item_held && state->look.fill < MENU_HELD_FILL_MIN ? MENU_HELD_FILL_MIN : state->look.fill;
        touch_ui_rect(ui, x, item_y, MENU_ITEM_WIDTH, MENU_ITEM_HEIGHT,
                      retro_ui_color_with_alpha(item_held ? palette->pad_center_held : palette->pad_center, fill));
        if (state->look.border > 0.0f)
        {
            touch_ui_outline(ui, x, item_y, MENU_ITEM_WIDTH, MENU_ITEM_HEIGHT, 1.0f,
                             retro_ui_color_with_alpha(palette->pad_border, state->look.border));
        }
        float text_width = touch_ui_text_width(ui, labels[i], TouchText::big);
        touch_ui_text(ui, std::floor(x + (MENU_ITEM_WIDTH - text_width) * 0.5f),
                      touch_ui_centered_text_y(ui, item_y, MENU_ITEM_HEIGHT, TouchText::big), labels[i],
                      retro_ui_color_with_alpha(palette->pad_center_text, item_held ? 1.0f : state->look.labels),
                      TouchText::big);
    }
    // The tap outside: submitted after the items. In ImGui the first item
    // under the finger takes it (later ones overlapping it are not hovered),
    // so with this one first every tap on an item closed the menu instead.
    bool outside_held = false;
    if (touch_ui_area(ui, "##pad_menu_outside", 0.0f, 0.0f, width, height, &outside_held, TouchNav::taps_only) &&
        press == OnscreenPadMenuPress::none)
    {
        press = OnscreenPadMenuPress::close;
    }
    return press;
}

OnscreenPadMenuPress onscreen_pad_menu_preview(const OnscreenPadState* state, RetroUiFont* pixel_font)
{
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    TouchUi ui;
    ui.font = pixel_font;
    ui.scale = touch_ui_scale_for(screen_width, screen_height);
    ui.palette = *retro_ui_palette();
    OnscreenPadState whole_screen = *state;
    whole_screen.picture = {};   // no game here: the screen's middle
    touch_ui_begin_overlay(&ui, "##pad_menu_preview");
    OnscreenPadMenuPress press = draw_menu(&ui, &whole_screen);
    touch_ui_end_screen();
    return press;
}

void onscreen_pad_draw(OnscreenPad* pad, const OnscreenPadState* state, RetroUiFont* pixel_font,
                       OnscreenPadAction* action)
{
    *action = {};
    pad->pad = RetroPad{};
    if (!pad->shown)
    {
        pad->menu_open = false;
        return;
    }
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    TouchUi ui;
    ui.font = pixel_font;
    ui.scale = touch_ui_scale_for(screen_width, screen_height);
    ui.palette = *retro_ui_palette();
    // While the burger menu is up it takes the taps: the pad's parts take no
    // finger (and so the burger itself closes it, as a tap outside).
    pad->menu_open = pad->menu_open && !pad->editor.active;
    ui.fingers = pad->menu_open ? nullptr : &pad->fingers;
    ui.pad = &pad->pad;
    // The same look in every layout, upright or sideways, under the picture
    // or over it (Andrés, 2026-11-26): the theme's levels, or the settings'
    // once set, which EDIT LAYOUT's sliders change (they reach the settings
    // in onscreen_pad_apply).
    pad->look = state->look;
    pad->editor.look = &pad->look;
    for (int32_t i = 0; i < PAD_LAYOUT_COUNT; i++)
    {
        pad->layouts.layouts[i].look = &pad->look;
        pad->layouts.layouts[i].style = pad->styled ? &pad->style : nullptr;
    }
    PadLayout* layout = pad_layouts_get(&pad->layouts, pad->layout);
    layout->menu_label = state->menu_label;     // retroui's menu button: the burger
    layout->action_label = nullptr;             // (its action button is piccoletto's HOTKEY)
    float width = touch_ui_width(&ui);
    float height = touch_ui_height(&ui);
    PadFrame frame = pad->placement.frame;
    // The overlay window takes the taps meanwhile (nothing under it reacts).
    touch_ui_begin_overlay(&ui, "##onscreen_pad");
    if (pad->editor.active)
    {
        retro_ui_bring_window_to_front();   // EDIT LAYOUT: above everything
    }
    draw_empty_space(&ui, state, width, height);   // before the parts
    bool open_menu = false;
    if (pad->editor.active)
    {
        if (pad_editor_draw(&pad->editor, &ui, &frame, layout))
        {
            action->edit_done = true;
        }
        if (pad->editor.look_changed)
        {
            pad->editor.look_changed = false;
            action->look_changed = true;
            action->look = pad->look;
        }
    }
    else
    {
        pad_layout_draw(&ui, &frame, layout);
        open_menu = !pad->menu_open && layout->menu_pressed;
        action->buzz = state->vibrate && pad->fingers.presses > 0;
    }
    if (pad->menu_open)
    {
        OnscreenPadMenuPress press = draw_menu(&ui, state);
        action->main_menu = press == OnscreenPadMenuPress::main_menu;
        action->quick_menu = press == OnscreenPadMenuPress::quick_menu;
        action->game_dvr = press == OnscreenPadMenuPress::game_dvr;
        action->controllers = press == OnscreenPadMenuPress::controllers;
        action->edit_layout = press == OnscreenPadMenuPress::edit_layout;
        if (press != OnscreenPadMenuPress::none)
        {
            pad->menu_open = false;
        }
    }
    // Opened last: the pop-up shows from the next frame, so the finger that
    // pressed the burger (still down) isn't a tap on its backdrop.
    if (open_menu)
    {
        pad->menu_open = true;
    }
    touch_ui_end_screen();
}

// ---------------------------------------------------------------------------
// Apply

static void save_layouts(OnscreenPad* pad)
{
    if (pad->path.empty())
    {
        return;
    }
    pad_layouts_save_file(&pad->layouts, &pad->file);
    settings_file_save(&pad->file, pad->path.c_str(), "piccolo's on-screen gamepad layouts (EDIT LAYOUT)");
}

void onscreen_pad_edit_end(OnscreenPad* pad)
{
    if (pad->editor.active)
    {
        pad->editor.active = false;
        save_layouts(pad);
    }
}

void onscreen_pad_edit(OnscreenPad* pad)
{
    pad->editor = PadEditor{};
    pad->editor.active = true;
    pad->hidden = false;
}

// `key` takes `theme_key`'s value (only when it differs: a set marks the
// settings changed).
static void follow_level(Settings* settings, const char* key, const char* theme_key)
{
    int32_t level = settings_get_int(settings, theme_key);
    if (settings_get_int(settings, key) != level)
    {
        settings_set_int(settings, SettingLayer::global, key, level);
    }
}

void onscreen_pad_apply(OnscreenPad* pad, const OnscreenPadAction* action, Settings* settings)
{
    if (action->look_changed)
    {
        settings_set_int(settings, SettingLayer::global, PICCOLO_TOUCH_PAD_OPACITY,
                         static_cast<int32_t>(std::lround(action->look.fill * 100.0f)));
        settings_set_int(settings, SettingLayer::global, PICCOLO_TOUCH_PAD_BORDER_OPACITY,
                         static_cast<int32_t>(std::lround(action->look.border * 100.0f)));
        settings_set_int(settings, SettingLayer::global, PICCOLO_TOUCH_PAD_LABEL_OPACITY,
                         static_cast<int32_t>(std::lround(action->look.labels * 100.0f)));
        settings_set_int(settings, SettingLayer::global, PICCOLO_TOUCH_PAD_BACKGROUND_OPACITY,
                         static_cast<int32_t>(std::lround(action->look.background * 100.0f)));
        // From now on these win for every theme.
        settings_set_bool(settings, SettingLayer::global, PICCOLO_TOUCH_PAD_LOOK_SET, true);
    }
    // The theme's look in force: the settings rows show its values.
    if (!settings_get_bool(settings, PICCOLO_TOUCH_PAD_LOOK_SET))
    {
        follow_level(settings, PICCOLO_TOUCH_PAD_OPACITY, PICCOLO_UI_PAD_FILL);
        follow_level(settings, PICCOLO_TOUCH_PAD_BORDER_OPACITY, PICCOLO_UI_PAD_BORDER);
        follow_level(settings, PICCOLO_TOUCH_PAD_LABEL_OPACITY, PICCOLO_UI_PAD_LABELS);
        follow_level(settings, PICCOLO_TOUCH_PAD_BACKGROUND_OPACITY, PICCOLO_UI_PAD_BACKGROUND);
    }
    if (action->edit_done)
    {
        onscreen_pad_edit_end(pad);
    }
    if (action->buzz)
    {
        haptics_buzz(&pad->haptics);
    }
}
