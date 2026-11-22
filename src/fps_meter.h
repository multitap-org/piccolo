#pragma once

// SHOW FPS: a small chip in the top right corner with what a second of play
// looked like, so speed can be judged without guessing (Andrés, 2026-11-21,
// first test on a phone's browser). Three numbers:
//
//   CORE 59.7/59.7   core frames run in the last second / the core's own rate
//   SCREEN 120       loops (frames presented) in the last second
//   9.4 MS           the loop's own work per loop, on average (input, core,
//                    drawing; not the time spent waiting for the display)
//
// A slow core frame shows as CORE under its rate with MS near or over the
// screen's period; a pacing problem shows as CORE under its rate with MS low.
// Shown when the setting is on, or with PICCOLO_SHOW_FPS=1 in the environment
// (the browser page's DEV channel sets it).

#include <cstdint>

struct ImFont;

struct FpsMeter
{
    uint64_t second_start_ns = 0;   // where the current second began (0: not yet)
    uint32_t loops           = 0;   // counted in the current second
    uint32_t core_frames     = 0;
    uint64_t work_ns         = 0;
    // The last whole second, shown.
    double   core_fps    = 0.0;
    double   screen_hz   = 0.0;
    double   work_ms     = 0.0;
    bool     has_numbers = false;
};

// One loop of the frontend: `core_frames` = core frames run in it (more than
// one when a late loop caught up), `work_ns` = the loop's own work. `now_ns`
// from SDL_GetTicksNS().
void fps_meter_loop(FpsMeter* meter, uint64_t now_ns, uint32_t core_frames, uint64_t work_ns);

// Forgets what was counted (a game started or stopped, the meter was shown).
void fps_meter_reset(FpsMeter* meter);

// The chip's text: "CORE 59.7/59.7  SCREEN 120  9.4 MS" (`target_fps` 0: no
// core running, CORE is left out).
void fps_meter_format(const FpsMeter* meter, double target_fps, char* out, uint32_t out_size);

// Draws the chip in the top right corner of a `display_width` wide screen.
void fps_meter_draw(const FpsMeter* meter, double target_fps, ImFont* pixel_font, int32_t scale, float display_width);
