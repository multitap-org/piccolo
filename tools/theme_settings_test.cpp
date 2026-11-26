// Checks how piccolo.cfg keeps the interface theme (Andrés, 2026-11-26: the
// theme, not its colors): ui_theme names it, the colors are filled from it
// at start and never saved; a piccolo.cfg from before, which kept the
// colors, names the theme they match (BLUE and PURPLE from before buttons
// got a face of their own too) or becomes a custom theme file.
//
// Also: the themes that ship in themes/ give buttons and windows a face of
// their own (not the color of what they sit on).
//
// Usage: piccolo_theme_settings_test <scratch directory> <shipped themes directory>
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>
#include <cstring>
#include <string>

#include "host_fs.h"
#include "piccolo_settings.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static void fresh(Settings* settings)
{
    *settings = Settings{};
    check(settings_init(settings, piccolo_settings_table(), piccolo_settings_count()), "the table is valid");
}

// `theme` as BLUE and PURPLE were before (buttons the background's color).
static RetroUiPalette old_theme(RetroUiTheme theme)
{
    RetroUiPalette before = *retro_ui_theme_palette(theme);
    before.button_face = before.background;
    before.button_bottom = before.background;
    before.button_light = before.background;
    before.button_dark = before.background;
    return before;
}

static std::string theme_of(const Settings* settings)
{
    return settings_get_text(settings, PICCOLO_UI_THEME);
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: %s <scratch directory> <shipped themes directory>\n", argv[0]);
        return 2;
    }
    std::string scratch = argv[1];
    std::string themes_dir = scratch + "/themes";
    std::string saved_file = themes_dir + "/saved-theme-custom.cfg";
    std::string cfg = scratch + "/piccolo.cfg";
    host_fs_make_directories(scratch.c_str());
    host_fs_remove_file(saved_file.c_str());   // a run before this one left it
    std::vector<PiccoloThemeFile> themes;
    Settings settings;

    // Nothing saved: BLUE.
    fresh(&settings);
    piccolo_settings_start_ui_theme(&settings, &themes, themes_dir.c_str());
    check(theme_of(&settings) == "blue", "nothing saved: blue");
    check(piccolo_settings_ui_theme(&settings) == RetroUiTheme::blue, "... with BLUE's colors");

    // A built-in theme by name.
    fresh(&settings);
    settings_set(&settings, SettingLayer::global, PICCOLO_UI_THEME, "zsnes");
    piccolo_settings_start_ui_theme(&settings, &themes, themes_dir.c_str());
    check(piccolo_settings_ui_theme(&settings) == RetroUiTheme::zsnes, "ui_theme zsnes: ZSNES's colors");

    // A theme file that is gone: BLUE.
    fresh(&settings);
    settings_set(&settings, SettingLayer::global, PICCOLO_UI_THEME, "file:gone.cfg");
    piccolo_settings_start_ui_theme(&settings, &themes, themes_dir.c_str());
    check(piccolo_settings_ui_theme(&settings) == RetroUiTheme::blue, "a theme file gone: BLUE");

    // A piccolo.cfg from before that kept BLUE's old colors: blue, today's.
    fresh(&settings);
    RetroUiPalette before = old_theme(RetroUiTheme::blue);
    piccolo_settings_apply_ui_palette(&settings, &before);
    piccolo_settings_start_ui_theme(&settings, &themes, themes_dir.c_str());
    check(theme_of(&settings) == "blue", "the old BLUE's colors: blue");
    check(piccolo_settings_ui_theme(&settings) == RetroUiTheme::blue, "... with today's colors");

    // ... PURPLE's old colors.
    fresh(&settings);
    before = old_theme(RetroUiTheme::purple);
    piccolo_settings_apply_ui_palette(&settings, &before);
    piccolo_settings_start_ui_theme(&settings, &themes, themes_dir.c_str());
    check(theme_of(&settings) == "purple", "the old PURPLE's colors: purple");

    // ... colors of its own: saved as a custom theme, which is the theme now.
    fresh(&settings);
    RetroUiPalette own = *retro_ui_theme_palette(RetroUiTheme::blue);
    own.highlight = 0xFFFF0000u;
    piccolo_settings_apply_ui_palette(&settings, &own);
    piccolo_settings_start_ui_theme(&settings, &themes, themes_dir.c_str());
    check(host_fs_exists(saved_file.c_str()), "colors of its own: saved as a theme file");
    check(theme_of(&settings) == "file:saved-theme-custom.cfg", "... named by ui_theme");
    check(piccolo_settings_ui_palette_is(&settings, &own), "... with those colors");

    // Saving: ui_theme, not the colors.
    check(piccolo_settings_save(&settings, cfg.c_str()), "piccolo.cfg is written");
    check(!settings.dirty, "... and the settings are clean");
    Settings loaded;
    fresh(&loaded);
    check(settings_load(&loaded, cfg.c_str()), "piccolo.cfg is read back");
    check(theme_of(&loaded) == "file:saved-theme-custom.cfg", "ui_theme is kept");
    check(settings_get_layer(&loaded, SettingLayer::global, PICCOLO_UI_COLOR_HIGHLIGHT) == nullptr &&
              settings_get_layer(&loaded, SettingLayer::global, PICCOLO_UI_BACKGROUND_TRANSPARENCY) == nullptr,
          "no color or level is kept");
    piccolo_settings_start_ui_theme(&loaded, &themes, themes_dir.c_str());
    check(piccolo_settings_ui_palette_is(&loaded, &own), "the next start has the theme's colors");

    // A color changed by hand is not saved: the next start has the theme's.
    settings_set_color(&loaded, SettingLayer::global, PICCOLO_UI_COLOR_HIGHLIGHT, 0xFF00FF00u);
    check(piccolo_settings_save(&loaded, cfg.c_str()), "saved again");
    fresh(&settings);
    settings_load(&settings, cfg.c_str());
    piccolo_settings_start_ui_theme(&settings, &themes, themes_dir.c_str());
    check(piccolo_settings_ui_palette_is(&settings, &own), "an unsaved change is gone at the next start");

    // The shipped theme files.
    std::vector<PiccoloThemeFile> shipped;
    piccolo_themes_load_directory(&shipped, argv[2]);
    check(!shipped.empty(), "themes/ has a theme");
    for (size_t i = 0; i < shipped.size(); i++)
    {
        const RetroUiPalette* p = &shipped[i].palette;
        if ((p->button_face & 0xFFFFFFu) == (p->background & 0xFFFFFFu) ||
            (p->button_face & 0xFFFFFFu) == (p->panel_face & 0xFFFFFFu) ||
            (p->panel_face & 0xFFFFFFu) == (p->background & 0xFFFFFFu) ||
            (p->field_face & 0xFFFFFFu) == (p->panel_face & 0xFFFFFFu) ||
            (p->field_face & 0xFFFFFFu) == (p->background & 0xFFFFFFu))
        {
            std::printf("FAIL %s: an element is the color of what it sits on\n", shipped[i].name.c_str());
            g_failures++;
        }
    }

    if (g_failures == 0)
    {
        std::printf("theme settings: all checks passed\n");
        return 0;
    }
    return 1;
}
