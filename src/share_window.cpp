#include "share_window.h"

#include "net_window.h"

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// The shared folder a virtual path starts with ("ROMS/gba" -> "ROMS").
static std::string root_of(const std::string* path)
{
    size_t slash = path->find('/');
    return slash == std::string::npos ? *path : path->substr(0, slash);
}

static std::string child(const std::string* path, const std::string* name)
{
    return path->empty() ? *name : *path + "/" + *name;
}

static void go_up(SharePane* pane)
{
    size_t slash = pane->path.rfind('/');
    pane->path = slash == std::string::npos ? std::string() : pane->path.substr(0, slash);
    pane->stale = true;
    pane->selected = -1;
}

static void go_to(SharePane* pane, const std::string* path)
{
    pane->path = *path;
    pane->stale = true;
    pane->selected = -1;
}

// The kind of the shared folder `path` is in (games, saves, states), from
// the root listing; games when it isn't known.
static NetShareKind root_kind(const std::vector<NetShareEntry>* roots, const std::string* path)
{
    std::string root = root_of(path);
    for (size_t i = 0; i < roots->size(); i++)
    {
        if ((*roots)[i].name == root)
        {
            return (*roots)[i].kind;
        }
    }
    return NetShareKind::games;
}

static bool is_folder(NetShareKind kind)
{
    return kind != NetShareKind::file;
}

// ---------------------------------------------------------------------------
// A game's saves and states
// ---------------------------------------------------------------------------

static bool ends_with(const std::string* text, const char* end)
{
    size_t size = std::strlen(end);
    return text->size() >= size && text->compare(text->size() - size, size, end) == 0;
}

// The core writes <stem>.srm, <stem>.state1, <stem>.state1.png...: one more
// extension (and the state's picture), never another game whose name only
// starts the same ("Mario.srm" isn't "Mario World.srm" nor "Mario.World.srm").
static bool belongs_to(const std::string* name, const std::string* stem)
{
    if (name->size() <= stem->size() + 1 || name->compare(0, stem->size(), *stem) != 0 ||
        (*name)[stem->size()] != '.')
    {
        return false;
    }
    std::string rest = name->substr(stem->size() + 1);
    if (ends_with(&rest, ".png"))
    {
        rest.resize(rest.size() - 4);
    }
    return !rest.empty() && rest.find('.') == std::string::npos;
}

// A listing of one of the folders the game's saves and states may be in: a
// shared folder itself (its core folders are looked at next) or a core
// folder (the game's files are kept).
static void companions_take(ShareCompanions* companions, const std::string* folder,
                            const std::vector<NetShareEntry>* entries, const std::vector<NetShareEntry>* roots)
{
    bool top = folder->find('/') == std::string::npos;
    NetShareKind kind = root_kind(roots, folder);
    for (size_t i = 0; i < entries->size(); i++)
    {
        const NetShareEntry* entry = &(*entries)[i];
        if (entry->kind == NetShareKind::folder)
        {
            if (top)
            {
                companions->folders.push_back(child(folder, &entry->name));
            }
            continue;
        }
        if (entry->kind != NetShareKind::file || !belongs_to(&entry->name, &companions->stem))
        {
            continue;
        }
        ShareCompanion companion;
        companion.path = child(folder, &entry->name);
        companion.size = entry->size;
        companion.mtime = entry->mtime;
        companions->files.push_back(companion);
        if (kind == NetShareKind::saves)
        {
            companions->saves++;
        }
        else if (!ends_with(&entry->name, ".png"))
        {
            companions->states++;
        }
    }
}

// A file was selected on one side: when it's a game, look for its saves
// and states there (this side at once; the other side's folders are listed
// one at a time through share_window_remote_wants_list).
static void companions_start(ShareWindow* window, bool remote, const NetShareFolders* local_folders)
{
    ShareCompanions* companions = &window->companions;
    *companions = ShareCompanions();
    companions->remote = remote;
    const SharePane* pane = remote ? &window->remote : &window->local;
    const std::vector<NetShareEntry>* roots = remote ? &window->remote_roots : &window->local_roots;
    if (pane->selected < 0 || static_cast<size_t>(pane->selected) >= pane->entries.size() ||
        root_kind(roots, &pane->path) != NetShareKind::games)
    {
        return;
    }
    const std::string* name = &pane->entries[static_cast<size_t>(pane->selected)].name;
    companions->game = child(&pane->path, name);
    size_t dot = name->rfind('.');
    companions->stem = dot == std::string::npos || dot == 0 ? *name : name->substr(0, dot);
    for (size_t i = 0; i < roots->size(); i++)
    {
        if ((*roots)[i].kind == NetShareKind::saves || (*roots)[i].kind == NetShareKind::states)
        {
            companions->folders.push_back((*roots)[i].name);
        }
    }
    if (remote || local_folders == nullptr)
    {
        return;
    }
    std::vector<NetShareEntry> entries;
    while (!companions->folders.empty())
    {
        std::string folder = companions->folders[0];
        companions->folders.erase(companions->folders.begin());
        if (net_share_list(local_folders, folder.c_str(), &entries) == NetShareStatus::ok)
        {
            companions_take(companions, &folder, &entries, roots);
        }
    }
}

