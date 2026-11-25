#include "cores_window.h"

#include <cstdio>
#include <string>

#include "piccolo_settings.h"
#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

// ---------------------------------------------------------------------------
// State

static CoresShow show_from_settings(const Settings* settings)
{
    CoresShow show = {};
    show.non_free = settings_get_bool(settings, PICCOLO_CORES_SHOW_NON_FREE);
    show.legacy = settings_get_bool(settings, PICCOLO_CORES_SHOW_LEGACY);
    show.experimental = settings_get_bool(settings, PICCOLO_CORES_SHOW_EXPERIMENTAL);
    return show;
}

// Listed: installed cores always (so they can still be removed or updated);
// the others when the SHOW switches allow both their license and their type.
static bool listed(const CoresShow* show, const CoreUpdater* updater, int32_t entry)
{
    if (core_updater_status(updater, entry) != CoreStatus::not_installed)
    {
        return true;
    }
    const CoreIndexEntry* chosen = &updater->index.entries[static_cast<size_t>(entry)];
    if (!chosen->free && !show->non_free)
    {
        return false;
    }
    switch (chosen->type)
    {
        case CoreType::main:         return true;
        case CoreType::legacy:       return show->legacy;
        case CoreType::experimental: return show->experimental;
    }
    return false;
}

// UPDATE ALL: whenever the downloader is idle, the next core with an update
// (stops at the first error, or when none is left).
void cores_window_update(CoresWindow* window, CoreUpdater* updater, const Settings* settings, const char* running_file)
{
    if (!window->open || !window->update_all || updater->state != CoreUpdaterState::idle)
    {
        return;
    }
    if (updater->message_is_error)
    {
        window->update_all = false;
        return;
    }
    CoresShow show = show_from_settings(settings);
    for (size_t i = 0; i < updater->index.entries.size(); i++)
    {
        int32_t entry = static_cast<int32_t>(i);
        if (listed(&show, updater, entry) && core_updater_status(updater, entry) == CoreStatus::update &&
            updater->index.entries[i].file != running_file &&
            (!updater->free_only || updater->index.entries[i].free))
        {
            core_updater_install(updater, entry, running_file);
            return;
        }
    }
    window->update_all = false;
}

void cores_window_state(CoresWindowState* state, CoreUpdater* updater, const Settings* settings,
                        const Localization* strings, const char* running_file)
{
    state->strings = strings;
    state->updater = updater;
    state->running_file = running_file;
    state->available = core_updater_available();
    state->show = show_from_settings(settings);
    core_updater_progress(updater, &state->received, &state->total);
}

// ---------------------------------------------------------------------------
// Drawing

static const char* status_text(const Localization* strings, CoreStatus status)
{
    switch (status)
    {
        case CoreStatus::not_installed:     return piccolo_text(strings, Str::cores_status_not_installed);
        case CoreStatus::installed:         return piccolo_text(strings, Str::cores_status_installed);
        case CoreStatus::update:            return piccolo_text(strings, Str::cores_status_update);
        case CoreStatus::installed_by_hand: return piccolo_text(strings, Str::cores_status_by_hand);
    }
    return "";
}

// "Game Boy|Game Boy Color" -> "Game Boy, Game Boy Color".
static std::string systems_text(const std::string& systems)
{
    std::string text;
    for (size_t i = 0; i < systems.size(); i++)
    {
        if (systems[i] == '|')
        {
            text += ", ";
        }
        else
        {
            text.push_back(systems[i]);
        }
    }
    return text;
}

