#pragma once

// The menu bar: its data (the menus in the current language, RECENT) and
// what picking an item does.
// (app_menus.cpp; each function's comment is at its definition.)

#include "app.h"

// The menu bar's data and RECENT
void build_menus(App* app);
void refresh_recent_labels(App* app);
void remember_recent(App* app, const char* core_path, const char* content_path);

// What a pick does
void handle_menu_action(App* app, MenuAction action);
void apply_menu_pick(App* app, int32_t picked);