// The selected file of `pane` is the game the companions were looked up for.
static bool companions_for(const ShareWindow* window, bool remote, const SharePane* pane)
{
    const ShareCompanions* companions = &window->companions;
    if (companions->remote != remote || companions->game.empty() || pane->selected < 0 ||
        static_cast<size_t>(pane->selected) >= pane->entries.size())
    {
        return false;
    }
    return companions->game == child(&pane->path, &pane->entries[static_cast<size_t>(pane->selected)].name);
}

static bool companions_looking(const ShareCompanions* companions)
{
    return !companions->folders.empty() || companions->waiting;
}

// ---------------------------------------------------------------------------
// Grey: on the other side already
// ---------------------------------------------------------------------------

static bool same_file(const NetShareEntry* a, const NetShareEntry* b)
{
    return a->kind == NetShareKind::file && b->kind == NetShareKind::file && a->mtime != 0 && a->mtime == b->mtime &&
           a->size == b->size && a->name == b->name;
}

// Once per listing (not per frame): which files of each side the other has.
static bool mark_same(SharePane* a, const SharePane* b)
{
    bool any = false;
    a->same.assign(a->entries.size(), 0);
    for (size_t i = 0; i < a->entries.size(); i++)
    {
        for (size_t j = 0; j < b->entries.size() && a->same[i] == 0; j++)
        {
            if (same_file(&a->entries[i], &b->entries[j]))
            {
                a->same[i] = 1;
                any = true;
            }
        }
    }
    return any;
}

// ---------------------------------------------------------------------------
// State

void share_window_remote_reset(ShareWindow* window)
{
    window->remote = SharePane();
    window->remote_roots.clear();
    window->remote_asked = false;
    window->same_stale = true;
    if (window->companions.remote)
    {
        window->companions = ShareCompanions();
    }
    window->conflicts.clear();
}

void share_window_add_conflict(ShareWindow* window, const ShareCopy* copy)
{
    window->conflicts.push_back(*copy);
}

bool share_window_remote_wants_list(ShareWindow* window, std::string* path)
{
    if (!window->open || window->remote_asked)
    {
        return false;
    }
    SharePane* pane = &window->remote;
    ShareCompanions* companions = &window->companions;
    if (pane->stale)
    {
        pane->stale = false;
        pane->waiting = true;
        *path = pane->path;
    }
    else if (companions->remote && !companions->folders.empty() && !companions->waiting)
    {
        companions->waiting = true;
        *path = companions->folders[0];
    }
    else
    {
        return false;
    }
    window->remote_asked = true;
    return true;
}

void share_window_set_remote_list(ShareWindow* window, const char* path, NetShareStatus status,
                                  const std::vector<NetShareEntry>* entries)
{
    window->remote_asked = false;
    ShareCompanions* companions = &window->companions;
    if (companions->remote && companions->waiting && !companions->folders.empty() && companions->folders[0] == path)
    {
        companions->waiting = false;
        std::string folder = companions->folders[0];
        companions->folders.erase(companions->folders.begin());
        if (status == NetShareStatus::ok)
        {
            companions_take(companions, &folder, entries, &window->remote_roots);
        }
        return;
    }
    SharePane* pane = &window->remote;
    if (!pane->waiting)
    {
        return;
    }
    pane->waiting = false;
    if (pane->path != path)
    {
        return;   // an answer for a folder we've left (the new one is listed next)
    }
    pane->status = status;
    pane->entries = *entries;
    pane->selected = -1;
    window->same_stale = true;
    if (pane->path.empty() && status == NetShareStatus::ok)
    {
        window->remote_roots = *entries;   // its shared folders, for the combo while it's browsed further in
    }
}

void share_window_update(ShareWindow* window, const NetShareFolders* local_folders, const NetShareLinkStatus* link)
{
    if (!window->open)
    {
        return;
    }
    bool connected = link != nullptr && link->state == NetShareLinkState::ready &&
                     net_access_allows(link->access, NetAccessLevel::share);
    if (window->local.stale && local_folders != nullptr)
    {
        window->local.stale = false;
        net_share_list(local_folders, "", &window->local_roots);
        window->local.status = net_share_list(local_folders, window->local.path.c_str(), &window->local.entries);
        window->local.selected = -1;
        window->same_stale = true;
        if (window->local.status != NetShareStatus::ok && !window->local.path.empty())
        {
            std::string top;
            go_to(&window->local, &top);   // gone meanwhile: back to the shared folders
        }
    }

    if (window->same_stale)
    {
        window->same_stale = false;
        bool any = false;
        if (connected && !window->remote.waiting)
        {
            any = mark_same(&window->local, &window->remote);
            mark_same(&window->remote, &window->local);
        }
        else
        {
            window->local.same.clear();
            window->remote.same.clear();
        }
        window->any_same = any;
    }
}

