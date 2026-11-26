#include "mobile_home.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"   // ClearActiveID: a drag drops the row's tap

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
constexpr float ROW_INSET    = 5.0f;    // RESUME inside a selected row
constexpr float SECTION_GAP  = 8.0f;    // above a menu's name in the sidebar
constexpr float SIDE_COLUMN  = 120.0f;  // landscape HOME: LOAD GAME's column
constexpr int32_t HEADER_ICONS = 2;     // ROTATE, ☰
constexpr float COVER_SHARE  = 0.55f;   // sideways: the CONTINUE picture's widest, of the screen's content

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

constexpr float DRAG_START = 3.0f;   // retro pixels a finger moves before a press is a drag

// Drag to scroll a list in (x, y, width, height); `content` its whole
// height. Called after the list's rows: a press on a row that moves
// DRAG_START scrolls instead, and the row (ImGui's active item) lets go, so
// lifting the finger doesn't tap it.
static void drag_scroll(const TouchUi* ui, MobileDrag* drag, float x, float y, float width, float height,
                        float content, float* scroll)
{
    float scale = static_cast<float>(ui->scale);
    bool inside = ImGui::IsMouseHoveringRect(ImVec2(x * scale, y * scale),
                                             ImVec2((x + width) * scale, (y + height) * scale), false);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        drag->armed = inside;
        drag->dragging = false;
        drag->moved = 0.0f;
    }
    if (drag->armed && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        float delta = ImGui::GetIO().MouseDelta.y / scale;
        if (!drag->dragging)
        {
            drag->moved += std::fabs(delta);
            if (drag->moved >= DRAG_START)
            {
                drag->dragging = true;
                ImGui::ClearActiveID();
            }
        }
        if (drag->dragging)
        {
            *scroll -= delta;
        }
    }
    else
    {
        drag->armed = false;
        drag->dragging = false;
    }
    if (inside)
    {
        *scroll -= ImGui::GetIO().MouseWheel * 16.0f;
    }
    float most = content > height ? content - height : 0.0f;
    *scroll = *scroll < 0.0f ? 0.0f : (*scroll > most ? most : *scroll);
}

// The keyboard's or gamepad's focus kept in view in a scrolled list: the
// focused row was drawn at `focus` this frame; the scroll moves for the next.
static void keep_focus_in_view(const TouchFocus* focus, float top, float height, float* scroll)
{
    if (!focus->has)
    {
        return;
    }
    float row_top = focus->y - ROW_GAP;
    float row_bottom = focus->y + focus->height + ROW_GAP;
    if (row_top < top)
    {
        *scroll -= top - row_top;
    }
    else if (row_bottom > top + height)
    {
        *scroll += row_bottom - (top + height);
    }
}

// ---------------------------------------------------------------------------
// HOME

