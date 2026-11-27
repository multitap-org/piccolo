#pragma once

// Savestates: the undo of a load and of a save, the AUTO state saved when
// a game closes, and the slots' actions.
// (app_states.cpp; each function's comment is at its definition.)

#include "app.h"

void drop_undos(App* app);
bool load_state_keeping_undo(App* app);
bool undo_load_state(App* app);
uint32_t undo_save_slot(const App* app);
bool undo_save_state(App* app, uint32_t slot);
void report_undo_save(App* app, bool undone, uint32_t slot, bool announce);
void report_undo_load(App* app, bool undone, bool announce);

void save_state_on_exit(App* app);
void resume_state_on_start(App* app);

// The slots (GAME menu, QUICK MENU, hotkeys)
bool save_state_with_thumbnail(App* app);
bool quick_save(App* app);
bool quick_load(App* app);
void save_state_from(App* app, bool announce);
void load_state_from(App* app, bool announce);
void undo_save_from(App* app, bool announce);
void undo_load_from(App* app, bool announce);