void share_window_state(ShareWindowState* state, const Localization* strings, const NetShareLinkStatus* link,
                        const ShareTransferLine* line)
{
    state->strings = strings;
    state->link = link;
    state->line = line;
    state->connected = link != nullptr && link->state == NetShareLinkState::ready &&
                       net_access_allows(link->access, NetAccessLevel::share);
}

// ---------------------------------------------------------------------------
// Drawing

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

static void format_size(uint64_t bytes, char* out, size_t size)
{
    if (bytes >= 1024ull * 1024 * 1024)
    {
        std::snprintf(out, size, "%.1f GIB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
    else if (bytes >= 1024ull * 1024)
    {
        std::snprintf(out, size, "%.1f MIB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    else if (bytes >= 1024)
    {
        std::snprintf(out, size, "%llu KIB", static_cast<unsigned long long>(bytes / 1024));
    }
    else
    {
        std::snprintf(out, size, "%llu B", static_cast<unsigned long long>(bytes));
    }
}

// Saves and states are told apart by when they were made: the date.
static void format_date(uint64_t mtime, char* out, size_t size)
{
    if (mtime == 0)
    {
        std::snprintf(out, size, "-");
        return;
    }
    std::time_t time = static_cast<std::time_t>(mtime);
    std::tm local = {};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::strftime(out, size, "%Y-%m-%d %H:%M", &local);
}

// ---------------------------------------------------------------------------
// One side
// ---------------------------------------------------------------------------

struct PaneLayout
{
    float width = 0.0f;
    float folders_height = 0.0f;
    float files_height = 0.0f;
};

// `*picked`: a file was selected (its saves and states are looked up).
static void draw_pane(SharePane* pane, std::vector<NetShareEntry>* roots, const char* name, bool connected,
                      const PaneLayout* layout, const Localization* strings, bool* picked)
{
    retro_ui_begin_group();
    retro_ui_push_id(name);
    retro_ui_text(name);

    // The shared folder we're in. Picking it again goes back to its top.
    std::string root = root_of(&pane->path);
    const char* preview = root.empty() ? piccolo_text(strings, Str::share_shared_folders) : root.c_str();
    retro_ui_next_width(layout->width);
    if (retro_ui_begin_choice("root", preview))
    {
        for (size_t i = 0; i < roots->size(); i++)
        {
            if (retro_ui_choice_item_repick(static_cast<uint32_t>(i), (*roots)[i].name.c_str(),
                                            (*roots)[i].name == root))
            {
                go_to(pane, &(*roots)[i].name);
            }
        }
        retro_ui_end_choice();
    }
    retro_ui_dim_text(pane->path.empty() ? " " : pane->path.c_str(), nullptr);

    bool listed = connected && !pane->waiting && pane->status == NetShareStatus::ok;

    // Folders (at the top: the shared folders themselves).
    std::string open_folder;
    bool up = false;
    retro_ui_begin_list("folders", layout->width, layout->folders_height);
    if (!connected)
    {
        retro_ui_dim_text(piccolo_text(strings, Str::share_not_connected), nullptr);
    }
    else if (pane->waiting)
    {
        retro_ui_dim_text("...", nullptr);
    }
    else
    {
        if (!pane->path.empty())
        {
            retro_ui_push_id("up");
            up = retro_ui_list_item(0, "..", false, nullptr);
            retro_ui_pop_id();
        }
        for (size_t i = 0; listed && i < pane->entries.size(); i++)
        {
            if (!is_folder(pane->entries[i].kind))
            {
                continue;
            }
            if (retro_ui_list_item(static_cast<uint32_t>(i), pane->entries[i].name.c_str(), false, nullptr))
            {
                open_folder = pane->entries[i].name;
            }
        }
        if (pane->status != NetShareStatus::ok)
        {
            retro_ui_dim_text(piccolo_text(strings, Str::share_list_failed), nullptr);
        }
    }
    retro_ui_end_list();

    // Files below: name, and the size (games) or the date (saves, states).
    bool by_date = root_kind(roots, &pane->path) != NetShareKind::games;
    retro_ui_begin_list("files", layout->width, layout->files_height);
    bool any_file = false;
    for (size_t i = 0; listed && i < pane->entries.size(); i++)
    {
        const NetShareEntry* entry = &pane->entries[i];
        if (entry->kind != NetShareKind::file)
        {
            continue;
        }
        any_file = true;
        bool grey = i < pane->same.size() && pane->same[i] != 0;
        char detail[48] = {};
        if (by_date)
        {
            format_date(entry->mtime, detail, sizeof(detail));
        }
        else
        {
            format_size(entry->size, detail, sizeof(detail));
        }
        if (retro_ui_list_item_detail(static_cast<uint32_t>(i), entry->name.c_str(),
                                      pane->selected == static_cast<int32_t>(i), detail, grey) &&
            pane->selected != static_cast<int32_t>(i))
        {
            pane->selected = static_cast<int32_t>(i);
            *picked = true;
        }
    }
    if (listed && !any_file && !pane->path.empty())
    {
        retro_ui_dim_text(piccolo_text(strings, Str::share_no_files), nullptr);
    }
    retro_ui_end_list();

    retro_ui_pop_id();
    retro_ui_end_group();

    if (up)
    {
        go_up(pane);
    }
    else if (!open_folder.empty())
    {
        std::string next = child(&pane->path, &open_folder);
        go_to(pane, &next);
    }
}

static Str refusal_text(NetClientRefusal refusal)
{
    switch (refusal)
    {
    case NetClientRefusal::not_paired:
        return Str::share_refused_not_paired;
    case NetClientRefusal::pairing_closed:
        return Str::share_refused_pairing_closed;
    case NetClientRefusal::wrong_code:
        return Str::share_refused_wrong_code;
    case NetClientRefusal::unknown_host:
        return Str::share_refused_unknown_host;
    case NetClientRefusal::host_offline:
        return Str::share_refused_host_offline;
    case NetClientRefusal::no_route:
        return Str::share_refused_no_route;
    case NetClientRefusal::beacon_unreachable:
        return Str::share_refused_beacon;
    default:
        return Str::share_refused_connection;
    }
}

// The other side while no piccolo is connected with OWNER access: the
// piccolos on this network, the code (only the first time) and CONNECT; or,
// connected at a lower level, what to ask for there.
// A piccolo on the LAN we paired with (by name: the list doesn't carry keys).
static bool found_is_paired(const NetShareLinkStatus* link, int32_t index)
{
    for (size_t i = 0; i < link->paired.size(); i++)
    {
        if (link->paired[i] == link->found[static_cast<size_t>(index)].name)
        {
            return true;
        }
    }
    return false;
}

static void draw_connect(ShareWindow* window, const NetShareLinkStatus* link, const PaneLayout* layout,
                         const Localization* strings, ShareWindowAction* action)
{
    retro_ui_begin_group();
    retro_ui_push_id("connect");
    retro_ui_text(piccolo_text(strings, Str::share_other_piccolo));
    // As tall as the side's combo, path and two lists (the spacing between
    // two rows: a line step less a line).
    float row_spacing = retro_ui_line_step() - retro_ui_line_height();
    float height = layout->folders_height + layout->files_height + row_spacing + retro_ui_line_step() +
                   retro_ui_text_line_step();
    retro_ui_begin_list("connect_box", layout->width, height);
    // Its text wraps at the box's edge (0).
    if (link == nullptr || link->state == NetShareLinkState::off)
    {
        retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::share_network_off), 0.0f);
    }
    else if (link->state == NetShareLinkState::ready)
    {
        // Connected, but not at OWNER: the other piccolo decides that.
        retro_ui_text_wrapped(link->host_name.c_str());
        retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::share_needs_owner), 0.0f);
        if (retro_ui_button(piccolo_text(strings, Str::share_disconnect)))
        {
            action->press = ShareWindowPress::disconnect;
        }
    }
    else
    {
        retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::share_found), 0.0f);
        if (window->found_selected >= static_cast<int32_t>(link->found.size()))
        {
            window->found_selected = -1;
        }
        for (size_t i = 0; i < link->found.size(); i++)
        {
            char address[64] = {};
            net_address_to_text(link->found[i].address, address, sizeof(address));
            char label[160] = {};
            std::snprintf(label, sizeof(label), "%s  %s", link->found[i].name.c_str(), address);
            if (retro_ui_list_item(static_cast<uint32_t>(i), label, window->found_selected == static_cast<int32_t>(i),
                                   nullptr))
            {
                window->found_selected = static_cast<int32_t>(i);
                window->paired_selected = -1;
                window->invite_selected = false;
            }
        }
        if (link->found.empty())
        {
            retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::share_found_none), 0.0f);
        }
        // The piccolos paired before, reached through the beacon when they
        // aren't on this network (Andrés, 2026-11-21).
        if (window->paired_selected >= static_cast<int32_t>(link->paired.size()))
        {
            window->paired_selected = -1;
        }
        if (!link->paired.empty())
        {
            retro_ui_spacing();
            retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::share_internet), 0.0f);
            retro_ui_item_tooltip(piccolo_text(strings, Str::share_internet_help));
            for (size_t i = 0; i < link->paired.size(); i++)
            {
                const char* state = "";
                if (i < link->paired_online.size())
                {
                    state = piccolo_text(strings, link->paired_online[i] != 0 ? Str::net_online : Str::net_offline);
                }
                char label[160] = {};
                std::snprintf(label, sizeof(label), "%s  %s", link->paired[i].c_str(), state);
                if (retro_ui_list_item(static_cast<uint32_t>(1000 + i), label,
                                       window->paired_selected == static_cast<int32_t>(i), nullptr))
                {
                    window->paired_selected = static_cast<int32_t>(i);
                    window->found_selected = -1;
                    window->invite_selected = false;
                }
            }
        }
        // Not paired yet and not on this network: our code, typed on the other
        // piccolo in TOOLS > INVITE, which waits for us on the beacon.
        retro_ui_spacing();
        retro_ui_push_id("invite");
        if (retro_ui_list_item(0, piccolo_text(strings, Str::share_pair_internet), window->invite_selected, nullptr))
        {
            window->invite_selected = true;
            window->found_selected = -1;
            window->paired_selected = -1;
        }
        retro_ui_pop_id();
        retro_ui_item_tooltip(piccolo_text(strings, Str::share_pair_internet_help));
        retro_ui_spacing();
        bool connecting = link->state == NetShareLinkState::connecting;
        // A piccolo we aren't paired with (on this network, or "another
        // network"): PAIR opens TOOLS > PAIR on it, the one place pairing
        // happens (Andrés, 2026-11-22). A paired one: CONNECT.
        bool unpaired = window->invite_selected ||
                        (window->found_selected >= 0 && !found_is_paired(link, window->found_selected));
        retro_ui_begin_disabled((window->found_selected < 0 && window->paired_selected < 0 && !window->invite_selected) ||
                                connecting);
        if (retro_ui_button(piccolo_text(strings, unpaired ? Str::pair_button : Str::share_connect)))
        {
            if (window->invite_selected)
            {
                action->press = ShareWindowPress::pair_internet;
            }
            else if (unpaired)
            {
                action->press = ShareWindowPress::pair;
            }
            else
            {
                action->press = ShareWindowPress::connect;
            }
        }
        retro_ui_end_disabled();
        if (connecting)
        {
            retro_ui_dim_text_wrapped_at(piccolo_text(strings, Str::share_connecting), 0.0f);
        }
        else if (link->refusal != NetClientRefusal::none)
        {
            retro_ui_dim_text_wrapped_at(piccolo_text(strings, refusal_text(link->refusal)), 0.0f);
        }
    }
    retro_ui_end_list();
    retro_ui_pop_id();
    retro_ui_end_group();
}

