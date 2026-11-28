#pragma once

// The state icons, a HUD widget (Andrés, 2026-11-21; piccolo-agents
// design-notes "Widgets: the API in code", mockup
// docs/mockups/widgets/icons-1-states.png): small square chips in the top
// right corner, one per state, right to left:
//
//   [||]   PAUSED (any cause)
//   [<<]   REWINDING (the DVR's window is open)
//   [ >]   PLAY: the game plays on after the DVR's PLAY FROM HERE (2.5 s),
//          or after the player's pause is lifted (1 s; testing round 3)
//   [>>]   FAST FORWARD
//
// REC comes later, with gameplay recording (Andrés: not the rewind history,
// which records whenever REWIND is on).

#include <cstdint>

struct HudStack;

constexpr uint64_t STATE_WIDGET_PLAY_MS = 2500;     // PLAY shows this long after PLAY FROM HERE...
constexpr uint64_t STATE_WIDGET_RESUMED_MS = 1000;  // ...and this long after the player lifts a pause

enum class StateIcon
{
    pause,
    rewind,
    play,
    fast_forward,
    count,
};

constexpr uint32_t STATE_ICON_COUNT = static_cast<uint32_t>(StateIcon::count);

// What is true this frame (app_picture.cpp fills it).
struct StateFacts
{
    bool     paused = false;
    bool     rewinding = false;        // the DVR's window is open
    bool     fast_forwarding = false;
    uint64_t played_from_ms = 0;       // the DVR's last PLAY FROM HERE (0 = none)
    uint64_t resumed_ms = 0;           // the player's pause last lifted (0 = never)
};

struct StateWidget
{
    bool debug_show[STATE_ICON_COUNT] = {};   // the debug window's WIDGETS: shown whatever the facts
};

// Its icons, into the frame's HUD stack.
void state_widget_add_hud(const StateWidget* widget, const StateFacts* facts, HudStack* stack, uint64_t now_ms);
