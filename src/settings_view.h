#pragma once

// What a settings screen shows and what it does, with no GUI: the layer
// between the settings model (settings.h) and the drawing (settings_ui.h).
//
//   settings_view_row      reads the model and decides: the value at the
//                          scope being edited, greyed out or not, which
//                          override marker, whether RESET shows
//   (settings_ui draws the row from that and returns a SettingRowAction)
//   settings_view_apply    applies the action to the model: clamping,
//                          rounding, "empty means default"
//
// Another GUI toolkit would only rewrite settings_ui.cpp.

#include <cstdint>

#include "localization.h"
#include "settings.h"

// Longest text a row can hand back (a path).
constexpr uint32_t SETTING_ROW_TEXT_MAX = 4096;

// One setting's row, as the scope being edited sees it.
struct SettingRowState
{
    const SettingDescriptor* descriptor = nullptr;   // type, limits, enum values
    const char* key   = nullptr;
    const char* label = nullptr;   // in the current language
    const char* help  = nullptr;   // in the current language; nullptr: none
    const char* value = nullptr;   // in effect at the scope being edited
    bool enabled = true;           // false: a global-only setting while a core or game scope is edited
    SettingLayer override_layer = SettingLayer::defaults;   // core or game: draw C or G; anything else: nothing
    bool can_reset = false;        // the scope being edited defines the value (RESET shows)
};

enum class SettingRowEdit
{
    none,
    set_bool,
    set_enum,
    set_integer,
    set_real,
    set_color,
    set_text,   // an empty text means "back to the default" (reset)
    reset,
};

// What the user did to a row, as plain data. Only the field of `edit` counts.
struct SettingRowAction
{
    SettingRowEdit edit = SettingRowEdit::none;
    bool     boolean    = false;
    uint32_t enum_index = 0;
    int32_t  integer    = 0;   // not clamped yet
    double   real       = 0.0;   // not rounded to the step yet
    uint32_t color      = 0;   // 0xFFRRGGBB
    char     text[SETTING_ROW_TEXT_MAX] = {};
};

// Fills *row for `key` at `scope`. False when the key isn't a setting.
bool settings_view_row(const Settings* settings, const Localization* strings, SettingLayer scope, const char* key,
                       SettingRowState* row);

// Applies *action to `key` at `scope`. True when a value changed (a reset
// that removed something counts).
bool settings_view_apply(Settings* settings, SettingLayer scope, const char* key, const SettingRowAction* action);

// The SCOPE a settings screen edits: with *choose_scope set (just opened) the
// most specific layer already in use, so changes keep going there, and
// *choose_scope is cleared. A scope that no longer exists (the game stopped)
// falls back to global.
void settings_view_scope(const Settings* settings, bool has_core, bool has_game, SettingLayer* scope,
                         bool* choose_scope);