// The game's saves and states go into the other side's SAVES and STATES,
// under the same core folder (made there when missing).
static void queue_companions(const ShareWindow* window, bool to_remote, std::vector<ShareCopy>* copies)
{
    const std::vector<NetShareEntry>* from_roots = to_remote ? &window->local_roots : &window->remote_roots;
    const std::vector<NetShareEntry>* to_roots = to_remote ? &window->remote_roots : &window->local_roots;
    const ShareCompanions* companions = &window->companions;
    for (size_t i = 0; i < companions->files.size(); i++)
    {
        const ShareCompanion* file = &companions->files[i];
        NetShareKind kind = root_kind(from_roots, &file->path);
        const std::string* to_root = nullptr;
        for (size_t j = 0; j < to_roots->size() && to_root == nullptr; j++)
        {
            if ((*to_roots)[j].kind == kind)
            {
                to_root = &(*to_roots)[j].name;
            }
        }
        size_t slash = file->path.find('/');
        if (to_root == nullptr || slash == std::string::npos)
        {
            continue;   // the other side shares no SAVES (or STATES)
        }
        ShareCopy copy;
        copy.to_remote = to_remote;
        copy.from = file->path;
        copy.to = *to_root + file->path.substr(slash);
        copy.size = file->size;
        copy.mtime = file->mtime;
        copies->push_back(copy);
    }
}

