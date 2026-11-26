// Checks how piccolo names controls (src/button_names.h) with no window and
// no SDL: the DVR's hints on Xbox, Switch and PlayStation pads, from the
// layouts in layouts/ and from what SDL would say of a pad without one, a
// rebound INTERFACE button, a profile's own name, the keyboard's keys; and
// that the pixel font draws the PlayStation glyphs the names use.
//
// Usage: piccolo_button_names_test <layouts folder>
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>

#include "button_names.h"
#include "input_layouts.h"
#include "pixel_font.h"

static int g_failures = 0;

static void check_name(const char* got, const char* expected, const char* what)
{
    if (std::strcmp(got, expected) != 0)
    {
        std::printf("FAIL %s: expected \"%s\", got \"%s\"\n", what, expected, got);
        g_failures++;
    }
}

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

// The DVR's four hints as dvr_widget_name_keys builds them: PLAY FROM HERE
// (accept), WATCH (open menu), BACK TO NOW (back), the jumps (page left / right).
struct DvrNames
{
    char accept[32] = {};
    char watch[32]  = {};
    char back[32]   = {};
    char pages[64]  = {};
};

static void action_name(const InputBindingSet* set, const InputLayout* layout, const InputProfile* profile,
                        const ButtonNamesPad* pad, InputAction action, char* out, size_t out_size)
{
    InputSource source = {};
    out[0] = '\0';
    if (button_names_first_source(set, action, &source))
    {
        button_names_source(layout, profile, pad, source, out, out_size);
    }
}

static void dvr_names(const InputBindingSet* set, const InputLayout* layout, const InputProfile* profile,
                      const ButtonNamesPad* pad, DvrNames* names)
{
    action_name(set, layout, profile, pad, InputAction::ui_accept, names->accept, sizeof(names->accept));
    action_name(set, layout, profile, pad, InputAction::ui_open_menu, names->watch, sizeof(names->watch));
    action_name(set, layout, profile, pad, InputAction::ui_back, names->back, sizeof(names->back));
    char left[32] = {};
    char right[32] = {};
    action_name(set, layout, profile, pad, InputAction::ui_page_left, left, sizeof(left));
    action_name(set, layout, profile, pad, InputAction::ui_page_right, right, sizeof(right));
    button_names_pair(left, right, names->pages, sizeof(names->pages));
}

static void check_dvr(const DvrNames* names, const char* accept, const char* watch, const char* back, const char* pages,
                      const char* what)
{
    char label[128] = {};
    std::snprintf(label, sizeof(label), "%s: PLAY FROM HERE", what);
    check_name(names->accept, accept, label);
    std::snprintf(label, sizeof(label), "%s: WATCH", what);
    check_name(names->watch, watch, label);
    std::snprintf(label, sizeof(label), "%s: BACK TO NOW", what);
    check_name(names->back, back, label);
    std::snprintf(label, sizeof(label), "%s: the jumps", what);
    check_name(names->pages, pages, label);
}

static void test_layouts(const InputLayouts* layouts)
{
    InputBindingSet pad = {};
    input_default_gamepad_bindings(&pad);
    struct Case
    {
        const char* layout;
        const char* accept;
        const char* watch;
        const char* back;
        const char* pages;
    };
    static const Case CASES[] = {
        {"Microsoft/xboxone", "A", "X", "B", "LB RB"},
        {"Microsoft/xbox360", "A", "X", "B", "LB RB"},
        {"Nintendo/switchpro", "B", "Y", "A", "L R"},
        {"Sony/dualshock", BUTTON_NAME_CROSS, BUTTON_NAME_SQUARE, BUTTON_NAME_CIRCLE, "L1 R1"},
        {"Sony/dualsense", BUTTON_NAME_CROSS, BUTTON_NAME_SQUARE, BUTTON_NAME_CIRCLE, "L1 R1"},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        const InputLayout* layout = input_layouts_find(layouts, CASES[i].layout);
        check(layout != nullptr, CASES[i].layout);
        if (layout == nullptr)
        {
            continue;
        }
        DvrNames names = {};
        dvr_names(&pad, layout, nullptr, nullptr, &names);
        check_dvr(&names, CASES[i].accept, CASES[i].watch, CASES[i].back, CASES[i].pages, CASES[i].layout);
    }

    // The PlayStation layouts' triangle (the top button).
    const InputLayout* dualsense = input_layouts_find(layouts, "Sony/dualsense");
    if (dualsense != nullptr)
    {
        char name[32] = {};
        InputSource north = {.type = InputSourceType::button, .code = static_cast<uint16_t>(InputButton::north)};
        button_names_source(dualsense, nullptr, nullptr, north, name, sizeof(name));
        check_name(name, BUTTON_NAME_TRIANGLE, "Sony/dualsense: the top button");
    }
}

