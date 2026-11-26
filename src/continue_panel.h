#pragma once

// The CONTINUE card on piccolo's screen (continue_card.h has the model,
// retroui's continue_view draws it): the record of the last game closed, its
// picture scaled up sharp for the card, and the card's words, plus the debug
// window's fake cards (MENU > DEBUG > CONTINUE CARD).
//
// Split like every window: continue_panel_update reads the files and the
// picture (no GUI), continue_panel_state fills the card's words,
// continue_view draws it (ui.cpp), main.cpp acts on what was pressed.

#include <cstdint>

#include "continue_card.h"
#include "continue_view.h"
#include "localization.h"
#include "ui_picture.h"

struct AppShell;

// The debug window's fake cards, with a made-up picture.
enum class ContinuePreview
{
    none,
    resume,       // RESUME (a 4:3 game)
    no_states,    // START: the core can't save states
    newer_save,   // START / RESUME: the save file is newer (a 10:9 game)
    phone,        // the phone's HOME card (resume), drawn over the screen
};

constexpr size_t CONTINUE_TEXT_MAX = 300;

struct ContinuePanel
{
    std::string    path;          // <settings>/continue.cfg ("": nothing kept)
    ContinueRecord record;
    bool           record_loaded = false;
    ContinueChoice choice;        // for the top of RECENT, read at `checked_ms`
    uint64_t       checked_ms = 0;
    bool           recheck = true;   // read the files again on the next update

    UiPicture        raw;         // the PNG as saved (the cropped frame)
    UiPicture        scaled;      // it scaled up by a whole number with nearest: drawn bilinear, sharp
    uint32_t         scaled_for = 0;   // the whole number `scaled` was made with
    std::string      raw_path;    // what `raw` holds ("": nothing)
    uint64_t         raw_modified = 0;

    ContinuePreview preview = ContinuePreview::none;
};

// What the card shows this frame (UiView).
struct ContinuePanelView
{
    bool             shown = false;    // the desktop panel
    bool             phone = false;    // the phone's card instead (the debug window's preview)
    ContinueViewCard card;
    char             detail[CONTINUE_TEXT_MAX] = {};
    char             when[CONTINUE_TEXT_MAX] = {};
    char             game[CONTINUE_TEXT_MAX] = {};
};

// Reads <settings>/continue.cfg (missing: no card yet).
void continue_panel_load(ContinuePanel* panel, const char* path);

// A game was closed: its record kept (and written) for the card.
void continue_panel_closed(ContinuePanel* panel, const ContinueRecord* record);

// Every frame: `top` the top of RECENT (nullptr: empty), `wanted` whether the
// card may show (no game, the desktop UI), `picture_width` / `_height` the
// picture's size on screen (window pixels). Reads the files about once a
// second while wanted, loads and scales the picture when it changes.
void continue_panel_update(ContinuePanel* panel, const RecentGame* top, bool wanted, float picture_width,
                           float picture_height, uint64_t now_ms);

// Which card it shows now (the debug window's fake one first).
ContinueChoice continue_panel_choice(const ContinuePanel* panel);
// The record shown (the fake one while previewing).
const ContinueRecord* continue_panel_record(const ContinuePanel* panel);

// The card's words and picture. `now` the local date and time.
void continue_panel_state(ContinuePanelView* view, const ContinuePanel* panel, const Localization* strings,
                          const ContinueDate* now, bool active);

// The debug window's fake card (none: the real one again).
void continue_panel_preview(ContinuePanel* panel, ContinuePreview preview);

// Uploads a newly made picture (after app_shell_acquire's caller rules, like
// QUICK MENU's).
void continue_panel_upload(ContinuePanel* panel, AppShell* shell);
void continue_panel_destroy(ContinuePanel* panel, AppShell* shell);
