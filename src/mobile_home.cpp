#include "mobile_home.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "piccolo_text.h"
#include "pixel_font_imgui.h"   // PIXEL_FONT_UTF8_MENU, _ROTATE
#include "touch_header.h"
#include "touch_ui.h"

// Layout, in retro pixels (piccoletto's HOME and quick menu).
constexpr float MARGIN       = 8.0f;
constexpr float CARD_PADDING = 10.0f;
constexpr float MAIN_BUTTON  = 36.0f;   // LOAD GAME
constexpr float SMALL_BUTTON = 18.0f;   // CLEAR ALL
constexpr float GAME_ROW     = 34.0f;   // a recent game: two lines
constexpr float MENU_ROW     = 24.0f;   // an item in the sidebar
constexpr float ROW_GAP      = 4.0f;
constexpr float SECTION_GAP  = 8.0f;    // above a menu's name in the sidebar
constexpr float SIDE_COLUMN  = 120.0f;  // landscape HOME: LOAD GAME's column
constexpr int32_t HEADER_ICONS = 2;     // ROTATE, ☰

// The sidebar's share of the screen's width (the mockup: 78% upright, 62% sideways).
constexpr float MENU_WIDTH_PORTRAIT  = 0.78f;
constexpr float MENU_WIDTH_LANDSCAPE = 0.62f;

static bool landscape(const TouchUi* ui)
{
    return touch_ui_width(ui) > touch_ui_height(ui);
}

// A menu item's label without the desktop's "..." (on a phone every row
// opens a page): "SOUND..." -> "SOUND".
static void page_label(const char* label, char* out, size_t out_size)
{
    std::snprintf(out, out_size, "%s", label);
    size_t length = std::strlen(out);
    if (length >= 3 && std::strcmp(out + length - 3, "...") == 0)
    {
        out[length - 3] = '\0';
    }
}

// Drag to scroll a list in (x, y, width, height); `content` its whole
// height. Submitted after the list's buttons, so they get their taps first.
static void drag_scroll(const TouchUi* ui, const char* id, float x, float y, float width, float height, float content,
                        float* scroll)
{
    bool held = false;
    touch_ui_area(ui, id, x, y, width, height, &held, TouchNav::taps_only);
    if (held)
    {
        *scroll -= ImGui::GetIO().MouseDelta.y / static_cast<float>(ui->scale);
    }
    if (ImGui::IsMouseHoveringRect(ImVec2(x * static_cast<float>(ui->scale), y * static_cast<float>(ui->scale)),
                                   ImVec2((x + width) * static_cast<float>(ui->scale),
                                          (y + height) * static_cast<float>(ui->scale)),
                                   false))
    {
        *scroll -= ImGui::GetIO().MouseWheel * 16.0f;
    }
    float most = content > height ? content - height : 0.0f;
    *scroll = *scroll < 0.0f ? 0.0f : (*scroll > most ? most : *scroll);
}

// ---------------------------------------------------------------------------
// HOME

// One recent game: its name, the core under it; tapping it starts it.
static bool draw_game(const TouchUi* ui, const MobileGameRow* game, int32_t index, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    char id[32];
    std::snprintf(id, sizeof(id), "##game%d", static_cast<int>(index));
    bool tapped = touch_ui_button(ui, id, x, y, width, GAME_ROW, nullptr, TouchButton::card, TouchText::normal);
    // A long name is cut at the row's edge.
    touch_ui_push_clip(ui, x, y, width - CARD_PADDING * 0.5f, GAME_ROW);
    float line = y + 7.0f;
    touch_ui_text(ui, x + CARD_PADDING, line, game->name, palette->text_highlight, TouchText::normal);
    line += touch_ui_text_height(ui, TouchText::normal) + 5.0f;
    touch_ui_text(ui, x + CARD_PADDING, line, game->core, palette->text_disabled, TouchText::normal);
    touch_ui_pop_clip();
    return tapped;
}

