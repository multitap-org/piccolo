#include "image_window.h"

#include <cstdio>
#include <cstdlib>

#include "piccolo_settings.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

// Shading over the cut-off parts, and the outline of what stays (0xAARRGGBB).
constexpr uint32_t CROP_SHADE   = 0xC0000000u;
constexpr uint32_t CROP_OUTLINE = 0xFFF0C040u;   // amber

static const char* const CROP_KEYS[] = {PICCOLO_VIDEO_CROP_TOP, PICCOLO_VIDEO_CROP_BOTTOM, PICCOLO_VIDEO_CROP_LEFT,
                                        PICCOLO_VIDEO_CROP_RIGHT};
constexpr int32_t CROP_KEY_COUNT = 4;

// The crop as it resolves at the scope being edited (what this window edits).
static float crop_at_scope(const SettingsUiContext* context, const char* key)
{
    const char* value = settings_get_at(context->settings, context->scope, key, nullptr);
    return value != nullptr ? static_cast<float>(std::strtol(value, nullptr, 10)) : 0.0f;
}

// The frame at `factor`, with the crop shaded and outlined, at the cursor.
static void draw_preview(const SettingsUiContext* context, const ImageWindowSource* source, float factor, float block)
{
    float width = static_cast<float>(source->frame_width) * factor;
    float height = static_cast<float>(source->frame_height) * factor;
    retro_ui_canvas(width, height);

    // retro_ui_begin_window() switched this window to nearest sampling: sharp pixels.
    retro_ui_canvas_image(source->texture_id, 0.0f, 0.0f, width, height, source->uv_max_x, source->uv_max_y,
                          0xFFFFFFFFu);

    float top = crop_at_scope(context, PICCOLO_VIDEO_CROP_TOP) * factor;
    float bottom = crop_at_scope(context, PICCOLO_VIDEO_CROP_BOTTOM) * factor;
    float left = crop_at_scope(context, PICCOLO_VIDEO_CROP_LEFT) * factor;
    float right = crop_at_scope(context, PICCOLO_VIDEO_CROP_RIGHT) * factor;
    // Same rule as the viewport: a crop that leaves nothing on an axis is ignored there.
    if (top + bottom >= height)
    {
        top = 0.0f;
        bottom = 0.0f;
    }
    if (left + right >= width)
    {
        left = 0.0f;
        right = 0.0f;
    }
    // The kept part, in the canvas.
    float keep_x = left;
    float keep_y = top;
    float keep_width = width - right - left;
    float keep_height = height - bottom - top;

    // Pass 0's result is the part the game shows. When it matches the kept
    // part (it may not: editing a scope whose crop isn't the one in use),
    // it's drawn there, so the picture controls show on the preview.
    if (source->processed_texture_id != 0 && static_cast<float>(source->processed_width) * factor == keep_width &&
        static_cast<float>(source->processed_height) * factor == keep_height)
    {
        retro_ui_canvas_image(source->processed_texture_id, keep_x, keep_y, keep_width, keep_height, 1.0f, 1.0f,
                              0xFFFFFFFFu);
    }
    retro_ui_canvas_rect(0.0f, 0.0f, width, keep_y, CROP_SHADE);
    retro_ui_canvas_rect(0.0f, keep_y + keep_height, width, height - keep_y - keep_height, CROP_SHADE);
    retro_ui_canvas_rect(0.0f, keep_y, keep_x, keep_height, CROP_SHADE);
    retro_ui_canvas_rect(keep_x + keep_width, keep_y, width - keep_x - keep_width, keep_height, CROP_SHADE);
    retro_ui_canvas_outline(keep_x, keep_y, keep_width, keep_height, CROP_OUTLINE, block);
}

