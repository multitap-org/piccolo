#pragma once

// piccolo's screen effects (Andrés, 2026-11-22): what plays on the game
// picture, and when. frontends-common's screen effect pass draws them; this
// only turns time into its knobs (screen_effect_params.h), once a frame.
//
//   SNAP     PLAY FROM HERE (the DVR resumes the game): the picture starts
//            grey, 5% zoomed in, with a faint white flash, and settles back
//            in 400 ms. Started when DvrWidget::played_from_ms changes.
//   SHUTTER  a screenshot: black closes from the top and the bottom in
//            70 ms and opens in 180 ms.
//
// SETTINGS > INTERFACE > EFFECTS turns them all off, or each one. Pure CPU, no clock of
// its own: the caller passes the time (tools/screen_effects_test.cpp).

#include <cstdint>

#include "screen_effect_params.h"

constexpr uint64_t SCREEN_EFFECTS_SNAP_MS = 400;
constexpr uint64_t SCREEN_EFFECTS_SNAP_FLASH_MS = 150;
constexpr uint64_t SCREEN_EFFECTS_SHUTTER_CLOSE_MS = 70;
constexpr uint64_t SCREEN_EFFECTS_SHUTTER_OPEN_MS = 180;

struct ScreenEffects
{
    uint64_t played_from_seen = 0;   // the DVR's last PLAY FROM HERE already handled
    bool     snap             = false;
    uint64_t snap_start_ms    = 0;
    bool     shutter          = false;
    uint64_t shutter_start_ms = 0;
    // Each effect's own switch (SETTINGS > INTERFACE > EFFECTS): off, it
    // doesn't start. The caller sets them before each update.
    bool     snap_on          = true;
    bool     shutter_on       = true;
};

// Starts SHUTTER (a screenshot was taken).
void screen_effects_shutter(ScreenEffects* effects, uint64_t now_ms);

// This frame's knobs. `played_from_ms` = DvrWidget::played_from_ms (a new
// value starts SNAP); `enabled` = the EFFECTS setting (off: everything at
// rest, and nothing starts).
void screen_effects_update(ScreenEffects* effects, uint64_t played_from_ms, uint64_t now_ms, bool enabled,
                           ScreenEffectParams* params);

// The curves, `ms` after each effect started (merged into `params`: the
// stronger value of each knob wins). False once the effect has finished.
bool screen_effects_snap_at(uint64_t ms, ScreenEffectParams* params);
bool screen_effects_shutter_at(uint64_t ms, ScreenEffectParams* params);
