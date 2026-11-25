#pragma once

// piccolo's IMAGE SETTINGS window, opened from the Picture tab of the video
// settings. The game's current frame on the left, at a whole-number scale (as
// big as fits next to the controls in piccolo's window); the image controls
// on the right, in collapsible sections: overscan crop, with the cut-off
// parts shaded on the picture, and pass 0's picture controls, shown on the
// kept part of the picture.
//
//   +- IMAGE SETTINGS ------------------------------------------------+
//   | EDITING: THIS GAME                                               |
//   | +-------------------------------+  - OVERSCAN CROP               |
//   | |#####  cut off (shaded)  ######|  C CROP TOP    [- 8 +] [RESET] |
//   | |  the frame, x3, sharp pixels  |    CROP BOTTOM [- 8 +]         |
//   | |#############################  |    CROP LEFT   [- 0 +]         |
//   | +-------------------------------+    CROP RIGHT  [- 0 +]         |
//   +------------------------------------------------------------------+
//
// Edits go to the scope chosen in the settings window.

#include <cstdint>

#include "localization.h"
#include "settings.h"
#include "settings_ui.h"

#include "retro_ui_font.h"

// The game picture to show (the core session's texture).
struct ImageWindowSource
{
    uint64_t texture_id   = 0;      // the GUI toolkit's texture id; 0 = no picture
    float    uv_max_x     = 0.0f;   // the used part of the texture
    float    uv_max_y     = 0.0f;
    uint32_t frame_width  = 0;
    uint32_t frame_height = 0;

    // The picture after pass 0, the shown part only (after the crop); 0 =
    // none. Drawn over the kept part when it's the same size.
    uint64_t processed_texture_id = 0;
    uint32_t processed_width      = 0;
    uint32_t processed_height     = 0;

    bool controls_available = false;   // pass 0 runs (librashader loaded): else its rows show a note
};

// What the window shows from the rest of piccolo, read only.
struct ImageWindowState
{
    const Settings*     settings   = nullptr;   // the rows read it (settings_ui)
    const Localization* strings    = nullptr;   // UI text (piccolo_text.h)
    SettingLayer        scope      = SettingLayer::global;   // the scope chosen in SETTINGS: edits go there
    const char*         scope_name = "";        // "GLOBAL", "THIS CORE", ... for "EDITING:"
    float               crop[4]    = {};        // top, bottom, left, right as they resolve at `scope`
    ImageWindowSource   source;                 // no picture: texture_id 0
};

// What the user did this frame: a row's edit, applied by image_window_apply.
struct ImageWindowAction
{
    SettingsUiEdit setting;
};

// Fills *state. `source` may be nullptr (no picture).
void image_window_state(ImageWindowState* state, const Settings* settings, const Localization* strings,
                        SettingLayer scope, const ImageWindowSource* source);

// Draws the window while *open; the close button clears it. With no picture
// (texture_id 0) the controls still work. What the user changes goes to
// *action.
void image_window_draw(bool* open, const ImageWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                       ImageWindowAction* action);

// Applies the row's edit. True when a setting changed.
bool image_window_apply(const ImageWindowAction* action, Settings* settings);
