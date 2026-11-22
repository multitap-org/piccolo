#include "core_config_window.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "imgui.h"

#include "input_window.h"
#include "pad_picture.h"
#include "piccolo_settings.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "settings_ui.h"

void core_config_window_open(CoreConfigWindow* config)
{
    config->select_tab = true;   // back on the tab it was closed on
    config->shader_choose_scope = true;
    config->choose_shader_request = false;
    config->remap = false;
    config->capturing = false;
    config->bind_all = false;
    config->pad_action = InputAction::count;
    config->combo_editing = false;
    config->macro_editing = false;
    config->open = true;
}

void core_config_window_open_controls(CoreConfigWindow* config)
{
    core_config_window_open(config);
    config->tab = CoreConfigTab::controls;
}

void core_config_window_open_remap(CoreConfigWindow* config, uint32_t port, uint32_t controller)
{
    core_config_window_open_controls(config);
    config->remap_section = 0;   // MAPPING open
    config->remap = true;
    config->remap_port = port;
    config->remap_controller = controller;
}

bool core_config_window_sub_view(const CoreConfigWindow* config)
{
    return config->open && config->tab == CoreConfigTab::controls && config->remap && !config->capturing;
}

void core_config_window_close_sub_view(CoreConfigWindow* config)
{
    config->remap = false;
}

// The SHADER tab: the scope, the preset (CHOOSE opens the shader picker,
// through piccolo) and FILTER, which applies when there's no preset. The
// same rows as SETTINGS > PICTURE, for changing the look without leaving the
// game.
static void draw_shader_tab(CoreConfigWindow* config, const CoreConfigContext* context)
{
    settings_ui_scope(context->settings, context->strings, context->core_name, context->game_name, &config->shader_scope,
                      &config->shader_choose_scope);
    ImGui::Spacing();
    SettingsUiContext ui = {};
    ui.settings = context->settings;
    ui.strings = context->strings;
    ui.scope = config->shader_scope;
    ui.label_width = settings_ui_label_width(context->settings, context->strings);
    bool choose = false;
    settings_ui_path_row_with_button(&ui, PICCOLO_VIDEO_SHADER_PRESET, piccolo_text(context->strings, Str::shader_preset_none),
                                     piccolo_text(context->strings, Str::shader_choose), &choose);
    if (choose)
    {
        config->choose_shader_request = true;
    }
    settings_ui_row(&ui, PICCOLO_VIDEO_FILTER, nullptr);
    if (!context->shaders_available)
    {
        ImGui::TextDisabled("%s", piccolo_text(context->strings, Str::shader_unavailable));
    }
}

// ---------------------------------------------------------------------------
// CONTROLS: remap one port's controller for this core and device
// ---------------------------------------------------------------------------

constexpr uint64_t REMAP_COOL_OFF_MS = 500;   // BIND ALL: before taking the next control
constexpr uint16_t HID_ESCAPE_KEY = 41;

// RetroPad button names when the core gives none (RETRO_DEVICE_ID_JOYPAD order).
static const Str PAD_LABELS[INPUT_PAD_BUTTON_COUNT] = {
    Str::input_action_pad_b, Str::input_action_pad_y, Str::input_action_pad_select, Str::input_action_pad_start,
    Str::input_action_pad_up, Str::input_action_pad_down, Str::input_action_pad_left, Str::input_action_pad_right,
    Str::input_action_pad_a, Str::input_action_pad_x, Str::input_action_pad_l, Str::input_action_pad_r,
    Str::input_action_pad_l2, Str::input_action_pad_r2, Str::input_action_pad_l3, Str::input_action_pad_r3,
};

// The RetroPad's stick directions, from pad_left_x_plus on.
static const Str STICK_LABELS[8] = {
    Str::input_action_left_x_plus,  Str::input_action_left_x_minus,  Str::input_action_left_y_plus,
    Str::input_action_left_y_minus, Str::input_action_right_x_plus, Str::input_action_right_x_minus,
    Str::input_action_right_y_plus, Str::input_action_right_y_minus,
};

struct RemapControl
{
    InputAction action;
    std::string label;   // the game's name for it (a stick's with the direction), or the RetroPad's
};

// The controls of the port's device, by the game's names: its joypad
// descriptors (each button once) and analog ones (each stick axis as its two
// directions: libretro's +Y is down), or the whole RetroPad if it sends none.
static void remap_controls(const CoreConfigContext* context, uint32_t port, std::vector<RemapControl>* controls)
{
    controls->clear();
    const LibretroHost* host = context->host;
    const Localization* strings = context->strings;
    bool seen[INPUT_PAD_BUTTON_COUNT] = {};
    bool seen_axis[2][2] = {};   // [left / right stick][x / y]
    uint32_t first_stick = static_cast<uint32_t>(InputAction::pad_left_x_plus);
    for (uint32_t d = 0; host != nullptr && d < host->input_info.descriptor_count; d++)
    {
        const LibretroInputDescriptor* descriptor = &host->input_info.descriptors[d];
        if (descriptor->port != port)
        {
            continue;
        }
        unsigned device = descriptor->device & RETRO_DEVICE_MASK;
        if (device == RETRO_DEVICE_JOYPAD && descriptor->id < INPUT_PAD_BUTTON_COUNT && !seen[descriptor->id])
        {
            seen[descriptor->id] = true;
            RemapControl control = {static_cast<InputAction>(descriptor->id), descriptor->description};
            controls->push_back(control);
        }
        else if (device == RETRO_DEVICE_ANALOG && descriptor->index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT &&
                 descriptor->id <= RETRO_DEVICE_ID_ANALOG_Y && !seen_axis[descriptor->index][descriptor->id])
        {
            seen_axis[descriptor->index][descriptor->id] = true;
            bool x = descriptor->id == RETRO_DEVICE_ID_ANALOG_X;
            uint32_t plus = first_stick + descriptor->index * 4 + descriptor->id * 2;
            RemapControl positive = {static_cast<InputAction>(plus),
                                     std::string(descriptor->description) + " " +
                                         piccolo_text(strings, x ? Str::input_action_pad_right : Str::input_action_pad_down)};
            RemapControl negative = {static_cast<InputAction>(plus + 1),
                                     std::string(descriptor->description) + " " +
                                         piccolo_text(strings, x ? Str::input_action_pad_left : Str::input_action_pad_up)};
            controls->push_back(positive);
            controls->push_back(negative);
        }
    }
    if (controls->empty())
    {
        for (uint32_t b = 0; b < INPUT_PAD_BUTTON_COUNT; b++)
        {
            RemapControl control = {static_cast<InputAction>(b), piccolo_text(strings, PAD_LABELS[b])};
            controls->push_back(control);
        }
        for (uint32_t s = 0; s < 8; s++)
        {
            RemapControl control = {static_cast<InputAction>(first_stick + s), piccolo_text(strings, STICK_LABELS[s])};
            controls->push_back(control);
        }
    }
}

// The remap being edited: the game's when it has its own, else the core's.
static InputCoreProfile* remap_profile(CoreConfigWindow* config, const CoreConfigContext* context)
{
    InputSystem* input = context->input;
    std::string id = input_controller_profile_id(input, config->remap_controller);
    uint32_t device = input->port_devices[config->remap_port];
    bool game_has = !input->game_name.empty() &&
                    input_core_profile(input, input->core_name.c_str(), input->game_name.c_str(), device, id) != nullptr;
    return input_core_profile_for(input, input->core_name.c_str(), game_has ? input->game_name.c_str() : "", device, id);
}

// A combo's binding in the remap being edited (made if it has none).
static InputComboBinding* remap_combo_binding(CoreConfigWindow* config, const CoreConfigContext* context, const std::string& combo)
{
    InputCoreProfile* profile = remap_profile(config, context);
    profile->decided = true;
    config->input_dirty = true;
    for (size_t k = 0; k < profile->combos.size(); k++)
    {
        if (profile->combos[k].combo == combo)
        {
            return &profile->combos[k];
        }
    }
    InputComboBinding binding = {};
    binding.combo = combo;
    profile->combos.push_back(binding);
    return &profile->combos.back();
}

// A combo's binding in what plays now, or nullptr.
static const InputComboBinding* combo_binding_of(const InputCoreProfile* profile, const std::string& combo)
{
    for (size_t k = 0; profile != nullptr && k < profile->combos.size(); k++)
    {
        if (profile->combos[k].combo == combo)
        {
            return &profile->combos[k];
        }
    }
    return nullptr;
}

// The captured source becomes this action's binding for the core and device.
// This action's change for the core and device, made from what plays now
// when it has none (switching to custom keeps the rest as it was).
static InputBindingOverride* remap_override(CoreConfigWindow* config, const CoreConfigContext* context, InputAction action)
{
    InputBindingSet effective = {};
    input_effective_bindings(context->input, config->remap_controller, config->remap_port, &effective);
    InputCoreProfile* profile = remap_profile(config, context);
    profile->decided = true;
    profile->custom = true;
    config->input_dirty = true;
    for (size_t o = 0; o < profile->overrides.size(); o++)
    {
        if (profile->overrides[o].action == action)
        {
            return &profile->overrides[o];
        }
    }
    InputBindingOverride change = {};
    change.action = action;
    for (uint32_t s = 0; s < INPUT_SOURCES_PER_ACTION; s++)
    {
        change.sources[s] = effective.sources[static_cast<uint32_t>(action)][s];
    }
    profile->overrides.push_back(change);
    return &profile->overrides.back();
}

// The captured source on `row` (BIND ALL: alone).
static void remap_set(CoreConfigWindow* config, const CoreConfigContext* context, InputAction action, uint32_t row, InputSource source)
{
    if (!config->capture_combo.empty())
    {
        input_sources_set(remap_combo_binding(config, context, config->capture_combo)->sources, row, source);
        return;
    }
    InputBindingOverride* change = remap_override(config, context, action);
    if (config->bind_all)
    {
        for (uint32_t s = 0; s < INPUT_SOURCES_PER_ACTION; s++)
        {
            change->sources[s] = InputSource{};
        }
        row = 0;
    }
    else if (row < INPUT_SOURCES_PER_ACTION)
    {
        source.turbo = change->sources[row].turbo;   // SET again keeps the row's turbo
    }
    input_sources_set(change->sources, row, source);
}

