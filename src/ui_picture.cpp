#include "ui_picture.h"

#include <cstring>

#include <SDL3/SDL.h>

#include "app_shell.h"

bool ui_picture_load_png(UiPicture* picture, const char* path, uint32_t capacity, const char* what)
{
    SDL_Surface* png = SDL_LoadPNG(path);
    SDL_Surface* converted = png != nullptr ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_ARGB8888) : nullptr;
    bool ok = converted != nullptr && converted->w > 0 && converted->h > 0 &&
              static_cast<uint32_t>(converted->w) <= capacity && static_cast<uint32_t>(converted->h) <= capacity;
    if (ok)
    {
        picture->width = static_cast<uint32_t>(converted->w);
        picture->height = static_cast<uint32_t>(converted->h);
        picture->pixels.resize(static_cast<size_t>(picture->width) * picture->height);
        for (uint32_t y = 0; y < picture->height; y++)
        {
            const uint8_t* row = static_cast<const uint8_t*>(converted->pixels) +
                                 static_cast<size_t>(y) * static_cast<size_t>(converted->pitch);
            std::memcpy(&picture->pixels[static_cast<size_t>(y) * picture->width], row,
                        static_cast<size_t>(picture->width) * 4);
        }
        ui_picture_changed(picture);
    }
    else
    {
        picture->width = 0;
        picture->height = 0;
        picture->shown = false;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: could not read %s: %s", what, path, SDL_GetError());
    }
    SDL_DestroySurface(converted);
    SDL_DestroySurface(png);
    return ok;
}

uint32_t ui_picture_whole_scale(uint32_t width, uint32_t height, float box_width, float box_height,
                                uint32_t capacity)
{
    if (width == 0 || height == 0 || box_width <= 0.0f || box_height <= 0.0f)
    {
        return 1;
    }
    uint32_t scale_x = static_cast<uint32_t>(box_width) / width;
    uint32_t scale_y = static_cast<uint32_t>(box_height) / height;
    uint32_t scale = scale_x < scale_y ? scale_x : scale_y;
    scale = scale >= 1 ? scale : 1;
    while (scale > 1 && (width * scale > capacity || height * scale > capacity))
    {
        scale--;
    }
    return scale;
}

void ui_picture_scale_from(UiPicture* picture, const UiPicture* source, uint32_t scale)
{
    scale = scale >= 1 ? scale : 1;
    picture->width = source->width * scale;
    picture->height = source->height * scale;
    picture->pixels.resize(static_cast<size_t>(picture->width) * picture->height);
    for (uint32_t y = 0; y < picture->height; y++)
    {
        const uint32_t* from = &source->pixels[static_cast<size_t>(y / scale) * source->width];
        uint32_t* row = &picture->pixels[static_cast<size_t>(y) * picture->width];
        for (uint32_t x = 0; x < picture->width; x++)
        {
            row[x] = from[x / scale];
        }
    }
    ui_picture_changed(picture);
}

void ui_picture_changed(UiPicture* picture)
{
    const VideoTexture* texture = &picture->texture;
    picture->upload_pending = true;
    picture->shown = false;
    picture->replace_wait = video_texture_created(texture) &&
                            (texture->capacity_width != picture->width || texture->capacity_height != picture->height);
}

void ui_picture_clear(UiPicture* picture)
{
    picture->modified = 0;
    picture->width = 0;
    picture->height = 0;
    picture->shown = false;
    picture->upload_pending = false;
}

// The texture (the renderer's, outside the toolkit) made the picture's size:
// bilinear sampling would blend in what lies past a smaller upload's edge.
void ui_picture_prepare(UiPicture* picture, AppShell* shell)
{
    if (!picture->upload_pending || picture->replace_wait)
    {
        return;
    }
    VideoTexture* texture = &picture->texture;
    if (video_texture_created(texture) &&
        (texture->capacity_width != picture->width || texture->capacity_height != picture->height))
    {
        video_texture_destroy(texture, shell);   // waits for the GPU; nothing drew it this frame (hidden)
    }
    if (!video_texture_created(texture) &&
        !video_texture_create(texture, shell, picture->width, picture->height, FramePixelFormat::xrgb8888, false))
    {
        picture->upload_pending = false;
    }
}

void ui_picture_upload(UiPicture* picture, AppShell* shell)
{
    if (picture->replace_wait)
    {
        picture->replace_wait = false;   // this frame drew the old texture: made again next frame
        return;
    }
    if (!picture->upload_pending)
    {
        return;
    }
    if (video_texture_upload(&picture->texture, shell, picture->pixels.data(), picture->width, picture->height))
    {
        picture->shown = true;
    }
    picture->upload_pending = false;
}

void ui_picture_destroy(UiPicture* picture, AppShell* shell)
{
    video_texture_destroy(&picture->texture, shell);   // waits for the GPU
}