// A pad never mapped (no layout): what SDL says of it.
static void test_sdl_labels()
{
    InputBindingSet pad = {};
    input_default_gamepad_bindings(&pad);

    ButtonNamesPad xbox = {};
    xbox.south = ButtonFaceLabel::a;
    xbox.east = ButtonFaceLabel::b;
    xbox.west = ButtonFaceLabel::x;
    xbox.north = ButtonFaceLabel::y;
    xbox.family = ButtonFamily::xbox;
    DvrNames names = {};
    dvr_names(&pad, nullptr, nullptr, &xbox, &names);
    check_dvr(&names, "A", "X", "B", "LB RB", "SDL, Xbox");

    ButtonNamesPad nintendo = {};
    nintendo.south = ButtonFaceLabel::b;
    nintendo.east = ButtonFaceLabel::a;
    nintendo.west = ButtonFaceLabel::y;
    nintendo.north = ButtonFaceLabel::x;
    nintendo.family = ButtonFamily::nintendo;
    dvr_names(&pad, nullptr, nullptr, &nintendo, &names);
    check_dvr(&names, "B", "Y", "A", "L R", "SDL, Switch");

    ButtonNamesPad sony = {};
    sony.south = ButtonFaceLabel::cross;
    sony.east = ButtonFaceLabel::circle;
    sony.west = ButtonFaceLabel::square;
    sony.north = ButtonFaceLabel::triangle;
    sony.family = ButtonFamily::playstation;
    dvr_names(&pad, nullptr, nullptr, &sony, &names);
    check_dvr(&names, BUTTON_NAME_CROSS, BUTTON_NAME_SQUARE, BUTTON_NAME_CIRCLE, "L1 R1", "SDL, PlayStation");

    char name[32] = {};
    InputSource trigger = {.type = InputSourceType::axis_plus, .code = static_cast<uint16_t>(InputAxis::left_trigger)};
    button_names_source(nullptr, nullptr, &xbox, trigger, name, sizeof(name));
    check_name(name, "LT", "SDL, Xbox: the left trigger");

    // Nothing known: the positions' names and the RetroPad's shoulders.
    dvr_names(&pad, nullptr, nullptr, nullptr, &names);
    check_dvr(&names, "BOTTOM BUTTON", "LEFT BUTTON", "RIGHT BUTTON", "L R", "nothing known");
}

// INTERFACE rebound, and a profile's own name for a button.
static void test_rebound(const InputLayouts* layouts)
{
    InputBindingSet pad = {};
    input_default_gamepad_bindings(&pad);
    input_unbind_all(&pad, InputAction::ui_accept);
    input_bind(&pad, InputAction::ui_accept, InputSource{.type = InputSourceType::button, .code = static_cast<uint16_t>(InputButton::north)});
    const InputLayout* xbox = input_layouts_find(layouts, "Microsoft/xboxone");
    DvrNames names = {};
    dvr_names(&pad, xbox, nullptr, nullptr, &names);
    check_name(names.accept, "Y", "rebound: accept on the top button of an Xbox pad");

    InputProfile profile = {};
    profile.button_labels[static_cast<uint32_t>(InputButton::left_shoulder)] = "L4";
    dvr_names(&pad, xbox, &profile, nullptr, &names);
    check_name(names.pages, "L4 RB", "the profile's own name wins over the layout's");

    input_unbind_all(&pad, InputAction::ui_back);
    InputSource none = {};
    check(!button_names_first_source(&pad, InputAction::ui_back, &none) && none.type == InputSourceType::none,
          "unbound: no source");
}

static void test_keyboard()
{
    InputBindingSet keys = {};
    input_default_bindings(InputDeviceKind::keyboard, &keys);
    DvrNames names = {};
    dvr_names(&keys, nullptr, nullptr, nullptr, &names);
    check_name(names.accept, "RETURN", "keyboard: PLAY FROM HERE");
    check_name(names.back, "ESCAPE", "keyboard: BACK TO NOW");
    check_name(names.pages, "PG UP DN", "keyboard: the jumps");
}

// The names' glyphs are in the pixel font (not the missing box).
static void test_font()
{
    check(pixel_font_init(), "the pixel font's tables");
    const char* glyphs[4] = {BUTTON_NAME_CROSS, BUTTON_NAME_CIRCLE, BUTTON_NAME_SQUARE, BUTTON_NAME_TRIANGLE};
    const uint32_t codepoints[4] = {PIXEL_FONT_PS_CROSS, PIXEL_FONT_PS_CIRCLE, PIXEL_FONT_PS_SQUARE, PIXEL_FONT_PS_TRIANGLE};
    for (uint32_t i = 0; i < 4; i++)
    {
        const char* text = glyphs[i];
        uint32_t codepoint = pixel_font_next_codepoint(&text);
        check(codepoint == codepoints[i] && *text == '\0', "a PlayStation name is its font glyph's code point");
        uint8_t rows[PIXEL_FONT_CELL_HEIGHT] = {};
        check(pixel_font_glyph_rows(codepoint, rows), "the pixel font has the PlayStation glyph");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: piccolo_button_names_test <layouts folder>\n");
        return 2;
    }
    InputLayouts layouts = {};
    input_layouts_load_directory(&layouts, argv[1]);
    test_layouts(&layouts);
    test_sdl_labels();
    test_rebound(&layouts);
    test_keyboard();
    test_font();
    if (g_failures == 0)
    {
        std::printf("button names: all checks passed\n");
        return 0;
    }
    return 1;
}
