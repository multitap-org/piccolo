#pragma once

// piccolo's side of the ports view (retroui's ports_view: the tabs over the
// game and the DEVICES board, drawn the same as piccoletto's). Here: who is
// on which port, from the input router, and the pads on DEVICES:
//
//   - The columns are rebuilt only when who is where changes (summaries
//     compared every frame, nothing allocated otherwise); a change makes the
//     tabs rise, the changed ports whole; a controller whose device went away
//     is greyed on the port it left.
//   - DEVICES (hotkey.ports) over the paused game: every controller moves
//     itself with LEFT / RIGHT (its own interface bindings, read here: the
//     router lets only port 1 drive the UI); port 1's controllers also pick
//     any controller with UP / DOWN and move that one, and close it with
//     BACK. A mouse picks and moves (ports_view). A move puts the controller
//     on that port only (as the network's DEVICES does). Remote (game_only)
//     pads move from their own device.
//
// Mice aren't shown (one would always sit on port 1).
//
// Split like every window: the updates and ports_widget_state (no GUI) say
// what it shows, ports_widget_draw_tabs / ports_widget_draw_board draw it and
// record what a mouse did on the board, and ports_widget_apply stages that
// move or closes DEVICES (no GUI). The columns, the pick and the staged moves
// stay the widget's own state.

#include <cstdint>
#include <vector>

#include "input_bindings.h"
#include "input_net.h"
#include "localization.h"
#include "ports_view.h"

#include "retro_ui_font.h"

struct PortsWidget
{
    PortsView view;
    uint32_t  port_count = 0;      // ports shown (0 = no game)
    bool      known = false;       // the summaries were taken for this game
    uint64_t  summary[INPUT_PORT_COUNT] = {};
    uint64_t  out_summary = 0;
    bool      open = false;        // DEVICES
    std::vector<uint8_t> held;     // per controller: its directions and BACK last frame
    // DEVICES' moves wait until it closes (a controller moved OUT keeps its
    // input until then); port 1's controllers as it opened lead it.
    std::vector<int32_t> staged;   // per controller: the port it's going to (-1 OUT)
    std::vector<uint8_t> moved;    // ...if it was moved
    std::vector<uint8_t> leader;   // on port 1 when DEVICES opened
    bool      staged_dirty = false;
    uint64_t  board_summary[INPUT_PORT_COUNT] = {};   // the router as the board last drew it
    uint64_t  board_out_summary = 0;
};

// Every frame, after input_update. `port_count` 0 = none shown (piccolo gives
// its PORTS setting without a game, so a join at the menu shows). `net` may be
// nullptr: its pads are the ones with ping bars.
void ports_widget_update(PortsWidget* widget, const InputSystem* input, const InputNet* net, const InputFrame* frame,
                         uint32_t port_count, uint64_t now_ms);

// The tabs or DEVICES show: the frontend keeps the pings fresh then.
bool ports_widget_showing(const PortsWidget* widget);
// Every remote pad's ping: `known[peer]` / `ping_ms[peer]` (NET_MAX_PEERS each).
void ports_widget_set_pings(PortsWidget* widget, const InputSystem* input, const InputNet* net, const bool* known,
                            const uint32_t* ping_ms);

// How far the tabs reach, in retro pixels (the status line starts past them).
float ports_widget_tabs_width(const PortsWidget* widget);

// What it shows from the rest of the program.
struct PortsWidgetState
{
    PortsViewText text;          // the words, in the player's language
    bool          menu_shown = false;   // the tabs peek
    bool          rows = false;         // the phone held upright: DEVICES has a row per port
    uint64_t      now_ms = 0;
};

// What a mouse did on DEVICES this frame.
struct PortsWidgetAction
{
    bool     move = false;   // move `key` to `port` (-1 = OUT), staged until DEVICES closes
    uint32_t key = 0;
    int32_t  port = -1;
    bool     done = false;   // DONE: closes it
};

// `rows`: the phone UI held upright (the board's ports under each other).
void ports_widget_state(PortsWidgetState* state, const Localization* strings, bool menu_shown, bool rows, uint64_t now_ms);

// The tabs, behind every window.
void ports_widget_draw_tabs(PortsWidget* widget, const PortsWidgetState* state, RetroUiFont* pixel_font, int32_t scale);

// DEVICES: open (what's held now is ignored until let go), close.
void ports_widget_open(PortsWidget* widget, const InputSystem* input);
// Closing applies the moves made on it.
void ports_widget_close(PortsWidget* widget, InputSystem* input);

// Every frame while open, after input_update: the pads' moves (and port 1's
// BACK, which closes it).
void ports_widget_board_update(PortsWidget* widget, InputSystem* input);

// DEVICES, above everything (what a mouse did on it goes to `action`).
void ports_widget_draw_board(PortsWidget* widget, const PortsWidgetState* state, RetroUiFont* pixel_font, int32_t scale,
                             PortsWidgetAction* action);

// The board's move (staged) or DONE (closes it, the moves happen).
void ports_widget_apply(PortsWidget* widget, const PortsWidgetAction* action, InputSystem* input);
