#pragma once

// Input, once per loop: the hotkeys, gamepads driving the UI, what the
// controllers say to the core session, and SDL's events.
// (app_input.cpp; each function's comment is at its definition.)

#include "app.h"

// The controllers, once per loop
bool gamepad_on_a_port(const App* app);
const InputLayout* find_layout_by_name(const InputLayouts* layouts, const char* name);
void app_update_input(App* app);

// Events
void handle_links(App* app);
void update_language(App* app);
void refresh_audio_devices(App* app);
void app_handle_events(App* app);