static void size_text(int64_t bytes, char* out, size_t out_size)
{
    if (bytes >= 1024 * 1024)
    {
        std::snprintf(out, out_size, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    else
    {
        std::snprintf(out, out_size, "%lld KB", static_cast<long long>((bytes + 1023) / 1024));
    }
}

static void press(CoresWindowAction* action, bool pressed, CoresWindowPress what)
{
    if (pressed)
    {
        action->press = what;
    }
}

static void draw_list(CoresWindow* window, const CoresWindowState* state, float block, float height)
{
    const CoreUpdater* updater = state->updater;
    const Localization* strings = state->strings;
    retro_ui_begin_list("cores", 0.0f, height);
    RetroUiColumn columns[4] = {};
    columns[0].label = piccolo_text(strings, Str::cores_column_core);
    columns[0].width = 80.0f * block;
    columns[1].label = piccolo_text(strings, Str::cores_column_systems);   // the rest
    columns[2].label = piccolo_text(strings, Str::cores_column_version);
    columns[2].width = 50.0f * block;
    columns[3].label = piccolo_text(strings, Str::cores_column_status);
    columns[3].width = 60.0f * block;
    // A gap between columns, so a long SYSTEMS text doesn't run into VERSION.
    if (retro_ui_begin_table("cores_table", columns, 4, 4.0f * block))
    {
        for (size_t i = 0; i < updater->index.entries.size(); i++)
        {
            const CoreIndexEntry* entry = &updater->index.entries[i];
            int32_t index = static_cast<int32_t>(i);
            if (!listed(&state->show, updater, index))
            {
                continue;
            }
            char label[300];
            std::snprintf(label, sizeof(label), "%s%s", entry->name.c_str(), entry->free ? "" : " *");
            if (retro_ui_table_row(static_cast<uint32_t>(index), label, window->selected == index))
            {
                window->selected = index;
            }
            retro_ui_table_cell(systems_text(entry->systems).c_str());
            retro_ui_table_cell(entry->version.c_str());
            retro_ui_table_cell(status_text(strings, core_updater_status(updater, index)));
        }
        retro_ui_end_table();
    }
    retro_ui_end_list();
}

static void draw_selected(CoresWindow* window, const CoresWindowState* state, CoresWindowAction* action)
{
    const CoreUpdater* updater = state->updater;
    const Localization* strings = state->strings;
    bool busy = updater->state != CoreUpdaterState::idle;
    if (window->selected >= static_cast<int32_t>(updater->index.entries.size()) ||
        (window->selected >= 0 && !listed(&state->show, updater, window->selected)))
    {
        window->selected = -1;   // hidden by a SHOW switch, or a shorter list
    }
    if (window->selected < 0)
    {
        retro_ui_dim_text(piccolo_text(strings, updater->index.entries.empty() ? Str::cores_empty : Str::cores_none_selected),
                          nullptr);
        return;
    }
    const CoreIndexEntry* entry = &updater->index.entries[static_cast<size_t>(window->selected)];
    CoreStatus status = core_updater_status(updater, window->selected);
    char size[32];
    size_text(entry->size, size, sizeof(size));
    retro_ui_text_format("%s %s (%s)   %s %s", piccolo_text(strings, Str::cores_license), entry->license.c_str(),
                         piccolo_text(strings, entry->free ? Str::cores_free : Str::cores_non_free),
                         piccolo_text(strings, Str::cores_size), size);
    if (status == CoreStatus::installed || status == CoreStatus::update)
    {
        retro_ui_same_line();
        retro_ui_text_format("   %s %s", piccolo_text(strings, Str::cores_installed_version),
                             core_updater_installed_version(updater, entry->file.c_str()).c_str());
    }
    // The notes wrap at the window's edge.
    if (!entry->free)
    {
        retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::cores_non_free_note), 0.0f);
    }
    if (entry->type == CoreType::legacy)
    {
        retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::cores_legacy_note), 0.0f);
    }
    else if (entry->type == CoreType::experimental)
    {
        retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::cores_experimental_note), 0.0f);
    }
    if (status == CoreStatus::installed_by_hand)
    {
        retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::cores_by_hand_note), 0.0f);
    }

    retro_ui_begin_disabled(busy);
    if (status != CoreStatus::installed)
    {
        Str install = Str::cores_install;
        if (status == CoreStatus::update)
        {
            install = Str::cores_update;
        }
        else if (status == CoreStatus::installed_by_hand)
        {
            install = Str::cores_replace;
        }
        press(action, retro_ui_button(piccolo_text(strings, install)), CoresWindowPress::install);
        retro_ui_same_line();
    }
    press(action, status != CoreStatus::not_installed && retro_ui_button(piccolo_text(strings, Str::cores_remove)),
          CoresWindowPress::remove);
    retro_ui_end_disabled();
    action->entry = window->selected;
}

