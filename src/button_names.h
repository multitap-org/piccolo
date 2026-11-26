#pragma once

// A control's name as the user sees it on that controller ("RETURN", "A",
// "✕", "LB", "D-PAD UP"...), with no SDL and no GUI, so it can be tested.
// INPUT, MAP CONTROLLER's pages, the remap card and the DVR's hints all name
// buttons through here (input_window_source_label fills ButtonNamesPad from
// SDL and calls button_names_source).
//
// Where a gamepad button's name comes from, first found wins:
//   1. the name the controller's profile gave it (MAP CONTROLLER's L4 / R4),
//   2. the controller's layout (layouts/<Maker>/<model>.cfg, its [labels]),
//   3. what SDL says of the pad: its face buttons' labels, and its type for
//      the shoulders and triggers (LB / L1 / L),
//   4. the RetroPad's names for the rest (L, R, L2, R2, START...).
// PlayStation's face buttons are the pixel font's glyphs ✕ ○ □ △.

#include <cstddef>
#include <cstdint>

#include "input_bindings.h"

struct InputLayout;

// PlayStation's face buttons, UTF-8 (the pixel font's PIXEL_FONT_PS_*).
constexpr const char* BUTTON_NAME_CROSS    = "\xE2\x9C\x95";   // ✕ U+2715
constexpr const char* BUTTON_NAME_CIRCLE   = "\xE2\x97\x8B";   // ○ U+25CB
constexpr const char* BUTTON_NAME_SQUARE   = "\xE2\x96\xA1";   // □ U+25A1
constexpr const char* BUTTON_NAME_TRIANGLE = "\xE2\x96\xB3";   // △ U+25B3

// A face button's label, as SDL reports it (SDL_GamepadButtonLabel).
enum class ButtonFaceLabel
{
    unknown,
    a,
    b,
    x,
    y,
    cross,
    circle,
    square,
    triangle,
};

// Whose names the shoulders and triggers get (from SDL's gamepad type).
enum class ButtonFamily
{
    other,         // L / R, L2 / R2 (the RetroPad's)
    xbox,          // LB / RB, LT / RT
    playstation,   // L1 / R1, L2 / R2
    nintendo,      // L / R, ZL / ZR
};

// What SDL tells of a pad (filled from SDL by input_window; zeroed: nothing known).
struct ButtonNamesPad
{
    ButtonFaceLabel south = ButtonFaceLabel::unknown;
    ButtonFaceLabel east  = ButtonFaceLabel::unknown;
    ButtonFaceLabel west  = ButtonFaceLabel::unknown;
    ButtonFaceLabel north = ButtonFaceLabel::unknown;
    ButtonFamily    family = ButtonFamily::other;
};

// The name of `source` on its device. `layout`, `profile` and `pad` may each
// be nullptr (nothing known from there). Keys get their key names.
void button_names_source(const InputLayout* layout, const InputProfile* profile, const ButtonNamesPad* pad,
                         InputSource source, char* out, size_t out_size);

// The first source bound to `action` in `bindings`: false (and `source`
// none) when nothing is.
bool button_names_first_source(const InputBindingSet* bindings, InputAction action, InputSource* source);

// Two names side by side in a hint, for a pair of controls ("LB RB",
// "L1 R1"); the keyboard's PAGEUP / PAGEDOWN as "PG UP DN", which fits.
void button_names_pair(const char* left, const char* right, char* out, size_t out_size);