static const NetShareEntry* selected_file(const SharePane* pane)
{
    if (pane->selected < 0 || static_cast<size_t>(pane->selected) >= pane->entries.size())
    {
        return nullptr;
    }
    const NetShareEntry* entry = &pane->entries[static_cast<size_t>(pane->selected)];
    return entry->kind == NetShareKind::file ? entry : nullptr;
}

// An arrow copies the selected file from `from` into the folder open in `to`.
static void draw_arrow(const ShareWindow* window, bool to_remote, bool connected, float width, float height,
                       const Localization* strings, ShareWindowAction* action)
{
    const SharePane* from = to_remote ? &window->local : &window->remote;
    const SharePane* to = to_remote ? &window->remote : &window->local;
    const NetShareEntry* file = selected_file(from);
    // With its saves and states: wait until they've all been found.
    bool along = window->with_companions && companions_for(window, !to_remote, from);
    bool can = connected && file != nullptr && !to->path.empty() && !to->waiting &&
               !(along && companions_looking(&window->companions));
    retro_ui_begin_disabled(!can);
    bool clicked = retro_ui_arrow_button(to_remote ? "##to_remote" : "##to_local",
                                         to_remote ? RetroUiArrow::right : RetroUiArrow::left, width, height);
    retro_ui_end_disabled();
    retro_ui_item_tooltip(piccolo_text(strings, to_remote ? Str::share_copy_to_remote_help : Str::share_copy_to_local_help));
    if (clicked && can)
    {
        ShareCopy copy;
        copy.to_remote = to_remote;
        copy.from = child(&from->path, &file->name);
        copy.to = child(&to->path, &file->name);
        copy.size = file->size;
        copy.mtime = file->mtime;
        action->copies.push_back(copy);
        if (along)
        {
            queue_companions(window, to_remote, &action->copies);
        }
    }
}

