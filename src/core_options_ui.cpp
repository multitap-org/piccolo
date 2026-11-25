#include "core_options_ui.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core_session.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

// Marker colors (0xAARRGGBB): core and game as in settings_ui, folder its own.
constexpr uint32_t MARKER_CORE   = 0xFFF0C040u;   // amber
constexpr uint32_t MARKER_FOLDER = 0xFF60B0F0u;   // light blue
constexpr uint32_t MARKER_GAME   = 0xFF60E080u;   // green

static bool running(const CoreOptionsUiContext* context)
{
    return context->running_core[0] != '\0' && context->session != nullptr;
}

static void add_unique(std::vector<std::string>* list, const std::string& name)
{
    for (size_t i = 0; i < list->size(); i++)
    {
        if ((*list)[i] == name)
        {
            return;
        }
    }
    list->push_back(name);
}

// The running core first, then every core the cache or the store knows.
static void core_list(const CoreOptionsUiContext* context, std::vector<std::string>* cores)
{
    cores->clear();
    if (context->running_core[0] != '\0')
    {
        cores->push_back(context->running_core);
    }
    std::vector<std::string> cached;
    core_options_cache_cores(context->cache, &cached);
    for (size_t i = 0; i < cached.size(); i++)
    {
        add_unique(cores, cached[i]);
    }
    const std::vector<SettingsFileSection>* sections = &context->store->file.sections;
    for (size_t i = 0; i < sections->size(); i++)
    {
        const std::string& name = (*sections)[i].name;
        if (name.empty() || (*sections)[i].entries.empty())
        {
            continue;
        }
        size_t slash = name.find('/');
        add_unique(cores, slash == std::string::npos ? name : name.substr(0, slash));
    }
}

static void draw_core_combo(CoreOptionsUiState* state, const CoreOptionsUiContext* context,
                            const std::vector<std::string>* cores)
{
    const Localization* strings = context->strings;
    char preview[300] = {};
    bool selected_running = state->selected_core == context->running_core;
    std::snprintf(preview, sizeof(preview), "%s%s%s%s", state->selected_core.c_str(), selected_running ? " (" : "",
                  selected_running ? piccolo_text(strings, Str::core_options_running) : "", selected_running ? ")" : "");
    RetroUiRow row = {};
    row.id = "core";
    row.marker_column = false;
    row.label = piccolo_text(strings, Str::core_options_core);
    row.label_width = context->label_width;
    retro_ui_row_begin(&row);
    if (retro_ui_begin_choice("choice", preview))
    {
        for (size_t i = 0; i < cores->size(); i++)
        {
            char label[300] = {};
            bool is_running = (*cores)[i] == context->running_core;
            std::snprintf(label, sizeof(label), "%s%s%s%s", (*cores)[i].c_str(), is_running ? " (" : "",
                          is_running ? piccolo_text(strings, Str::core_options_running) : "", is_running ? ")" : "");
            if (retro_ui_choice_item(static_cast<uint32_t>(i), label, (*cores)[i] == state->selected_core))
            {
                state->selected_core = (*cores)[i];
            }
        }
        retro_ui_end_choice();
    }
    retro_ui_row_end(&row);
}

static const char* layer_name(const CoreOptionsUiContext* context, CoreOptionLayer layer, char* buffer, size_t size)
{
    const Localization* strings = context->strings;
    switch (layer)
    {
    case CoreOptionLayer::core:
        std::snprintf(buffer, size, "%s (%s)", piccolo_text(strings, Str::core_options_layer_core), context->running_core);
        break;
    case CoreOptionLayer::directory:
        std::snprintf(buffer, size, "%s (%s)", piccolo_text(strings, Str::core_options_layer_folder), context->folder);
        break;
    case CoreOptionLayer::game:
        std::snprintf(buffer, size, "%s (%s)", piccolo_text(strings, Str::core_options_layer_game), context->game);
        break;
    }
    return buffer;
}