// No recent games: one card that says so, LOAD GAME as its button
// (piccoletto's NO DEVICES YET).
static bool draw_empty(const TouchUi* ui, const Localization* strings, float x, float y, float width, float height)
{
    const RetroUiPalette* palette = &ui->palette;
    touch_ui_rect(ui, x, y, width, height, palette->panel_face);
    float inner_x = x + CARD_PADDING;
    float line = y + CARD_PADDING;
    touch_ui_text(ui, inner_x, line, piccolo_text(strings, Str::mobile_no_games), palette->text_highlight, TouchText::big);
    line += touch_ui_text_height(ui, TouchText::big) + 8.0f;
    touch_ui_text(ui, inner_x, line, piccolo_text(strings, Str::mobile_no_games_hint), palette->text_disabled,
                  TouchText::normal);
    line += touch_ui_text_height(ui, TouchText::normal) + 6.0f;
    touch_ui_text(ui, inner_x, line, piccolo_text(strings, Str::mobile_no_games_more), palette->text_disabled,
                  TouchText::normal);
    float button_y = y + height - CARD_PADDING - MAIN_BUTTON;
    return touch_ui_button(ui, "##empty_load", inner_x, button_y, width - 2.0f * CARD_PADDING, MAIN_BUTTON,
                           piccolo_text(strings, Str::mobile_load_game), TouchButton::primary, TouchText::big);
}

// The games' list: one column upright, two sideways; scrolls when it doesn't fit.
static void draw_games(MobileHome* home, const TouchUi* ui, const MobileHomeState* state, float x, float y, float width,
                       float height, MobileHomeAction* action)
{
    int32_t columns = landscape(ui) ? 2 : 1;
    float column_width = std::floor((width - static_cast<float>(columns - 1) * ROW_GAP) / static_cast<float>(columns));
    int32_t rows = (state->game_count + columns - 1) / columns;
    float content = static_cast<float>(rows) * (GAME_ROW + ROW_GAP);
    touch_ui_push_clip(ui, x, y, width, height);
    for (int32_t i = 0; i < state->game_count; i++)
    {
        int32_t column = i % columns;
        int32_t row = i / columns;
        float row_x = x + static_cast<float>(column) * (column_width + ROW_GAP);
        float row_y = y + static_cast<float>(row) * (GAME_ROW + ROW_GAP) - home->scroll;
        if (row_y + GAME_ROW < y || row_y > y + height)
        {
            continue;   // scrolled away
        }
        if (draw_game(ui, &state->games[i], i, row_x, row_y, column_width))
        {
            action->picked = state->first_game_id + i;
        }
    }
    drag_scroll(ui, "##games_scroll", x, y, width, height, content, &home->scroll);
    touch_ui_pop_clip();
}

static void draw_home(MobileHome* home, const TouchUi* ui, const MobileHomeState* state, MobileHomeAction* action)
{
    const RetroUiPalette* palette = &ui->palette;
    const Localization* strings = state->strings;
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool wide = landscape(ui);

    // The header: the name (a logo later) at the left, the icons at the right.
    touch_header_title(ui, "piccolo");
    if (touch_header_icon(ui, "##home_rotate", 0, HEADER_ICONS, PIXEL_FONT_UTF8_ROTATE))
    {
        action->rotate = true;
    }
    if (touch_header_icon(ui, "##home_menu", 1, HEADER_ICONS, PIXEL_FONT_UTF8_MENU))
    {
        action->open_menu = true;
    }

    float content_y = touch_header_bottom(ui) + (wide ? 6.0f : 12.0f);
    if (state->game_count == 0)
    {
        float empty_height = wide ? height - content_y - MARGIN : 120.0f;
        if (draw_empty(ui, strings, MARGIN, content_y, width - 2.0f * MARGIN, empty_height))
        {
            action->picked = state->load_game_id;
        }
        return;
    }

    // Sideways LOAD GAME has its own column at the right (under the thumb);
    // upright it's across the bottom.
    float list_width = wide ? width - 3.0f * MARGIN - SIDE_COLUMN : width - 2.0f * MARGIN;
    float list_bottom = wide ? height - MARGIN : height - 2.0f * MARGIN - MAIN_BUTTON;

    // RECENT GAMES, CLEAR ALL at its right.
    const char* clear = piccolo_text(strings, Str::menu_clear_recents);
    float clear_width = touch_ui_text_width(ui, clear, TouchText::normal) + 12.0f;
    touch_ui_text(ui, MARGIN + 2.0f, touch_ui_centered_text_y(ui, content_y, SMALL_BUTTON, TouchText::normal),
                  piccolo_text(strings, Str::mobile_recent_games), palette->text_disabled, TouchText::normal);
    if (touch_ui_button(ui, "##clear_recents", MARGIN + list_width - clear_width, content_y, clear_width, SMALL_BUTTON,
                        clear, TouchButton::normal, TouchText::normal))
    {
        action->picked = state->clear_recents_id;
    }
    float list_y = content_y + SMALL_BUTTON + 6.0f;
    draw_games(home, ui, state, MARGIN, list_y, list_width, list_bottom - list_y, action);

    float button_x = wide ? width - MARGIN - SIDE_COLUMN : MARGIN;
    float button_width = wide ? SIDE_COLUMN : width - 2.0f * MARGIN;
    float button_y = height - MARGIN - MAIN_BUTTON;
    if (touch_ui_button(ui, "##load_game", button_x, button_y, button_width, MAIN_BUTTON,
                        piccolo_text(strings, Str::mobile_load_game), TouchButton::primary, TouchText::big))
    {
        action->picked = state->load_game_id;
    }
}

