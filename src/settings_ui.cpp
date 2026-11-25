// Settings rows drawn with retroui's controls (settings_ui.h). What a row
// shows comes from settings_view (no GUI); this file picks the control for
// each type and hands back what the user did. No toolkit calls here: the
// layout, IDs and greying out are retroui's (retro_ui_controls.h).

#include "settings_ui.h"

#include <cstdio>
#include <cstring>

#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "settings_view.h"

// Marker colors: our own, distinct from the theme's blues (0xAARRGGBB).
constexpr uint32_t MARKER_CORE = 0xFFF0C040u;   // amber
constexpr uint32_t MARKER_GAME = 0xFF60E080u;   // green

const char* settings_ui_scope_name(const Localization* strings, SettingLayer scope)
{
    switch (scope)
    {
    case SettingLayer::defaults:
    case SettingLayer::global:
        return piccolo_text(strings, Str::scope_global);
    case SettingLayer::core:
        return piccolo_text(strings, Str::scope_core);
    case SettingLayer::game:
        return piccolo_text(strings, Str::scope_game);
    }
    return piccolo_text(strings, Str::scope_global);
}

float settings_ui_label_width(const Settings* settings, const Localization* strings)
{
    float widest = 0.0f;
    for (uint32_t i = 0; i < settings->descriptor_count; i++)
    {
        const SettingDescriptor* descriptor = &settings->descriptors[i];
        if (descriptor->type == SettingType::boolean || descriptor->label == SETTING_NO_TEXT)
        {
            continue;
        }
        float width = retro_ui_text_width(piccolo_text_id(strings, descriptor->label));
        widest = width > widest ? width : widest;
    }
    return retro_ui_label_column_width(widest);
}

// ---------------------------------------------------------------------------
// One row: its SettingRowState as a retroui row, the control for its type.
// Nothing here reads or changes the settings: what the user did goes into a
// SettingRowAction, and settings_view_apply applies it.

// The retroui row for a setting: C / G marker, label column, greyed out when
// global-only here, RESET when this scope defines the value. `label`: false
// for a checkbox, which carries its own. `reset_room`: RESET's room is kept
// even while it isn't shown, so the control keeps its width.
static RetroUiRow make_row(const SettingsUiContext* context, const SettingRowState* state, bool label,
                           bool reset_room)
{
    RetroUiRow row = {};
    row.id = state->key;
    if (state->override_layer == SettingLayer::core || state->override_layer == SettingLayer::game)
    {
        bool core = state->override_layer == SettingLayer::core;
        row.marker = core ? "C" : "G";
        row.marker_color = core ? MARKER_CORE : MARKER_GAME;
        row.marker_help = piccolo_text(context->strings, core ? Str::marker_core_help : Str::marker_game_help);
    }
    if (label)
    {
        row.label = state->label;
        row.help = state->help;
        row.label_width = context->label_width;
    }
    row.enabled = state->enabled;
    row.disabled_help = piccolo_text(context->strings, Str::global_only_help);
    if (state->can_reset || reset_room)
    {
        row.end_button = piccolo_text(context->strings, Str::reset);
        row.end_button_hidden = !state->can_reset;
        Str help = context->scope == SettingLayer::global ? Str::reset_help_global : Str::reset_help_override;
        row.end_button_help = piccolo_text(context->strings, help);
    }
    return row;
}

// An enum value's label in the current language, or its key ("4:3") when it has none.
static const char* value_label(const SettingsUiContext* context, const SettingEnumValue* value)
{
    return value->label == SETTING_NO_TEXT ? value->key : piccolo_text_id(context->strings, value->label);
}

static void draw_checkbox(const SettingRowState* state, SettingRowAction* action)
{
    bool checked = state->checked;
    if (retro_ui_check("value", state->label, &checked))
    {
        action->edit = SettingRowEdit::set_bool;
        action->boolean = checked;
    }
    if (state->help != nullptr)
    {
        retro_ui_item_tooltip(state->help);
    }
}

static void draw_enum(const SettingsUiContext* context, const SettingRowState* state, SettingRowAction* action)
{
    const SettingDescriptor* descriptor = state->descriptor;
    if (!retro_ui_begin_choice("value", value_label(context, &descriptor->values[state->enum_index])))
    {
        return;
    }
    for (uint32_t i = 0; i < descriptor->value_count; i++)
    {
        if (retro_ui_choice_item(i, value_label(context, &descriptor->values[i]), i == state->enum_index))
        {
            action->edit = SettingRowEdit::set_enum;
            action->enum_index = i;
        }
    }
    retro_ui_end_choice();
}

