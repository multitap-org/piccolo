// Checks piccolo's screen effects (src/widgets/screen_effects.h) with no window: when
// SNAP and SHUTTER start and end, their curves, and the EFFECTS switch.
//
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdio>

#include "screen_effects.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static void test_snap()
{
    ScreenEffects effects = {};
    ScreenEffectParams params = {};
    screen_effects_update(&effects, 0, 1000, true, &params);
    check(screen_effect_params_at_rest(&params), "nothing played from: at rest");

    screen_effects_update(&effects, 5000, 5000, true, &params);
    check(params.zoom > 1.04f && params.desaturate > 0.99f && params.flash > 0.24f, "PLAY FROM HERE: grey, zoomed, flash");
    screen_effects_update(&effects, 5000, 5100, true, &params);
    check(params.zoom > 1.0f && params.zoom < 1.05f && params.desaturate < 1.0f && params.flash > 0.0f,
          "100 ms in: settling");
    screen_effects_update(&effects, 5000, 5200, true, &params);
    check(params.flash <= 0.0f && params.desaturate > 0.0f, "200 ms in: the flash is gone, still a little grey");
    screen_effects_update(&effects, 5000, 5400, true, &params);
    check(screen_effect_params_at_rest(&params), "400 ms in: at rest");
    screen_effects_update(&effects, 5000, 9000, true, &params);
    check(screen_effect_params_at_rest(&params), "the same PLAY FROM HERE doesn't start it again");

    screen_effects_update(&effects, 9500, 9500, true, &params);
    check(params.desaturate > 0.99f, "a new PLAY FROM HERE starts it again");
}

static void test_shutter()
{
    ScreenEffects effects = {};
    ScreenEffectParams params = {};
    screen_effects_shutter(&effects, 100);
    screen_effects_update(&effects, 0, 100, true, &params);
    check(params.bars <= 0.0f, "0 ms: open");
    screen_effects_update(&effects, 0, 135, true, &params);
    check(params.bars > 0.24f && params.bars < 0.26f, "35 ms: half closed");
    screen_effects_update(&effects, 0, 170, true, &params);
    check(params.bars > 0.49f, "70 ms: closed");
    screen_effects_update(&effects, 0, 260, true, &params);
    check(params.bars > 0.0f && params.bars < 0.5f, "160 ms: opening");
    screen_effects_update(&effects, 0, 350, true, &params);
    check(screen_effect_params_at_rest(&params), "250 ms: at rest");
}

static void test_switch()
{
    ScreenEffects effects = {};
    ScreenEffectParams params = {};
    screen_effects_shutter(&effects, 0);
    screen_effects_update(&effects, 50, 50, false, &params);
    check(screen_effect_params_at_rest(&params), "EFFECTS off: at rest");
    screen_effects_update(&effects, 50, 60, true, &params);
    check(screen_effect_params_at_rest(&params), "turned on again: what started while off doesn't play");
}

int main()
{
    test_snap();
    test_shutter();
    test_switch();
    if (g_failures == 0)
    {
        std::printf("screen effects: all checks passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
