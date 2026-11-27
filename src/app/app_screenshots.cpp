#include "app_screenshots.h"

#include "app_files.h"
#include "app_game.h"

// ---------------------------------------------------------------------------
// Screenshots (GAME > SCREENSHOT, the screenshot hotkey; SETTINGS > GENERAL:
// SCREENSHOTS mode and folder). RAW copies the core's frame now; GPU asks
// video_output for the picture as shown, which comes back a frame or two
// later (collect_screenshots). PNGs are written on screenshot_writer's thread.
// Memory is only taken when one is taken, never per frame.
// ---------------------------------------------------------------------------

// "<folder>/<game> 2026-11-22 15-04-05<suffix>.png", with " (2)"... when that
// file is already there (two in one second).
static std::string screenshot_path(const App* app, const std::string& folder, const char* suffix)
{
    SDL_Time now = 0;
    SDL_DateTime date = {};
    char stamp[32] = "";
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &date, true))
    {
        std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d-%02d-%02d", date.year, date.month, date.day,
                      date.hour, date.minute, date.second);
    }
    const char* name = app->game_name;
    if (name[0] == '\0')
    {
        name = app->core_name[0] != '\0' ? app->core_name : "piccolo";   // a core without content
    }
    std::string base = folder + "/" + name + " " + stamp + suffix;
    std::string path = base + ".png";
    for (uint32_t n = 2; n < 100 && host_fs_exists(path.c_str()); n++)
    {
        path = base + " (" + std::to_string(n) + ").png";
    }
    return path;
}

// The core's frame as the host keeps it (its own pixel format), copied.
static bool raw_screenshot(App* app, const std::string& path)
{
    const LibretroVideoFrame* video = &app->session.host->video;
    if (video->pixels == nullptr || video->width == 0 || video->height == 0)
    {
        return false;
    }
    ScreenshotImage image = {};
    image.width = video->width;
    image.height = video->height;
    image.pitch = video->width * video->bytes_per_pixel;
    image.format = ScreenshotPixels::xrgb8888;
    if (video->format == RETRO_PIXEL_FORMAT_RGB565)
    {
        image.format = ScreenshotPixels::rgb565;
    }
    else if (video->format == RETRO_PIXEL_FORMAT_0RGB1555)
    {
        image.format = ScreenshotPixels::xrgb1555;
    }
    size_t size = static_cast<size_t>(image.pitch) * image.height;
    image.pixels = new uint8_t[size];
    std::memcpy(image.pixels, video->pixels, size);
    image.path = path;
    return screenshot_writer_submit(&app->screenshot_writer, &image);
}

void take_screenshot(App* app)
{
    if (!game_running(app) || app->session.host == nullptr)
    {
        return;
    }
    if (app->screenshot_gpu_pending || screenshot_writer_busy(&app->screenshot_writer))
    {
        set_status(app, piccolo_text(&app->strings, Str::status_screenshot_busy));
        return;
    }
    std::string folder = folder_setting(app, PICCOLO_PATH_SCREENSHOTS, app->paths.screenshots);
    if ((app->screenshot_writer.thread == nullptr && !screenshot_writer_create(&app->screenshot_writer)) ||
        folder.empty() || !host_fs_make_directories(folder.c_str()))
    {
        set_status(app, piccolo_text(&app->strings, Str::status_screenshot_failed));
        return;
    }
    const char* mode = settings_get(&app->settings, PICCOLO_SCREENSHOT_MODE, nullptr);
    bool both = std::strcmp(mode, "both") == 0;
    bool raw = both || std::strcmp(mode, "raw") == 0;
    bool gpu = both || !raw;
    std::string gpu_path = screenshot_path(app, folder, both ? " gpu" : "");
    if (gpu && !video_output_request_capture(&app->video))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Screenshot: no GPU capture here; saving the RAW frame instead");
        gpu = false;
        raw = true;
        both = false;
    }
    bool ok = true;
    if (raw)
    {
        ok = raw_screenshot(app, screenshot_path(app, folder, both ? " raw" : ""));
    }
    if (gpu)
    {
        app->screenshot_gpu_pending = true;
        app->screenshot_gpu_path = gpu_path;
    }
    if (!ok && !gpu)
    {
        set_status(app, piccolo_text(&app->strings, Str::status_screenshot_failed));
        return;
    }
    screen_effects_shutter(&app->screen_effects, SDL_GetTicks());
}

// Every frame, after the GPU work is recorded: a GPU capture that came back
// goes to the writer; finished files go to the status line.
void collect_screenshots(App* app)
{
    if (!game_running(app))
    {
        app->screenshot_gpu_pending = false;   // the game closed first: its capture is dropped when it comes
    }
    VideoCapture capture = {};
    if (video_output_take_capture(&app->video, &capture))
    {
        if (app->screenshot_gpu_pending)
        {
            ScreenshotImage image = {};
            image.pixels = capture.pixels;   // handed over: the writer frees it
            image.width = capture.width;
            image.height = capture.height;
            image.pitch = capture.pitch;
            image.format = capture.format == VideoCaptureFormat::bgra8 ? ScreenshotPixels::bgra8 : ScreenshotPixels::rgba8;
            image.path = app->screenshot_gpu_path;
            capture = VideoCapture{};
            screenshot_writer_submit(&app->screenshot_writer, &image);
            app->screenshot_gpu_pending = false;
        }
        video_capture_free(&capture);
    }
    ScreenshotResult result = {};
    while (screenshot_writer_poll(&app->screenshot_writer, &result))
    {
        if (!result.ok)
        {
            set_status(app, piccolo_text(&app->strings, Str::status_screenshot_failed));
            continue;
        }
        set_status(app, piccolo_text(&app->strings, Str::status_screenshot_saved));
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Screenshot: %s", result.path.c_str());
    }
}
