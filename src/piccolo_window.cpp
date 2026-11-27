#include "piccolo_window.h"

#include <cmath>

#include <SDL3/SDL.h>

#include "piccolo_settings.h"
#include "window_size.h"

// The room a window can have on its display, in pixels: the usable area
// (without task bars / panels) minus the window's own frame. `density` =
// pixels per window coordinate (HiDPI).
static bool usable_area(SDL_Window* window, uint32_t* width, uint32_t* height, float* density)
{
    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (display == 0)
    {
        display = SDL_GetPrimaryDisplay();
    }
    SDL_Rect bounds = {};
    if (!SDL_GetDisplayUsableBounds(display, &bounds))
    {
        return false;
    }
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
    SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right);   // stays 0 where unknown
    *density = SDL_GetWindowPixelDensity(window);
    if (*density <= 0.0f)
    {
        *density = 1.0f;
    }
    int room_width = bounds.w - left - right;
    int room_height = bounds.h - top - bottom;
    if (room_width <= 0 || room_height <= 0)
    {
        return false;
    }
    *width = static_cast<uint32_t>(static_cast<float>(room_width) * *density);
    *height = static_cast<uint32_t>(static_cast<float>(room_height) * *density);
    return true;
}

// Sets the window's size from pixels.
static void set_window_pixels(SDL_Window* window, uint32_t width, uint32_t height, float density)
{
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED)
    {
        SDL_RestoreWindow(window);   // a maximized window ignores size requests
    }
    int window_width = static_cast<int>(std::lround(static_cast<float>(width) / density));
    int window_height = static_cast<int>(std::lround(static_cast<float>(height) / density));
    SDL_SetWindowSize(window, window_width, window_height);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Window: %ux%u pixels", width, height);
}

// How much of the saved rectangle must lie on a display to put the window
// there: less and it could not be grabbed (a monitor unplugged since).
static constexpr int32_t RESTORE_MIN_VISIBLE = 64;

// The usable bounds of the display the saved rectangle belongs to, and whether
// at least RESTORE_MIN_VISIBLE pixels each way of it are inside them.
static bool saved_rect_on_display(const PiccoloWindow* state, SDL_Rect* usable)
{
    SDL_Rect saved = {state->saved_x, state->saved_y, state->saved_width, state->saved_height};
    if (saved.w < RESTORE_MIN_VISIBLE)
    {
        saved.w = RESTORE_MIN_VISIBLE;   // no size saved (not FIXED): its corner is what counts
    }
    if (saved.h < RESTORE_MIN_VISIBLE)
    {
        saved.h = RESTORE_MIN_VISIBLE;
    }
    SDL_DisplayID display = SDL_GetDisplayForRect(&saved);
    if (display == 0 || !SDL_GetDisplayUsableBounds(display, usable))
    {
        return false;
    }
    SDL_Rect common = {};
    if (!SDL_GetRectIntersection(&saved, usable, &common))
    {
        return false;
    }
    return common.w >= RESTORE_MIN_VISIBLE && common.h >= RESTORE_MIN_VISIBLE;
}

void piccolo_window_restore(PiccoloWindow* state, SDL_Window* window, const Settings* settings)
{
    if (state->phone_window || !state->has_saved || !settings_get_bool(settings, PICCOLO_WINDOW_REMEMBER))
    {
        return;   // the first piccolo_window_apply() sets the mode's size
    }
    uint32_t mode = settings_get_enum(settings, PICCOLO_WINDOW_MODE);
    // A display that is gone (or a resolution that shrank) must not leave the
    // window off screen: then the system places it and the size is clamped.
    SDL_Rect usable = {};
    bool on_display = saved_rect_on_display(state, &usable);
    if (mode == PICCOLO_WINDOW_FIXED && state->saved_width > 0 && state->saved_height > 0)
    {
        int32_t width = state->saved_width;
        int32_t height = state->saved_height;
        if (on_display)
        {
            if (width > usable.w)
            {
                width = usable.w;
            }
            if (height > usable.h)
            {
                height = usable.h;
            }
        }
        else
        {
            SDL_Rect primary = {};
            if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &primary))
            {
                width = width < primary.w ? width : primary.w;
                height = height < primary.h ? height : primary.h;
            }
        }
        SDL_SetWindowSize(window, width, height);
        // Counts as applied: the remembered size wins over the preset.
        state->fixed_aspect = piccolo_settings_window_aspect(settings);
        state->fixed_height = piccolo_settings_window_height(settings);
    }
    // Ignored where the desktop places windows itself (Wayland).
    if (on_display)
    {
        SDL_SetWindowPosition(window, state->saved_x, state->saved_y);
    }
    state->mode = mode;
    state->applied = true;
}