// The bottom line: the copy running, or what the arrows do.
static void draw_line(const ShareWindow* window, const ShareTransferLine* line, bool connected,
                      const Localization* strings, float block, ShareWindowAction* action)
{
    if (line == nullptr || !line->active)
    {
        retro_ui_dim_label(window->message.empty() ? piccolo_text(strings, Str::share_hint) : window->message.c_str(),
                           nullptr);
        if (connected)
        {
            RetroUiEndButton disconnect = {};
            disconnect.id = "disconnect";
            disconnect.label = piccolo_text(strings, Str::share_disconnect);
            if (retro_ui_end_buttons(&disconnect, 1, true) == 0)
            {
                action->press = ShareWindowPress::disconnect;
            }
        }
        return;
    }
    const char* verb = piccolo_text(strings, line->sending ? Str::share_sending : Str::share_receiving);
    char what[512] = {};
    std::snprintf(what, sizeof(what), "%s %s", verb, line->name.c_str());
    retro_ui_label(what);
    retro_ui_same_line();

    // The bar: the field color, filled with the highlight as far as it's done.
    float bar_width = 80.0f * block;
    float bar_height = 6.0f * block;
    float bar_y = (retro_ui_line_height() - bar_height) * 0.5f;
    retro_ui_canvas(bar_width, retro_ui_line_height());
    retro_ui_canvas_rect(0.0f, bar_y, bar_width, bar_height, retro_ui_palette()->field_face);
    float done = line->size > 0 ? static_cast<float>(static_cast<double>(line->done) / static_cast<double>(line->size)) : 1.0f;
    retro_ui_canvas_rect(0.0f, bar_y, bar_width * done, bar_height, retro_ui_palette()->highlight);
    retro_ui_same_line();

    char done_text[32] = {};
    char size_text[32] = {};
    format_size(line->done, done_text, sizeof(done_text));
    format_size(line->size, size_text, sizeof(size_text));
    if (line->paused)
    {
        retro_ui_text_format("%s / %s  %s", done_text, size_text, piccolo_text(strings, Str::share_paused));
    }
    else
    {
        retro_ui_text_format("%s / %s  %.1f MBIT/S", done_text, size_text, static_cast<double>(line->rate_kbps) / 1000.0);
    }
    if (line->queued > 0)
    {
        retro_ui_same_line();
        retro_ui_text_format("+%u", line->queued);
    }
    RetroUiEndButton cancel = {};
    cancel.id = "cancel";
    cancel.label = piccolo_text(strings, Str::share_cancel);
    if (retro_ui_end_buttons(&cancel, 1, true) == 0)
    {
        action->press = ShareWindowPress::cancel;
    }
}

// Under one side: WITH ITS SAVE AND N STATES for the game selected there.
// False when that side has nothing to show (the legend may go there).
static bool companions_shown(const ShareWindow* window, bool remote)
{
    const ShareCompanions* companions = &window->companions;
    return companions_for(window, remote, remote ? &window->remote : &window->local) &&
           (companions_looking(companions) || companions->saves > 0 || companions->states > 0);
}

static bool draw_companions(ShareWindow* window, bool remote, const Localization* strings)
{
    const ShareCompanions* companions = &window->companions;
    if (!companions_shown(window, remote))
    {
        return false;
    }
    if (companions_looking(companions))
    {
        retro_ui_dim_label("...", nullptr);
        return true;
    }
    char text[96] = {};
    if (companions->saves > 0 && companions->states > 1)
    {
        std::snprintf(text, sizeof(text), piccolo_text(strings, Str::share_with_save_states), companions->states);
    }
    else if (companions->saves > 0 && companions->states == 1)
    {
        std::snprintf(text, sizeof(text), "%s", piccolo_text(strings, Str::share_with_save_state));
    }
    else if (companions->saves > 0)
    {
        std::snprintf(text, sizeof(text), "%s", piccolo_text(strings, Str::share_with_save));
    }
    else if (companions->states > 1)
    {
        std::snprintf(text, sizeof(text), piccolo_text(strings, Str::share_with_states), companions->states);
    }
    else
    {
        std::snprintf(text, sizeof(text), "%s", piccolo_text(strings, Str::share_with_state));
    }
    retro_ui_check(remote ? "with_remote" : "with_local", text, &window->with_companions);
    retro_ui_item_tooltip(piccolo_text(strings, Str::share_with_help));
    return true;
}

static void draw_same_legend(const Localization* strings)
{
    retro_ui_dim_label(piccolo_text(strings, Str::share_same_legend), piccolo_text(strings, Str::share_same_help));
}

