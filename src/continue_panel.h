#pragma once

// The CONTINUE card on piccolo's screen (continue_card.h has the model,
// retroui's continue_view draws it): the game in the card (RECENT's top until
// another is chosen from the shelf), its record and picture scaled up sharp,
// the shelf of the other recent games with theirs, and the words; plus the
// debug window's fake cards (MENU > DEBUG > CONTINUE CARD).
//
// Split like every window: continue_panel_update reads the files and the
// picture (no GUI), continue_panel_state fills the card's words,
// continue_view draws it (ui.cpp), main.cpp acts on what was pressed.

#include <cstdint>
#include <string>
#include <vector>

#include "continue_card.h"
#include "continue_view.h"
#include "localization.h"
#include "ui_picture.h"

struct AppShell;

// The debug window's fake cards (with a fake shelf of games), with made-up
// pictures.
enum class ContinuePreview
{
    none,
    resume,       // RESUME / RESTART (a 4:3 game)
    no_states,    // RESTART: the core can't save states
    newer_save,   // RESTART / RESUME: the save file is newer (a 10:9 game)
    vertical,     // a vertical (3:4) game in the card
    phone,        // the phone's HOME card (resume), drawn over the screen
};

// The keys that move the panel's focus (the interface's directions).
enum class ContinueKey
{
    up,
    down,
    left,
    right,
};

constexpr size_t CONTINUE_TEXT_MAX = 300;

// A picture of the panel (the card's or a tile's): the PNG, and it scaled.
struct ContinuePicture
{
    UiPicture   raw;              // the PNG as saved (the cropped frame)
    UiPicture   scaled;           // it scaled up by a whole number with nearest: drawn bilinear, sharp
    uint32_t    scaled_for = 0;   // the whole number `scaled` was made with
    std::string raw_path;         // what `raw` holds ("": nothing; a fake one: "fake:<n>")
    uint64_t    raw_modified = 0;
};

struct ContinuePanel
{
    std::string                 path;      // <settings>/continue.cfg ("": nothing kept)
    std::vector<ContinueRecord> records;   // one per game, most recent first
    std::vector<RecentGame>     games;     // RECENT as the panel last saw it
    std::vector<uint32_t>       order;     // indices into `games` (continue_order_*): [0] the card, [1...] the shelf
    uint32_t                    shelf_page = 0;
    int32_t                     focus_tile = -1;       // the keys: on this tile of the page, -1 = on the buttons
    bool                        focus_second = false;  // ... on the second button
    bool                        as_list = false;   // the other games are a list at the right (Down / Up along it), not a shelf
    ContinueChoice              choice;    // for the card's game, read at `checked_ms`
    bool                        card_recorded = false;   // the card's game has a record
    uint64_t                    checked_ms = 0;
    bool                        recheck = true;   // read the files again on the next update

    ContinuePicture card_picture;
    ContinuePicture tile_pictures[CONTINUE_VIEW_TILES_MAX];   // the shelf page shown

    ContinuePreview             preview = ContinuePreview::none;
    std::vector<ContinueRecord> fakes;   // the fake games (preview), `order` indexes them then
};

// What the card shows this frame (UiView).
struct ContinuePanelView
{
    bool             shown = false;    // the desktop panel
    bool             phone = false;    // the phone's card instead (the debug window's preview)
    ContinueViewCard card;
    char             core[CONTINUE_TEXT_MAX] = {};
    char             when[CONTINUE_TEXT_MAX] = {};
    char             game[CONTINUE_TEXT_MAX] = {};
    char             hint[CONTINUE_TEXT_MAX] = {};
    char             tile_game[CONTINUE_VIEW_TILES_MAX][CONTINUE_TEXT_MAX] = {};
    char             tile_line[CONTINUE_VIEW_TILES_MAX][CONTINUE_TEXT_MAX] = {};
    char             tile_core[CONTINUE_VIEW_TILES_MAX][CONTINUE_TEXT_MAX] = {};
};

// Reads <settings>/continue.cfg (missing: no records yet).
void continue_panel_load(ContinuePanel* panel, const char* path);

// A game was closed: its record kept (and written) for its card.
void continue_panel_closed(ContinuePanel* panel, const ContinueRecord* record);

// Every frame: `games` RECENT's games as shown (when they change, the order
// goes back to RECENT's), `wanted` whether the card may show (no game, the
// desktop UI), `picture_width` / `_height` the card's picture size on screen
// (window pixels), `tile_scale` the UI scale for the shelf's pictures (0: no
// pictures: the phone, or the other games are a text list, `as_list`). Reads the files about once a second while wanted, loads
// and scales the pictures of the card and of the shelf page shown, and only
// those, when they change.
void continue_panel_update(ContinuePanel* panel, const std::vector<RecentGame>* games, bool wanted,
                           float picture_width, float picture_height, int32_t tile_scale, bool as_list,
                           uint64_t now_ms);

// Which card it shows now (the debug window's fake one first).
ContinueChoice continue_panel_choice(const ContinuePanel* panel);
// The record of the game in the card (an empty one when it has none, or
// the fake one while previewing).
const ContinueRecord* continue_panel_record(const ContinuePanel* panel);
// Whether the card's game has a record (the phone shows no card without one).
bool continue_panel_card_has_record(const ContinuePanel* panel);
// The game in the card, nullptr while previewing or without games.
const RecentGame* continue_panel_card_game(const ContinuePanel* panel);

// The card's words, picture, buttons and the shelf page. `now` the local
// date and time. `phone`: the phone's card (no shelf, one button).
void continue_panel_state(ContinuePanelView* view, const ContinuePanel* panel, const Localization* strings,
                          const ContinueDate* now, bool active, bool phone);

// The two buttons of the card, by what it offers.
void continue_panel_buttons(ContinueKind kind, ContinueViewButton* first, ContinueViewButton* second);
// The button the keys are on (A / Enter presses it): none while on a tile.
ContinueViewButton continue_panel_focused_button(const ContinuePanel* panel);

// A direction key: Left / Right between the buttons, Down into the shelf,
// along its tiles and over to the next page, Up back to the buttons. With a
// list instead (`as_list`): Right from the last button into it, Down / Up
// along its rows and over the pages, Left back to the buttons.
void continue_panel_key(ContinuePanel* panel, ContinueKey key);
// A tile of the shelf page chosen (A / Enter on it, a click): its game goes
// to the card, the card's to the shelf's front. Focus back on the first button.
void continue_panel_choose(ContinuePanel* panel, uint32_t tile);
// The page indicator's arrows (-1 / +1).
void continue_panel_page(ContinuePanel* panel, int32_t delta);

// The debug window's fake card (none: the real one again).
void continue_panel_preview(ContinuePanel* panel, ContinuePreview preview);

// Uploads newly made pictures (after app_shell_acquire's caller rules, like
// QUICK MENU's).
void continue_panel_upload(ContinuePanel* panel, AppShell* shell);
void continue_panel_destroy(ContinuePanel* panel, AppShell* shell);
