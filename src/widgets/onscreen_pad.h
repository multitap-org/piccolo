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
//     theme's pad colors (one set per kind of part) at the FILL / BORDER /
//     LABEL opacities. While the pad shows, the space the game picture leaves
//     empty is the theme's pad_background at the BACKGROUND opacity, over
//     black. The opacities are the theme's own (ui_pad_*) until the user
//     sets theirs (EDIT LAYOUT's sliders, or SETTINGS > INPUT > ON-SCREEN
//     GAMEPAD: touch_pad_look_set), then those win for every theme.
//   - The middle row is SELECT, a menu button (the burger, U+2630) and START.
//     Pressing the burger opens a pop-up over the picture: MAIN MENU, QUICK
//     MENU, GAME DVR, CONTROLLERS and EDIT LAYOUT, in the pad_center colors
//     at the same opacities; a tap outside (or the burger again) closes it.
//   - SHOW: AUTO (a touch screen: shown until a real gamepad plays, a tap
//     brings it back), ON, OFF.
//   - STYLE: AUTO (the default) looks like the game's controller, by its file
//     extension (layouts/*.cfg extensions =): a NES or Game Boy game the NES /
//     GAME BOY pad, a Super Nintendo game the SUPER NINTENDO one, each in its
//     MINIMAL arrangement (only its buttons, bigger: retroui PadMinimal), any
//     other game the RetroPad (FULL). RETROPAD or a layout by name otherwise.
//   - EDIT LAYOUT (SETTINGS > INPUT > ON-SCREEN GAMEPAD, or the pad's burger
//     menu): piccoletto's editor; saved in <settings>/touch_layouts.cfg, its
//     sliders in the settings.
//
// Fingers come from SDL's finger events (touch_fingers); with the mouse on
// a desktop (left button held), so it can be tried there too.
//
// Split like every window: onscreen_pad_state fills what it shows (no GUI),
// onscreen_pad_draw draws it and records what was pressed, and
// onscreen_pad_apply acts on it (no GUI): EDIT LAYOUT's save and sliders, the
// buzz. The menus the burger menu names are main.cpp's. The
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
    std::string  style_name;       // the setting's value it was made from (AUTO: with the layout it picked)
    PadMinimal   minimal = PadMinimal::none;   // the style's MINIMAL arrangement (its layout's touch =)
    PadLook      look;             // this frame's opacities (the theme's, the settings, or EDIT LAYOUT's sliders)
    bool         menu_open = false;   // the burger menu's pop-up is up (drawn from the frame after the press)
    // A finger that went down off the pop-up's items while it's up: its lift
    // closes it. The finger that pressed the burger went down before, so
    // lifting it leaves the pop-up open (on Android its release came to
    // ImGui as a tap on the backdrop and closed it at once).
    bool         menu_tap = false;
    uint64_t     menu_tap_finger = 0;
};

// STYLE: look like `layout` (a controller layout from layouts/: only its
// buttons, with its names; its sticks only if it has them; in its MINIMAL
// arrangement if it names one, touch = two / snes); nullptr = the whole
// RetroPad. `name` is what it was made from, kept to see it change.
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

// Where the game's picture is this frame, window pixels (the toolkit's
// units); `shown` false = none (the whole screen is empty space).
struct OnscreenPadPicture
{
    bool  shown = false;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct OnscreenPadState
{
    const char* menu_label = "";           // the burger button's label
    const char* main_menu_label = "";      // the burger menu's items
    const char* quick_menu_label = "";
    const char* game_dvr_label = "";
    const char* controllers_label = "";
    const char* edit_layout_label = "";
    PadLook     look;                      // FILL / BORDER / LABEL / BACKGROUND OPACITY, 0..1
    bool        vibrate = false;           // a buzz on each press
    OnscreenPadPicture picture;            // set by main.cpp (the pad doesn't place the picture)
};

struct OnscreenPadAction
{
    // The burger menu's picks (main.cpp opens them).
    bool main_menu = false;
    bool quick_menu = false;
    bool game_dvr = false;
    bool controllers = false;
    bool edit_layout = false;    // EDIT LAYOUT (main.cpp starts it: onscreen_pad_edit)
    bool edit_done = false;      // EDIT LAYOUT's DONE
    bool look_changed = false;   // EDIT LAYOUT's sliders moved: `look` goes to the settings
    PadLook look;
    bool buzz = false;        // a press with VIBRATE on
};

// What the burger menu's pop-up returned: a pick, or a tap outside.
enum class OnscreenPadMenuPress
{
    none,
    close,
    main_menu,
    quick_menu,
    game_dvr,
    controllers,
    edit_layout,
};

// The look in force (the theme's levels, or the settings' once the user set
// them): the pad's, and the browser page's.
PadLook onscreen_pad_look(const Settings* settings);

void onscreen_pad_state(OnscreenPadState* state, const Settings* settings, const Localization* strings);

// The pad (or EDIT LAYOUT), over the game.
void onscreen_pad_draw(OnscreenPad* pad, const OnscreenPadState* state, RetroUiFont* pixel_font,
                       OnscreenPadAction* action);

// EDIT LAYOUT's DONE (saved), its sliders (into `settings`, global, and
// touch_pad_look_set), the buzz; while the look is the theme's, the settings
// rows are kept showing the theme's values. The burger menu's picks are
// main.cpp's.
void onscreen_pad_apply(OnscreenPad* pad, const OnscreenPadAction* action, Settings* settings);

// After the frame: the fingers' frame ends.
void onscreen_pad_end_frame(OnscreenPad* pad);

// EDIT LAYOUT on (the layout shown now); saved when DONE.
void onscreen_pad_edit(OnscreenPad* pad);

// The burger menu's pop-up alone, over the whole screen, for MENU > DEBUG
// (the caller ignores a pick; a tap outside returns `close`). `state` from
// onscreen_pad_state.
OnscreenPadMenuPress onscreen_pad_menu_preview(const OnscreenPadState* state, RetroUiFont* pixel_font);