static void draw_layer_combo(CoreOptionsUiState* state, const CoreOptionsUiContext* context)
{
    CoreOptionsStore* store = context->store;
    bool has_folder = !core_options_store_section(store, CoreOptionLayer::directory)->empty();
    bool has_game = !core_options_store_section(store, CoreOptionLayer::game)->empty();
    if (state->choose_layer)
    {
        // The most specific layer already in use, so changes keep going there.
        state->choose_layer = false;
        state->layer = CoreOptionLayer::core;
        if (has_game && core_options_store_layer_has_values(store, CoreOptionLayer::game))
        {
            state->layer = CoreOptionLayer::game;
        }
        else if (has_folder && core_options_store_layer_has_values(store, CoreOptionLayer::directory))
        {
            state->layer = CoreOptionLayer::directory;
        }
    }
    if ((state->layer == CoreOptionLayer::game && !has_game) || (state->layer == CoreOptionLayer::directory && !has_folder))
    {
        state->layer = CoreOptionLayer::core;
    }

    char preview[600] = {};
    RetroUiRow row = {};
    row.id = "layer";
    row.marker_column = false;
    row.label = piccolo_text(context->strings, Str::core_options_save_to);
    row.help = piccolo_text(context->strings, Str::core_options_save_to_help);
    row.label_width = context->label_width;
    retro_ui_row_begin(&row);
    if (retro_ui_begin_choice("choice", layer_name(context, state->layer, preview, sizeof(preview))))
    {
        CoreOptionLayer layers[3] = {CoreOptionLayer::core, CoreOptionLayer::directory, CoreOptionLayer::game};
        bool exists[3] = {true, has_folder, has_game};
        for (int i = 0; i < 3; i++)
        {
            if (!exists[i])
            {
                continue;
            }
            char label[600] = {};
            layer_name(context, layers[i], label, sizeof(label));
            if (retro_ui_choice_item(static_cast<uint32_t>(i), label, state->layer == layers[i]))
            {
                state->layer = layers[i];
            }
        }
        retro_ui_end_choice();
    }
    retro_ui_row_end(&row);
}

// C / D / G in front of an option that a layer stores for what runs, with
// its tooltip, in the row's marker (none: its space is kept).
static void set_marker(const CoreOptionsUiContext* context, const char* key, RetroUiRow* row)
{
    CoreOptionLayer source = CoreOptionLayer::core;
    const char* stored = core_options_store_get(context->store, key, &source);
    if (stored == nullptr)
    {
        return;
    }
    row->marker = source == CoreOptionLayer::game ? "G" : (source == CoreOptionLayer::directory ? "D" : "C");
    row->marker_color = source == CoreOptionLayer::game ? MARKER_GAME
                        : (source == CoreOptionLayer::directory ? MARKER_FOLDER : MARKER_CORE);
    Str help = source == CoreOptionLayer::game ? Str::core_options_marker_game_help
               : (source == CoreOptionLayer::directory ? Str::core_options_marker_folder_help
                                                       : Str::core_options_marker_core_help);
    row->marker_help = piccolo_text(context->strings, help);
}

static const char* value_label(const LibretroOption* option, uint32_t index)
{
    const char* label = option->value_labels[index];
    return label != nullptr ? label : option->values[index];
}

static const char* option_label(const LibretroOption* option, bool in_category)
{
    const char* label = in_category && option->label_in_category != nullptr ? option->label_in_category : option->label;
    return label != nullptr ? label : option->key;
}

static void draw_option(const CoreOptionsUiState* state, const CoreOptionsUiContext* context,
                        const LibretroOption* option, bool in_category, float label_width)
{
    // Labels longer than the column are cut, not drawn over the combo (the
    // whole label is in the tooltip with the core's help).
    const char* label = option_label(option, in_category);
    std::string help = option->info != nullptr ? std::string(label) + "\n\n" + option->info : std::string(label);
    // RESET keeps its room while hidden, so the combos line up.
    bool stored_here = core_options_store_get_layer(context->store, state->layer, option->key) != nullptr;
    RetroUiRow row = {};
    row.id = option->key;
    set_marker(context, option->key, &row);
    row.label = label;
    row.help = help.c_str();
    row.label_width = label_width;
    row.clip_label = true;
    row.end_button = piccolo_text(context->strings, Str::reset);
    row.end_button_hidden = !stored_here;
    row.end_button_help = piccolo_text(context->strings, Str::reset_help_override);
    retro_ui_row_begin(&row);

    // The value as it resolves at the edited layer (or the core's default).
    const char* at_layer = core_options_store_get_at(context->store, state->layer, option->key, nullptr);
    uint32_t current = option->default_index;
    for (uint32_t v = 0; at_layer != nullptr && v < option->value_count; v++)
    {
        if (std::strcmp(option->values[v], at_layer) == 0)
        {
            current = v;
        }
    }
    if (retro_ui_begin_choice("value", value_label(option, current)))
    {
        for (uint32_t v = 0; v < option->value_count; v++)
        {
            if (retro_ui_choice_item(v, value_label(option, v), v == current))
            {
                core_session_set_option(context->session, state->layer, option->key, option->values[v]);
            }
        }
        retro_ui_end_choice();
    }
    if (retro_ui_row_end(&row))
    {
        core_session_reset_option(context->session, state->layer, option->key);
    }
}