static void remap_start_capture(CoreConfigWindow* config, const CoreConfigContext* context, InputAction action, uint32_t row)
{
    InputSystem* input = context->input;
    if (!config->capturing)
    {
        config->capture_backup = *remap_profile(config, context);   // a new pass: CANCEL goes back to this
    }
    config->capturing = true;
    config->capture_action = action;
    config->capture_combo.clear();
    config->capture_row = row;
    config->capture_started_ms = context->now_ms;
    config->capture_seen = true;
    input_capture_begin(input, input->controllers[config->remap_controller].device_id);
}

// SET / + on a combo's row.
static void remap_start_combo_capture(CoreConfigWindow* config, const CoreConfigContext* context, const std::string& combo, uint32_t row)
{
    config->bind_all = false;
    remap_start_capture(config, context, InputAction::count, row);
    config->capture_combo = combo;
}

// The next control of a BIND ALL (in the list's order), or the end.
static void remap_next(CoreConfigWindow* config, const CoreConfigContext* context, const std::vector<RemapControl>& controls)
{
    if (config->bind_all && config->capture_combo.empty())
    {
        for (size_t i = 0; i + 1 < controls.size() && controls[i + 1].action != InputAction::pad_turbo; i++)
        {
            if (controls[i].action == config->capture_action)
            {
                remap_start_capture(config, context, controls[i + 1].action, 0);
                return;
            }
        }
    }
    config->bind_all = false;
    config->capturing = false;
    input_capture_cancel(context->input);
}

static void remap_run_capture(CoreConfigWindow* config, const CoreConfigContext* context, const std::vector<RemapControl>& controls)
{
    if (!config->capturing)
    {
        return;
    }
    config->capture_seen = true;
    uint64_t since = context->now_ms - config->capture_started_ms;
    InputSource source = {};
    if ((!config->bind_all || since >= REMAP_COOL_OFF_MS) && input_capture_poll(context->input, context->now_ms, &source))
    {
        if (source.type == InputSourceType::key && source.code == HID_ESCAPE_KEY)
        {
            config->bind_all = false;
            config->capturing = false;
            return;
        }
        remap_set(config, context, config->capture_action, config->capture_row, source);
        remap_next(config, context, controls);
        return;
    }
    InputSource held = {};
    bool holding = input_capture_candidate(context->input, &held);   // no time-out while a control is being held
    if (!context->input->capture.active ||
        (!holding && since > context->input->settings.press_ms + (config->bind_all ? REMAP_COOL_OFF_MS : 0)))
    {
        remap_next(config, context, controls);
    }
}

