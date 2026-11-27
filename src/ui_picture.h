#pragma once

// A picture the UI draws from the CPU: a savestate's thumbnail (QUICK MENU),
// the CONTINUE card's picture. Pixels are B8G8R8A8 (opaque), loaded from a
// PNG or made here, then uploaded into a texture of their own size later in
// the frame (outside rendering).
//
// Sharp bilinear (pixels sharp, soft only where one falls between two screen
// pixels): the picture scaled up by a whole number with nearest
// (ui_picture_scale_from), then drawn bilinear to its size on screen.
//
// A texture of another size is made again, but not in the frame that may
// still draw the old one: the picture is hidden first (`shown` false) and its
// new texture made a frame later.

#include <cstdint>
#include <vector>

#include "video_texture.h"

struct AppShell;

struct UiPicture
{
    VideoTexture          texture;   // texture.ui_texture: what the toolkit draws; made the picture's size
    std::vector<uint32_t> pixels;
    uint32_t              width = 0;
    uint32_t              height = 0;
    bool                  upload_pending = false;
    bool                  shown = false;      // the texture holds `pixels`
    int64_t               modified = 0;       // what the pixels are for (a file's date; 0: none)
    bool                  replace_wait = false;   // its texture is made again (another size) next frame
};

// A PNG into `picture`'s pixels (no bigger than `capacity` either way), to be
// uploaded. False (logged, `what` names it) when it can't be read: the
// picture is then empty and hidden.
bool ui_picture_load_png(UiPicture* picture, const char* path, uint32_t capacity, const char* what);

// The biggest whole number that scales a `width` x `height` picture into
// `box_width` x `box_height` (at least 1), and never past `capacity` either
// way: the nearest prescale of sharp bilinear.
uint32_t ui_picture_whole_scale(uint32_t width, uint32_t height, float box_width, float box_height,
                                uint32_t capacity);

// `picture` = `source` scaled up `scale` times with nearest, to be uploaded.
void ui_picture_scale_from(UiPicture* picture, const UiPicture* source, uint32_t scale);

// New pixels in `picture`: uploaded later in the frame (ui_picture_upload).
void ui_picture_changed(UiPicture* picture);
// The picture hidden and emptied (nothing to show).
void ui_picture_clear(UiPicture* picture);

// Uploading, in two steps around app_shell_acquire: prepare makes the
// texture (before), upload copies the pixels (after, outside rendering).
void ui_picture_prepare(UiPicture* picture, AppShell* shell);
void ui_picture_upload(UiPicture* picture, AppShell* shell);

// Frees the texture (waits for the GPU).
void ui_picture_destroy(UiPicture* picture, AppShell* shell);