// A copy found another file with its name there: replace it? One at a
// time, or all of them (a game with its states may bring several).
static void draw_conflict(ShareWindow* window, const Localization* strings, ShareWindowAction* action)
{
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    RetroUiModal modal = {};
    modal.id = "share_replace";
    modal.title = piccolo_text(strings, Str::share_replace_title);
    modal.width = static_cast<float>(static_cast<int>(screen_width * 0.4f));
    if (!retro_ui_begin_modal(&modal, !window->conflicts.empty()))
    {
        return;
    }
    if (window->conflicts.empty())
    {
        retro_ui_close_modal();
        retro_ui_end_modal();
        return;
    }
    const ShareCopy* first = &window->conflicts[0];
    retro_ui_text_wrapped(piccolo_text(strings, Str::share_replace_text));
    retro_ui_dim_text_wrapped_at(first->to.c_str(), 0.0f);
    retro_ui_spacing();
    // Escape (B on a gamepad) skips them all: nothing is replaced unasked.
    bool skip_all = retro_ui_key_pressed(RetroUiKey::escape, true);
    if (retro_ui_button(piccolo_text(strings, Str::share_replace)))
    {
        ShareCopy copy = *first;
        copy.replace = true;
        action->copies.push_back(copy);
        window->conflicts.erase(window->conflicts.begin());
    }
    retro_ui_same_line();
    if (retro_ui_button(piccolo_text(strings, Str::share_skip)) && !window->conflicts.empty())
    {
        window->conflicts.erase(window->conflicts.begin());
    }
    if (window->conflicts.size() > 1)
    {
        char all[64] = {};
        std::snprintf(all, sizeof(all), piccolo_text(strings, Str::share_replace_all),
                      static_cast<unsigned>(window->conflicts.size()));
        if (retro_ui_button(all))
        {
            for (size_t i = 0; i < window->conflicts.size(); i++)
            {
                ShareCopy copy = window->conflicts[i];
                copy.replace = true;
                action->copies.push_back(copy);
            }
            window->conflicts.clear();
        }
        retro_ui_same_line();
        if (retro_ui_button(piccolo_text(strings, Str::share_skip_all)))
        {
            skip_all = true;
        }
    }
    if (skip_all)
    {
        window->conflicts.clear();
    }
    if (window->conflicts.empty())
    {
        retro_ui_close_modal();
    }
    retro_ui_end_modal();
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

void share_window_draw(ShareWindow* window, const ShareWindowState* state, RetroUiFont* pixel_font, int32_t scale,
                       ShareWindowAction* action)
{
    if (!window->open)
    {
        return;
    }
    const Localization* strings = state->strings;
    const NetShareLinkStatus* link = state->link;
    bool connected = state->connected;

    float block = static_cast<float>(scale);
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    retro_ui_push_theme(pixel_font, scale);
    // 560 x 300 retro pixels, never bigger than the window.
    float width = 560.0f * block;
    float height = 300.0f * block;
    width = width < screen_width - 8.0f * block ? width : screen_width - 8.0f * block;
    height = height < screen_height - 24.0f * block ? height : screen_height - 24.0f * block;
    RetroUiWindow frame = {};
    frame.id = "share";
    frame.title = piccolo_text(strings, Str::share_title);
    frame.width = width;
    frame.height = height;
    frame.centered_on_open = true;
    bool keep_open = true;
    bool closed = false;
    if (retro_ui_window_begin(&frame, scale, &closed))
    {
        if (closed)
        {
            keep_open = false;
        }
        // Two sides with the arrows between them; two rows left at the bottom.
        float middle = 30.0f * block;
        float spacing = retro_ui_item_spacing();
        PaneLayout layout;
        layout.width = static_cast<float>(static_cast<int32_t>((retro_ui_width_left() - middle - spacing * 2.0f) * 0.5f));
        // (header: name, combo, path; below: WITH ITS SAVE..., the copy line)
        float lists = retro_ui_height_left() - retro_ui_line_step() * 3.0f - retro_ui_text_line_step() * 2.0f -
                      retro_ui_line_step() * 2.0f;
        layout.folders_height = static_cast<float>(static_cast<int32_t>(lists * 0.38f));
        layout.files_height = static_cast<float>(static_cast<int32_t>(lists - layout.folders_height));

        float left_x = retro_ui_cursor_x();
        bool local_picked = false;
        bool remote_picked = false;
        draw_pane(&window->local, &window->local_roots, piccolo_text(strings, Str::share_this_piccolo), true, &layout,
                  strings, &local_picked);

        retro_ui_same_line();
        retro_ui_begin_group();
        // The arrows, about level with where the two lists meet.
        float top = retro_ui_line_step() + retro_ui_text_line_step() * 2.0f + layout.folders_height - 9.0f * block;
        retro_ui_space(middle, top);
        float arrow_width = 22.0f * block;
        float arrow_height = 18.0f * block;
        retro_ui_set_cursor_x(retro_ui_cursor_x() + (middle - arrow_width) * 0.5f);
        draw_arrow(window, true, connected, arrow_width, arrow_height, strings, action);
        retro_ui_set_cursor_x(retro_ui_cursor_x() + (middle - arrow_width) * 0.5f);
        draw_arrow(window, false, connected, arrow_width, arrow_height, strings, action);
        retro_ui_end_group();
        retro_ui_same_line();

        float right_x = retro_ui_cursor_x();
        if (connected)
        {
            draw_pane(&window->remote, &window->remote_roots, link->host_name.c_str(), true, &layout, strings,
                      &remote_picked);
        }
        else
        {
            draw_connect(window, link, &layout, strings, action);
        }
        // Its saves and states are looked up in share_window_apply.
        action->picked_local = local_picked;
        action->picked_remote = remote_picked;

        // Under the sides: a game's saves and states, and what grey means.
        // The legend takes the side without a checkbox (the right one first).
        bool legend = connected && window->any_same;
        bool right_box = connected && companions_shown(window, true);
        retro_ui_set_cursor_x(left_x);
        if (!draw_companions(window, false, strings))
        {
            if (legend && right_box)
            {
                draw_same_legend(strings);
                legend = false;
            }
            else
            {
                retro_ui_space(1.0f, retro_ui_line_height());
            }
        }
        retro_ui_same_line_at(right_x);
        if (!(connected && draw_companions(window, true, strings)))
        {
            if (legend)
            {
                draw_same_legend(strings);
            }
            else
            {
                retro_ui_space(1.0f, retro_ui_line_height());
            }
        }

        draw_line(window, state->line, connected, strings, block, action);
        draw_conflict(window, strings, action);
    }
    retro_ui_window_end();
    retro_ui_pop_theme();
    if (!keep_open)
    {
        window->open = false;
    }
}

// ---------------------------------------------------------------------------
// Apply

void share_window_open(ShareWindow* window)
{
    window->open = true;
    window->local.stale = true;
    window->remote.stale = true;
    window->remote.waiting = false;
    // A listing asked before the window closed may never be handed in: ask
    // again (a late answer for another folder is ignored).
    window->remote_asked = false;
    window->companions.waiting = false;
    window->same_stale = true;
}

ShareWindowRequest share_window_apply(ShareWindow* window, const ShareWindowAction* action,
                                      const NetShareFolders* local_folders, NetShareLink* link,
                                      const NetShareLinkStatus* status, const char* beacon)
{
    if (action->picked_local)
    {
        companions_start(window, false, local_folders);
    }
    else if (action->picked_remote)
    {
        companions_start(window, true, local_folders);
    }
    if (!net_share_link_started(link))
    {
        return ShareWindowRequest::none;   // nothing to connect, copy or cancel on
    }
    ShareWindowRequest request = ShareWindowRequest::none;
    switch (action->press)
    {
    case ShareWindowPress::none:
        break;
    case ShareWindowPress::pair:
        request = ShareWindowRequest::pair;
        break;
    case ShareWindowPress::pair_internet:
        request = ShareWindowRequest::pair_internet;
        break;
    case ShareWindowPress::connect:
        // Across the internet through BEACON SERVER (whatever INTERNET says:
        // that switch is about being reachable here, not reaching others).
        net_share_link_set_beacon(link, beacon);
        if (window->found_selected >= 0 && static_cast<size_t>(window->found_selected) < status->found.size())
        {
            // Always with a fresh code: a piccolo that knows us welcomes us
            // and it's never used; one that doesn't asks its user to type it.
            net_pair_code_generate(window->code);
            net_share_link_connect(link, status->found[static_cast<size_t>(window->found_selected)].address,
                                   window->code);
        }
        else if (window->paired_selected >= 0)
        {
            net_share_link_connect_paired(link, static_cast<uint32_t>(window->paired_selected));
        }
        else if (window->invite_selected)
        {
            // Across the internet to a piccolo that doesn't know us: our code,
            // which its user types in TOOLS > INVITE.
            net_pair_code_generate(window->code);
            net_share_link_pair_beacon(link, window->code);
        }
        break;
    case ShareWindowPress::disconnect:
        net_share_link_disconnect(link);
        break;
    case ShareWindowPress::cancel:
        break;   // after the copies, below
    }
    for (size_t i = 0; i < action->copies.size(); i++)
    {
        const ShareCopy* copy = &action->copies[i];
        const char* local = copy->to_remote ? copy->from.c_str() : copy->to.c_str();
        const char* remote = copy->to_remote ? copy->to.c_str() : copy->from.c_str();
        net_share_link_copy(link, copy->to_remote, local, remote, copy->size, copy->mtime,
                            copy->replace ? NET_SHARE_PUT_REPLACE : 0);
        window->message.clear();
    }
    if (action->press == ShareWindowPress::cancel)
    {
        net_share_link_cancel(link);
    }
    return request;
}
