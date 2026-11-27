#pragma once

// The glue of SETTINGS, CORE OPTIONS and CORES: what the windows show,
// from the App, and what their choices change.
// (app_settings.cpp; each function's comment is at its definition.)

#include "app.h"

// Choosing a folder
void choose_folder_done(App* app, const FilePickerChoice* picked);

// SETTINGS
void settings_view(App* app, UiView* view, const AppUiFrame* frame);
void settings_apply(App* app, const AppUiFrame* frame, SettingsWindowAction* action);

// CORE OPTIONS and CORES
void core_options_state(App* app, CoreOptionsUiState* state);
void open_cores_window(App* app);