// NON-FREE, and the confirmation turning it on asks for.
static void draw_non_free_switch(CoresWindow* window, const Localization* strings, CoresWindowAction* action)
{
    bool shown = action->show.non_free;
    if (retro_ui_check("non_free", piccolo_text(strings, Str::cores_non_free), &shown))
    {
        if (shown)
        {
            window->confirm_non_free = true;   // on only after YES
        }
        else
        {
            action->show.non_free = false;
            action->show_changed = true;
        }
    }
    retro_ui_item_tooltip(piccolo_text(strings, Str::cores_show_non_free_help));

    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    RetroUiQuestion question = {};
    question.id = "non_free_confirm";
    question.title = piccolo_text(strings, Str::cores_non_free_confirm_title);
    question.text = piccolo_text(strings, Str::cores_non_free_confirm_text);
    question.yes = piccolo_text(strings, Str::cores_non_free_confirm_yes);
    question.no = piccolo_text(strings, Str::cores_cancel);
    question.width = static_cast<float>(static_cast<int>(screen_width * 0.4f));
    RetroUiAnswer answer = retro_ui_question(&question, window->confirm_non_free);
    if (answer == RetroUiAnswer::yes)
    {
        action->show.non_free = true;
        action->show_changed = true;
    }
    if (answer != RetroUiAnswer::none)
    {
        window->confirm_non_free = false;
    }
}

// One SHOW switch without a confirmation (OLDER VERSIONS, EXPERIMENTAL).
static void draw_show_switch(const Localization* strings, const char* id, Str label, Str help, bool* value,
                             CoresWindowAction* action)
{
    bool shown = *value;
    if (retro_ui_check(id, piccolo_text(strings, label), &shown))
    {
        *value = shown;
        action->show_changed = true;
    }
    retro_ui_item_tooltip(piccolo_text(strings, help));
}

// SHOW: NON-FREE (not in store builds), OLDER VERSIONS, EXPERIMENTAL.
static void draw_show_row(CoresWindow* window, const CoresWindowState* state, CoresWindowAction* action)
{
    const Localization* strings = state->strings;
    retro_ui_label(piccolo_text(strings, Str::cores_show));
    if (!state->updater->free_only)
    {
        retro_ui_same_line();
        draw_non_free_switch(window, strings, action);
    }
    retro_ui_same_line();
    draw_show_switch(strings, "legacy", Str::cores_show_legacy, Str::cores_show_legacy_help, &action->show.legacy,
                     action);
    retro_ui_same_line();
    draw_show_switch(strings, "experimental", Str::cores_show_experimental, Str::cores_show_experimental_help,
                     &action->show.experimental, action);
}