static void draw_integer(const SettingRowState* state, SettingRowAction* action)
{
    int32_t number = state->integer;
    if (retro_ui_number_field("value", &number, state->descriptor->step))
    {
        action->edit = SettingRowEdit::set_integer;
        action->integer = number;
    }
}

static void draw_real(const SettingRowState* state, SettingRowAction* action)
{
    float number = state->real;
    if (retro_ui_slider("value", &number, static_cast<float>(state->descriptor->min_real),
                        static_cast<float>(state->descriptor->max_real), "%.1f%%"))
    {
        action->edit = SettingRowEdit::set_real;
        action->real = static_cast<double>(number);
    }
}

static void draw_color(const SettingRowState* state, SettingRowAction* action)
{
    uint32_t color = state->color;
    if (retro_ui_color_field("value", &color))
    {
        action->edit = SettingRowEdit::set_color;
        action->color = color;
    }
}

// Empty text: back to the default (settings_view_apply).
static void draw_text(const SettingRowState* state, const char* hint, SettingRowAction* action)
{
    if (retro_ui_text_field("value", hint, state->value, action->text, sizeof(action->text)))
    {
        action->edit = SettingRowEdit::set_text;
    }
}

static void draw_row(const SettingsUiContext* context, const SettingRowState* state, const char* hint,
                     SettingRowAction* action)
{
    bool checkbox = state->descriptor->type == SettingType::boolean;
    RetroUiRow row = make_row(context, state, !checkbox, true);
    retro_ui_row_begin(&row);
    switch (state->descriptor->type)
    {
    case SettingType::boolean:
        draw_checkbox(state, action);
        break;
    case SettingType::enumeration:
        draw_enum(context, state, action);
        break;
    case SettingType::integer:
        draw_integer(state, action);
        break;
    case SettingType::real:
        draw_real(state, action);
        break;
    case SettingType::path:
    case SettingType::text:
        draw_text(state, hint, action);
        break;
    case SettingType::color:
        draw_color(state, action);
        break;
    }
    if (retro_ui_row_end(&row))
    {
        action->edit = SettingRowEdit::reset;
    }
}

// ---------------------------------------------------------------------------
// The rows the windows use: state from settings_view, drawing above, the
// action handed to the window (context->edit), which applies it after
// drawing with settings_ui_apply_edit.

// Hands a row's action out. True when the user changed something.
static bool finish_row(const SettingsUiContext* context, const char* key, const SettingRowAction* action)
{
    if (context->edit == nullptr || action->edit == SettingRowEdit::none)
    {
        return false;
    }
    SettingsUiEdit* edit = context->edit;
    if (edit->count == SETTINGS_UI_EDITS_MAX)
    {
        return true;   // a third change in one frame doesn't happen (SETTINGS_UI_EDITS_MAX); the first ones win
    }
    SettingsUiEditEntry* entry = &edit->entries[edit->count];
    edit->count++;
    entry->scope = context->scope;
    std::snprintf(entry->key, sizeof(entry->key), "%s", key);
    entry->action = *action;
    return true;
}

bool settings_ui_apply_edit(Settings* settings, SettingsUiEdit* edit)
{
    bool changed = false;
    for (uint32_t i = 0; i < edit->count; i++)
    {
        const SettingsUiEditEntry* entry = &edit->entries[i];
        if (settings_view_apply(settings, entry->scope, entry->key, &entry->action))
        {
            changed = true;
        }
    }
    edit->count = 0;
    return changed;
}

bool settings_ui_row(const SettingsUiContext* context, const char* key, const char* hint)
{
    SettingRowState state = {};
    if (!settings_view_row(context->settings, context->strings, context->scope, key, &state))
    {
        return false;
    }
    SettingRowAction action = {};
    draw_row(context, &state, hint, &action);
    return finish_row(context, key, &action);
}

bool settings_ui_folder_row(const SettingsUiContext* context, const char* key, const char* hint)
{
    SettingRowState state = {};
    if (!settings_view_row(context->settings, context->strings, context->scope, key, &state))
    {
        return false;
    }
    SettingRowAction action = {};
    // The field reaches the right edge; RESET's room only while it shows.
    RetroUiRow row = make_row(context, &state, true, false);
    retro_ui_row_begin(&row);
    bool clicked = retro_ui_picker_field("value", hint, state.value,
                                         piccolo_text(context->strings, Str::game_folder_choose_help)) &&
                   state.enabled;
    if (retro_ui_row_end(&row))
    {
        action.edit = SettingRowEdit::reset;
    }
    finish_row(context, key, &action);
    return clicked;
}