void piccolo_window_apply(PiccoloWindow* state, SDL_Window* window, const Settings* settings,
                          const ViewportInput* picture)
{
    if (state->phone_window)
    {
        return;   // --mobile: the phone's size, turned only by ROTATE
    }
    // A phone is always full screen, borderless (PICCOLO_MOBILE).
    bool fullscreen = PICCOLO_MOBILE || settings_get_bool(settings, PICCOLO_DISPLAY_FULLSCREEN);
    uint32_t fullscreen_type =
        PICCOLO_MOBILE ? PICCOLO_FULLSCREEN_BORDERLESS : settings_get_enum(settings, PICCOLO_DISPLAY_FULLSCREEN_TYPE);
    bool was_fullscreen = state->fullscreen;
    if (fullscreen != state->fullscreen || (fullscreen && fullscreen_type != state->fullscreen_type))
    {
        if (fullscreen)
        {
            // Exclusive: the display's own (desktop) mode; borderless: none.
            const SDL_DisplayMode* display_mode = nullptr;
            if (fullscreen_type == PICCOLO_FULLSCREEN_EXCLUSIVE)
            {
                display_mode = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window));
            }
            SDL_SetWindowFullscreenMode(window, display_mode);
            SDL_SetWindowFullscreen(window, true);
        }
        else
        {
            SDL_SetWindowFullscreen(window, false);   // SDL brings back the windowed size
        }
        state->fullscreen = fullscreen;
        state->fullscreen_type = fullscreen_type;
        const char* kind = fullscreen_type == PICCOLO_FULLSCREEN_EXCLUSIVE ? "exclusive" : "borderless";
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Fullscreen: %s", fullscreen ? kind : "off");
    }

    uint32_t room_width = 0;
    uint32_t room_height = 0;
    float density = 1.0f;
    if (!usable_area(window, &room_width, &room_height, &density))
    {
        return;
    }

    // What the mode wants (also shown in the settings window).
    uint32_t mode = settings_get_enum(settings, PICCOLO_WINDOW_MODE);
    double aspect = piccolo_settings_window_aspect(settings);
    uint32_t height = piccolo_settings_window_height(settings);
    state->target_width = 0;
    state->target_height = 0;
    state->target_scale = 0;
    if (mode == PICCOLO_WINDOW_FIXED)
    {
        window_size_fixed(aspect, height, room_width, room_height, &state->target_width, &state->target_height);
    }
    else if (picture != nullptr)
    {
        uint32_t scale = static_cast<uint32_t>(settings_get_int(settings, PICCOLO_WINDOW_SCALE));
        window_size_dynamic(picture, scale, room_width, room_height, &state->target_width, &state->target_height,
                            &state->target_scale);
    }

    // Fullscreen covers the screen; the windowed size waits until it's off
    // (or right after it was switched off: SDL restores the old size first).
    if (fullscreen || was_fullscreen)
    {
        return;
    }

    bool mode_changed = !state->applied || mode != state->mode;
    if (mode == PICCOLO_WINDOW_FIXED)
    {
        if (mode_changed || aspect != state->fixed_aspect || height != state->fixed_height)
        {
            set_window_pixels(window, state->target_width, state->target_height, density);
            state->fixed_aspect = aspect;
            state->fixed_height = height;
        }
    }
    else if (state->target_width > 0)
    {
        if (mode_changed || state->target_width != state->dynamic_width || state->target_height != state->dynamic_height)
        {
            set_window_pixels(window, state->target_width, state->target_height, density);
            state->dynamic_width = state->target_width;
            state->dynamic_height = state->target_height;
        }
    }
    state->mode = mode;
    state->applied = true;
}

void piccolo_window_capture(PiccoloWindow* state, SDL_Window* window)
{
    SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    if (state->phone_window || (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)))
    {
        return;   // keep the windowed size we had (not --mobile's phone)
    }
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    if (!SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0)
    {
        return;
    }
    SDL_GetWindowPosition(window, &x, &y);
    state->saved_width = width;
    state->saved_height = height;
    state->saved_x = x;
    state->saved_y = y;
    state->has_saved = true;
}

void piccolo_window_rotate(SDL_Window* window)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
#if defined(SDL_PLATFORM_ANDROID)
    // As piccoletto: the orientations allowed, then the activity told again
    // (SDL applies the hint when the window's resizable flag changes).
    const char* other = (width > height) ? "Portrait" : "LandscapeLeft LandscapeRight";
    SDL_SetHint(SDL_HINT_ORIENTATIONS, other);
    SDL_SetWindowResizable(window, false);
    SDL_SetWindowResizable(window, true);
#else
    SDL_SetWindowSize(window, height, width);
#endif
}
