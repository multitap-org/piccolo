#pragma once

// Widgets for editing settings (settings.h) in piccolo's retro style. Shared
// by the settings window and the crop window. Use inside the retro_ui theme.
//
// One row per setting:
//
//   M LABEL           [ widget                         ] [RESET]
//
//   M      override marker: "C" (set for this core) or "G" (set for this
//          game) in its own color, with a tooltip. Not color alone.
//   widget shows the value as it resolves at the scope being edited, and a
//          change goes to that scope.
//   RESET  shown when the scope being edited defines the value; removes it
//          from that scope (back to the layer below).
// Global-only settings are greyed out while a core or game scope is edited.
//
// Only the choice of controls is here. What a row shows (value, greyed out,
// marker, RESET) is decided by settings_view.h, which has no GUI; the
// controls and the row's layout are retroui's (retro_ui_controls.h), so this
// file makes no toolkit calls. Another GUI toolkit reimplements retroui.

#include "localization.h"
#include "settings.h"
#include "settings_view.h"

constexpr uint32_t SETTINGS_UI_KEY_MAX = 128;

// Two edits a frame at most: a text field reports its change when it loses
// focus, which happens on the same frame as the click on another control
// (an integer's +, a slider) that changes its own value at once.
constexpr uint32_t SETTINGS_UI_EDITS_MAX = 2;

struct SettingsUiEditEntry
{
    SettingLayer     scope  = SettingLayer::global;
    char             key[SETTINGS_UI_KEY_MAX] = {};
    SettingRowAction action = {};
};

// The settings the user changed this frame, in the order they were drawn,
// for the window's apply step (settings_ui_apply_edit), so the drawing never
// changes the settings.
struct SettingsUiEdit
{
    uint32_t            count = 0;
    SettingsUiEditEntry entries[SETTINGS_UI_EDITS_MAX] = {};
};

// Which layer edits go to: SettingLayer::global, ::core or ::game.
struct SettingsUiContext
{
    const Settings*     settings    = nullptr;
    const Localization* strings     = nullptr;   // UI text (piccolo_text.h)
    SettingLayer scope       = SettingLayer::global;
    float        label_width = 0.0f;   // width of the label column
    SettingsUiEdit* edit     = nullptr;   // where a change goes (the window applies it after drawing)
};

// Applies the pending edits to the settings, in order, and clears them. True
// when a value changed (as settings_view_apply).
bool settings_ui_apply_edit(Settings* settings, SettingsUiEdit* edit);

// One setting's row. `hint` is shown in an empty path field (e.g. the
// default folder); may be nullptr. Returns true if the user changed the
// value (the change is in context->edit, applied after drawing).
bool settings_ui_row(const SettingsUiContext* context, const char* key, const char* hint);

// A folder setting changed with CHOOSE FOLDER: the row of settings_ui_row,
// but the field is read-only and a click on it returns true (the caller
// opens the picker). `hint` as in settings_ui_row; RESET as usual.
bool settings_ui_folder_row(const SettingsUiContext* context, const char* key, const char* hint);

// A path setting's row with a button after the field (e.g. CHOOSE, which
// opens a picker). Sets *button_clicked. Returns true if the user changed
// the value (typed in the field, or reset).
bool settings_ui_path_row_with_button(const SettingsUiContext* context, const char* key, const char* hint,
                                      const char* button, bool* button_clicked);

// A text setting picked from a list (e.g. the audio devices connected now).
// "" shows `empty_label`; a stored value that isn't in the list is shown
// with `missing_label` after it, and kept until something else is picked.
// Returns true if the user changed the value.
bool settings_ui_text_choice_row(const SettingsUiContext* context, const char* key, const char* const* choices,
                                 uint32_t choice_count, const char* empty_label, const char* missing_label);

// "GLOBAL", "THIS CORE", "THIS GAME" in the current language.
const char* settings_ui_scope_name(const Localization* strings, SettingLayer scope);

// The label column's width: the widest label of the rows that have one.
float settings_ui_label_width(const Settings* settings, const Localization* strings);

// The same for the rows of these settings only: a window with a few rows
// (IMAGE SETTINGS' narrow column) keeps the room for its controls.
float settings_ui_label_width_of(const Settings* settings, const Localization* strings, const char* const* keys,
                                 uint32_t key_count);

// A group of rows that can be folded away (GENERAL > REWIND, INTERFACE >
// COLORS...): a header bar across the window with `label`, and `detail` after
// it (the mode in use now; nullptr or "": none). True while it's open (draw
// its rows then). `open_at_first`: open the first time it's drawn; after
// that the toolkit keeps what the user chose, while piccolo runs. The id
// stays while the label changes (the language, the detail).
bool settings_ui_section(const char* id, const char* label, const char* detail, bool open_at_first);

// The SCOPE selector: GLOBAL / THIS CORE (name) / THIS GAME (name), only the
// ones that exist now. With *choose_scope set (just opened) it picks the most
// specific layer already in use, so changes keep going there, and clears it.
void settings_ui_scope(const Settings* settings, const Localization* strings, const char* core_name, const char* game_name,
                       SettingLayer* scope, bool* choose_scope);
