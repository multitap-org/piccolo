#pragma once

// (app_ui_modes.cpp; each function's comment is at its definition.)

#include "app.h"

// Windows and input grabs
UiGrab grab_now(const App* app);
void cancel_grabs(App* app);

// Changing mode
void ui_follow_changes(App* app);
void app_enter_game(App* app);
void app_leave_main_menu(App* app);
void app_enter_main_menu(App* app);
bool quick_menu_up(const App* app);
void app_open_widget(App* app, UiWidget widget);
void app_open_quick_menu(App* app);
void app_open_core_config(App* app, uint32_t port, uint32_t controller);

// Once per loop: following what opened and closed
void ui_sync_windows(App* app);
void ui_follow_widgets(App* app);
void ui_follow_grabs(App* app);
UiDerived app_derive(const App* app);

// Who has the input, and BACK
bool game_has_keyboard(const App* app);
bool ui_is_up(const App* app);
void ui_do_back(App* app);
void app_back_button(App* app);
void resume_game(App* app);
