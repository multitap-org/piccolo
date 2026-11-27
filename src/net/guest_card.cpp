#include "guest_card.h"

#include <cstdio>
#include <cstring>

#include "host_log.h"
#if defined(PICCOLO_GUEST_CARD)
#include "net_http.h"
#endif

static SDL_PixelFormat sdl_format(GuestCardPixels format)
{
    switch (format)
    {
        case GuestCardPixels::rgb565:
            return SDL_PIXELFORMAT_RGB565;
        case GuestCardPixels::xrgb1555:
            return SDL_PIXELFORMAT_XRGB1555;
        default:
            return SDL_PIXELFORMAT_XRGB8888;
    }
}

// `surface` scaled by `factor` (nearest), saved as PNG into `png`.
static bool encode(SDL_Surface* surface, uint32_t factor, std::vector<uint8_t>* png)
{
    int32_t width = surface->w * static_cast<int32_t>(factor);
    int32_t height = surface->h * static_cast<int32_t>(factor);
    SDL_Surface* scaled = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGB24);
    if (scaled == nullptr)
    {
        return false;
    }
    bool ok = SDL_BlitSurfaceScaled(surface, nullptr, scaled, nullptr, SDL_SCALEMODE_NEAREST);
    SDL_IOStream* stream = ok ? SDL_IOFromDynamicMem() : nullptr;
    if (stream != nullptr && SDL_SavePNG_IO(scaled, stream, false))
    {
        Sint64 size = SDL_TellIO(stream);
        const uint8_t* data = static_cast<const uint8_t*>(
            SDL_GetPointerProperty(SDL_GetIOProperties(stream), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
        ok = data != nullptr && size > 0;
        if (ok)
        {
            png->assign(data, data + size);
        }
    }
    else
    {
        ok = false;
    }
    if (stream != nullptr)
    {
        SDL_CloseIO(stream);
    }
    SDL_DestroySurface(scaled);
    return ok;
}

bool guest_card_png(const void* pixels, uint32_t width, uint32_t height, uint32_t pitch, GuestCardPixels format,
                    std::vector<uint8_t>* png)
{
    png->clear();
    if (pixels == nullptr || width == 0 || height == 0 || width > NET_GUEST_CARD_MAX_WIDTH ||
        height > NET_GUEST_CARD_MAX_HEIGHT)
    {
        return false;
    }
    SDL_Surface* frame = SDL_CreateSurfaceFrom(static_cast<int32_t>(width), static_cast<int32_t>(height),
                                               sdl_format(format), const_cast<void*>(pixels),
                                               static_cast<int32_t>(pitch));
    if (frame == nullptr)
    {
        return false;
    }
    // The biggest whole factor that fits; smaller ones if the PNG is too big.
    uint32_t factor = NET_GUEST_CARD_MAX_WIDTH / width;
    if (NET_GUEST_CARD_MAX_HEIGHT / height < factor)
    {
        factor = NET_GUEST_CARD_MAX_HEIGHT / height;
    }
    bool ok = false;
    for (; factor >= 1 && !ok; factor--)
    {
        ok = encode(frame, factor, png) && png->size() <= NET_GUEST_CARD_MAX_PNG;
    }
    SDL_DestroySurface(frame);
    if (!ok)
    {
        png->clear();
    }
    return ok;
}

bool guest_card_frame_copy(GuestCardFrame* frame, const void* pixels, uint32_t width, uint32_t height, uint32_t pitch,
                           GuestCardPixels format)
{
    frame->pixels.clear();
    frame->width = 0;
    frame->height = 0;
    frame->pitch = 0;
    if (pixels == nullptr || width == 0 || height == 0 || width > NET_GUEST_CARD_MAX_WIDTH ||
        height > NET_GUEST_CARD_MAX_HEIGHT)
    {
        return false;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(pixels);
    frame->pixels.assign(bytes, bytes + static_cast<size_t>(pitch) * height);
    frame->width = width;
    frame->height = height;
    frame->pitch = pitch;
    frame->format = format;
    return true;
}

std::string guest_card_url(const char* beacon, const NetGuestLink* link)
{
    return std::string("https://") + beacon + "/join/" + link->room + "/card";
}

bool guest_card_frame_xrgb(const void* pixels, uint32_t width, uint32_t height, uint32_t pitch, GuestCardPixels format,
                           std::vector<uint32_t>* out)
{
    out->clear();
    if (pixels == nullptr || width == 0 || height == 0 || width > NET_GUEST_CARD_MAX_WIDTH ||
        height > NET_GUEST_CARD_MAX_HEIGHT)
    {
        return false;
    }
    out->resize(static_cast<size_t>(width) * height);
    if (!SDL_ConvertPixels(static_cast<int32_t>(width), static_cast<int32_t>(height), sdl_format(format), pixels,
                           static_cast<int32_t>(pitch), SDL_PIXELFORMAT_XRGB8888, out->data(),
                           static_cast<int32_t>(width * 4)))
    {
        out->clear();
        return false;
    }
    return true;
}

static int upload_thread(void* user)
{
    GuestCardUpload* upload = static_cast<GuestCardUpload*>(user);
    bool sent = false;
    // The scaling and the PNG, here and not on the main thread under the
    // network lock. A frame that can't be made into a PNG goes without one.
    std::vector<uint8_t> png;
    if (!upload->frame.pixels.empty() &&
        !guest_card_png(upload->frame.pixels.data(), upload->frame.width, upload->frame.height, upload->frame.pitch,
                        upload->frame.format, &png))
    {
        host_log(LogLevel::warning, "Guest link: no picture for the card");
    }
    NetGuestLink link;   // only the card key is used by the body
    std::memcpy(link.card_key, upload->card_key, sizeof(link.card_key));
    if (!net_guest_card_body(&link, upload->title, png.data(), png.size(), &upload->body))
    {
        std::snprintf(upload->error, sizeof(upload->error), "the card's body is out of bounds");
        host_log(LogLevel::warning, "Guest link: the card wasn't sent: %s", upload->error);
        SDL_SetAtomicInt(&upload->state, static_cast<int>(GuestCardState::failed));
        return 0;
    }
#if defined(PICCOLO_GUEST_CARD)
    for (uint32_t attempt = 0; attempt < GUEST_CARD_TRIES && !sent; attempt++)
    {
        if (attempt > 0)
        {
            SDL_Delay(1000);
        }
        upload->status = 0;
        bool answered = net_http_post(upload->url.c_str(), "application/octet-stream", upload->body.data(),
                                      upload->body.size(), &upload->status, upload->error, sizeof(upload->error));
        sent = answered && upload->status == 204;
        if (answered && !sent)
        {
            std::snprintf(upload->error, sizeof(upload->error), "the beacon answered HTTP %u",
                          static_cast<unsigned>(upload->status));
        }
        // Only "the room isn't there yet" (404) and no answer are worth a new try.
        if (answered && upload->status != 404)
        {
            break;
        }
    }
#else
    std::snprintf(upload->error, sizeof(upload->error), "no HTTP in this build");
#endif
    if (!sent)
    {
        host_log(LogLevel::warning, "Guest link: the card wasn't sent: %s", upload->error);
    }
    SDL_SetAtomicInt(&upload->state, static_cast<int>(sent ? GuestCardState::sent : GuestCardState::failed));
    return 0;
}

void guest_card_finish(GuestCardUpload* upload)
{
    if (upload->thread != nullptr)
    {
        SDL_WaitThread(upload->thread, nullptr);
        upload->thread = nullptr;
    }
}

bool guest_card_send(GuestCardUpload* upload, const char* beacon, const NetGuestLink* link, const char* title,
                     GuestCardFrame* frame)
{
    guest_card_finish(upload);
    upload->status = 0;
    upload->error[0] = '\0';
    upload->url = guest_card_url(beacon, link);
    size_t title_size = std::strlen(title);
    if (title_size == 0 || title_size > NET_GUEST_MAX_TITLE)
    {
        SDL_SetAtomicInt(&upload->state, static_cast<int>(GuestCardState::failed));
        return false;
    }
    std::memcpy(upload->title, title, title_size + 1);
    std::memcpy(upload->card_key, link->card_key, sizeof(upload->card_key));
    // The hand-off: the thread (not started yet, the last one joined above)
    // gets the frame's buffer, the caller gets the old one back to reuse. From
    // SDL_CreateThread until the state leaves `sending` only the thread
    // touches `upload->frame`.
    upload->frame.pixels.swap(frame->pixels);
    upload->frame.width = frame->width;
    upload->frame.height = frame->height;
    upload->frame.pitch = frame->pitch;
    upload->frame.format = frame->format;
    frame->pixels.clear();
    frame->width = 0;
    frame->height = 0;
    frame->pitch = 0;
    SDL_SetAtomicInt(&upload->state, static_cast<int>(GuestCardState::sending));
    upload->thread = SDL_CreateThread(upload_thread, "guest card", upload);
    if (upload->thread == nullptr)
    {
        SDL_SetAtomicInt(&upload->state, static_cast<int>(GuestCardState::failed));
        return false;
    }
    return true;
}

GuestCardState guest_card_state(GuestCardUpload* upload)
{
    return static_cast<GuestCardState>(SDL_GetAtomicInt(&upload->state));
}