static bool in_section(const LibretroOptions* options, const LibretroOption* option, const std::string& section)
{
    bool known_category = false;
    for (uint32_t c = 0; option->category_key != nullptr && c < options->category_count; c++)
    {
        known_category = known_category || std::strcmp(options->categories[c].key, option->category_key) == 0;
    }
    if (!known_category)
    {
        return section.empty();   // no category (or one the core didn't declare): GENERAL
    }
    return section == option->category_key;
}

// SECTION: GENERAL (options without a category) and each category, one at a
// time (a long list in one scroll was hard to use). Hidden when the core has
// no categories.
static void draw_section_combo(CoreOptionsUiState* state, const CoreOptionsUiContext* context)
{
    const LibretroOptions* options = &context->session->host->options;
    bool has_general = false;
    for (uint32_t i = 0; i < options->count; i++)
    {
        has_general = has_general || (options->options[i].visible && in_section(options, &options->options[i], ""));
    }
    // A section that no longer exists (another core): the first one.
    bool known = state->section.empty() && has_general;
    for (uint32_t c = 0; !known && c < options->category_count; c++)
    {
        known = state->section == options->categories[c].key;
    }
    if (!known)
    {
        state->section = has_general || options->category_count == 0 ? "" : options->categories[0].key;
    }
    if (options->category_count == 0)
    {
        return;
    }

    const char* general = piccolo_text(context->strings, Str::core_options_general);
    const char* preview = general;
    for (uint32_t c = 0; c < options->category_count; c++)
    {
        if (state->section == options->categories[c].key)
        {
            preview = options->categories[c].label;
        }
    }
    RetroUiRow row = {};
    row.id = "section";
    row.marker_column = false;
    row.label = piccolo_text(context->strings, Str::core_options_section);
    row.label_width = context->label_width;
    retro_ui_row_begin(&row);
    if (retro_ui_begin_choice("choice", preview))
    {
        // GENERAL first, then the categories (item c + 1).
        if (has_general && retro_ui_choice_item(0, general, state->section.empty()))
        {
            state->section.clear();
        }
        for (uint32_t c = 0; c < options->category_count; c++)
        {
            if (retro_ui_choice_item(c + 1, options->categories[c].label, state->section == options->categories[c].key))
            {
                state->section = options->categories[c].key;
            }
        }
        retro_ui_end_choice();
    }
    retro_ui_row_end(&row);
}

// The running core's options: SAVE TO, SECTION, then that section's options
// in their own scrolling area (the rest of the window).
static void draw_live(CoreOptionsUiState* state, const CoreOptionsUiContext* context)
{
    const LibretroOptions* options = &context->session->host->options;
    draw_layer_combo(state, context);
    if (options->count == 0)
    {
        retro_ui_dim_text(piccolo_text(context->strings, Str::core_options_none), nullptr);
        return;
    }
    draw_section_combo(state, context);

    // The label column fits this section's labels, up to half the width.
    bool categorized = !state->section.empty();
    float widest = 0.0f;
    for (uint32_t i = 0; i < options->count; i++)
    {
        const LibretroOption* option = &options->options[i];
        if (option->visible && in_section(options, option, state->section))
        {
            float width = retro_ui_text_width(option_label(option, categorized));
            widest = width > widest ? width : widest;
        }
    }
    float label_width = retro_ui_label_column_width(widest);
    float most = retro_ui_width_left() * 0.5f;
    label_width = label_width < most ? label_width : most;

    float height = retro_ui_height_left();
    float minimum = retro_ui_line_step() * 4.0f;
    if (retro_ui_begin_scroll_field("options", 0.0f, height > minimum ? height : minimum))
    {
        for (uint32_t i = 0; i < options->count; i++)
        {
            const LibretroOption* option = &options->options[i];
            if (option->visible && in_section(options, option, state->section))
            {
                draw_option(state, context, option, categorized, label_width);
            }
        }
    }
    retro_ui_end_scroll_area();
}

