// Settings rows as plain data (settings_view.h): no GUI here.

#include "settings_view.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "piccolo_text.h"

static int32_t clamp_integer(const SettingDescriptor* descriptor, int32_t number)
{
    return number < descriptor->min ? descriptor->min : (number > descriptor->max ? descriptor->max : number);
}

static double round_to_step(const SettingDescriptor* descriptor, double number)
{
    if (descriptor->step_real <= 0.0)
    {
        return number;
    }
    return std::round(number / descriptor->step_real) * descriptor->step_real;
}

// The row's text value read for its type, so the drawing doesn't parse text.
static void read_typed_value(const SettingDescriptor* descriptor, SettingRowState* row)
{
    switch (descriptor->type)
    {
    case SettingType::boolean:
        row->checked = std::strcmp(row->value, "true") == 0;
        break;
    case SettingType::enumeration:
        for (uint32_t i = 0; i < descriptor->value_count; i++)
        {
            if (std::strcmp(descriptor->values[i].key, row->value) == 0)
            {
                row->enum_index = i;
            }
        }
        break;
    case SettingType::integer:
        row->integer = static_cast<int32_t>(std::strtol(row->value, nullptr, 10));
        break;
    case SettingType::real:
    {
        // The settings' own parser: strtof would read "0.5" as 0 where the locale's decimal mark is a comma.
        double parsed = 0.0;
        row->real = settings_parse_real(row->value, &parsed) ? static_cast<float>(parsed) : 0.0f;
        break;
    }
    case SettingType::color:
        settings_color_from_text(row->value, &row->color);
        break;
    case SettingType::path:
    case SettingType::text:
        break;
    }
}

bool settings_view_row(const Settings* settings, const Localization* strings, SettingLayer scope, const char* key,
                       SettingRowState* row)
{
    const SettingDescriptor* descriptor = settings_find(settings, key);
    if (descriptor == nullptr)
    {
        return false;
    }
    *row = {};
    row->descriptor = descriptor;
    row->key = key;
    row->label = piccolo_text_id(strings, descriptor->label);   // "" when it has none
    row->help = descriptor->help == SETTING_NO_TEXT ? nullptr : piccolo_text_id(strings, descriptor->help);
    row->value = settings_get_at(settings, scope, key, nullptr);
    read_typed_value(descriptor, row);
    row->enabled = descriptor->overridable || scope == SettingLayer::global;
    // The marker shows where the effective value comes from, whatever scope is edited.
    SettingLayer source = SettingLayer::defaults;
    settings_get(settings, key, &source);
    row->override_layer = source == SettingLayer::core || source == SettingLayer::game ? source : SettingLayer::defaults;
    row->can_reset = row->enabled && settings_get_layer(settings, scope, key) != nullptr;
    return true;
}

bool settings_view_apply(Settings* settings, SettingLayer scope, const char* key, const SettingRowAction* action)
{
    const SettingDescriptor* descriptor = settings_find(settings, key);
    if (descriptor == nullptr)
    {
        return false;
    }
    switch (action->edit)
    {
    case SettingRowEdit::none:
        return false;
    case SettingRowEdit::set_bool:
        return settings_set_bool(settings, scope, key, action->boolean);
    case SettingRowEdit::set_enum:
        return settings_set_enum(settings, scope, key, action->enum_index);
    case SettingRowEdit::set_integer:
        return settings_set_int(settings, scope, key, clamp_integer(descriptor, action->integer));
    case SettingRowEdit::set_real:
        return settings_set_real(settings, scope, key, round_to_step(descriptor, action->real));
    case SettingRowEdit::set_color:
        return settings_set_color(settings, scope, key, action->color);
    case SettingRowEdit::set_text:
        if (action->text[0] == '\0')
        {
            return settings_reset(settings, scope, key);   // empty = default
        }
        return settings_set(settings, scope, key, action->text);
    case SettingRowEdit::reset:
        return settings_reset(settings, scope, key);
    }
    return false;
}

void settings_view_scope(const Settings* settings, bool has_core, bool has_game, SettingLayer* scope,
                         bool* choose_scope)
{
    has_game = has_game && has_core;
    if (*choose_scope)
    {
        *choose_scope = false;
        *scope = SettingLayer::global;
        if (has_game && settings_layer_has_values(settings, SettingLayer::game))
        {
            *scope = SettingLayer::game;
        }
        else if (has_core && settings_layer_has_values(settings, SettingLayer::core))
        {
            *scope = SettingLayer::core;
        }
    }
    if ((*scope == SettingLayer::core && !has_core) || (*scope == SettingLayer::game && !has_game))
    {
        *scope = SettingLayer::global;   // the game stopped
    }
}