// ---------------------------------------------------------------------------
// The ☰ sidebar

// A menu's height in the sidebar: its name and its items (separators are a gap).
static float menu_height(const TouchUi* ui, const RetroUiMenu* menu)
{
    float height = SECTION_GAP + touch_ui_text_height(ui, TouchText::normal) + 4.0f;
    for (int32_t i = 0; i < menu->item_count; i++)
    {
        height += menu->items[i].id == RETRO_UI_MENU_SEPARATOR ? ROW_GAP : MENU_ROW + ROW_GAP;
    }
    return height;
}

// One menu: its name, then a row per item; a greyed item is drawn dim and
// takes no taps. Returns the picked id (RETRO_UI_MENU_NONE: none).
static int32_t draw_menu(const TouchUi* ui, const RetroUiMenu* menu, int32_t menu_index, float x, float y, float width)
{
    const RetroUiPalette* palette = &ui->palette;
    int32_t picked = RETRO_UI_MENU_NONE;
    y += SECTION_GAP;
    touch_ui_text(ui, x + 2.0f, y, menu->label, palette->text_disabled, TouchText::normal);
    y += touch_ui_text_height(ui, TouchText::normal) + 4.0f;
    for (int32_t i = 0; i < menu->item_count; i++)
    {
        const RetroUiMenuItem* item = &menu->items[i];
        if (item->id == RETRO_UI_MENU_SEPARATOR)
        {
            y += ROW_GAP;
            continue;
        }
        char label[200];
        page_label(item->label, label, sizeof(label));
        if (!item->enabled)
        {
            touch_ui_rect(ui, x, y, width, MENU_ROW, palette->panel_face);
            touch_ui_text(ui, x + CARD_PADDING, touch_ui_centered_text_y(ui, y, MENU_ROW, TouchText::normal), label,
                          palette->text_disabled, TouchText::normal);
        }
        else
        {
            char id[48];
            std::snprintf(id, sizeof(id), "##menu%d_%d", static_cast<int>(menu_index), static_cast<int>(i));
            if (touch_ui_button(ui, id, x, y, width, MENU_ROW, label, TouchButton::card, TouchText::normal))
            {
                picked = item->id;
            }
        }
        y += MENU_ROW + ROW_GAP;
    }
    return picked;
}