// One recent game: its name, the core under it; tapping it starts it.
// A recent game's row. A tap only selects it (Andrés: a game opening on a
// tap is bound for mistakes, a scroll or a stray touch): the selected row is
// outlined and has RESUME at its right, which starts the game (its
// automatic state, if it has one). Returns the row tapped; *resume RESUME.
static bool draw_game(const TouchUi* ui, const MobileGameRow* game, int32_t index, float x, float y, float width,
                      bool selected, const char* resume_label, bool* resume)
{
    const RetroUiPalette* palette = &ui->palette;
    char id[32];
    float row_width = width;
    if (selected)
    {
        float button_width = touch_ui_text_width(ui, resume_label, TouchText::normal) + 16.0f;
        float button_x = x + width - ROW_INSET - button_width;
        row_width = button_x - ROW_INSET - x;
        touch_ui_rect(ui, x, y, width, GAME_ROW, palette->panel_face);   // under RESUME
        std::snprintf(id, sizeof(id), "##resume%d", static_cast<int>(index));
        *resume = touch_ui_button(ui, id, button_x, y + ROW_INSET, button_width, GAME_ROW - 2.0f * ROW_INSET,
                                  resume_label, TouchButton::primary, TouchText::normal);
    }
    std::snprintf(id, sizeof(id), "##game%d", static_cast<int>(index));
    bool tapped = touch_ui_button(ui, id, x, y, row_width, GAME_ROW, nullptr, TouchButton::card, TouchText::normal);
    if (selected)
    {
        touch_ui_outline(ui, x, y, width, GAME_ROW, 1.0f, palette->highlight);
    }
    // A long name is cut at the row's edge (RESUME's, when selected).
    touch_ui_push_clip(ui, x, y, row_width - CARD_PADDING * 0.5f, GAME_ROW);
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
// `first`: the first game listed (1 when the CONTINUE card shows games[0]);
// `columns`: two sideways, one when the card takes half the room.
static void draw_games(MobileHome* home, const TouchUi* ui, const MobileHomeState* state, float x, float y, float width,
                       float height, int32_t first, int32_t columns, MobileHomeAction* action)
{
    float column_width = std::floor((width - static_cast<float>(columns - 1) * ROW_GAP) / static_cast<float>(columns));
    int32_t listed = state->game_count - first;
    int32_t rows = (listed + columns - 1) / columns;
    float content = static_cast<float>(rows) * (GAME_ROW + ROW_GAP);
    // Every row is submitted, the ones scrolled away too (clipped), so the
    // keyboard and gamepad can move onto them; the list follows the focus.
    TouchFocus focus;
    TouchUi list_ui = *ui;
    list_ui.focus = &focus;
    const char* resume_label = piccolo_text(state->strings, Str::continue_resume);
    touch_ui_push_clip(ui, x, y, width, height);
    for (int32_t i = first; i < state->game_count; i++)
    {
        int32_t column = (i - first) % columns;
        int32_t row = (i - first) / columns;
        float row_x = x + static_cast<float>(column) * (column_width + ROW_GAP);
        float row_y = y + static_cast<float>(row) * (GAME_ROW + ROW_GAP) - home->scroll;
        bool resume = false;
        if (draw_game(&list_ui, &state->games[i], i, row_x, row_y, column_width, home->selected == i, resume_label,
                      &resume))
        {
            home->selected = i;
        }
        if (resume)
        {
            action->picked = state->first_game_id + i;
        }
    }
    keep_focus_in_view(&focus, y, height, &home->scroll);
    drag_scroll(ui, &home->drag, x, y, width, height, content, &home->scroll);
    touch_ui_pop_clip();
}

float mobile_home_card_width(float width)
{
    return width - 2.0f * MARGIN;
}

// Sideways: where the content starts (under the header), and the CONTINUE
// picture's room under its name.
static float wide_content_y()
{
    return 6.0f + TOUCH_HEADER_ICON + 6.0f;   // touch_header_bottom sideways, then the gap
}

void mobile_home_cover_box(float width, float height, float* box_width, float* box_height)
{
    float top = wide_content_y() + SMALL_BUTTON + 6.0f;
    *box_width = std::floor((width - 2.0f * MARGIN) * COVER_SHARE);
    *box_height = height - MARGIN - top;
}

// A section's name at the left of a SMALL_BUTTON-tall row (RECENT GAMES,
// CONTINUE).
static void section_name(const TouchUi* ui, float x, float y, const char* name)
{
    touch_ui_text(ui, x + 2.0f, touch_ui_centered_text_y(ui, y, SMALL_BUTTON, TouchText::normal), name,
                  ui->palette.text_disabled, TouchText::normal);
}

// Sideways (Andrés's layout C): CONTINUE, and under it the picture as tall
// as the screen allows with the words on a band over it. Returns its width.
static float draw_continue_cover(const TouchUi* ui, const MobileHomeState* state, float x, float y,
                                 MobileHomeAction* action)
{
    section_name(ui, x, y, piccolo_text(state->strings, Str::mobile_continue));
    float box_width = 0.0f;
    float box_height = 0.0f;
    mobile_home_cover_box(touch_ui_width(ui), touch_ui_height(ui), &box_width, &box_height);
    float drawn = 0.0f;
    ContinueViewButton pressed =
        continue_view_draw_cover(ui, state->card, x, y + SMALL_BUTTON + 6.0f, box_width, box_height, &drawn);
    if (pressed != ContinueViewButton::none)
    {
        action->continue_pressed = pressed;
    }
    float name = touch_ui_text_width(ui, piccolo_text(state->strings, Str::mobile_continue), TouchText::normal) + 4.0f;
    return drawn > name ? drawn : name;
}

// CONTINUE and its card at (x, y), `width` wide, no taller than `room`
// (its picture gets shorter first). Returns its bottom.
static float draw_continue(const TouchUi* ui, const MobileHomeState* state, float x, float y, float width, float room,
                           MobileHomeAction* action)
{
    // Upright (sideways is draw_continue_cover).
    section_name(ui, x, y, piccolo_text(state->strings, Str::mobile_continue));
    float card_y = y + SMALL_BUTTON + 6.0f;
    room -= SMALL_BUTTON + 6.0f;
    ContinueViewCard card = *state->card;
    float tall = continue_view_card_height(ui, &card, width);
    if (tall > room)
    {
        float picture_width = 0.0f;
        float picture_height = 0.0f;
        continue_view_card_picture_size(&card, width, &picture_width, &picture_height);
        float picture = picture_height - (tall - room);
        card.picture_max_height = picture > 24.0f ? picture : 24.0f;
    }
    float drawn = 0.0f;
    ContinueViewButton pressed = continue_view_draw_card(ui, &card, x, card_y, width, &drawn);
    if (pressed != ContinueViewButton::none)
    {
        action->continue_pressed = pressed;
    }
    return card_y + drawn;
}

static void draw_home(MobileHome* home, const TouchUi* ui, const MobileHomeState* state, MobileHomeAction* action)
{
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

    float content_y = wide ? wide_content_y() : touch_header_bottom(ui) + 12.0f;
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
    float button_x = wide ? width - MARGIN - SIDE_COLUMN : MARGIN;
    float button_width = wide ? SIDE_COLUMN : width - 2.0f * MARGIN;

    // The CONTINUE card first (the mockup): upright above the games, leaving
    // room for two of them. Sideways the picture at the left, as tall as the
    // screen allows, and the games at its right with LOAD GAME under them.
    float list_x = MARGIN;
    float list_y = content_y;
    int32_t first = 0;
    int32_t columns = wide ? 2 : 1;
    if (state->card != nullptr)
    {
        first = 1;   // the card's game
        if (wide)
        {
            float card_width = draw_continue_cover(ui, state, list_x, content_y, action);
            list_x += card_width + MARGIN;
            list_width = width - MARGIN - list_x;
            list_bottom = height - 2.0f * MARGIN - MAIN_BUTTON;
            button_x = list_x;
            button_width = list_width;
            columns = 1;
        }
        else
        {
            float room = list_bottom - content_y - SMALL_BUTTON - 6.0f - 2.0f * (GAME_ROW + ROW_GAP);
            list_y = draw_continue(ui, state, list_x, content_y, list_width, room, action) + 8.0f;
        }
    }

    // RECENT GAMES, CLEAR ALL at its right.
    const char* clear = piccolo_text(strings, Str::menu_clear_recents);
    float clear_width = touch_ui_text_width(ui, clear, TouchText::normal) + 12.0f;
    section_name(ui, list_x, list_y, piccolo_text(strings, Str::mobile_recent_games));
    if (touch_ui_button(ui, "##clear_recents", list_x + list_width - clear_width, list_y, clear_width, SMALL_BUTTON,
                        clear, TouchButton::normal, TouchText::normal))
    {
        action->picked = state->clear_recents_id;
    }
    list_y += SMALL_BUTTON + 6.0f;
    draw_games(home, ui, state, list_x, list_y, list_width, list_bottom - list_y, first, columns, action);

    float button_y = height - MARGIN - MAIN_BUTTON;
    // With the CONTINUE card RESUME is the main action, not LOAD GAME.
    TouchButton load_style = state->card != nullptr ? TouchButton::normal : TouchButton::primary;
    if (touch_ui_button(ui, "##load_game", button_x, button_y, button_width, MAIN_BUTTON,
                        piccolo_text(strings, Str::mobile_load_game), load_style, TouchText::big))
    {
        action->picked = state->load_game_id;
    }
}

// ---------------------------------------------------------------------------
// The ☰ sidebar: the menus' names, then one menu's items (Andrés,
// 2026-11-25: "tap on game you get on the game menu and a back button to go
// back to the list")

// A row with a name and ▶ at its right: opens that menu's items.
static bool draw_menu_row(const TouchUi* ui, const char* id, const char* label, float x, float y, float width)
{
    bool tapped = touch_ui_button(ui, id, x, y, width, MENU_ROW, label, TouchButton::card, TouchText::normal);
    float arrow_width = touch_ui_text_width(ui, PIXEL_FONT_UTF8_ARROW_RIGHT, TouchText::normal);
    touch_ui_text(ui, x + width - CARD_PADDING - arrow_width, touch_ui_centered_text_y(ui, y, MENU_ROW, TouchText::normal),
                  PIXEL_FONT_UTF8_ARROW_RIGHT, ui->palette.text_disabled, TouchText::normal);
    return tapped;
}

// One menu's items, a row each; a greyed item is drawn dim and takes no
// taps; separators are a gap. Returns the y under them; *picked the tapped
// item's id.
static float draw_menu_items(const TouchUi* ui, const RetroUiMenu* menu, float x, float y, float width, int32_t* picked)
{
    const RetroUiPalette* palette = &ui->palette;
    for (int32_t i = 0; i < menu->item_count; i++)
    {
        const RetroUiMenuItem* item = &menu->items[i];
        if (item->id == RETRO_UI_MENU_SEPARATOR)
        {
            y += SECTION_GAP;
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
            char id[32];
            std::snprintf(id, sizeof(id), "##item%d", static_cast<int>(i));
            if (touch_ui_button(ui, id, x, y, width, MENU_ROW, label, TouchButton::card, TouchText::normal))
            {
                *picked = item->id;
            }
        }
        y += MENU_ROW + ROW_GAP;
    }
    return y;
}

static void draw_sidebar(MobileHome* home, const TouchUi* ui, const MobileHomeState* state, MobileHomeAction* action)
{
    const RetroUiPalette* palette = &ui->palette;
    float width = touch_ui_width(ui);
    float height = touch_ui_height(ui);
    bool wide = landscape(ui);
    float side_width = std::floor(width * (wide ? MENU_WIDTH_LANDSCAPE : MENU_WIDTH_PORTRAIT));
    float side_x = width - side_width;
    int32_t open = home->submenu > 0 && home->submenu < state->menu_count ? home->submenu : 0;

    // The rest of the screen dimmed: a tap there closes it.
    bool held = false;
    if (touch_ui_area(ui, "##menu_outside", 0.0f, 0.0f, side_x, height, &held, TouchNav::taps_only))
    {
        action->close_menu = true;
    }
    touch_ui_rect(ui, 0.0f, 0.0f, side_x, height, 0x99000000u);
    touch_ui_rect(ui, side_x, 0.0f, side_width, height, palette->background);

    // Its header where the screen's is: MENU, or ◀ and the open menu's name;
    // › at the right closes it.
    float header_y = touch_header_y(ui);
    float title_x = side_x + MARGIN + 4.0f;
    const char* title = piccolo_text(state->strings, Str::mobile_menu);
    if (open > 0)
    {
        if (touch_ui_button(ui, "##menu_back", side_x + MARGIN - 2.0f, header_y, TOUCH_HEADER_ICON, TOUCH_HEADER_ICON,
                            PIXEL_FONT_UTF8_ARROW_LEFT, TouchButton::plain, TouchText::big))
        {
            action->submenu_back = true;
        }
        title_x = side_x + MARGIN + TOUCH_HEADER_ICON + 2.0f;
        title = state->menus[open].label;
    }
    touch_ui_text(ui, title_x, touch_ui_centered_text_y(ui, header_y, TOUCH_HEADER_ICON, TouchText::big), title,
                  palette->text_highlight, TouchText::big);
    if (touch_header_icon(ui, "##menu_close", 0, 1, PIXEL_FONT_UTF8_ARROW_RIGHT))
    {
        action->close_menu = true;
    }

    float top = touch_header_bottom(ui) + 4.0f;
    float list_height = height - top;
    float x = side_x + MARGIN;
    float row_width = side_width - 2.0f * MARGIN;
    touch_ui_push_clip(ui, side_x, top, side_width, list_height);
    float y = top + SECTION_GAP - home->menu_scroll;
    // The keyboard's or gamepad's focus: the list scrolls to keep it in view.
    TouchFocus focus;
    TouchUi list_ui = *ui;
    list_ui.focus = &focus;

    if (open == 0)
    {
        // Over a paused game: back to it first (the pad is hidden while the
        // menu is up; Andrés, 2026-11-25: "no way to show it again"), a row
        // like the others in the highlight color.
        if (state->menu_in_game)
        {
            if (touch_ui_button(&list_ui, "##menu_resume", x, y, row_width, MENU_ROW,
                                piccolo_text(state->strings, Str::menu_resume), TouchButton::primary, TouchText::normal))
            {
                action->close_menu = true;
            }
            y += MENU_ROW + SECTION_GAP;
        }
        // The menus by name, RECENT left out (it is HOME).
        for (int32_t m = 1; m < state->menu_count; m++)
        {
            char id[32];
            std::snprintf(id, sizeof(id), "##menu%d", static_cast<int>(m));
            if (draw_menu_row(&list_ui, id, state->menus[m].label, x, y, row_width))
            {
                action->open_submenu = m;
            }
            y += MENU_ROW + ROW_GAP;
        }
    }
    else
    {
        int32_t picked = RETRO_UI_MENU_NONE;
        y = draw_menu_items(&list_ui, &state->menus[open], x, y, row_width, &picked);
        if (picked != RETRO_UI_MENU_NONE)
        {
            action->picked = picked;
        }
    }
    float content = y + home->menu_scroll - top + MARGIN;
    keep_focus_in_view(&focus, top, list_height, &home->menu_scroll);
    drag_scroll(ui, &home->menu_drag, side_x, top, side_width, list_height, content, &home->menu_scroll);
    touch_ui_pop_clip();
}

// ---------------------------------------------------------------------------

void mobile_home_draw(MobileHome* home, const MobileHomeState* state, RetroUiFont* pixel_font, int32_t scale,
                      MobileHomeAction* action)
{
    *action = MobileHomeAction{};
    if (!state->home_shown || home->selected >= state->game_count)
    {
        home->selected = -1;   // a game runs (the list changes when it closes), or the list got shorter
    }
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

void mobile_home_apply(MobileHome* home, const MobileHomeAction* action, bool menu_shown)
{
    if (action->open_menu)
    {
        home->menu_open = true;
        home->menu_scroll = 0.0f;
    }
    if (action->open_submenu > 0)
    {
        home->submenu = action->open_submenu;
        home->menu_scroll = 0.0f;
    }
    if (action->submenu_back)
    {
        home->submenu = 0;
        home->menu_scroll = 0.0f;
    }
    if (action->close_menu || action->picked != RETRO_UI_MENU_NONE)
    {
        home->menu_open = false;
    }
    // It opens on the menus' names every time.
    bool closed = action->close_menu || action->picked != RETRO_UI_MENU_NONE;
    if (!menu_shown || closed)
    {
        home->submenu = 0;
        home->menu_scroll = 0.0f;
    }
}
