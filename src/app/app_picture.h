#pragma once

// The game's picture and its timing: the window, the viewport, the frame
// delay, and what is drawn behind the windows (background, HUD, snow).
// (app_picture.cpp; each function's comment is at its definition.)

#include "app.h"

// The window and the picture
void toggle_fullscreen(App* app);
void update_window(App* app);
void prepare_game_picture(App* app, UiView* view);

// Frame delay
void search_frame_delay_again(App* app);
bool stream_delay_overrides(const App* app);
void frame_delay(App* app);

// Behind the windows: background, HUD, snow
uint32_t background_over_black(const RetroUiPalette* palette);
uint32_t menu_backdrop(const App* app);
void hud_fill(App* app, UiView* view);
void snow_fill(App* app, UiView* view);
