#include "fps_meter.h"

#include <cmath>
#include <cstdio>

#include "retro_ui.h"

// The chip, in the retro UI's blocks (the DVR's HUD chip has the same size).
constexpr float    MARGIN     = 4.0f;
constexpr float    CHIP       = 11.0f;
constexpr float    CHIP_PAD   = 4.0f;
constexpr uint32_t CARD_ALPHA = 0xB0000000u;

constexpr uint64_t NS_PER_SECOND = 1000000000u;

void fps_meter_reset(FpsMeter* meter)
{
    *meter = FpsMeter{};
}

void fps_meter_loop(FpsMeter* meter, uint64_t now_ns, uint32_t core_frames, uint64_t work_ns)
{
    if (meter->second_start_ns == 0 || now_ns < meter->second_start_ns)
    {
        meter->second_start_ns = now_ns;
        meter->loops = 0;
        meter->core_frames = 0;
        meter->work_ns = 0;
        return;
    }
    meter->loops++;
    meter->core_frames += core_frames;
    meter->work_ns += work_ns;
    uint64_t elapsed = now_ns - meter->second_start_ns;
    if (elapsed < NS_PER_SECOND)
    {
        return;
    }
    double seconds = static_cast<double>(elapsed) / static_cast<double>(NS_PER_SECOND);
    meter->core_fps = static_cast<double>(meter->core_frames) / seconds;
    meter->screen_hz = static_cast<double>(meter->loops) / seconds;
    meter->work_ms = meter->loops > 0 ? static_cast<double>(meter->work_ns) / static_cast<double>(meter->loops) / 1.0e6 : 0.0;
    meter->has_numbers = true;
    meter->second_start_ns = now_ns;
    meter->loops = 0;
    meter->core_frames = 0;
    meter->work_ns = 0;
}

void fps_meter_format(const FpsMeter* meter, double target_fps, char* out, uint32_t out_size)
{
    if (!meter->has_numbers)
    {
        std::snprintf(out, out_size, "FPS ...");
        return;
    }
    if (target_fps > 0.0)
    {
        std::snprintf(out, out_size, "CORE %.1f/%.1f  SCREEN %.0f  %.1f MS", meter->core_fps, target_fps, meter->screen_hz,
                      meter->work_ms);
    }
    else
    {
        std::snprintf(out, out_size, "SCREEN %.0f  %.1f MS", meter->screen_hz, meter->work_ms);
    }
}

void fps_meter_draw(const FpsMeter* meter, double target_fps, ImFont* pixel_font, int32_t scale, float display_width)
{
    char text[96];
    fps_meter_format(meter, target_fps, text, sizeof(text));
    const RetroUiPalette* palette = retro_ui_palette();
    float block = static_cast<float>(scale > 0 ? scale : 1);
    retro_ui_begin_layer("##fps_meter", pixel_font, scale, true);
    float w = std::floor(retro_ui_layer_text_width(text)) + 2.0f * CHIP_PAD * block;
    float x = display_width - MARGIN * block - w;
    retro_ui_layer_rect(x, MARGIN * block, w, CHIP * block, retro_ui_color_set_alpha(palette->panel_dark, CARD_ALPHA));
    retro_ui_layer_text(x + CHIP_PAD * block, (MARGIN + 2.0f) * block, text, retro_ui_color_set_alpha(palette->section, 0xFF000000u));
    retro_ui_end_layer();
}
