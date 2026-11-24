#include "settings_ui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"

#include "piccolo_text.h"
#include "retro_ui.h"
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
        float width = ImGui::CalcTextSize(piccolo_text_id(strings, descriptor->label)).x;
        widest = width > widest ? width : widest;
    }
    return widest + ImGui::GetStyle().ItemSpacing.x * 2.0f;
}

// ---------------------------------------------------------------------------
// Drawing one row from its SettingRowState. Nothing here reads or changes the
// settings: what the user did goes into a SettingRowAction, and
// settings_view_apply applies it.

// The C / G marker. Takes the space of one glyph even when there's none, so
// rows line up.
static void draw_marker(const SettingsUiContext* context, const SettingRowState* row)
{
    ImVec2 size = ImGui::CalcTextSize("C");
    if (row->override_layer != SettingLayer::core && row->override_layer != SettingLayer::game)
    {
        ImGui::Dummy(size);
        return;
    }
    bool core = row->override_layer == SettingLayer::core;
    ImGui::TextColored(retro_ui_color_vec4(core ? MARKER_CORE : MARKER_GAME), "%s", core ? "C" : "G");
    retro_ui_item_tooltip(piccolo_text(context->strings, core ? Str::marker_core_help : Str::marker_game_help));
}

static float reset_width(const SettingsUiContext* context)
{
    return ImGui::CalcTextSize(piccolo_text(context->strings, Str::reset)).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

// RESET at the right edge of the row, when the scope being edited defines
// the value. Sets a reset action when pressed.
static void draw_reset(const SettingsUiContext* context, const SettingRowState* row, SettingRowAction* action)
{
    if (!row->can_reset)
    {
        return;
    }
    ImGui::SameLine(ImGui::GetContentRegionMax().x - reset_width(context));
    ImGui::PushID("reset");
    bool clicked = retro_ui_button(piccolo_text(context->strings, Str::reset));
    ImGui::PopID();
    Str help = context->scope == SettingLayer::global ? Str::reset_help_global : Str::reset_help_override;
    retro_ui_item_tooltip(piccolo_text(context->strings, help));
    if (clicked)
    {
        action->edit = SettingRowEdit::reset;
    }
}

// The label column: text, then the cursor moves to the widget column.
static void draw_label(const SettingsUiContext* context, const SettingRowState* row)
{
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    float start = ImGui::GetCursorPosX();
    ImGui::TextUnformatted(row->label);
    if (row->help != nullptr)
    {
        retro_ui_item_tooltip(row->help);
    }
    ImGui::SameLine(start + context->label_width);
}

// Room left for the widget, keeping space for the reset button.
static float widget_width(const SettingsUiContext* context)
{
    return ImGui::GetContentRegionAvail().x - reset_width(context) - ImGui::GetStyle().ItemSpacing.x;
}

// Starts greying out a global-only row. Only when it is: ImGui counts
// BeginDisabled(false) too, and its color swatches then draw with a saved
// opacity that may be 0 (invisible).
static void begin_global_only(const SettingRowState* row)
{
    if (!row->enabled)
    {
        ImGui::BeginDisabled();
    }
}

// Ends it, with the tooltip saying why (a disabled item needs
// AllowWhenDisabled for its tooltip). Returns true when the row is enabled.
static bool end_global_only(const SettingsUiContext* context, const SettingRowState* row)
{
    if (row->enabled)
    {
        return true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        retro_ui_tooltip(piccolo_text(context->strings, Str::global_only_help));
    }
    return false;
}

// An enum value's label in the current language, or its key ("4:3") when it has none.
static const char* value_label(const SettingsUiContext* context, const SettingEnumValue* value)
{
    return value->label == SETTING_NO_TEXT ? value->key : piccolo_text_id(context->strings, value->label);
}

static void draw_checkbox(const SettingRowState* row, SettingRowAction* action)
{
    ImGui::SameLine();
    bool checked = std::strcmp(row->value, "true") == 0;
    // "###" keeps the widget's ID the same whatever the language.
    char label[256] = {};
    std::snprintf(label, sizeof(label), "%s###check", row->label);
    if (retro_ui_checkbox(label, &checked))
    {
        action->edit = SettingRowEdit::set_bool;
        action->boolean = checked;
    }
    if (row->help != nullptr)
    {
        retro_ui_item_tooltip(row->help);
    }
}

static void draw_enum(const SettingsUiContext* context, const SettingRowState* row, SettingRowAction* action)
{
    const SettingDescriptor* descriptor = row->descriptor;
    uint32_t current = 0;
    for (uint32_t i = 0; i < descriptor->value_count; i++)
    {
        if (std::strcmp(descriptor->values[i].key, row->value) == 0)
        {
            current = i;
        }
    }
    ImGui::SetNextItemWidth(widget_width(context));
    if (retro_ui_begin_combo("##value", value_label(context, &descriptor->values[current])))
    {
        for (uint32_t i = 0; i < descriptor->value_count; i++)
        {
            ImGui::PushID(static_cast<int>(i));
            bool picked = ImGui::Selectable(value_label(context, &descriptor->values[i]), i == current);
            ImGui::PopID();
            if (picked && i != current)
            {
                action->edit = SettingRowEdit::set_enum;
                action->enum_index = i;
            }
        }
        ImGui::EndCombo();
    }
}

static void draw_integer(const SettingsUiContext* context, const SettingRowState* row, SettingRowAction* action)
{
    int number = static_cast<int>(std::strtol(row->value, nullptr, 10));
    ImGui::SetNextItemWidth(widget_width(context));
    retro_ui_push_field_text();
    int step = row->descriptor->step > 0 ? row->descriptor->step : 1;
    bool edited = ImGui::InputInt("##value", &number, step, 8);
    retro_ui_pop_field_text();
    if (edited)
    {
        action->edit = SettingRowEdit::set_integer;
        action->integer = static_cast<int32_t>(number);
    }
}

static void draw_real(const SettingsUiContext* context, const SettingRowState* row, SettingRowAction* action)
{
    float number = std::strtof(row->value, nullptr);
    ImGui::SetNextItemWidth(widget_width(context));
    if (ImGui::SliderFloat("##value", &number, static_cast<float>(row->descriptor->min_real),
                           static_cast<float>(row->descriptor->max_real), "%.1f%%"))
    {
        action->edit = SettingRowEdit::set_real;
        action->real = static_cast<double>(number);
    }
}

// A swatch (click: ImGui's color picker) and the value as #RRGGBB.
static void draw_color(const SettingRowState* row, SettingRowAction* action)
{
    uint32_t argb = 0xFF000000u;
    settings_color_from_text(row->value, &argb);
    ImVec4 color = retro_ui_color_vec4(argb);
    float rgb[3] = {color.x, color.y, color.z};
    if (ImGui::ColorEdit3("##value", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
                                             ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoAlpha))
    {
        uint32_t picked = 0xFF000000u;
        for (int i = 0; i < 3; i++)
        {
            float channel = rgb[i] < 0.0f ? 0.0f : (rgb[i] > 1.0f ? 1.0f : rgb[i]);
            picked |= static_cast<uint32_t>(channel * 255.0f + 0.5f) << (16 - 8 * i);
        }
        action->edit = SettingRowEdit::set_color;
        action->color = picked;
    }
    retro_ui_raised_item_edge();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("#%06X", static_cast<unsigned>(argb & 0xFFFFFFu));
}

// A text field `width` wide. The text is handed back when editing ends
// (empty: back to the default).
static void draw_text_field(const SettingRowState* row, const char* hint, float width, SettingRowAction* action)
{
    // ImGui keeps its own copy of the text while the field is being edited;
    // this buffer only seeds it.
    char buffer[SETTING_ROW_TEXT_MAX] = {};
    std::snprintf(buffer, sizeof(buffer), "%s", row->value);
    ImGui::SetNextItemWidth(width);
    retro_ui_push_field_text();
    ImGui::InputTextWithHint("##value", hint != nullptr ? hint : "", buffer, sizeof(buffer));
    retro_ui_pop_field_text();
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        action->edit = SettingRowEdit::set_text;
        std::snprintf(action->text, sizeof(action->text), "%s", buffer);
    }
}