static const CachedCoreOption* cached_option(const std::vector<CachedCoreOption>* cached, const std::string& key)
{
    for (size_t i = 0; i < cached->size(); i++)
    {
        if ((*cached)[i].key == key)
        {
            return &(*cached)[i];
        }
    }
    return nullptr;
}

// After the stored values changed under the running core: it gets what applies now.
static void reapply(const CoreOptionsUiContext* context)
{
    if (!running(context))
    {
        return;
    }
    LibretroHost* host = context->session->host;
    for (uint32_t i = 0; i < host->options.count; i++)
    {
        const LibretroOption* option = &host->options.options[i];
        const char* stored = core_options_store_get(context->store, option->key, nullptr);
        libretro_host_set_option(host, option->key, stored != nullptr ? stored : option->values[option->default_index]);
    }
}

// What core_options.cfg keeps for `core`, from any core (no need to load it).
static void draw_stored(const CoreOptionsUiState* state, const CoreOptionsUiContext* context)
{
    const Localization* strings = context->strings;
    const char* core = state->selected_core.c_str();
    std::vector<std::string> sections;
    core_options_store_sections_of(context->store, core, &sections);
    if (!retro_ui_section_header("stored", piccolo_text(strings, Str::core_options_stored), true))
    {
        return;
    }
    if (sections.empty())
    {
        retro_ui_dim_text(piccolo_text(strings, Str::core_options_stored_none), nullptr);
        return;
    }
    std::vector<CachedCoreOption> cached;
    core_options_cache_get(context->cache, core, &cached);
    bool changed = false;
    const char* delete_text = piccolo_text(strings, Str::core_options_delete);

    for (size_t s = 0; s < sections.size() && !changed; s++)
    {
        retro_ui_push_index(static_cast<uint32_t>(s));
        const std::string& name = sections[s];
        size_t core_length = std::strlen(core);
        char title[600] = {};
        if (name.size() == core_length)
        {
            std::snprintf(title, sizeof(title), "%s", piccolo_text(strings, Str::core_options_section_core));
        }
        else if (name.compare(core_length, 5, "/dir:") == 0)
        {
            std::snprintf(title, sizeof(title), "%s: %s", piccolo_text(strings, Str::core_options_section_folder),
                          name.c_str() + core_length + 5);
        }
        else
        {
            size_t colon = name.find(':', core_length);
            std::snprintf(title, sizeof(title), "%s: %s", piccolo_text(strings, Str::core_options_section_game),
                          colon != std::string::npos ? name.c_str() + colon + 1 : name.c_str());
        }
        retro_ui_separator_text(title);
        const char* delete_section = piccolo_text(strings, Str::core_options_delete_section);
        if (retro_ui_button(delete_section))
        {
            changed = core_options_store_remove_section(context->store, name.c_str());
        }

        int32_t index = settings_file_find_section(&context->store->file, name.c_str());
        const SettingsFileSection* section =
            changed || index < 0 ? nullptr : &context->store->file.sections[static_cast<size_t>(index)];
        for (size_t e = 0; section != nullptr && e < section->entries.size() && !changed; e++)
        {
            const SettingsFileEntry* entry = &section->entries[e];
            const CachedCoreOption* option = cached_option(&cached, entry->key);
            const char* value_text = entry->value.c_str();
            for (size_t v = 0; option != nullptr && v < option->values.size(); v++)
            {
                if (option->values[v] == entry->value && !option->labels[v].empty())
                {
                    value_text = option->labels[v].c_str();
                }
            }
            // A row: "OPTION = VALUE" (the key as its tooltip), DELETE at the right end.
            char text[600] = {};
            RetroUiRow row = {};
            row.id = "entry";
            row.marker_column = false;
            row.label = text;
            row.end_button = delete_text;
            if (option != nullptr)
            {
                std::snprintf(text, sizeof(text), "%s = %s", option->label.c_str(), value_text);
                row.help = entry->key.c_str();
            }
            else
            {
                // Without cached definitions (or a key the core dropped): the raw text.
                std::snprintf(text, sizeof(text), "%s = %s", entry->key.c_str(), value_text);
            }
            retro_ui_push_index(static_cast<uint32_t>(e));
            retro_ui_row_begin(&row);
            if (option == nullptr && !cached.empty())
            {
                char gone[200] = {};
                std::snprintf(gone, sizeof(gone), "(%s)", piccolo_text(strings, Str::core_options_gone));
                retro_ui_dim_text(gone, nullptr);
            }
            if (retro_ui_row_end(&row))
            {
                changed = core_options_store_remove(context->store, name.c_str(), entry->key.c_str());
            }
            retro_ui_pop_id();
        }
        retro_ui_pop_id();
    }

    retro_ui_spacing();
    if (!changed && retro_ui_button(piccolo_text(strings, Str::core_options_delete_all)))
    {
        for (size_t s = 0; s < sections.size(); s++)
        {
            core_options_store_remove_section(context->store, sections[s].c_str());
        }
        changed = true;
    }
    retro_ui_item_tooltip(piccolo_text(strings, Str::core_options_delete_all_help));
    if (changed && state->selected_core == context->running_core)
    {
        reapply(context);
    }
}

