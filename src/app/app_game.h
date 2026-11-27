#pragma once

// A game's life: starting it (from a file, a menu, another device), its
// pause, and the actions on it while it runs.
// (app_game.cpp; each function's comment is at its definition.)

#include "app.h"

// What runs, and telling the player
bool game_running(const App* app);
bool text_input_pending(const App* app);
void set_status(App* app, const char* text);
void report_session(App* app, bool ok, bool announce);

// Pause
void toggle_user_pause(App* app);
void update_pause(App* app);

// A game ends
void app_game_ending(App* app);
void app_game_ended(App* app);

// Starting a game
bool start_game(App* app, const FilePickerChoice* choice);
void app_open_file(void* data, const char* path);

bool soft_reset(App* app);
bool hard_reset(App* app);
void close_game(App* app);
void open_game_dvr(App* app);
