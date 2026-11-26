// Checks the settings rows as plain data (src/settings_view.h) with no
// window: what a row shows at each scope (greyed out, C / G marker, RESET)
// and how an action lands in the model (clamping, rounding, empty text,
// reset), and which scope a settings screen opens on.
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>

#include "settings_view.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static const SettingEnumValue ASPECTS[] = {{"4:3"}, {"16:9"}, {"8:7"}};

static const SettingDescriptor DESCRIPTORS[] = {
    {"volume", SettingType::integer, "50", true, 0, 100, 5},
    {"language", SettingType::text, "en", false},
    {"scale", SettingType::real, "1.0", true, 0, 0, 1, 0.5, 4.0, 0.5},
    {"folder", SettingType::path, "", true},
    {"vsync", SettingType::boolean, "true", true},
    {"aspect", SettingType::enumeration, "16:9", true, 0, 0, 1, 0.0, 0.0, 0.0, ASPECTS, 3},
    {"tint", SettingType::color, "#FF8000", true},
};
constexpr uint32_t DESCRIPTOR_COUNT = sizeof(DESCRIPTORS) / sizeof(DESCRIPTORS[0]);

static void make_settings(Settings* settings)
{
    check(settings_init(settings, DESCRIPTORS, DESCRIPTOR_COUNT), "the test table is valid");
    check(settings_set_context(settings, "core", "game"), "a core and a game run");
}

static void test_rows()
{
    Settings settings;
    make_settings(&settings);
    SettingRowState row = {};

    check(!settings_view_row(&settings, nullptr, SettingLayer::global, "nope", &row), "unknown key: no row");

    check(settings_view_row(&settings, nullptr, SettingLayer::global, "volume", &row), "a row for a known key");
    check(std::strcmp(row.value, "50") == 0, "global, nothing set: the default");
    check(row.enabled && !row.can_reset, "global, nothing set: enabled, no RESET");
    check(row.override_layer == SettingLayer::defaults, "nothing set: no marker");

    settings_set_int(&settings, SettingLayer::core, "volume", 70);
    settings_view_row(&settings, nullptr, SettingLayer::global, "volume", &row);
    check(std::strcmp(row.value, "50") == 0, "editing global: the core's value doesn't show");
    check(row.override_layer == SettingLayer::core, "set for the core: C, whatever scope is edited");
    check(!row.can_reset, "editing global: the core's value isn't global's to reset");
    settings_view_row(&settings, nullptr, SettingLayer::core, "volume", &row);
    check(std::strcmp(row.value, "70") == 0 && row.can_reset, "editing the core: its value, RESET");

    settings_set_int(&settings, SettingLayer::game, "volume", 90);
    settings_view_row(&settings, nullptr, SettingLayer::core, "volume", &row);
    check(row.override_layer == SettingLayer::game, "set for the game too: G");

    settings_view_row(&settings, nullptr, SettingLayer::core, "language", &row);
    check(!row.enabled && !row.can_reset, "global-only while editing a core: greyed out, no RESET");
    settings_view_row(&settings, nullptr, SettingLayer::global, "language", &row);
    check(row.enabled, "global-only while editing global: enabled");
}

static void test_typed_values()
{
    Settings settings;
    make_settings(&settings);
    SettingRowState row = {};

    settings_view_row(&settings, nullptr, SettingLayer::global, "vsync", &row);
    check(row.checked, "a boolean: read as checked");
    settings_view_row(&settings, nullptr, SettingLayer::global, "aspect", &row);
    check(row.enum_index == 1, "an enum: read as its index");
    settings_view_row(&settings, nullptr, SettingLayer::global, "volume", &row);
    check(row.integer == 50, "an integer: read as a number");
    settings_view_row(&settings, nullptr, SettingLayer::global, "scale", &row);
    check(row.real == 1.0f, "a real: read as a number");
    settings_set_real(&settings, SettingLayer::global, "scale", 2.5);
    settings_view_row(&settings, nullptr, SettingLayer::global, "scale", &row);
    check(row.real == 2.5f, "a real with a decimal part: read with '.' as the mark");
    settings_view_row(&settings, nullptr, SettingLayer::global, "tint", &row);
    check(row.color == 0xFFFF8000u, "a color: read as ARGB");

    settings_set_bool(&settings, SettingLayer::game, "vsync", false);
    settings_view_row(&settings, nullptr, SettingLayer::game, "vsync", &row);
    check(!row.checked, "the typed value follows the scope being edited");
}

static void test_apply()
{
    Settings settings;
    make_settings(&settings);
    SettingRowAction action = {};

    check(!settings_view_apply(&settings, SettingLayer::global, "volume", &action), "no edit: nothing changes");

    action.edit = SettingRowEdit::set_integer;
    action.integer = 250;
    check(settings_view_apply(&settings, SettingLayer::global, "volume", &action), "an integer is set");
    check(settings_get_int(&settings, "volume") == 100, "an integer is clamped to its range");

    action = {};
    action.edit = SettingRowEdit::set_real;
    action.real = 1.7;
    settings_view_apply(&settings, SettingLayer::core, "scale", &action);
    check(settings_get_real(&settings, "scale") == 1.5, "a real is rounded to its step");

    action = {};
    action.edit = SettingRowEdit::set_text;
    std::snprintf(action.text, sizeof(action.text), "%s", "/games");
    settings_view_apply(&settings, SettingLayer::game, "folder", &action);
    check(std::strcmp(settings_get_text(&settings, "folder"), "/games") == 0, "a text is set");
    action.text[0] = '\0';
    check(settings_view_apply(&settings, SettingLayer::game, "folder", &action), "an empty text resets");
    check(settings_get_layer(&settings, SettingLayer::game, "folder") == nullptr, "an empty text: back to the default");

    action = {};
    action.edit = SettingRowEdit::reset;
    check(settings_view_apply(&settings, SettingLayer::core, "scale", &action), "RESET removes the core's value");
    check(!settings_view_apply(&settings, SettingLayer::core, "scale", &action), "RESET again: nothing to remove");

    action = {};
    action.edit = SettingRowEdit::set_text;
    std::snprintf(action.text, sizeof(action.text), "%s", "es");
    check(!settings_view_apply(&settings, SettingLayer::core, "language", &action),
          "a global-only setting can't be set for a core");
}

static void test_scope()
{
    Settings settings;
    make_settings(&settings);
    SettingLayer scope = SettingLayer::core;
    bool choose = true;

    settings_view_scope(&settings, true, true, &scope, &choose);
    check(scope == SettingLayer::global && !choose, "opened, nothing overridden: GLOBAL");

    settings_set_int(&settings, SettingLayer::core, "volume", 70);
    choose = true;
    settings_view_scope(&settings, true, true, &scope, &choose);
    check(scope == SettingLayer::core, "opened, the core has values: THIS CORE");

    settings_set_int(&settings, SettingLayer::game, "volume", 90);
    choose = true;
    settings_view_scope(&settings, true, true, &scope, &choose);
    check(scope == SettingLayer::game, "opened, the game has values: THIS GAME");

    settings_view_scope(&settings, true, false, &scope, &choose);
    check(scope == SettingLayer::global, "the game stopped: back to GLOBAL");
    scope = SettingLayer::game;
    settings_view_scope(&settings, false, true, &scope, &choose);
    check(scope == SettingLayer::global, "a game needs a core");
}

int main()
{
    test_rows();
    test_typed_values();
    test_apply();
    test_scope();
    if (g_failures == 0)
    {
        std::printf("settings view: all checks passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
