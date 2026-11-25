#include "share_paths.h"

std::string share_paths_root(const std::string* path)
{
    size_t slash = path->find('/');
    return slash == std::string::npos ? *path : path->substr(0, slash);
}

const NetShareEntry* share_paths_find_root(const std::vector<NetShareEntry>* roots, const std::string* path)
{
    std::string root = share_paths_root(path);
    for (size_t i = 0; i < roots->size(); i++)
    {
        if ((*roots)[i].name == root)
        {
            return &(*roots)[i];
        }
    }
    return nullptr;
}

const NetShareEntry* share_paths_matching_root(const std::vector<NetShareEntry>* to_roots, const NetShareEntry* root)
{
    const NetShareEntry* first = nullptr;
    for (size_t i = 0; i < to_roots->size(); i++)
    {
        const NetShareEntry* candidate = &(*to_roots)[i];
        if (candidate->kind != root->kind)
        {
            continue;
        }
        if (candidate->name == root->name)
        {
            return candidate;
        }
        if (first == nullptr)
        {
            first = candidate;
        }
    }
    return first;
}

bool share_paths_mirror(const std::vector<NetShareEntry>* from_roots, const std::vector<NetShareEntry>* to_roots,
                        const std::string* from, std::string* to)
{
    size_t slash = from->find('/');
    const NetShareEntry* root = share_paths_find_root(from_roots, from);
    if (slash == std::string::npos || root == nullptr)
    {
        return false;   // not a file inside a shared folder
    }
    const NetShareEntry* target = share_paths_matching_root(to_roots, root);
    if (target == nullptr)
    {
        return false;
    }
    *to = target->name + from->substr(slash);
    return true;
}

bool share_paths_target(bool mirror, const std::vector<NetShareEntry>* from_roots,
                        const std::vector<NetShareEntry>* to_roots, const std::string* from,
                        const std::string* open_folder, std::string* to)
{
    if (mirror && share_paths_mirror(from_roots, to_roots, from, to))
    {
        return true;
    }
    // Into the folder open there (also when mirroring finds no folder of the
    // file's kind there: an older piccolo without SCREENSHOTS, say).
    if (open_folder->empty())
    {
        return false;
    }
    size_t slash = from->rfind('/');
    std::string name = slash == std::string::npos ? *from : from->substr(slash + 1);
    *to = *open_folder + "/" + name;
    return true;
}