// The image controls, in the column right of the picture. Each group is a
// collapsible section; more join them later.
static void draw_controls(const SettingsUiContext* context, const ImageWindowSource* source)
{
    if (retro_ui_section_header("crop", piccolo_text(context->strings, Str::image_section_crop), true))
    {
        for (int32_t i = 0; i < CROP_KEY_COUNT; i++)
        {
            settings_ui_row(context, CROP_KEYS[i], nullptr);
        }
    }

    if (retro_ui_section_header("profile", piccolo_text(context->strings, Str::image_section_profile), true))
    {
        if (!source->controls_available)
        {
            // Without librashader there's no pass 0: the values (here and in
            // PICTURE CONTROLS) are kept, not used.
            retro_ui_dim_text(piccolo_text(context->strings, Str::image_controls_unavailable), nullptr);
        }
        settings_ui_row(context, PICCOLO_IMAGE_COLOR_PROFILE, nullptr);
        settings_ui_row(context, PICCOLO_IMAGE_PROFILE_STRENGTH, nullptr);
    }

    if (retro_ui_section_header("controls", piccolo_text(context->strings, Str::image_section_controls), true))
    {
        for (uint32_t i = 0; i < PICCOLO_PASS0_CONTROL_COUNT; i++)
        {
            settings_ui_row(context, PICCOLO_PASS0_CONTROLS[i].key, nullptr);
        }
    }
}

void image_window_draw(bool* open, const SettingsUiContext* context, const ImageWindowSource* source,
                       ImFont* pixel_font, int32_t scale)
{
    if (!*open)
    {
        return;
    }
    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    retro_ui_push_theme(pixel_font, scale);

    // Layout: the picture on the left, the controls' column on the right.
    // The picture gets the biggest whole factor that fits next to the column
    // in piccolo's window.
    float controls_width = 190.0f * block;
    // Both sections' rows and headers (and the note); taller than the window
    // allows, the column scrolls.
    float controls_height = static_cast<float>(CROP_KEY_COUNT + static_cast<int32_t>(PICCOLO_PASS0_CONTROL_COUNT) + 6) *
                            retro_ui_line_step();
    float chrome_x = 0.0f;
    float chrome_y = 0.0f;
    retro_ui_window_chrome(&chrome_x, &chrome_y);   // the padding and the title bar
    chrome_x += retro_ui_item_spacing();              // between the picture and the column
    chrome_y += retro_ui_text_line_step();            // "EDITING: ..."
    float max_width = screen_width - 8.0f * block;
    float max_height = screen_height - 16.0f * block;
    bool has_picture = source->texture_id != 0 && source->frame_width > 0 && source->frame_height > 0;
    float factor = 1.0f;
    if (has_picture)
    {
        float fit_x = (max_width - chrome_x - controls_width) / static_cast<float>(source->frame_width);
        float fit_y = (max_height - chrome_y) / static_cast<float>(source->frame_height);
        float fit = fit_x < fit_y ? fit_x : fit_y;
        factor = fit >= 1.0f ? static_cast<float>(static_cast<int32_t>(fit)) : 1.0f;
    }
    float picture_width = has_picture ? static_cast<float>(source->frame_width) * factor : 160.0f * block;
    float picture_height = has_picture ? static_cast<float>(source->frame_height) * factor : controls_height;
    float body_height = picture_height > controls_height ? picture_height : controls_height;
    float width = picture_width + controls_width + chrome_x;
    float height = body_height + chrome_y;
    RetroUiWindow window = {};
    window.id = "image_settings";
    window.title = piccolo_text(context->strings, Str::image_title);
    window.width = width < max_width ? width : max_width;
    window.height = height < max_height ? height : max_height;
    window.centered_on_open = true;
    bool closed = false;
    if (retro_ui_window_begin(&window, scale, &closed))
    {
        if (closed)
        {
            *open = false;
        }
        char editing[200] = {};
        std::snprintf(editing, sizeof(editing), "%s: %s", piccolo_text(context->strings, Str::image_editing),
                      settings_ui_scope_name(context->strings, context->scope));
        retro_ui_dim_text(editing, nullptr);

        if (has_picture)
        {
            draw_preview(context, source, factor, block);
        }
        else
        {
            // The note, in a block as wide as the picture would be: the column stays beside it.
            retro_ui_begin_group();
            retro_ui_dim_text(piccolo_text(context->strings, Str::image_no_picture), nullptr);
            retro_ui_canvas(picture_width, 0.0f);
            retro_ui_end_group();
        }

        // A scroll area of its own, so the rows (and their RESET buttons) fit the column.
        retro_ui_same_line();
        if (retro_ui_begin_scroll_area("controls", 0.0f, body_height))
        {
            draw_controls(context, source);
        }
        retro_ui_end_scroll_area();
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}
