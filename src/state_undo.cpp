#include "state_undo.h"

#include <cstdint>

#include "host_fs.h"
#include "host_log.h"

static bool ends_with(const std::string* text, const char* end)
{
    std::string tail = end;
    return text->size() >= tail.size() && text->compare(text->size() - tail.size(), tail.size(), tail) == 0;
}

// `a` and `b` swapped, either of them possibly missing. `spare`: a free name
// for the moment both exist.
static bool swap_files(const std::string* a, const std::string* b, const std::string* spare)
{
    bool has_a = host_fs_exists(a->c_str());
    bool has_b = host_fs_exists(b->c_str());
    if (has_a && has_b)
    {
        return host_fs_rename(a->c_str(), spare->c_str()) && host_fs_rename(b->c_str(), a->c_str()) &&
               host_fs_rename(spare->c_str(), b->c_str());
    }
    if (has_a)
    {
        return host_fs_rename(a->c_str(), b->c_str());
    }
    if (has_b)
    {
        return host_fs_rename(b->c_str(), a->c_str());
    }
    return true;
}

// The empty undo of a slot that was empty (zero bytes: never a savestate).
static bool write_empty(const std::string* path)
{
    uint8_t nothing = 0;
    return host_fs_write_file(path->c_str(), &nothing, 0);
}

bool state_undo_keep(const char* state_path)
{
    std::string state = state_path;
    std::string undo = state + ".undo";
    std::string picture = state + ".png";
    std::string picture_undo = picture + ".undo";
    bool had_state = host_fs_exists(state.c_str());
    bool ok = had_state ? host_fs_rename(state.c_str(), undo.c_str()) : write_empty(&undo);
    bool kept_state = ok;   // the undo file is now the slot's old state (or its empty marker)
    if (ok)
    {
        ok = host_fs_exists(picture.c_str()) ? host_fs_rename(picture.c_str(), picture_undo.c_str())
                                             : host_fs_remove_file(picture_undo.c_str());
    }
    if (!ok)
    {
        host_log(LogLevel::warning, "Undo save state: could not keep %s; no undo for this save", state_path);
        host_fs_remove_file(picture_undo.c_str());   // an older picture's leftover: its state is going
        if (kept_state && had_state)
        {
            // The ".undo" is now the only copy of the old state, and the save
            // that follows may fail too: put it back instead of deleting it.
            // If that fails as well, leave it where it is rather than lose it.
            if (!host_fs_rename(undo.c_str(), state.c_str()))
            {
                host_log(LogLevel::error, "Undo save state: could not put %s back", state_path);
            }
        }
        else
        {
            // Either an empty marker or an older undo that was never replaced:
            // nothing of the slot's current state to lose.
            host_fs_remove_file(undo.c_str());
        }
    }
    return ok;
}

void state_undo_cancel(const char* state_path)
{
    std::string state = state_path;
    std::string undo = state + ".undo";
    std::string picture = state + ".png";
    std::string picture_undo = picture + ".undo";
    if (!host_fs_exists(undo.c_str()))
    {
        return;   // nothing was kept
    }
    uint64_t undo_size = 0;
    uint64_t undo_mtime = 0;
    bool was_empty = host_fs_file_info(undo.c_str(), &undo_size, &undo_mtime) && undo_size == 0;
    bool ok = was_empty ? host_fs_remove_file(undo.c_str()) && host_fs_remove_file(state.c_str())
                        : host_fs_rename(undo.c_str(), state.c_str());
    if (ok)
    {
        ok = host_fs_exists(picture_undo.c_str()) ? host_fs_rename(picture_undo.c_str(), picture.c_str())
                                                  : host_fs_remove_file(picture.c_str());
    }
    if (!ok)
    {
        host_log(LogLevel::error, "Undo save state: could not put %s back after a failed save", state_path);
    }
}

bool state_undo_swap(const char* state_path)
{
    std::string state = state_path;
    std::string undo = state + ".undo";
    std::string spare = undo + ".swap";
    if (!host_fs_exists(undo.c_str()))
    {
        return false;
    }
    uint64_t undo_size = 0;
    uint64_t undo_mtime = 0;
    if (!host_fs_file_info(undo.c_str(), &undo_size, &undo_mtime))
    {
        return false;
    }
    bool slot_used = host_fs_exists(state.c_str());
    bool ok = true;
    if (undo_size == 0)
    {
        // The slot was empty before its last save: empty again, the save
        // kept as the undo (nothing to do when both are empty).
        if (slot_used)
        {
            ok = host_fs_rename(state.c_str(), undo.c_str());
        }
    }
    else if (slot_used)
    {
        ok = swap_files(&state, &undo, &spare);
    }
    else
    {
        // The slot was emptied outside piccolo: the undo comes back, and
        // undoing again empties it as it was.
        ok = host_fs_rename(undo.c_str(), state.c_str()) && write_empty(&undo);
    }
    if (ok)
    {
        std::string picture = state + ".png";
        std::string picture_undo = picture + ".undo";
        std::string picture_spare = picture_undo + ".swap";
        ok = swap_files(&picture, &picture_undo, &picture_spare);
    }
    if (!ok)
    {
        host_log(LogLevel::error, "Undo save state: could not swap %s with its undo", state_path);
    }
    return ok;
}

bool state_undo_available(const char* state_path)
{
    std::string undo = state_path;
    undo += ".undo";
    return host_fs_exists(undo.c_str());
}

bool state_undo_is_undo_file(const std::string* name)
{
    return ends_with(name, ".undo") || ends_with(name, ".undo.swap");
}
