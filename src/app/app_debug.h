#pragma once

// The debug window's glue (MENU > DEBUG): what it shows and what it forces.
// (app_debug.cpp; each function's comment is at its definition.)

#include "app.h"

// The debug window
void debug_window_state(App* app, DebugWindowState* state);
void debug_window_apply(App* app, const DebugWindowAction* action);