bool settings_ui_path_row_with_button(const SettingsUiContext* context, const char* key, const char* hint,
                                      const char* button, bool* button_clicked)
{
    *button_clicked = false;
    SettingRowState state = {};
    if (!settings_view_row(context->settings, context->strings, context->scope, key, &state) ||
        state.descriptor->type != SettingType::path || !state.descriptor->overridable)
    {
        return false;   // only for paths every scope can set (no "global only" handling here)
    }
    SettingRowAction action = {};
    RetroUiRow row = make_row(context, &state, true, true);
    retro_ui_row_begin(&row);
    retro_ui_reserve_button(button);
    draw_text(&state, hint, &action);
    *button_clicked = retro_ui_row_button("button", button);
    if (retro_ui_row_end(&row))
    {
        action.edit = SettingRowEdit::reset;
    }
    return finish_row(context, key, &action);
}

bool settings_ui_text_choice_row(const SettingsUiContext* context, const char* key, const char* const* choices,
                                 uint32_t choice_count, const char* empty_label, const char* missing_label)
{
    SettingRowState state = {};
    if (!settings_view_row(context->settings, context->strings, context->scope, key, &state))
    {
        return false;
    }
    const char* value = state.value;
    bool listed = value[0] == '\0';
    for (uint32_t i = 0; i < choice_count && !listed; i++)
    {
        listed = std::strcmp(choices[i], value) == 0;
    }
    char preview[512] = {};
    if (value[0] == '\0')
    {
        std::snprintf(preview, sizeof(preview), "%s", empty_label);
    }
    else if (listed)
    {
        std::snprintf(preview, sizeof(preview), "%s", value);
    }
    else
    {
        std::snprintf(preview, sizeof(preview), "%s (%s)", value, missing_label);
    }

    SettingRowAction action = {};
    RetroUiRow row = make_row(context, &state, true, true);
    retro_ui_row_begin(&row);
    if (retro_ui_begin_choice("value", preview))
    {
        if (retro_ui_choice_item(0, empty_label, value[0] == '\0'))
        {
            action.edit = SettingRowEdit::reset;   // "" = the default
        }
        for (uint32_t i = 0; i < choice_count; i++)
        {
            if (retro_ui_choice_item(i + 1, choices[i], std::strcmp(choices[i], value) == 0))
            {
                action.edit = SettingRowEdit::set_text;
                std::snprintf(action.text, sizeof(action.text), "%s", choices[i]);
            }
        }
        retro_ui_end_choice();
    }
    if (retro_ui_row_end(&row))
    {
        action.edit = SettingRowEdit::reset;
    }
    return finish_row(context, key, &action);
}

void settings_ui_scope(const Settings* settings, const Localization* strings, const char* core_name, const char* game_name,
                       SettingLayer* scope, bool* choose_scope)
{
    bool has_core = core_name[0] != '\0';
    bool has_game = has_core && game_name[0] != '\0';
    settings_view_scope(settings, has_core, has_game, scope, choose_scope);
    if (!has_core)
    {
        char text[300] = {};
        std::snprintf(text, sizeof(text), "%s: %s", piccolo_text(strings, Str::scope),
                      piccolo_text(strings, Str::scope_global));
        retro_ui_dim_text(text, piccolo_text(strings, Str::scope_start_game_help));
        return;
    }

    char labels[3][600] = {};
    std::snprintf(labels[0], sizeof(labels[0]), "%s", piccolo_text(strings, Str::scope_global));
    std::snprintf(labels[1], sizeof(labels[1]), "%s (%s)", piccolo_text(strings, Str::scope_core), core_name);
    std::snprintf(labels[2], sizeof(labels[2]), "%s (%s)", piccolo_text(strings, Str::scope_game), game_name);
    const SettingLayer layers[3] = {SettingLayer::global, SettingLayer::core, SettingLayer::game};
    uint32_t count = has_game ? 3 : 2;
    uint32_t current = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        current = layers[i] == *scope ? i : current;
    }

    RetroUiRow row = {};
    row.id = "scope";
    row.marker_column = false;
    row.label = piccolo_text(strings, Str::scope);
    row.help = piccolo_text(strings, Str::scope_help);
    retro_ui_row_begin(&row);
    if (retro_ui_begin_choice("value", labels[current]))
    {
        for (uint32_t i = 0; i < count; i++)
        {
            if (retro_ui_choice_item(i, labels[i], i == current))
            {
                *scope = layers[i];
            }
        }
        retro_ui_end_choice();
    }
    retro_ui_row_end(&row);
}