static void draw_sidebar(MobileHome* home, const TouchUi* ui, const MobileHomeState* state, MobileHomeAction* action)
{
    const RetroUiPalette* palette = &ui->palette;
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool wide = landscape(ui);
    float side_width = std::floor(width * (wide ? MENU_WIDTH_LANDSCAPE : MENU_WIDTH_PORTRAIT));
    float side_x = width - side_width;

    // The rest of the screen dimmed: a tap there closes it.
    bool held = false;
    if (touch_ui_area(ui, "##menu_outside", 0.0f, 0.0f, side_x, height, &held, TouchNav::taps_only))
    {
        action->close_menu = true;
    }
    touch_ui_rect(ui, 0.0f, 0.0f, side_x, height, 0x99000000u);
    touch_ui_rect(ui, side_x, 0.0f, side_width, height, palette->background);

    // Its header where the screen's is: MENU, › at the right.
    float header_y = touch_header_y(ui);
    touch_ui_text(ui, side_x + MARGIN + 4.0f, touch_ui_centered_text_y(ui, header_y, TOUCH_HEADER_ICON, TouchText::big),
                  piccolo_text(state->strings, Str::mobile_menu), palette->text_highlight, TouchText::big);
    if (touch_header_icon(ui, "##menu_close", 0, 1, PIXEL_FONT_UTF8_ARROW_RIGHT))
    {
        action->close_menu = true;
    }

    float top = touch_header_bottom(ui) + 4.0f;
    float list_height = height - top;
    float inner_x = side_x + MARGIN;
    float inner_width = side_width - 2.0f * MARGIN;
    touch_ui_push_clip(ui, side_x, top, side_width, list_height);
    float y = top - home->menu_scroll;

    // Over a paused game: back to it first (the pad is hidden while the menu
    // is up; Andrés, 2026-11-25: "no way to show it again").
    if (state->menu_in_game)
    {
        if (touch_ui_button(ui, "##menu_resume", inner_x, y, inner_width, MAIN_BUTTON,
                            piccolo_text(state->strings, Str::menu_resume), TouchButton::primary, TouchText::big))
        {
            action->close_menu = true;
        }
        y += MAIN_BUTTON;
    }

    // The menus, RECENT left out (HOME). Sideways in two columns, each menu
    // into the shorter one.
    int32_t columns = wide ? 2 : 1;
    float column_width = std::floor((inner_width - static_cast<float>(columns - 1) * MARGIN) / static_cast<float>(columns));
    float column_y[2] = {y, y};
    for (int32_t m = 1; m < state->menu_count; m++)
    {
        int32_t column = (columns == 2 && column_y[1] < column_y[0]) ? 1 : 0;
        float x = inner_x + static_cast<float>(column) * (column_width + MARGIN);
        int32_t picked = draw_menu(ui, &state->menus[m], m, x, column_y[column], column_width);
        if (picked != RETRO_UI_MENU_NONE)
        {
            action->picked = picked;
        }
        column_y[column] += menu_height(ui, &state->menus[m]);
    }
    float bottom = column_y[0] > column_y[1] ? column_y[0] : column_y[1];
    float content = bottom + home->menu_scroll - top + MARGIN;
    drag_scroll(ui, "##menu_scroll", side_x, top, side_width, list_height, content, &home->menu_scroll);
    touch_ui_pop_clip();
}

// ---------------------------------------------------------------------------

void mobile_home_draw(MobileHome* home, const MobileHomeState* state, RetroUiFont* pixel_font, int32_t scale,
                      MobileHomeAction* action)
{
    *action = MobileHomeAction{};
    if (!state->home_shown && !state->menu_shown)
    {
        return;
    }
    TouchUi ui;
    ui.font = pixel_font;
    ui.scale = scale;
    ui.palette = *retro_ui_palette();
    if (state->home_shown)
    {
        touch_ui_begin_screen(&ui, "##mobile_home");
        draw_home(home, &ui, state, action);
        touch_ui_end_screen();
    }
    if (state->menu_shown)
    {
        touch_ui_begin_overlay(&ui, "##mobile_menu");
        draw_sidebar(home, &ui, state, action);
        touch_ui_end_screen();
    }
}

void mobile_home_apply(MobileHome* home, const MobileHomeAction* action)
{
    if (action->open_menu)
    {
        home->menu_open = true;
        home->menu_scroll = 0.0f;
    }
    if (action->close_menu || action->picked != RETRO_UI_MENU_NONE)
    {
        home->menu_open = false;
    }
}