// CORE DEVICE n [ SNES Joypad v ]: what the core has on the port, from its
// own list (just its name when it lists one). Applied at once and kept per
// core.
static void draw_device_combo(const CoreConfigContext* context, uint32_t p, float label_width)
{
    LibretroHost* host = context->host;
    const Localization* strings = context->strings;
    if (host == nullptr || !host->game_loaded || p >= host->input_info.port_count)
    {
        return;
    }
    const LibretroPortControllers* types = &host->input_info.ports[p];
    ImGui::PushID(static_cast<int>(100 + p));
    ImGui::AlignTextToFramePadding();
    char label[96] = {};
    std::snprintf(label, sizeof(label), "%s %u", piccolo_text(strings, Str::quick_menu_device), static_cast<unsigned>(p + 1));
    float start = ImGui::GetCursorPosX();   // (indented in a section)
    ImGui::TextUnformatted(label);
    // At least `label_width`, wider if the label (in this language) needs it.
    float needed = start + ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SameLine(needed > label_width ? needed : label_width);
    const char* current = libretro_input_controller_name(&host->input_info, p, host->port_devices[p]);
    if (types->count < 2)
    {
        ImGui::TextUnformatted(current != nullptr ? current : "RETROPAD");   // nothing to choose
        ImGui::PopID();
        return;
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (retro_ui_begin_combo("##device", current != nullptr ? current : "?"))
    {
        for (uint32_t t = 0; t < types->count; t++)
        {
            ImGui::PushID(static_cast<int>(t));
            if (ImGui::Selectable(types->types[t].description, types->types[t].id == host->port_devices[p]))
            {
                libretro_host_set_port_device(host, p, types->types[t].id);
                if (context->settings != nullptr)
                {
                    settings_set_int(context->settings, SettingLayer::core, PICCOLO_INPUT_PORT_DEVICES[p],
                                     static_cast<int32_t>(types->types[t].id));
                }
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
}

// SCOPE [ THIS CORE (fbneo) v ]: the remap is the core's, or the game's own
// (THIS GAME starts as a copy of the core's; back to THIS CORE drops it).
static void draw_remap_scope(CoreConfigWindow* config, const CoreConfigContext* context, float label_width)
{
    InputSystem* input = context->input;
    const Localization* strings = context->strings;
    if (input->game_name.empty())
    {
        return;
    }
    std::string id = input_controller_profile_id(input, config->remap_controller);
    uint32_t device = input->port_devices[config->remap_port];
    const InputCoreProfile* game = input_core_profile(input, input->core_name.c_str(), input->game_name.c_str(), device, id);
    char core_label[300] = {};
    char game_label[600] = {};
    std::snprintf(core_label, sizeof(core_label), "%s (%s)", piccolo_text(strings, Str::scope_core), input->core_name.c_str());
    std::snprintf(game_label, sizeof(game_label), "%s (%s)", piccolo_text(strings, Str::scope_game), input->game_name.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(piccolo_text(strings, Str::scope));
    retro_ui_item_tooltip(piccolo_text(strings, Str::remap_scope_help));
    ImGui::SameLine(label_width);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (retro_ui_begin_combo("##remap_scope", game != nullptr ? game_label : core_label))
    {
        if (ImGui::Selectable(core_label, game == nullptr) && game != nullptr)
        {
            input_core_profile_remove(input, game);
            config->input_dirty = true;
        }
        if (ImGui::Selectable(game_label, game != nullptr) && game == nullptr)
        {
            const InputCoreProfile* core = input_core_profile(input, input->core_name.c_str(), "", device, id);
            InputCoreProfile copy = core != nullptr ? *core : InputCoreProfile{};
            copy.core = input->core_name;
            copy.game = input->game_name;
            copy.device = device;
            copy.profile_id = id;
            copy.decided = true;
            input->core_profiles.push_back(copy);
            config->input_dirty = true;
        }
        ImGui::EndCombo();
    }
}

// The name of a RetroPad button in this game (its descriptor), else the RetroPad's.
static std::string button_label(uint32_t button, const std::vector<RemapControl>& controls, const Localization* strings)
{
    for (size_t i = 0; i < controls.size(); i++)
    {
        if (static_cast<uint32_t>(controls[i].action) == button)
        {
            return controls[i].label;
        }
    }
    return piccolo_text(strings, PAD_LABELS[button]);
}

// A combo as shown: its name, else its buttons ("A + B").
static std::string combo_label(const InputCombo* combo, const std::vector<RemapControl>& controls, const Localization* strings)
{
    if (!combo->name.empty())
    {
        return combo->name;
    }
    std::string text;
    for (uint32_t b = 0; b < INPUT_PAD_BUTTON_COUNT; b++)
    {
        if ((combo->buttons & (1u << b)) != 0)
        {
            text += text.empty() ? "" : " + ";
            text += button_label(b, controls, strings);
        }
    }
    return text;
}

// Making or changing a combo (in its modal): its NAME (optional) and its buttons.
static void draw_combo_editor(CoreConfigWindow* config, const CoreConfigContext* context, const std::vector<RemapControl>& controls,
                              float label_width)
{
    InputSystem* input = context->input;
    const Localization* strings = context->strings;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(piccolo_text(strings, Str::remap_combo_name));
    ImGui::SameLine(label_width);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    retro_ui_push_field_text();
    ImGui::InputTextWithHint("##combo_name", piccolo_text(strings, Str::remap_combo_name_hint), config->combo_edit_name,
                             sizeof(config->combo_edit_name));
    retro_ui_pop_field_text();
    // The buttons this game has (each once), else the whole RetroPad.
    bool any = false;
    for (size_t i = 0; i < controls.size(); i++)
    {
        any = any || static_cast<uint32_t>(controls[i].action) < INPUT_PAD_BUTTON_COUNT;
    }
    for (uint32_t b = 0; b < INPUT_PAD_BUTTON_COUNT; b++)
    {
        bool listed = !any;
        for (size_t i = 0; !listed && i < controls.size(); i++)
        {
            listed = static_cast<uint32_t>(controls[i].action) == b;
        }
        if (!listed)
        {
            continue;
        }
        ImGui::PushID(static_cast<int>(b));
        bool on = (config->combo_edit_buttons & (1u << b)) != 0;
        if (retro_ui_checkbox(button_label(b, controls, strings).c_str(), &on))
        {
            config->combo_edit_buttons = static_cast<uint16_t>(on ? (config->combo_edit_buttons | (1u << b))
                                                                : (config->combo_edit_buttons & ~(1u << b)));
        }
        ImGui::PopID();
    }
    ImGui::BeginDisabled(config->combo_edit_buttons == 0);
    if (retro_ui_button(piccolo_text(strings, Str::remap_combo_save)))
    {
        std::string name = config->combo_edit_name;
        size_t first = name.find_first_not_of(' ');
        size_t last = name.find_last_not_of(' ');
        name = first != std::string::npos ? name.substr(first, last - first + 1) : std::string();
        InputCombo* combo = nullptr;
        for (size_t c = 0; c < input->combos.size() && !config->combo_edit_id.empty(); c++)
        {
            if (input->combos[c].core == input->core_name && input->combos[c].id == config->combo_edit_id)
            {
                combo = &input->combos[c];
            }
        }
        if (combo == nullptr)
        {
            combo = input_combo_add(input, input->core_name.c_str(), name, config->combo_edit_buttons);
        }
        combo->name = name;
        combo->buttons = config->combo_edit_buttons;
        config->combo_editing = false;
        config->input_dirty = true;
    }
    ImGui::EndDisabled();
    if (!config->combo_edit_id.empty())
    {
        ImGui::SameLine();
        if (retro_ui_button(piccolo_text(strings, Str::remap_combo_delete)))
        {
            input_combo_remove(input, input->core_name.c_str(), config->combo_edit_id);
            config->combo_editing = false;
            config->input_dirty = true;
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::remap_combo_delete_help));
    }
    ImGui::SameLine();
    if (retro_ui_button(piccolo_text(strings, Str::map_cancel)))
    {
        config->combo_editing = false;
    }
}

static void open_combo_editor(CoreConfigWindow* config, const InputCombo* combo)
{
    config->combo_editing = true;
    config->combo_edit_id = combo != nullptr ? combo->id : std::string();
    std::snprintf(config->combo_edit_name, sizeof(config->combo_edit_name), "%s", combo != nullptr ? combo->name.c_str() : "");
    config->combo_edit_buttons = combo != nullptr ? combo->buttons : 0;
}

// The combo editor as a modal over the quick config (B / Escape = CANCEL).
static void draw_combo_modal(CoreConfigWindow* config, const CoreConfigContext* context, const std::vector<RemapControl>& controls,
                             float label_width)
{
    char title[128] = {};
    std::snprintf(title, sizeof(title), "%s###combo_editor", piccolo_text(context->strings, Str::remap_combos));
    if (config->combo_editing && !ImGui::IsPopupOpen(title))
    {
        ImGui::OpenPopup(title);
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(static_cast<int>(viewport->Size.x * 0.45f)), 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            config->combo_editing = false;   // (B on a gamepad sends Escape while a popup is up)
        }
        else
        {
            draw_combo_editor(config, context, controls, label_width);
        }
        if (!config->combo_editing)
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Text cut short with "…" to fit `width` (all of it in the tooltip).
static void text_fit(const char* text, float width)
{
    if (ImGui::CalcTextSize(text).x <= width)
    {
        ImGui::TextUnformatted(text);
        return;
    }
    const char* ellipsis = "\xE2\x80\xA6";
    std::string cut = text;
    while (!cut.empty() && ImGui::CalcTextSize((cut + ellipsis).c_str()).x > width)
    {
        cut.pop_back();
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0u) == 0x80u)
        {
            cut.pop_back();   // the rest of a UTF-8 character
        }
    }
    ImGui::TextUnformatted((cut + ellipsis).c_str());
    retro_ui_item_tooltip(text);
}

// A macro as shown: its name, else its steps ("DOWN, DOWN + RIGHT, RIGHT + A").
static std::string macro_label(const InputMacro* macro, const Localization* strings)
{
    if (!macro->name.empty())
    {
        return macro->name;
    }
    std::string text;
    for (size_t s = 0; s < macro->steps.size(); s++)
    {
        if (macro->steps[s].buttons == 0)
        {
            continue;
        }
        std::string step;
        for (uint32_t b = 0; b < INPUT_PAD_BUTTON_COUNT; b++)
        {
            if ((macro->steps[s].buttons & (1u << b)) != 0)
            {
                step += step.empty() ? "" : " + ";
                step += piccolo_text(strings, PAD_LABELS[b]);
            }
        }
        text += text.empty() ? "" : ", ";
        text += step;
    }
    return text;
}

static void open_macro_editor(CoreConfigWindow* config, const InputMacro* macro)
{
    config->macro_editing = true;
    config->macro_edit_id = macro != nullptr ? macro->id : std::string();
    std::snprintf(config->macro_edit_name, sizeof(config->macro_edit_name), "%s", macro != nullptr ? macro->name.c_str() : "");
    config->macro_edit_steps = macro != nullptr ? macro->steps : std::vector<InputMacroStep>();
    config->macro_edit_repeat = macro != nullptr && macro->repeat;
    config->macro_edit_selected = -1;
}

constexpr uint64_t MACRO_GET_READY_MS = 3000;
constexpr uint64_t MACRO_IDLE_STOP_MS = 1000;   // nothing held this long after a press: done
constexpr uint64_t MACRO_RECORD_MAX_MS = 10000;
constexpr uint16_t MACRO_FRAMES_MAX = 9999;

// The recording as steps (frames at the core's rate), replacing the macro's.
static void macro_record_finish(CoreConfigWindow* config, const CoreConfigContext* context, uint64_t now_ms)
{
    config->macro_record = 0;
    context->input->suspended = false;
    std::vector<uint64_t>* times = &config->macro_record_times;
    std::vector<uint16_t>* buttons = &config->macro_record_buttons;
    if (times->empty())
    {
        return;   // nothing was pressed
    }
    times->push_back(now_ms);   // the end of the last change
    buttons->push_back(0);
    double fps = context->host != nullptr && context->host->av_info.timing.fps > 1.0 ? context->host->av_info.timing.fps : 60.0;
    std::vector<InputMacroStep> steps;
    for (size_t i = 0; i + 1 < times->size(); i++)
    {
        uint64_t from = static_cast<uint64_t>(static_cast<double>((*times)[i] - (*times)[0]) * fps / 1000.0 + 0.5);
        uint64_t to = static_cast<uint64_t>(static_cast<double>((*times)[i + 1] - (*times)[0]) * fps / 1000.0 + 0.5);
        if (to <= from)
        {
            continue;   // shorter than a frame
        }
        uint64_t frames = to - from;
        if (!steps.empty() && steps.back().buttons == (*buttons)[i])
        {
            frames += steps.back().frames;
            steps.pop_back();
        }
        InputMacroStep step = {};
        step.buttons = (*buttons)[i];
        step.frames = static_cast<uint16_t>(frames < MACRO_FRAMES_MAX ? frames : MACRO_FRAMES_MAX);
        steps.push_back(step);
    }
    while (!steps.empty() && steps.back().buttons == 0)
    {
        steps.pop_back();   // the wait at the end
    }
    config->macro_edit_steps = steps;
    config->macro_edit_selected = -1;
}

static void macro_record_cancel(CoreConfigWindow* config, const CoreConfigContext* context)
{
    if (config->macro_record != 0)
    {
        config->macro_record = 0;
        context->input->suspended = false;
    }
}

// The recorder, a frame: GET READY (the router suspended, so the presses
// only go here), then the controller's RetroPad changes until it rests.
static void macro_record_run(CoreConfigWindow* config, const CoreConfigContext* context)
{
    if (config->macro_record == 0)
    {
        return;
    }
    config->macro_record_seen = true;
    uint64_t now = context->now_ms;
    if (config->macro_record == 1)
    {
        if (now - config->macro_record_ms >= MACRO_GET_READY_MS)
        {
            config->macro_record = 2;
            config->macro_record_ms = now;
        }
        return;
    }
    uint16_t held = input_controller_pad_buttons(context->input, config->remap_controller, config->remap_port);
    uint16_t last = config->macro_record_buttons.empty() ? 0 : config->macro_record_buttons.back();
    if (held != last)
    {
        config->macro_record_times.push_back(now);   // (the first press starts it)
        config->macro_record_buttons.push_back(held);
        config->macro_record_ms = now;
    }
    bool started = !config->macro_record_times.empty();
    if ((started && held == 0 && now - config->macro_record_ms >= MACRO_IDLE_STOP_MS) ||
        (started && now - config->macro_record_times[0] >= MACRO_RECORD_MAX_MS) ||
        (!started && now - config->macro_record_ms >= MACRO_RECORD_MAX_MS))
    {
        macro_record_finish(config, context, held == 0 ? config->macro_record_ms : now);
    }
}

// A frame count field (1 .. 9999).
static bool frames_field(const char* id, int32_t* value, float width)
{
    ImGui::SetNextItemWidth(width);
    retro_ui_push_field_text();
    bool changed = ImGui::InputInt(id, value, 1, 10);
    retro_ui_pop_field_text();
    *value = *value < 1 ? 1 : (*value > MACRO_FRAMES_MAX ? MACRO_FRAMES_MAX : *value);
    return changed;
}

// Making or changing a macro (in its modal): NAME, REPEAT WHILE HELD, its
// steps (frames, EDIT for its buttons, move, remove), + STEP, + PAUSE,
// RECORD, CHANGE STEPS / PAUSES TO.
static void draw_macro_editor(CoreConfigWindow* config, const CoreConfigContext* context, const std::vector<RemapControl>& controls,
                              float label_width)
{
    InputSystem* input = context->input;
    const Localization* strings = context->strings;
    std::vector<InputMacroStep>* steps = &config->macro_edit_steps;
    float frames_width = ImGui::CalcTextSize("9999").x + ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().FramePadding.x * 4.0f;
    macro_record_run(config, context);
    if (config->macro_record != 0)
    {
        // Recording: only what it's doing, and STOP.
        char text[160] = {};
        if (config->macro_record == 1)
        {
            uint64_t left = (MACRO_GET_READY_MS - (context->now_ms - config->macro_record_ms) + 999) / 1000;
            std::snprintf(text, sizeof(text), "%s %u", piccolo_text(strings, Str::remap_macro_get_ready), static_cast<unsigned>(left));
        }
        else
        {
            std::snprintf(text, sizeof(text), "%s (%s)", piccolo_text(strings, Str::remap_macro_recording),
                          input_controller_label(input, config->remap_controller).c_str());
        }
        retro_ui_push_section_text();
        ImGui::TextUnformatted(text);
        retro_ui_pop_section_text();
        if (retro_ui_button(piccolo_text(strings, Str::remap_macro_stop)))
        {
            if (config->macro_record == 2)
            {
                macro_record_finish(config, context, context->now_ms);
            }
            else
            {
                macro_record_cancel(config, context);
            }
        }
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(piccolo_text(strings, Str::remap_combo_name));
    ImGui::SameLine(label_width);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    retro_ui_push_field_text();
    ImGui::InputTextWithHint("##macro_name", piccolo_text(strings, Str::remap_combo_name_hint), config->macro_edit_name,
                             sizeof(config->macro_edit_name));
    retro_ui_pop_field_text();
    retro_ui_checkbox(piccolo_text(strings, Str::remap_macro_repeat), &config->macro_edit_repeat);
    retro_ui_item_tooltip(piccolo_text(strings, Str::remap_macro_repeat_help));

    retro_ui_separator_text(piccolo_text(strings, Str::remap_macro_steps));
    const char* edit = piccolo_text(strings, Str::remap_combo_edit);
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float padding = ImGui::GetStyle().FramePadding.x * 2.0f;
    float row_buttons = frames_width + ImGui::CalcTextSize(edit).x + ImGui::CalcTextSize("X").x + padding * 2.0f +
                        ImGui::GetFrameHeight() * 2.0f + spacing * 4.0f;
    int32_t remove = -1;
    int32_t move_from = -1;
    int32_t move_to = -1;
    for (size_t i = 0; i < steps->size(); i++)
    {
        InputMacroStep* step = &(*steps)[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        std::string text;
        for (uint32_t b = 0; b < INPUT_PAD_BUTTON_COUNT; b++)
        {
            if ((step->buttons & (1u << b)) != 0)
            {
                text += text.empty() ? "" : " + ";
                text += piccolo_text(strings, PAD_LABELS[b]);
            }
        }
        bool selected = config->macro_edit_selected == static_cast<int32_t>(i);
        if (selected)
        {
            retro_ui_push_section_text();
        }
        ImGui::Text("%u  %s", static_cast<unsigned>(i + 1), text.empty() ? piccolo_text(strings, Str::remap_macro_pause) : text.c_str());
        if (selected)
        {
            retro_ui_pop_section_text();
        }
        float x = ImGui::GetWindowContentRegionMax().x - row_buttons;
        ImGui::SameLine(x > ImGui::GetCursorPosX() ? x : 0.0f);
        int32_t frames = step->frames;
        frames_field("##frames", &frames, frames_width);
        retro_ui_item_tooltip(piccolo_text(strings, Str::remap_macro_frames_help));
        step->frames = static_cast<uint16_t>(frames);
        ImGui::SameLine();
        if (retro_ui_button(edit))
        {
            config->macro_edit_selected = selected ? -1 : static_cast<int32_t>(i);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(i == 0);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up))
        {
            move_from = static_cast<int32_t>(i);
            move_to = static_cast<int32_t>(i) - 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i + 1 >= steps->size());
        if (ImGui::ArrowButton("##down", ImGuiDir_Down))
        {
            move_from = static_cast<int32_t>(i);
            move_to = static_cast<int32_t>(i) + 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (retro_ui_button("X"))
        {
            remove = static_cast<int32_t>(i);
        }
        if (selected)
        {
            // Its buttons (the game's names), indented under it.
            ImGui::Indent(label_width * 0.5f);
            for (uint32_t b = 0; b < INPUT_PAD_BUTTON_COUNT; b++)
            {
                ImGui::PushID(static_cast<int>(100 + b));
                bool on = (step->buttons & (1u << b)) != 0;
                if (retro_ui_checkbox(button_label(b, controls, strings).c_str(), &on))
                {
                    step->buttons = static_cast<uint16_t>(on ? (step->buttons | (1u << b)) : (step->buttons & ~(1u << b)));
                }
                ImGui::PopID();
            }
            ImGui::Unindent(label_width * 0.5f);
        }
        ImGui::PopID();
    }
    if (move_from >= 0)
    {
        InputMacroStep moved = (*steps)[static_cast<size_t>(move_from)];
        (*steps)[static_cast<size_t>(move_from)] = (*steps)[static_cast<size_t>(move_to)];
        (*steps)[static_cast<size_t>(move_to)] = moved;
        if (config->macro_edit_selected == move_from)
        {
            config->macro_edit_selected = move_to;
        }
        else if (config->macro_edit_selected == move_to)
        {
            config->macro_edit_selected = move_from;
        }
    }
    if (remove >= 0)
    {
        steps->erase(steps->begin() + remove);
        config->macro_edit_selected = config->macro_edit_selected == remove ? -1
                                    : config->macro_edit_selected > remove ? config->macro_edit_selected - 1
                                                                          : config->macro_edit_selected;
    }
    if (retro_ui_button(piccolo_text(strings, Str::remap_macro_add_step)))
    {
        InputMacroStep step = {};
        step.frames = static_cast<uint16_t>(config->macro_change_steps);
        steps->push_back(step);
        config->macro_edit_selected = static_cast<int32_t>(steps->size() - 1);   // its buttons next
    }
    ImGui::SameLine();
    if (retro_ui_button(piccolo_text(strings, Str::remap_macro_add_pause)))
    {
        InputMacroStep pause = {};
        pause.frames = static_cast<uint16_t>(config->macro_change_pauses);
        steps->push_back(pause);
    }
    ImGui::SameLine();
    if (retro_ui_button(piccolo_text(strings, Str::remap_macro_record)))
    {
        config->macro_record = 1;
        config->macro_record_ms = context->now_ms;
        config->macro_record_seen = true;
        config->macro_record_times.clear();
        config->macro_record_buttons.clear();
        input->suspended = true;
    }
    retro_ui_item_tooltip(piccolo_text(strings, Str::remap_macro_record_help));

    // Every step, or every pause, to one length.
    const char* apply = piccolo_text(strings, Str::remap_macro_apply);
    for (int kind = 0; kind < 2; kind++)
    {
        ImGui::PushID(200 + kind);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(piccolo_text(strings, kind == 0 ? Str::remap_macro_change_steps : Str::remap_macro_change_pauses));
        // (Its field under the steps' frames.)
        float field_x = ImGui::GetWindowContentRegionMax().x - row_buttons;
        ImGui::SameLine(field_x > ImGui::GetCursorPosX() ? field_x : 0.0f);
        int32_t* value = kind == 0 ? &config->macro_change_steps : &config->macro_change_pauses;
        frames_field("##to", value, frames_width);
        ImGui::SameLine();
        if (retro_ui_button(apply))
        {
            for (size_t i = 0; i < steps->size(); i++)
            {
                bool pause = (*steps)[i].buttons == 0;
                if (pause == (kind == 1))
                {
                    (*steps)[i].frames = static_cast<uint16_t>(*value);
                }
            }
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    bool playable = false;
    for (size_t i = 0; i < steps->size(); i++)
    {
        playable = playable || (*steps)[i].buttons != 0;
    }
    ImGui::BeginDisabled(!playable);
    if (retro_ui_button(piccolo_text(strings, Str::remap_combo_save)))
    {
        std::string name = config->macro_edit_name;
        size_t first = name.find_first_not_of(' ');
        size_t last = name.find_last_not_of(' ');
        name = first != std::string::npos ? name.substr(first, last - first + 1) : std::string();
        InputMacro* macro = nullptr;
        for (size_t m = 0; m < input->macros.size() && !config->macro_edit_id.empty(); m++)
        {
            if (input->macros[m].core == input->core_name && input->macros[m].id == config->macro_edit_id)
            {
                macro = &input->macros[m];
            }
        }
        if (macro == nullptr)
        {
            macro = input_macro_add(input, input->core_name.c_str(), name);
        }
        macro->name = name;
        macro->steps = *steps;
        macro->repeat = config->macro_edit_repeat;
        config->macro_editing = false;
        config->input_dirty = true;
    }
    ImGui::EndDisabled();
    if (!config->macro_edit_id.empty())
    {
        ImGui::SameLine();
        if (retro_ui_button(piccolo_text(strings, Str::remap_combo_delete)))
        {
            input_macro_remove(input, input->core_name.c_str(), config->macro_edit_id);
            config->macro_editing = false;
            config->input_dirty = true;
        }
        retro_ui_item_tooltip(piccolo_text(strings, Str::remap_macro_delete_help));
    }
    ImGui::SameLine();
    if (retro_ui_button(piccolo_text(strings, Str::map_cancel)))
    {
        config->macro_editing = false;
    }
}

// The macro editor as a modal (B / Escape = CANCEL; while recording, it stops that only).
static void draw_macro_modal(CoreConfigWindow* config, const CoreConfigContext* context, const std::vector<RemapControl>& controls,
                             float label_width)
{
    char title[128] = {};
    std::snprintf(title, sizeof(title), "%s###macro_editor", piccolo_text(context->strings, Str::remap_macros));
    if (config->macro_editing && !ImGui::IsPopupOpen(title))
    {
        ImGui::OpenPopup(title);
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(static_cast<int>(viewport->Size.x * 0.55f)), 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            if (config->macro_record != 0)
            {
                macro_record_cancel(config, context);
            }
            else
            {
                config->macro_editing = false;
            }
        }
        else
        {
            draw_macro_editor(config, context, controls, label_width);
        }
        if (!config->macro_editing)
        {
            macro_record_cancel(config, context);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// COMBOS / MACROS (`macros`): the core's, each bound to inputs in this
// remap like a control (+ / SET / CLEAR), EDIT to change it; + NEW.
static void draw_combo_rows(CoreConfigWindow* config, const CoreConfigContext* context, bool macros, const std::vector<RemapControl>& controls,
                            const InputDevice* pad, const InputLayout* pad_layout, const InputProfile* pad_profile,
                            float label_width)
{
    InputSystem* input = context->input;
    const Localization* strings = context->strings;
    const InputCoreProfile* active = input_active_core_profile(input, config->remap_controller, config->remap_port);
    const char* set = piccolo_text(strings, Str::input_set);
    const char* clear = piccolo_text(strings, Str::input_clear);
    const char* edit = piccolo_text(strings, Str::remap_combo_edit);
    const char* add = "+";
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float padding = ImGui::GetStyle().FramePadding.x * 2.0f;
    float row_buttons = ImGui::CalcTextSize(set).x + ImGui::CalcTextSize(clear).x + padding * 2.0f + spacing;
    float first_buttons = row_buttons + ImGui::CalcTextSize(add).x + ImGui::CalcTextSize(edit).x + padding * 2.0f + spacing * 2.0f;
    // (A copy of the ids: EDIT > DELETE changes the list.)
    std::vector<std::string> ids;
    for (size_t c = 0; !macros && c < input->combos.size(); c++)
    {
        if (input->combos[c].core == input->core_name)
        {
            ids.push_back(input->combos[c].id);
        }
    }
    for (size_t m = 0; macros && m < input->macros.size(); m++)
    {
        if (input->macros[m].core == input->core_name)
        {
            ids.push_back(input->macros[m].id);
        }
    }
    for (size_t c = 0; c < ids.size(); c++)
    {
        const InputCombo* combo = input_combo(input, input->core_name.c_str(), ids[c]);
        const InputMacro* macro = input_macro(input, input->core_name.c_str(), ids[c]);
        if (combo == nullptr && macro == nullptr)
        {
            continue;
        }
        const std::string id = ids[c];
        const InputComboBinding* binding = combo_binding_of(active, id);
        InputSource none[INPUT_SOURCES_PER_ACTION] = {};
        const InputSource* sources = binding != nullptr ? binding->sources : none;
        bool waiting = config->capturing && config->capture_combo == id;
        uint32_t count = input_sources_count(sources);
        uint32_t rows = count > 0 ? count : 1;
        if (waiting && config->capture_row >= rows)
        {
            rows = config->capture_row + 1;
        }
        std::string label = combo != nullptr ? combo_label(combo, controls, strings) : macro_label(macro, strings);
        ImGui::PushID(("combo/" + id).c_str());
        for (uint32_t r = 0; r < rows; r++)
        {
            ImGui::PushID(static_cast<int>(r));
            ImGui::AlignTextToFramePadding();
            if (waiting && r == 0)
            {
                retro_ui_push_section_text();
            }
            text_fit(r == 0 ? label.c_str() : "", label_width - ImGui::GetStyle().ItemSpacing.x);
            if (waiting && r == 0)
            {
                retro_ui_pop_section_text();
            }
            float buttons_x = ImGui::GetWindowContentRegionMax().x - (r == 0 ? first_buttons : row_buttons);
            ImGui::SameLine(label_width);
            if (waiting && config->capture_row == r)
            {
                uint64_t elapsed = context->now_ms - config->capture_started_ms;
                uint64_t total = input->settings.press_ms;
                uint64_t left = elapsed < total ? (total - elapsed + 999) / 1000 : 0;
                input_window_draw_waiting(pad != nullptr && pad->kind == InputDeviceKind::keyboard, left,
                                          input_capture_hold_progress(input, context->now_ms), strings);
                ImGui::SetScrollHereY(0.5f);
            }
            else
            {
                InputSource one[INPUT_SOURCES_PER_ACTION] = {};
                one[0] = r < count ? sources[r] : InputSource{};
                input_window_draw_sources(context->sdl, pad, pad_layout, pad_profile, one,
                                          buttons_x - spacing - ImGui::GetCursorPosX(), strings);
            }
            ImGui::SameLine(buttons_x > ImGui::GetCursorPosX() ? buttons_x : 0.0f);
            ImGui::BeginDisabled(config->capturing);
            if (r == 0)
            {
                if (retro_ui_button(edit))
                {
                    if (combo != nullptr)
                    {
                        open_combo_editor(config, combo);
                    }
                    else
                    {
                        open_macro_editor(config, macro);
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(count == 0 || count >= INPUT_SOURCES_PER_ACTION);
                if (retro_ui_button(add))
                {
                    remap_start_combo_capture(config, context, id, count);
                }
                retro_ui_item_tooltip(piccolo_text(strings, Str::input_add_help));
                ImGui::EndDisabled();
                ImGui::SameLine();
            }
            if (retro_ui_button(set))
            {
                remap_start_combo_capture(config, context, id, r < count ? r : count);
            }
            if (ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible))
            {
                config->pad_combo = id;
                config->pad_row = r;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(r >= count);
            if (retro_ui_button(clear))
            {
                InputComboBinding* changed = remap_combo_binding(config, context, id);
                input_sources_remove(changed->sources, r);
                if (input_sources_count(changed->sources) == 0)
                {
                    // Nothing on it any more: unbound.
                    InputCoreProfile* profile = remap_profile(config, context);
                    for (size_t k = 0; k < profile->combos.size(); k++)
                    {
                        if (profile->combos[k].combo == id)
                        {
                            profile->combos.erase(profile->combos.begin() + static_cast<long>(k));
                            break;
                        }
                    }
                }
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::BeginDisabled(config->capturing);
    if (retro_ui_button(piccolo_text(strings, macros ? Str::remap_macro_new : Str::remap_combo_new)))
    {
        if (macros)
        {
            open_macro_editor(config, nullptr);
        }
        else
        {
            open_combo_editor(config, nullptr);
        }
    }
    ImGui::EndDisabled();
}


// One of the remap's sections: a header that opens it, closing the one
// open before (one at a time). True while it is open.
static bool remap_section(CoreConfigWindow* config, int32_t index, const char* label, const char* summary, Str help,
                          const Localization* strings, uint32_t label_chars)
{
    // The label padded to `label_chars` characters (a fixed-width font), so
    // the counts after the labels line up.
    std::string padded = label;
    uint32_t chars = 0;
    for (size_t c = 0; c < padded.size(); c++)
    {
        chars += (static_cast<unsigned char>(padded[c]) & 0xC0u) != 0x80u ? 1u : 0u;
    }
    for (; chars < label_chars; chars++)
    {
        padded += ' ';
    }
    char text[256] = {};
    std::snprintf(text, sizeof(text), summary[0] != '\0' ? "%s (%s)###section%d" : "%s%s###section%d", padded.c_str(), summary,
                  static_cast<int>(index));
    ImGui::SetNextItemOpen(config->remap_section == index, ImGuiCond_Always);
    bool open = retro_ui_collapsing_header(text, 0);
    retro_ui_item_tooltip(piccolo_text(strings, help));
    if (open && config->remap_section != index)
    {
        config->remap_section = index;
    }
    else if (!open && config->remap_section == index)
    {
        config->remap_section = -1;
    }
    return open;
}

// What the remap draws its rows with.
struct RemapRowsContext
{
    InputBindingSet     effective;
    const InputDevice*  pad = nullptr;
    const InputLayout*  layout = nullptr;
    const InputProfile* profile = nullptr;
    float               label_width = 0.0f;
};

// Rows of controls: each with its inputs (one a row), + / SET / CLEAR, and
// for a stick direction the percent of a key or button on it. A row with
// turbo says so.
static void draw_action_rows(CoreConfigWindow* config, const CoreConfigContext* context, const std::vector<RemapControl>& controls,
                             const RemapRowsContext* rows_context)
{
    InputSystem* input = context->input;
    const Localization* strings = context->strings;
    const InputDevice* pad = rows_context->pad;
    float label_width = rows_context->label_width;
    const char* set = piccolo_text(strings, Str::input_set);
    const char* clear = piccolo_text(strings, Str::input_clear);
    const char* add = "+";
    const char* turbo_note = piccolo_text(strings, Str::remap_turbo);
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float padding = ImGui::GetStyle().FramePadding.x * 2.0f;
    float row_buttons = ImGui::CalcTextSize(set).x + ImGui::CalcTextSize(clear).x + padding * 2.0f + spacing;
    float first_buttons = row_buttons + ImGui::CalcTextSize(add).x + padding + spacing;
    float percent_width = ImGui::CalcTextSize("100%").x + padding + ImGui::GetFrameHeight();
    for (size_t i = 0; i < controls.size(); i++)
    {
        InputAction action = controls[i].action;
        uint32_t index = static_cast<uint32_t>(action);
        const InputSource* sources = rows_context->effective.sources[index];
        bool waiting = config->capturing && config->capture_combo.empty() && config->capture_action == action;
        // A stick direction: a key or button on it pushes it a set amount (the percent).
        bool analog = index >= static_cast<uint32_t>(InputAction::pad_left_x_plus) &&
                      index <= static_cast<uint32_t>(InputAction::pad_right_y_minus);
        uint32_t count = input_sources_count(sources);
        uint32_t rows = count > 0 ? count : 1;
        if (waiting && config->capture_row >= rows)
        {
            rows = config->capture_row + 1;   // + : the new row, waiting
        }
        ImGui::PushID(static_cast<int>(index));
        for (uint32_t r = 0; r < rows; r++)
        {
            ImGui::PushID(static_cast<int>(r));
            ImGui::AlignTextToFramePadding();
            if (r == 0)
            {
                if (waiting)
                {
                    retro_ui_push_section_text();   // the one being bound stands out
                }
                text_fit(controls[i].label.c_str(), label_width - spacing);
                if (waiting)
                {
                    retro_ui_pop_section_text();
                }
            }
            else
            {
                ImGui::TextUnformatted("");   // more sources of the same control: no label
            }
            float buttons_x = ImGui::GetWindowContentRegionMax().x - (r == 0 ? first_buttons : row_buttons);
            InputSource source = r < count ? sources[r] : InputSource{};
            bool digital = source.type == InputSourceType::key || source.type == InputSourceType::button ||
                           source.type == InputSourceType::mouse;
            bool being_set = waiting && config->capture_row == r;
            bool show_percent = analog && digital && !being_set;
            bool show_turbo = source.turbo > 0 && !being_set;
            float turbo_width = ImGui::CalcTextSize(turbo_note).x + ImGui::CalcTextSize("()").x;
            float text_end = buttons_x - spacing - (show_percent ? percent_width + spacing : 0.0f) -
                             (show_turbo ? turbo_width + spacing : 0.0f);
            ImGui::SameLine(label_width);
            if (being_set)
            {
                uint64_t elapsed = context->now_ms - config->capture_started_ms;
                uint64_t total = input->settings.press_ms + (config->bind_all ? REMAP_COOL_OFF_MS : 0);
                uint64_t left = elapsed < total ? (total - elapsed + 999) / 1000 : 0;
                input_window_draw_waiting(pad != nullptr && pad->kind == InputDeviceKind::keyboard, left,
                                          input_capture_hold_progress(input, context->now_ms), strings);
                ImGui::SetScrollHereY(0.5f);   // BIND ALL: keep it in view
            }
            else
            {
                InputSource one[INPUT_SOURCES_PER_ACTION] = {};
                one[0] = source;
                input_window_draw_sources(context->sdl, pad, rows_context->layout, rows_context->profile, one,
                                          text_end - ImGui::GetCursorPosX(), strings);
            }
            if (show_turbo)
            {
                ImGui::SameLine(text_end + spacing);
                ImGui::TextDisabled("(%s)", turbo_note);   // (TURBO: the TURBO section)
            }
            if (show_percent)
            {
                // How far it pushes the stick (D-pad right at 20 %: walking, not running).
                char current[16] = {};
                std::snprintf(current, sizeof(current), "%u%%", static_cast<unsigned>(source.percent));
                ImGui::SameLine(buttons_x - spacing - percent_width);
                ImGui::SetNextItemWidth(percent_width);
                ImGui::BeginDisabled(config->capturing);
                if (retro_ui_begin_combo("##percent", current))
                {
                    for (uint32_t p = 10; p <= 100; p += 10)
                    {
                        char item[16] = {};
                        std::snprintf(item, sizeof(item), "%u%%", static_cast<unsigned>(p));
                        if (ImGui::Selectable(item, source.percent == p))
                        {
                            InputBindingOverride* change = remap_override(config, context, action);
                            change->sources[r].percent = static_cast<uint8_t>(p);
                        }
                    }
                    ImGui::EndCombo();
                }
                retro_ui_item_tooltip(piccolo_text(strings, Str::remap_percent_help));
                ImGui::EndDisabled();
            }
            ImGui::SameLine(buttons_x > ImGui::GetCursorPosX() ? buttons_x : 0.0f);
            ImGui::BeginDisabled(config->capturing);
            if (r == 0)
            {
                ImGui::BeginDisabled(count == 0 || count >= INPUT_SOURCES_PER_ACTION);
                if (retro_ui_button(add))
                {
                    config->bind_all = false;
                    remap_start_capture(config, context, action, count);   // a new row
                }
                retro_ui_item_tooltip(piccolo_text(strings, Str::input_add_help));
                ImGui::EndDisabled();
                ImGui::SameLine();
            }
            if (retro_ui_button(set))
            {
                config->bind_all = false;
                remap_start_capture(config, context, action, r < count ? r : count);
            }
            // The pad shows the row under the mouse, or the one in focus while
            // navigating with a gamepad or keys (a click also focuses, but that
            // focus shouldn't keep the row lit once its SET is over).
            if (ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible))
            {
                config->pad_action = action;   // the pad shows it on the next frame
                config->pad_row = r;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(r >= count);
            if (retro_ui_button(clear))
            {
                input_sources_remove(remap_override(config, context, action)->sources, r);
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

// TURBO: each key or button row of a RetroPad button, with its TURBO box.
static void draw_turbo_rows(CoreConfigWindow* config, const CoreConfigContext* context, const std::vector<RemapControl>& controls,
                            const RemapRowsContext* rows_context)
{
    const Localization* strings = context->strings;
    const char* turbo_label = piccolo_text(strings, Str::remap_turbo);
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float box_width = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(turbo_label).x;
    for (size_t i = 0; i < controls.size(); i++)
    {
        InputAction action = controls[i].action;
        uint32_t index = static_cast<uint32_t>(action);
        if (index >= INPUT_PAD_BUTTON_COUNT)
        {
            continue;   // sticks: no turbo
        }
        const InputSource* sources = rows_context->effective.sources[index];
        uint32_t count = input_sources_count(sources);
        ImGui::PushID(static_cast<int>(1000 + index));
        for (uint32_t r = 0; r < count; r++)
        {
            InputSource source = sources[r];
            bool digital = source.type == InputSourceType::key || source.type == InputSourceType::button ||
                           source.type == InputSourceType::mouse;
            if (!digital)
            {
                continue;
            }
            ImGui::PushID(static_cast<int>(r));
            ImGui::AlignTextToFramePadding();
            text_fit(r == 0 ? controls[i].label.c_str() : "", rows_context->label_width - spacing);
            float box_x = ImGui::GetWindowContentRegionMax().x - box_width;
            ImGui::SameLine(rows_context->label_width);
            InputSource one[INPUT_SOURCES_PER_ACTION] = {};
            one[0] = source;
            input_window_draw_sources(context->sdl, rows_context->pad, rows_context->layout, rows_context->profile, one,
                                      box_x - spacing - ImGui::GetCursorPosX(), strings);
            ImGui::SameLine(box_x);
            ImGui::BeginDisabled(config->capturing);
            bool turbo = source.turbo > 0;
            if (retro_ui_checkbox(turbo_label, &turbo))
            {
                InputBindingOverride* change = remap_override(config, context, action);
                change->sources[r].turbo = turbo ? INPUT_TURBO_DEFAULT : 0;
            }
            retro_ui_item_tooltip(piccolo_text(strings, Str::remap_turbo_help));
            if (ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible))
            {
                config->pad_action = action;
                config->pad_row = r;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

// The core's name for a lightgun button on the port (its descriptor), or `fallback`.
static std::string lightgun_label(const LibretroHost* host, uint32_t port, unsigned id, const char* fallback)
{
    for (uint32_t d = 0; host != nullptr && d < host->input_info.descriptor_count; d++)
    {
        const LibretroInputDescriptor* descriptor = &host->input_info.descriptors[d];
        if (descriptor->port == port && (descriptor->device & RETRO_DEVICE_MASK) == RETRO_DEVICE_LIGHTGUN &&
            descriptor->id == id && descriptor->description[0] != '\0')
        {
            return descriptor->description;
        }
    }
    return fallback;
}

// A mouse or lightgun port's controls (its own buttons, not the RetroPad's).
static void device_controls(const CoreConfigContext* context, uint32_t port, bool lightgun, std::vector<RemapControl>* controls)
{
    const Localization* strings = context->strings;
    controls->clear();
    static const InputAction AIM[4] = {InputAction::cursor_up, InputAction::cursor_down, InputAction::cursor_left,
                                       InputAction::cursor_right};
    static const Str AIM_LABELS[4] = {Str::input_action_cursor_up, Str::input_action_cursor_down, Str::input_action_cursor_left,
                                      Str::input_action_cursor_right};
    for (uint32_t a = 0; a < 4; a++)
    {
        RemapControl control = {AIM[a], piccolo_text(strings, AIM_LABELS[a])};
        controls->push_back(control);
    }
    if (lightgun)
    {
        struct Gun { InputAction action; unsigned id; Str label; };
        static const Gun GUN[7] = {
            {InputAction::mouse_left, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER, Str::input_action_mouse_left},
            {InputAction::gun_reload, RETRO_DEVICE_ID_LIGHTGUN_RELOAD, Str::input_action_gun_reload},
            {InputAction::gun_aux_a, RETRO_DEVICE_ID_LIGHTGUN_AUX_A, Str::input_action_gun_aux_a},
            {InputAction::gun_aux_b, RETRO_DEVICE_ID_LIGHTGUN_AUX_B, Str::input_action_gun_aux_b},
            {InputAction::gun_aux_c, RETRO_DEVICE_ID_LIGHTGUN_AUX_C, Str::input_action_gun_aux_c},
            {InputAction::gun_start, RETRO_DEVICE_ID_LIGHTGUN_START, Str::input_action_gun_start},
            {InputAction::gun_select, RETRO_DEVICE_ID_LIGHTGUN_SELECT, Str::input_action_gun_select},
        };
        for (uint32_t g = 0; g < 7; g++)
        {
            RemapControl control = {GUN[g].action, lightgun_label(context->host, port, GUN[g].id, piccolo_text(strings, GUN[g].label))};
            controls->push_back(control);
        }
        return;
    }
    static const InputAction MOUSE[5] = {InputAction::mouse_left, InputAction::mouse_right, InputAction::mouse_middle,
                                         InputAction::mouse_wheel_up, InputAction::mouse_wheel_down};
    static const Str MOUSE_LABELS[5] = {Str::input_action_mouse_left, Str::input_action_mouse_right,
                                        Str::input_action_mouse_middle, Str::input_action_mouse_wheel_up,
                                        Str::input_action_mouse_wheel_down};
    for (uint32_t m = 0; m < 5; m++)
    {
        RemapControl control = {MOUSE[m], piccolo_text(strings, MOUSE_LABELS[m])};
        controls->push_back(control);
    }
}

// The remap of one port's controller for the core and the port's device:
// BACK (keeps it, not asked again), SCOPE, CORE DEVICE, then sections, one
// open at a time. A joypad port: MAPPING (BIND ALL, DEFAULT ALL, CANCEL and
// its controls), TURBO, COMBOS, MACROS. A mouse or lightgun port: only its
// own controls.
static void draw_remap(CoreConfigWindow* config, const CoreConfigContext* context, float block)
{
    InputSystem* input = context->input;
    const Localization* strings = context->strings;
    LibretroHost* host = context->host;
    if (host == nullptr || !host->game_loaded || config->remap_controller >= input->controllers.size() ||
        config->remap_port >= INPUT_PORT_COUNT)
    {
        config->remap = false;
        return;
    }
    unsigned base = host->port_devices[config->remap_port] & RETRO_DEVICE_MASK;
    bool mouse_port = base == RETRO_DEVICE_MOUSE;
    bool lightgun_port = base == RETRO_DEVICE_LIGHTGUN;
    std::vector<RemapControl> controls;   // MAPPING's (BIND ALL goes through these), or the device's
    if (mouse_port || lightgun_port)
    {
        device_controls(context, config->remap_port, lightgun_port, &controls);
    }
    else
    {
        remap_controls(context, config->remap_port, &controls);
    }
    remap_run_capture(config, context, controls);

    // <controller> -> <core device> MAPPING: what the controller is mapped to, not what it is.
    const char* device = libretro_input_controller_name(&host->input_info, config->remap_port, host->port_devices[config->remap_port]);
    char title[300] = {};
    std::snprintf(title, sizeof(title), piccolo_text(strings, Str::remap_title),
                  input_controller_label(input, config->remap_controller).c_str(), device != nullptr ? device : "RETROPAD");
    ImGui::BeginDisabled(config->capturing);
    bool back = retro_ui_button(piccolo_text(strings, Str::map_back));
    retro_ui_item_tooltip(piccolo_text(strings, Str::remap_back_help));
    ImGui::EndDisabled();
    if (back)
    {
        // Kept as it is, and seen: not asked again.
        remap_profile(config, context)->decided = true;
        config->input_dirty = true;
        config->remap = false;   // back to the ports (B on a gamepad does the same)
        config->combo_editing = false;
        config->macro_editing = false;
        return;
    }
    retro_ui_separator_text(title);
    ImGui::BeginDisabled(config->capturing || config->combo_editing);
    draw_remap_scope(config, context, 100.0f * block);
    draw_device_combo(context, config->remap_port, 100.0f * block);
    ImGui::EndDisabled();

    RemapRowsContext rows_context = {};
    input_effective_bindings(input, config->remap_controller, config->remap_port, &rows_context.effective);
    const InputDevice* pad = input_devices_find_const(&input->devices, input->controllers[config->remap_controller].device_id);
    rows_context.pad = pad;
    rows_context.layout = input_window_layout(input, context->layouts, pad);
    rows_context.profile = input_profile(input, input_controller_profile_id(input, config->remap_controller));
    rows_context.label_width = 110.0f * block;
    // A gamepad's pad, floating in the screen's corner (the rows keep the
    // window's width), with where the control being bound (or the row in
    // focus) is now.
    bool show_pad = pad != nullptr && pad->kind == InputDeviceKind::gamepad;
    InputAction shown = config->capturing ? config->capture_action : config->pad_action;
    std::string shown_combo = config->capturing ? config->capture_combo : config->pad_combo;
    InputMapControl lit = InputMapControl::count;
    bool minus = false;
    uint32_t shown_index = static_cast<uint32_t>(shown);
    InputSource held = {};
    if (config->capturing && input_capture_candidate(input, &held))
    {
        // Being held to be bound: that one (let go sooner and it isn't taken).
        lit = input_map_control_for_source(held);
        minus = held.type == InputSourceType::axis_minus;
    }
    uint32_t shown_row = config->capturing ? config->capture_row : config->pad_row;
    if (lit == InputMapControl::count && !shown_combo.empty() && shown_row < INPUT_SOURCES_PER_ACTION)
    {
        const InputComboBinding* binding =
            combo_binding_of(input_active_core_profile(input, config->remap_controller, config->remap_port), shown_combo);
        if (binding != nullptr)
        {
            lit = input_map_control_for_source(binding->sources[shown_row]);
            minus = binding->sources[shown_row].type == InputSourceType::axis_minus;
        }
    }
    else if (lit == InputMapControl::count && shown_index < INPUT_ACTION_COUNT && shown_row < INPUT_SOURCES_PER_ACTION)
    {
        InputSource source = rows_context.effective.sources[shown_index][shown_row];
        lit = input_map_control_for_source(source);
        minus = source.type == InputSourceType::axis_minus;
    }
    config->pad_action = InputAction::count;
    config->pad_combo.clear();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("remap_rows", ImVec2(0.0f, 0.0f));
    if (mouse_port || lightgun_port)
    {
        if (remap_section(config, 0, piccolo_text(strings, lightgun_port ? Str::remap_lightgun : Str::remap_mouse), "",
                          Str::remap_cursor_help, strings, 0))
        {
            draw_action_rows(config, context, controls, &rows_context);
        }
    }
    else
    {
        // (The sections' labels as long as the longest, so their counts line up.)
        uint32_t section_chars = 0;
        static const Str SECTIONS[4] = {Str::remap_mapping, Str::remap_turbo, Str::remap_combos, Str::remap_macros};
        for (uint32_t t = 0; t < 4; t++)
        {
            const char* text = piccolo_text(strings, SECTIONS[t]);
            uint32_t chars = 0;
            for (const char* c = text; *c != '\0'; c++)
            {
                chars += (static_cast<unsigned char>(*c) & 0xC0u) != 0x80u ? 1u : 0u;
            }
            section_chars = chars > section_chars ? chars : section_chars;
        }
        if (remap_section(config, 0, piccolo_text(strings, Str::remap_mapping), "", Str::input_bind_all_help, strings, section_chars))
        {
            ImGui::BeginDisabled(config->capturing);
            if (retro_ui_button(piccolo_text(strings, Str::input_bind_all)) && !controls.empty())
            {
                config->bind_all = true;
                remap_start_capture(config, context, controls[0].action, 0);
            }
            retro_ui_item_tooltip(piccolo_text(strings, Str::input_bind_all_help));
            ImGui::SameLine();
            if (retro_ui_button(piccolo_text(strings, Str::remap_use_retropad)))
            {
                InputCoreProfile* profile = remap_profile(config, context);
                profile->decided = true;
                profile->custom = false;
                profile->overrides.clear();
                profile->combos.clear();
                config->input_dirty = true;
            }
            retro_ui_item_tooltip(piccolo_text(strings, Str::remap_use_retropad_help));
            ImGui::EndDisabled();
            // CANCEL: stops a SET / BIND ALL and puts back what it changed.
            ImGui::SameLine();
            ImGui::BeginDisabled(!config->capturing);
            if (retro_ui_button(piccolo_text(strings, Str::input_cancel_binding)))
            {
                *remap_profile(config, context) = config->capture_backup;
                input_capture_cancel(input);
                config->capturing = false;
                config->bind_all = false;
            }
            retro_ui_item_tooltip(piccolo_text(strings, Str::input_cancel_binding_help));
            ImGui::EndDisabled();
            draw_action_rows(config, context, controls, &rows_context);
        }
        // TURBO: how many rows have it, and its button.
        uint32_t turbo_rows = 0;
        for (uint32_t b = 0; b < INPUT_PAD_BUTTON_COUNT; b++)
        {
            for (uint32_t s = 0; s < INPUT_SOURCES_PER_ACTION; s++)
            {
                turbo_rows += rows_context.effective.sources[b][s].turbo > 0 ? 1u : 0u;
            }
        }
        char summary[64] = {};
        if (turbo_rows > 0)
        {
            std::snprintf(summary, sizeof(summary), "%u", static_cast<unsigned>(turbo_rows));
        }
        if (remap_section(config, 1, piccolo_text(strings, Str::remap_turbo), summary, Str::remap_turbo_button_help, strings,
                          section_chars))
        {
            std::vector<RemapControl> turbo_button;
            RemapControl button = {InputAction::pad_turbo, piccolo_text(strings, Str::input_action_pad_turbo)};
            turbo_button.push_back(button);
            draw_action_rows(config, context, turbo_button, &rows_context);
            ImGui::Spacing();
            draw_turbo_rows(config, context, controls, &rows_context);
        }
        uint32_t combos = 0;
        uint32_t macros = 0;
        for (size_t c = 0; c < input->combos.size(); c++)
        {
            combos += input->combos[c].core == input->core_name ? 1u : 0u;
        }
        for (size_t m = 0; m < input->macros.size(); m++)
        {
            macros += input->macros[m].core == input->core_name ? 1u : 0u;
        }
        std::snprintf(summary, sizeof(summary), "%u", static_cast<unsigned>(combos));
        if (remap_section(config, 2, piccolo_text(strings, Str::remap_combos), combos > 0 ? summary : "", Str::remap_combos_help,
                          strings, section_chars))
        {
            draw_combo_rows(config, context, false, controls, pad, rows_context.layout, rows_context.profile, rows_context.label_width);
        }
        std::snprintf(summary, sizeof(summary), "%u", static_cast<unsigned>(macros));
        if (remap_section(config, 3, piccolo_text(strings, Str::remap_macros), macros > 0 ? summary : "", Str::remap_macros_help,
                          strings, section_chars))
        {
            draw_combo_rows(config, context, true, controls, pad, rows_context.layout, rows_context.profile, rows_context.label_width);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    draw_combo_modal(config, context, controls, 60.0f * block);
    draw_macro_modal(config, context, controls, 60.0f * block);
    if (show_pad)
    {
        pad_picture_draw_floating(rows_context.layout, lit, minus, 1.25f * block);
    }
}

// The buttons at the right of a row, `width` wide: where they start.
static void right_align(float width)
{
    float x = ImGui::GetWindowContentRegionMax().x - width;
    ImGui::SameLine(x > ImGui::GetCursorPosX() ? x : 0.0f);
}

static float button_width(const char* text)
{
    return ImGui::CalcTextSize(text).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

// CONTROLS: the core's ports, each a section (one open at a time; its header
// says who is on it): its CORE DEVICE, its controllers (N:N) with REMAP and
// UNASSIGN (that port only), ADD (any connected controller not on it), MOVE
// UP / MOVE DOWN (the whole port). START on a controller joins it.
static void draw_controls_tab(CoreConfigWindow* config, const CoreConfigContext* context, float block)
{
    if (config->remap)
    {
        draw_remap(config, context, block);
        return;
    }
    InputSystem* input = context->input;
    const Localization* strings = context->strings;
    const LibretroHost* host = context->host;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    bool can_remap = host != nullptr && host->game_loaded;
    const char* unassign = piccolo_text(strings, Str::input_leave);
    const char* up = piccolo_text(strings, Str::input_move_up);
    const char* down = piccolo_text(strings, Str::input_move_down);
    const char* remap = piccolo_text(strings, Str::quick_menu_remap);
    // The core's ports, as many as SETTINGS > INPUT > PORTS says (4, or per
    // core). Controllers on ports not shown stay there, unseen.
    uint32_t ports = INPUT_PORT_COUNT;
    if (context->settings != nullptr)
    {
        uint32_t core_ports = host != nullptr && host->game_loaded ? host->input_info.port_count : 0;
        ports = piccolo_settings_ports(context->settings, core_ports);
    }
    bool shown[INPUT_PORT_COUNT] = {true, true, true, true};
    float label_width = 60.0f * block;
    bool anybody = false;
    // One port open at a time: its CORE DEVICE, its controllers, ADD, MOVE UP / DOWN.
    for (uint32_t p = 0; p < ports; p++)
    {
        if (!shown[p])
        {
            continue;
        }
        anybody = anybody || !input->ports[p].empty();
        // The header: PORT n and who is on it.
        std::string who;
        for (size_t m = 0; m < input->ports[p].size(); m++)
        {
            who += m == 0 ? "" : ", ";
            who += input_controller_label(input, input->ports[p][m]);
        }
        char header[400] = {};
        std::snprintf(header, sizeof(header), "%s %u   %s###port%u", piccolo_text(strings, Str::input_port), static_cast<unsigned>(p + 1),
                      who.empty() ? piccolo_text(strings, Str::input_port_empty) : who.c_str(), static_cast<unsigned>(p));
        ImGui::SetNextItemOpen(config->ports_selected == p, ImGuiCond_Always);
        bool open = retro_ui_collapsing_header(header, 0);
        if (open && config->ports_selected != p)
        {
            config->ports_selected = p;
        }
        else if (!open && config->ports_selected == p)
        {
            config->ports_selected = UINT32_MAX;
        }
        if (!open)
        {
            continue;
        }
        ImGui::PushID(static_cast<int>(p));
        ImGui::Indent(ImGui::GetStyle().IndentSpacing);
        draw_device_combo(context, p, label_width);
        // (A copy: UNASSIGN changes the list while it is drawn.)
        std::vector<uint32_t> members = input->ports[p];
        for (size_t m = 0; m < members.size(); m++)
        {
            uint32_t controller = members[m];
            if (controller >= input->controllers.size())
            {
                continue;
            }
            ImGui::PushID(static_cast<int>(controller));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(input_controller_label(input, controller).c_str());
            right_align(button_width(unassign) + (can_remap ? button_width(remap) + spacing : 0.0f));
            if (can_remap)
            {
                if (retro_ui_button(remap))
                {
                    config->remap = true;
                    config->remap_port = p;
                    config->remap_controller = controller;
                    config->remap_section = 0;
                }
                ImGui::SameLine();
            }
            if (retro_ui_button(unassign))
            {
                input_port_remove(input, p, controller);
            }
            retro_ui_item_tooltip(piccolo_text(strings, Str::input_leave_help));
            ImGui::PopID();
        }
        // ADD: a connected controller that isn't on this port (it may be on others).
        std::vector<uint32_t> others;
        for (uint32_t c = 0; c < input->controllers.size(); c++)
        {
            const InputDevice* device = input_devices_find_const(&input->devices, input->controllers[c].device_id);
            if (device != nullptr && device->connected && !input_devices_ignored(device) && !input_port_has(input, p, c))
            {
                others.push_back(c);
            }
        }
        if (!others.empty())
        {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (retro_ui_begin_combo("##add", piccolo_text(strings, Str::input_port_add)))
            {
                for (size_t o = 0; o < others.size(); o++)
                {
                    ImGui::PushID(static_cast<int>(others[o]));
                    if (ImGui::Selectable(input_controller_label(input, others[o]).c_str(), false))
                    {
                        input_port_add(input, p, others[o]);
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        }
        // MOVE UP / MOVE DOWN: the whole port (its controllers) swaps with the one shown next to it.
        int32_t above = -1;
        int32_t below = -1;
        for (int32_t q = static_cast<int32_t>(p) - 1; q >= 0 && above < 0; q--)
        {
            above = shown[q] ? q : -1;
        }
        for (uint32_t q = p + 1; q < ports && below < 0; q++)
        {
            below = shown[q] ? static_cast<int32_t>(q) : -1;
        }
        // At the right, like REMAP / UNASSIGN.
        float move_width = button_width(up) + button_width(down) + spacing;
        float move_x = ImGui::GetWindowContentRegionMax().x - move_width;
        ImGui::SetCursorPosX(move_x > ImGui::GetCursorPosX() ? move_x : ImGui::GetCursorPosX());
        ImGui::BeginDisabled(above < 0);
        if (retro_ui_button(up))
        {
            input_swap_ports(input, p, static_cast<uint32_t>(above));
            config->ports_selected = static_cast<uint32_t>(above);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(below < 0);
        if (retro_ui_button(down))
        {
            input_swap_ports(input, p, static_cast<uint32_t>(below));
            config->ports_selected = static_cast<uint32_t>(below);
        }
        ImGui::EndDisabled();
        ImGui::Unindent(ImGui::GetStyle().IndentSpacing);
        ImGui::PopID();
    }
    if (!anybody)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", piccolo_text(strings, Str::quick_menu_no_ports));
    }
}

// A SET / BIND ALL nobody looks at any more (the window closed by any of its
// ways, another tab, out of the remap view) is dropped: a capture left on
// would keep every action and the game from the controller.
static void drop_unseen_capture(CoreConfigWindow* config, const CoreConfigContext* context)
{
    if (config->capturing && !config->capture_seen)
    {
        config->capturing = false;
        config->bind_all = false;
        input_capture_cancel(context->input);
    }
    config->capture_seen = false;
    if (config->macro_record != 0 && !config->macro_record_seen)
    {
        macro_record_cancel(config, context);   // (a recording keeps the router suspended)
    }
    config->macro_record_seen = false;
}

// The tabs' content scrolls under the fixed title line and tab bar.
static void begin_tab_content()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("content", ImVec2(0.0f, 0.0f));
}

static void end_tab_content()
{
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static const char* with_id(char* buffer, size_t size, const char* text, const char* id)
{
    std::snprintf(buffer, size, "%s###%s", text, id);
    return buffer;
}

// One tab: selected when asked to be, and current while it's the one shown.
static bool begin_tab(CoreConfigWindow* config, CoreConfigTab tab, const char* label, const char* id)
{
    char text[200] = {};
    ImGuiTabItemFlags flags = config->select_tab && config->tab == tab ? ImGuiTabItemFlags_SetSelected : 0;
    if (!ImGui::BeginTabItem(with_id(text, sizeof(text), label, id), nullptr, flags))
    {
        return false;
    }
    if (!config->select_tab)
    {
        config->tab = tab;
    }
    begin_tab_content();
    return true;
}

static void end_tab()
{
    end_tab_content();
    ImGui::EndTabItem();
}

void core_config_window_draw(CoreConfigWindow* config, const CoreConfigContext* context, ImFont* pixel_font,
                             int32_t scale)
{
    if (!config->open)
    {
        drop_unseen_capture(config, context);
        return;
    }
    const Localization* strings = context->strings;
    float block = static_cast<float>(scale);
    const ImGuiIO& io = ImGui::GetIO();
    retro_ui_push_theme(pixel_font, scale);
    // 420 x 280 retro pixels (the remap view's drawn pad needs the room),
    // never bigger than the window.
    ImVec2 size(420.0f * block, 280.0f * block);
    size.x = size.x < io.DisplaySize.x - 8.0f * block ? size.x : io.DisplaySize.x - 8.0f * block;
    size.y = size.y < io.DisplaySize.y - 24.0f * block ? size.y : io.DisplaySize.y - 24.0f * block;
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    char title[200] = {};
    if (ImGui::Begin(with_id(title, sizeof(title), piccolo_text(strings, Str::core_config_title), "core_config"), nullptr,
                     flags))
    {
        retro_ui_begin_window(scale);
        if (retro_ui_close_button())
        {
            config->open = false;
        }
        // What runs (the pixel font draws it in capitals).
        if (context->game_name[0] != '\0')
        {
            ImGui::TextDisabled("%s - %s", context->game_name, context->core_name);
        }
        else
        {
            ImGui::TextDisabled("%s", context->core_name);
        }
        if (ImGui::BeginTabBar("tabs"))
        {
            int target = retro_ui_tab_switch(static_cast<int>(config->tab), static_cast<int>(CoreConfigTab::count));
            if (target >= 0)
            {
                config->tab = static_cast<CoreConfigTab>(target);
                config->select_tab = true;
            }
            bool selecting = config->select_tab;
            if (begin_tab(config, CoreConfigTab::options, piccolo_text(strings, Str::quick_menu_tab_options), "options"))
            {
                if (context->core_options_state != nullptr)
                {
                    CoreOptionsUiContext core = context->core_options;
                    core.strings = strings;
                    core.label_width = 80.0f * block;   // SAVE TO / SECTION; the options size their own column
                    core_options_ui_draw_running(context->core_options_state, &core);
                }
                end_tab();
            }
            if (begin_tab(config, CoreConfigTab::controls, piccolo_text(strings, Str::quick_menu_tab_controls), "controls"))
            {
                if (context->input != nullptr)
                {
                    draw_controls_tab(config, context, block);
                }
                end_tab();
            }
            if (begin_tab(config, CoreConfigTab::shader, piccolo_text(strings, Str::quick_menu_tab_shader), "shader"))
            {
                if (context->settings != nullptr)
                {
                    draw_shader_tab(config, context);
                }
                end_tab();
            }
            if (selecting)
            {
                config->select_tab = false;   // (asked once)
            }
            ImGui::EndTabBar();
        }
        retro_ui_end_window();
    }
    ImGui::End();
    drop_unseen_capture(config, context);
    retro_ui_pop_theme();
}