static void draw_row(const SettingsUiContext* context, const SettingRowState* row, const char* hint,
                     SettingRowAction* action)
{
    ImGui::PushID(row->key);
    draw_marker(context, row);
    begin_global_only(row);
    if (row->descriptor->type == SettingType::boolean)
    {
        draw_checkbox(row, action);
    }
    else
    {
        draw_label(context, row);
        switch (row->descriptor->type)
        {
        case SettingType::enumeration:
            draw_enum(context, row, action);
            break;
        case SettingType::integer:
            draw_integer(context, row, action);
            break;
        case SettingType::real:
            draw_real(context, row, action);
            break;
        case SettingType::path:
        case SettingType::text:
            draw_text_field(row, hint, widget_width(context), action);
            break;
        case SettingType::color:
            draw_color(row, action);
            break;
        case SettingType::boolean:
            break;
        }
    }
    if (end_global_only(context, row))
    {
        draw_reset(context, row, action);
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// The rows the windows use: state from settings_view, drawing above, the
// action applied by settings_view.

bool settings_ui_row(const SettingsUiContext* context, const char* key, const char* hint)
{
    SettingRowState row = {};
    if (!settings_view_row(context->settings, context->strings, context->scope, key, &row))
    {
        return false;
    }
    SettingRowAction action = {};
    draw_row(context, &row, hint, &action);
    return settings_view_apply(context->settings, context->scope, key, &action);
}

bool settings_ui_folder_field(const SettingsUiContext* context, const char* value, const char* hint, float reserved)
{
    // Read-only: it looks like any other field (and a long path can be
    // scrolled), but a click opens CHOOSE FOLDER, as "..." does for whoever
    // doesn't think of clicking the field.
    const char* dots = "...";
    float dots_width = ImGui::CalcTextSize(dots).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    char buffer[SETTING_ROW_TEXT_MAX] = {};
    std::snprintf(buffer, sizeof(buffer), "%s", value);
    // Up to the right edge (Andrés: no empty column after it), less what
    // the caller's button needs.
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - reserved - dots_width - spacing);
    retro_ui_push_field_text();
    ImGui::InputTextWithHint("##value", hint != nullptr ? hint : "", buffer, sizeof(buffer),
                             ImGuiInputTextFlags_ReadOnly);
    retro_ui_pop_field_text();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    retro_ui_item_tooltip(piccolo_text(context->strings, Str::game_folder_choose_help));
    ImGui::SameLine();
    ImGui::PushID("choose");
    if (retro_ui_button(dots))
    {
        clicked = true;
    }
    ImGui::PopID();
    retro_ui_item_tooltip(piccolo_text(context->strings, Str::game_folder_choose_help));
    return clicked;
}

bool settings_ui_folder_row(const SettingsUiContext* context, const char* key, const char* hint)
{
    SettingRowState row = {};
    if (!settings_view_row(context->settings, context->strings, context->scope, key, &row))
    {
        return false;
    }
    SettingRowAction action = {};
    ImGui::PushID(key);
    draw_marker(context, &row);
    begin_global_only(&row);
    draw_label(context, &row);
    // RESET only when this scope sets the folder: then its room is kept.
    float reserved = row.can_reset ? reset_width(context) + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    bool clicked = settings_ui_folder_field(context, row.value, hint, reserved) && row.enabled;
    if (end_global_only(context, &row))
    {
        draw_reset(context, &row, &action);
    }
    ImGui::PopID();
    settings_view_apply(context->settings, context->scope, key, &action);
    return clicked;
}

bool settings_ui_path_row_with_button(const SettingsUiContext* context, const char* key, const char* hint,
                                      const char* button, bool* button_clicked)
{
    *button_clicked = false;
    SettingRowState row = {};
    if (!settings_view_row(context->settings, context->strings, context->scope, key, &row) ||
        row.descriptor->type != SettingType::path || !row.descriptor->overridable)
    {
        return false;   // only for paths every scope can set (no "global only" handling here)
    }
    SettingRowAction action = {};
    ImGui::PushID(key);
    draw_marker(context, &row);
    draw_label(context, &row);
    // The field leaves room for the button, as for the reset.
    float button_width = ImGui::CalcTextSize(button).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    draw_text_field(&row, hint, widget_width(context) - button_width - ImGui::GetStyle().ItemSpacing.x, &action);
    ImGui::SameLine();
    *button_clicked = retro_ui_button(button);
    draw_reset(context, &row, &action);
    ImGui::PopID();
    return settings_view_apply(context->settings, context->scope, key, &action);
}

bool settings_ui_text_choice_row(const SettingsUiContext* context, const char* key, const char* const* choices,
                                 uint32_t choice_count, const char* empty_label, const char* missing_label)
{
    SettingRowState row = {};
    if (!settings_view_row(context->settings, context->strings, context->scope, key, &row))
    {
        return false;
    }
    const char* value = row.value;
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
    ImGui::PushID(key);
    draw_marker(context, &row);
    begin_global_only(&row);
    draw_label(context, &row);
    ImGui::SetNextItemWidth(widget_width(context));
    if (retro_ui_begin_combo("##value", preview))
    {
        if (ImGui::Selectable(empty_label, value[0] == '\0') && value[0] != '\0')
        {
            action.edit = SettingRowEdit::reset;   // "" = the default
        }
        for (uint32_t i = 0; i < choice_count; i++)
        {
            ImGui::PushID(static_cast<int>(i));
            bool current = std::strcmp(choices[i], value) == 0;
            bool picked = ImGui::Selectable(choices[i], current);
            ImGui::PopID();
            if (picked && !current)
            {
                action.edit = SettingRowEdit::set_text;
                std::snprintf(action.text, sizeof(action.text), "%s", choices[i]);
            }
        }
        ImGui::EndCombo();
    }
    if (end_global_only(context, &row))
    {
        draw_reset(context, &row, &action);
    }
    ImGui::PopID();
    return settings_view_apply(context->settings, context->scope, key, &action);
}

void settings_ui_scope(Settings* settings, const Localization* strings, const char* core_name, const char* game_name,
                       SettingLayer* scope, bool* choose_scope)
{
    bool has_core = core_name[0] != '\0';
    bool has_game = has_core && game_name[0] != '\0';
    settings_view_scope(settings, has_core, has_game, scope, choose_scope);
    if (!has_core)
    {
        ImGui::TextDisabled("%s: %s", piccolo_text(strings, Str::scope), piccolo_text(strings, Str::scope_global));
        retro_ui_item_tooltip(piccolo_text(strings, Str::scope_start_game_help));
        return;
    }

    char core_label[400] = {};
    char game_label[600] = {};
    std::snprintf(core_label, sizeof(core_label), "%s (%s)###core", piccolo_text(strings, Str::scope_core),
                  core_name);
    std::snprintf(game_label, sizeof(game_label), "%s (%s)###game", piccolo_text(strings, Str::scope_game),
                  game_name);
    char global_label[200] = {};
    std::snprintf(global_label, sizeof(global_label), "%s###global", piccolo_text(strings, Str::scope_global));
    // The combo shows the label without its "###" part.
    const char* preview = *scope == SettingLayer::core   ? core_label
                          : *scope == SettingLayer::game ? game_label
                                                                : global_label;
    char preview_text[600] = {};
    std::snprintf(preview_text, sizeof(preview_text), "%s", preview);
    char* hidden = std::strstr(preview_text, "###");
    if (hidden != nullptr)
    {
        *hidden = '\0';
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(piccolo_text(strings, Str::scope));
    retro_ui_item_tooltip(piccolo_text(strings, Str::scope_help));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (retro_ui_begin_combo("##scope", preview_text))
    {
        if (ImGui::Selectable(global_label, *scope == SettingLayer::global))
        {
            *scope = SettingLayer::global;
        }
        if (ImGui::Selectable(core_label, *scope == SettingLayer::core))
        {
            *scope = SettingLayer::core;
        }
        if (has_game && ImGui::Selectable(game_label, *scope == SettingLayer::game))
        {
            *scope = SettingLayer::game;
        }
        ImGui::EndCombo();
    }
}

