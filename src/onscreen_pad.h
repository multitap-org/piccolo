#pragma once

// The on-screen gamepad (Andrés, 2026-11-20): piccoletto's touch pad
// (retroui's pad_layout, the same layouts and EDIT LAYOUT) over the game.
//
//   - A controller like any other in the input router: "TOUCH PAD", a
//     device that hands a RetroPad (input_devices_set_retropad, as phones'
//     pads do). START joins a port; remaps, turbo, DEVICES all work on it.
//   - Landscape: over the picture. Upright: under the picture when it fits
//     (the picture is drawn in the space above it), else over its lower part
//     (a tall picture). Drawn the same everywhere (Andrés, 2026-11-26): the
//     theme's overlay colors at the FILL / BORDER / LABEL opacities
//     (SETTINGS > INPUT, or EDIT LAYOUT's sliders).
//   - The middle button is MENU (retroui's action button): the QUICK MENU.
//   - SHOW: AUTO (a touch screen: shown until a real gamepad plays, a tap
//     brings it back), ON, OFF.
//   - EDIT LAYOUT (SETTINGS > INPUT > ON-SCREEN GAMEPAD, or the pad's own EDIT
//     button): piccoletto's editor; saved in <settings>/touch_layouts.cfg,
//     its sliders in the settings.
//
// Fingers come from SDL's finger events (touch_fingers); with the mouse on
// a desktop (left button held), so it can be tried there too.
//
// Split like every window: onscreen_pad_state fills what it shows (no GUI),
// onscreen_pad_draw draws it and records what was pressed, and
// onscreen_pad_apply acts on it (no GUI): EDIT LAYOUT's save, the buzz. The
// fingers' RetroPad stays the pad's own state (the router reads it next
// frame, in onscreen_pad_update).

#include <cstdint>
#include <string>

#include "haptics.h"
#include "input_bindings.h"
#include "input_layouts.h"
#include "localization.h"
#include "pad_editor.h"
#include "pad_layouts.h"
#include "retropad.h"
#include "settings.h"
#include "settings_file.h"
#include "touch_fingers.h"

union SDL_Event;
#include "retro_ui_font.h"

enum class OnscreenPadShow
{
    automatic,
    on,
    off,
};

struct OnscreenPad
{
    TouchFingers fingers;
    PadLayouts   layouts;
    PadEditor    editor;
    Haptics      haptics;
    SettingsFile file;          // touch_layouts.cfg
    std::string  path;
    RetroPad     pad;           // what the fingers held at the last draw: the router's next frame
    bool         plugged = false;
    uint32_t     device = 0;    // in the router while plugged
    bool         hidden = false;   // AUTO: a real gamepad played (a tap shows it again)
    bool         touch_screen = false;   // there is one (at start, or a finger came since)
    // Where it goes this frame (onscreen_pad_place): under the picture
    // (`below`, the picture keeps `picture_height` window pixels at the top)
    // or over it.
    bool         shown = false;
    bool         below = false;
    float        picture_height = 0.0f;
    PadLayoutId  layout = PadLayoutId::remote_overlay_landscape;   // the one shown
    PadPlacement placement;     // retroui's pad_layouts_place: the layout, its frame, the picture's room
    // STYLE (touch_pad_style): the controller it looks like; its labels point
    // into the InputLayouts it came from, which live as long as the app.
    PadStyle     style;
    bool         styled = false;   // false: the whole RetroPad
    std::string  style_name;       // the setting's value it was made from
    PadLook      look;             // this frame's opacities (the settings, or EDIT LAYOUT's sliders)
};

// STYLE: look like `layout` (a controller layout from layouts/: only its
// buttons, with its names; its sticks only if it has them); nullptr = the
// whole RetroPad. `name` is the setting's value, kept to see it change.
void onscreen_pad_set_style(OnscreenPad* pad, const InputLayout* layout, const char* name);

// Loads the layouts (`settings_dir` "" = nothing saved) and opens the vibrator.
void onscreen_pad_init(OnscreenPad* pad, const char* settings_dir);
void onscreen_pad_shutdown(OnscreenPad* pad);

// Every SDL event (fingers; the toolkit's screen size for their positions).
void onscreen_pad_event(OnscreenPad* pad, const SDL_Event* event, float display_width, float display_height);

// SHOW resolved: AUTO = a touch screen (seen at start, or a finger since).
bool onscreen_pad_enabled(const OnscreenPad* pad, OnscreenPadShow show);

// EDIT LAYOUT off (Escape, DONE): saved.
void onscreen_pad_edit_end(OnscreenPad* pad);

// Every frame before input_update: in the router while `enabled` (plugged /
// unplugged), what the fingers held handed over while `playing` (else
// nothing held); AUTO hides it when a real gamepad plays.
void onscreen_pad_update(OnscreenPad* pad, InputSystem* input, bool enabled, OnscreenPadShow show, bool playing);

// Before the game is drawn: shown or not (`playing`: a game runs with no
// menu up, or EDIT LAYOUT), and where, for a picture of `aspect` (width /
// height) in a `width` x `height` window.
void onscreen_pad_place(OnscreenPad* pad, bool playing, float aspect, float width, float height);

struct OnscreenPadState
{
    const char* menu_label = "";    // the MAIN MENU button
    const char* quick_label = "";   // the QUICK MENU button
    const char* edit_label = "";    // the EDIT button: EDIT LAYOUT
    PadLook     look;               // FILL / BORDER / LABEL OPACITY, 0..1
    bool        vibrate = false;    // a buzz on each press
};

struct OnscreenPadAction
{
    bool menu = false;        // MENU: the MAIN MENU (main.cpp opens it)
    bool quick = false;       // QUICK: the QUICK MENU (main.cpp opens it)
    bool edit = false;        // EDIT: EDIT LAYOUT (main.cpp opens it)
    bool edit_done = false;   // EDIT LAYOUT's DONE
    bool look_changed = false;   // EDIT LAYOUT's sliders moved: `look` goes to the settings
    PadLook look;
    bool buzz = false;        // a press with VIBRATE on
};

void onscreen_pad_state(OnscreenPadState* state, const Settings* settings, const Localization* strings);

// The pad (or EDIT LAYOUT), over the game.
void onscreen_pad_draw(OnscreenPad* pad, const OnscreenPadState* state, RetroUiFont* pixel_font,
                       OnscreenPadAction* action);

// EDIT LAYOUT's DONE (saved), its sliders (into `settings`, global), the
// buzz. MENU, QUICK and EDIT are main.cpp's.
void onscreen_pad_apply(OnscreenPad* pad, const OnscreenPadAction* action, Settings* settings);

// After the frame: the fingers' frame ends.
void onscreen_pad_end_frame(OnscreenPad* pad);

// EDIT LAYOUT on (the layout shown now); saved when DONE.
void onscreen_pad_edit(OnscreenPad* pad);
