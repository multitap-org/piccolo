#pragma once

// The files piccolo reads at start and keeps up to date: the last used core
// and folder, the settings (and the guard for the ones that could not be
// read), each core's learned latency, the running marker behind SAFE START,
// and the frame delays found per game.
// (app_files.cpp; each function's comment is at its definition.)

#include "app.h"

// Last used core and folder (<settings>/last_used.cfg)
void save_last_used(const App* app);
void load_last_used(App* app);

// Settings: folders, saving, the files that could not be read
std::string folder_setting(const App* app, const char* key, const std::string& default_folder);
void apply_folders(App* app);
void save_input_config(App* app);
void build_settings_error_files(App* app);
void save_settings(App* app);
void update_settings_context(App* app);
void save_core_options(App* app);
void save_after_options_windows(App* app);

// AUTO latency, learned per core
void apply_learned_latency(App* app);
void save_learned_latency(App* app);

// The running marker (SAFE START)
void write_running_marker(App* app);
void remove_running_marker(App* app);
void check_running_marker(App* app);
bool needs_safe_start(App* app, const FilePickerChoice* choice);

// Frame delays found per game (frame_delay.cfg)
std::string frame_delay_section(const App* app);
void save_frame_delay_file(App* app);
