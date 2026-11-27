#pragma once

// (app_continue.cpp; each function's comment is at its definition.)

#include "app.h"

bool continue_card_active(const App* app);
void continue_card_press(App* app, ContinueViewButton button);
void continue_card_update(App* app, const UiView* view);
void continue_card_view(const App* app, UiView* view);
