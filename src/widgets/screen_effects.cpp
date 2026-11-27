#include "screen_effects.h"

// 0..1 of the way through, slowing down at the end (cubic ease-out).
static float ease_out(uint64_t ms, uint64_t length_ms)
{
    if (ms >= length_ms)
    {
        return 1.0f;
    }
    float t = static_cast<float>(ms) / static_cast<float>(length_ms);
    float rest = 1.0f - t;
    return 1.0f - rest * rest * rest;
}

static float max_of(float a, float b)
{
    return a > b ? a : b;
}

bool screen_effects_snap_at(uint64_t ms, ScreenEffectParams* params)
{
    if (ms >= SCREEN_EFFECTS_SNAP_MS)
    {
        return false;
    }
    float settled = ease_out(ms, SCREEN_EFFECTS_SNAP_MS);
    params->zoom = max_of(params->zoom, 1.05f - 0.05f * settled);
    params->desaturate = max_of(params->desaturate, 1.0f - settled);
    if (ms < SCREEN_EFFECTS_SNAP_FLASH_MS)
    {
        // A faint white flash, fading straight out.
        float flash = 0.25f * (1.0f - static_cast<float>(ms) / static_cast<float>(SCREEN_EFFECTS_SNAP_FLASH_MS));
        params->flash = max_of(params->flash, flash);
        params->flash_red = 1.0f;
        params->flash_green = 1.0f;
        params->flash_blue = 1.0f;
    }
    return true;
}

bool screen_effects_shutter_at(uint64_t ms, ScreenEffectParams* params)
{
    uint64_t total = SCREEN_EFFECTS_SHUTTER_CLOSE_MS + SCREEN_EFFECTS_SHUTTER_OPEN_MS;
    if (ms >= total)
    {
        return false;
    }
    // Each bar covers up to half the height: fully closed at 0.5.
    float closed = 0.0f;
    if (ms < SCREEN_EFFECTS_SHUTTER_CLOSE_MS)
    {
        closed = static_cast<float>(ms) / static_cast<float>(SCREEN_EFFECTS_SHUTTER_CLOSE_MS);
    }
    else
    {
        closed = 1.0f - ease_out(ms - SCREEN_EFFECTS_SHUTTER_CLOSE_MS, SCREEN_EFFECTS_SHUTTER_OPEN_MS);
    }
    params->bars = max_of(params->bars, 0.5f * closed);
    return true;
}

void screen_effects_shutter(ScreenEffects* effects, uint64_t now_ms)
{
    if (!effects->shutter_on)
    {
        return;
    }
    effects->shutter = true;
    effects->shutter_start_ms = now_ms;
}

void screen_effects_test_snap(ScreenEffects* effects, uint64_t now_ms)
{
    effects->snap = true;
    effects->snap_start_ms = now_ms;
}

void screen_effects_test_shutter(ScreenEffects* effects, uint64_t now_ms)
{
    effects->shutter = true;
    effects->shutter_start_ms = now_ms;
}

void screen_effects_update(ScreenEffects* effects, uint64_t played_from_ms, uint64_t now_ms, bool enabled,
                           ScreenEffectParams* params)
{
    *params = ScreenEffectParams{};
    if (played_from_ms != effects->played_from_seen)
    {
        effects->played_from_seen = played_from_ms;
        if (played_from_ms != 0 && effects->snap_on)
        {
            effects->snap = true;
            effects->snap_start_ms = now_ms;
        }
    }
    if (!enabled)
    {
        effects->snap = false;
        effects->shutter = false;
        return;
    }
    if (effects->snap)
    {
        uint64_t ms = now_ms >= effects->snap_start_ms ? now_ms - effects->snap_start_ms : 0;
        effects->snap = screen_effects_snap_at(ms, params);
    }
    if (effects->shutter)
    {
        uint64_t ms = now_ms >= effects->shutter_start_ms ? now_ms - effects->shutter_start_ms : 0;
        effects->shutter = screen_effects_shutter_at(ms, params);
    }
}