void cores_window_draw(CoresWindow* window, const CoresWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                       CoresWindowAction* action)
{
    *action = {};
    action->show = state->show;
    if (!window->open)
    {
        return;
    }
    const CoreUpdater* updater = state->updater;
    const Localization* strings = state->strings;

    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    retro_ui_push_theme(pixel_font, scale);
    // The same size as SETTINGS: 360 x 240 retro pixels, never bigger than the
    // window. It doesn't scroll: the list does.
    float width = 360.0f * block;
    float height = 240.0f * block;
    RetroUiWindow frame = {};
    frame.id = "cores_window";
    frame.title = piccolo_text(strings, Str::cores_title);
    frame.width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    frame.height = height < screen_height - 24.0f * block ? height : screen_height - 24.0f * block;
    frame.centered_on_open = true;
    frame.scrolls = false;
    bool closed = false;
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        if (closed)
        {
            window->open = false;
        }
        if (!state->available)
        {
            retro_ui_dim_text(piccolo_text(strings, Str::cores_unavailable), nullptr);
        }
        else
        {
            // The list takes what's left above the details, buttons and status,
            // measured last frame (their height changes with notes and buttons;
            // the first frame guesses 4 lines).
            float line = retro_ui_line_step();
            float bottom = window->bottom_height > 0.0f ? window->bottom_height : line * 4.0f;
            float list_height = retro_ui_height_left() - bottom;
            draw_list(window, state, block, list_height > line * 3.0f ? list_height : line * 3.0f);
            float bottom_start = retro_ui_cursor_y();
            draw_selected(window, state, action);

            bool busy = updater->state != CoreUpdaterState::idle;
            retro_ui_begin_disabled(busy);
            press(action, retro_ui_button(piccolo_text(strings, Str::cores_refresh)), CoresWindowPress::refresh);
            retro_ui_same_line();
            press(action, retro_ui_button(piccolo_text(strings, Str::cores_update_all)), CoresWindowPress::update_all);
            retro_ui_end_disabled();
            if (busy)
            {
                retro_ui_same_line();
                press(action, retro_ui_button(piccolo_text(strings, Str::cores_cancel)), CoresWindowPress::cancel);
            }
            draw_show_row(window, state, action);

            // Status: the download's progress, or what the downloader did last.
            if (updater->state == CoreUpdaterState::installing && state->total > 0)
            {
                retro_ui_text_format("%s %s %d%%", piccolo_text(strings, Str::cores_downloading),
                                     updater->task_entry.name.c_str(),
                                     static_cast<int>(state->received * 100 / state->total));
            }
            else if (updater->message_is_error)
            {
                retro_ui_push_section_text();   // the attention color
                retro_ui_text(updater->message.c_str());
                retro_ui_pop_section_text();
            }
            else
            {
                retro_ui_dim_text(updater->message.c_str(), nullptr);
            }
            window->bottom_height = retro_ui_cursor_y() - bottom_start;
        }
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
}

// ---------------------------------------------------------------------------
// Apply

void cores_window_open(CoresWindow* window, CoreUpdater* updater)
{
    window->open = true;
    window->selected = -1;
    window->update_all = false;
    core_updater_refresh(updater);
}

bool cores_window_apply(CoresWindow* window, const CoresWindowAction* action, CoreUpdater* updater, Settings* settings,
                        const char* running_file)
{
    switch (action->press)
    {
    case CoresWindowPress::none:
        break;
    case CoresWindowPress::install:
        core_updater_install(updater, action->entry, running_file);
        break;
    case CoresWindowPress::remove:
        core_updater_remove(updater, action->entry, running_file);
        break;
    case CoresWindowPress::refresh:
        window->update_all = false;
        core_updater_refresh(updater);
        break;
    case CoresWindowPress::update_all:
        window->update_all = true;
        updater->message_is_error = false;   // a new run: don't stop on the last error
        break;
    case CoresWindowPress::cancel:
        window->update_all = false;
        core_updater_cancel(updater);
        break;
    }
    if (!action->show_changed)
    {
        return false;
    }
    settings_set_bool(settings, SettingLayer::global, PICCOLO_CORES_SHOW_NON_FREE, action->show.non_free);
    settings_set_bool(settings, SettingLayer::global, PICCOLO_CORES_SHOW_LEGACY, action->show.legacy);
    settings_set_bool(settings, SettingLayer::global, PICCOLO_CORES_SHOW_EXPERIMENTAL, action->show.experimental);
    return true;
}
