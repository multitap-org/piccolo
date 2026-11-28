#include "app_continue.h"

#include "app_game.h"
#include "app_menus.h"

// ---------------------------------------------------------------------------
// The CONTINUE card (continue_panel.h): the last game closed, on the empty
// screen. Part of it, not a window: no game running, MAIN MENU's bar alone
// or with things over it; it's never in Esc's chain. A, Enter or a click
// press its first button only while nothing is open over it.
// ---------------------------------------------------------------------------

static bool continue_card_wanted(const App* app)
{
    return !game_running(app) && app->ui.mode == UiMode::main_menu && !app->onscreen_pad.editor.active;
}

// Shown at all this frame (the real one has a card to show; a fake one
// from the debug window shows over a game too).
static bool continue_card_shown(const App* app)
{
    if (app->continue_panel.preview != ContinuePreview::none)
    {
        return true;
    }
    return continue_card_wanted(app) && continue_panel_choice(&app->continue_panel).kind != ContinueKind::none;
}

// Nothing open over it: its first button is highlighted and takes A / Enter.
bool continue_card_active(const App* app)
{
    if (app->continue_panel.preview == ContinuePreview::phone)
    {
        return true;   // over everything
    }
    bool nothing_open = app->ui.window_count == 0 && !ui_toolkit_has_popup() && !app->pair_window.open &&
                        !app->guest_window.open && !debug_preview_shown(&app->debug_previews) &&
                        app->ui.mode == UiMode::main_menu;
    return continue_card_shown(app) && nothing_open;
}

// What the card's button does: RESUME starts the game in the card (not
// necessarily RECENT's top) with its core (SAVE STATE ON EXIT loads the AUTO
// state); START / RESTART starts it without that. A fake card's buttons only
// end the preview.
void continue_card_press(App* app, ContinueViewButton button)
{
    if (button == ContinueViewButton::none)
    {
        return;
    }
    if (app->continue_panel.preview != ContinuePreview::none)
    {
        continue_panel_preview(&app->continue_panel, ContinuePreview::none);
        return;
    }
    const RecentGame* game = continue_panel_card_game(&app->continue_panel);
    if (game == nullptr || game_running(app))
    {
        return;
    }
    app->pending_start = FilePickerChoice{};
    std::snprintf(app->pending_start.core_path, sizeof(app->pending_start.core_path), "%s", game->core_path.c_str());
    std::snprintf(app->pending_start.content_path, sizeof(app->pending_start.content_path), "%s",
                  game->content_path.c_str());
    app->has_pending_start = true;   // started at the top of the next loop, like RECENT
    app->resume_skip = button == ContinueViewButton::start;
}

// A tile / row of the shelf chosen (a click, or A / Enter on it): its game
// goes into the card. Nothing starts: that is RESUME or RESTART.
void continue_card_choose(App* app, uint32_t tile)
{
    continue_panel_choose(&app->continue_panel, tile);
}

// Before the view: the files and the picture, scaled for its size on screen.
void continue_card_update(App* app, const UiView* view)
{
    ContinuePanel* panel = &app->continue_panel;
    const ContinueRecord* record = continue_panel_record(panel);
    ContinueViewCard card = {};
    card.aspect_width = record->aspect_width;
    card.aspect_height = record->aspect_height;
    float width = 0.0f;
    float height = 0.0f;
    float tile_width = 0.0f;   // the shelf's / the list's pictures: only on the desktop
    float tile_height = 0.0f;
    bool as_list = false;
    // RECENT as shown: the panel keeps the order of its games from it.
    std::vector<RecentGame> shown;
    for (size_t i = 0; i < app->recent_shown.size(); i++)
    {
        shown.push_back(app->recent.games[app->recent_shown[i]]);
    }
    if (panel->preview == ContinuePreview::phone)
    {
        int32_t touch_scale = touch_ui_scale_for(view->screen_width, view->screen_height);
        float phone = view->screen_width / static_cast<float>(touch_scale);
        phone = phone < 195.0f ? phone : 195.0f;
        continue_view_card_picture_size(&card, phone - 16.0f, &width, &height);
        width *= static_cast<float>(touch_scale);
        height *= static_cast<float>(touch_scale);
    }
    else if (app->phone_ui)
    {
        // HOME's card (mobile_home.h), at the phone UI's scale. Upright its
        // picture may be drawn a little shorter when the list needs the
        // room: still sharp, a touch softer.
        float scale = static_cast<float>(app->ui_scale);
        float screen_width = std::floor(view->screen_width / scale);
        float screen_height = std::floor(view->screen_height / scale);
        if (screen_width > screen_height)
        {
            float box_width = 0.0f;
            float box_height = 0.0f;
            mobile_home_cover_box(screen_width, screen_height, &box_width, &box_height);
            continue_view_cover_picture_size(&card, box_width, box_height, &width, &height);
        }
        else
        {
            continue_view_card_picture_size(&card, mobile_home_card_width(screen_width), &width, &height);
        }
        width *= scale;
        height *= scale;
    }
    else
    {
        // The sizes the panel will be drawn at (ui.cpp, the same call). It
        // shows only in MAIN MENU on the desktop, under the menu bar (the
        // view's menu_shown is set later this frame).
        float bar = retro_ui_menu_bar_height(app->ui_scale);
        ContinueViewPanelGeometry geometry;
        continue_view_panel_geometry(app->ui_scale, shown.size() > 1, bar, view->screen_width, view->screen_height,
                                     &geometry);
        continue_view_panel_picture_size(&card, &geometry, &width, &height);
        as_list = geometry.layout == ContinueViewLayout::side_list;
        tile_width = geometry.tile_width;
        tile_height = geometry.tile_height;
    }
    continue_panel_update(panel, &shown, continue_card_wanted(app), width, height, tile_width, tile_height, as_list,
                          SDL_GetTicks());
}

// The desktop's panel, or the card on the phone's HOME (or a fake one).
void continue_card_view(const App* app, UiView* view)
{
    bool fake = app->continue_panel.preview != ContinuePreview::none;
    bool real = !fake && continue_card_shown(app);
    view->continue_card.shown = fake || (real && !app->phone_ui);
    if (!fake && !real)
    {
        return;
    }
    SDL_Time now_time = 0;
    SDL_GetCurrentTime(&now_time);
    SDL_DateTime date = {};
    SDL_TimeToDateTime(now_time, &date, true);
    ContinueDate now = {};
    now.year = date.year;
    now.month = date.month;
    now.day = date.day;
    now.weekday = date.day_of_week;
    now.hour = date.hour;
    now.minute = date.minute;
    continue_panel_state(&view->continue_card, &app->continue_panel, &app->strings, &now, continue_card_active(app),
                         app->phone_ui);
    // The phone's HOME shows a card only for a game with a record (a game
    // without one is just in its list).
    if (real && app->phone_ui && view->mobile_home.home_shown && continue_panel_card_has_record(&app->continue_panel))
    {
        view->continue_card.card.hint = "";   // the phone shows no keys
        view->mobile_home.card = &view->continue_card.card;
    }
}