void core_options_ui_draw_running(CoreOptionsUiState* state, const CoreOptionsUiContext* context)
{
    if (running(context))
    {
        draw_live(state, context);
    }
}

void core_options_ui_draw(CoreOptionsUiState* state, const CoreOptionsUiContext* context)
{
    std::vector<std::string> cores;
    core_list(context, &cores);
    if (cores.empty())
    {
        retro_ui_dim_text(piccolo_text(context->strings, Str::core_options_no_cores), nullptr);
        return;
    }
    bool known = false;
    for (size_t i = 0; i < cores.size(); i++)
    {
        known = known || cores[i] == state->selected_core;
    }
    if (state->selected_core.empty() || !known)
    {
        state->selected_core = cores[0];   // the running core, when there is one
    }
    draw_core_combo(state, context, &cores);

    // The running core's options are changed in the quick menu; here only
    // what is stored is seen and deleted.
    bool selected_running = running(context) && state->selected_core == context->running_core;
    retro_ui_dim_text_wrapped_at(piccolo_text(context->strings, selected_running ? Str::core_options_running_note
                                                                                  : Str::core_options_not_running),
                                 0.0f);
    retro_ui_spacing();
    draw_stored(state, context);
}

void core_options_manager_open(bool* open, CoreOptionsUiState* state)
{
    if (!*open)
    {
        state->selected_core.clear();   // the running core (or the first one)
    }
    *open = true;
}

void core_options_manager_draw(bool* open, CoreOptionsUiState* state, const CoreOptionsUiContext* context,
                               RetroUiFont* pixel_font, int32_t scale)
{
    if (!*open)
    {
        return;
    }
    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    retro_ui_push_theme(pixel_font, scale);
    // The same size as SETTINGS: 360 x 240 retro pixels, never bigger than the window.
    float width = 360.0f * block;
    float height = 240.0f * block;
    RetroUiWindow window = {};
    window.id = "core_options_manager";
    window.title = piccolo_text(context->strings, Str::core_options_manager_title);
    window.width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    window.height = height < screen_height - 24.0f * block ? height : screen_height - 24.0f * block;
    window.centered_on_open = true;
    // The list scrolls in its own area (a scrolling window jumped back to the top).
    window.scrolls = false;
    bool closed = false;
    if (retro_ui_window_begin(&window, scale, &closed))
    {
        if (closed)
        {
            *open = false;
        }
        CoreOptionsUiContext manager = *context;
        manager.label_width = 50.0f * block;   // CORE
        if (retro_ui_begin_scroll_area("content", 0.0f, 0.0f))
        {
            if (manager.store != nullptr && manager.cache != nullptr)
            {
                core_options_ui_draw(state, &manager);
            }
        }
        retro_ui_end_scroll_area();
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}
