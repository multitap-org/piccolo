#pragma once

// Shader presets: loading the stages, the question when a preset is too
// slow (shader_check.h), and the preset picker.
// (app_shaders.cpp; each function's comment is at its definition.)

#include "app.h"

// The stages
void update_shader_stages(App* app, ViewportFilter filter);

// The slow preset question
bool shader_check_asking(const App* app);
void shader_check_answer(App* app, ShaderCheckChoice choice);
void shader_check_end_for_game(App* app);
void measure_shader_check(App* app);

// The preset picker
void open_shader_picker(App* app, SettingLayer scope);
void shader_picker_apply(App* app, const FilePickerAction* action);
