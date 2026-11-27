#pragma once

// (app_ui.cpp; each function's comment is at its definition.)

#include "app.h"

void app_ui_update(App* app, AppUiFrame* frame, UiView* view);
void app_ui_view(App* app, AppUiFrame* frame, UiView* view);
void app_ui_windows(App* app, UiWindows* windows);
void app_apply_ui(App* app, const UiView* view, const AppUiFrame* frame, UiActions* actions);
