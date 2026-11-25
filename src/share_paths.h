#pragma once

// Where FILE TRANSFER's copies land (share_window.h), without the GUI, so
// tools/share_paths_test.cpp checks the rule.
//
// MIRROR FOLDER STRUCTURE (a checkbox in FILE TRANSFER, global, on by default; Andrés,
// 2026-11-25): a file goes to the same path relative to its shared folder on
// the other side, whatever folder is open there: "roms/gba/Metroid
// Fusion.gba" -> "ROMS/gba/Metroid Fusion.gba". The other side's shared
// folder is the one of the same kind (games -> games, saves -> saves, ...);
// when several share that kind, the one with the same name, otherwise the
// first. Missing folders are made by the side that receives
// (net_share_begin_put). Off, the file goes into the folder open on the other
// side, as before.
//
// Paths are virtual (net_share.h): the first part names a shared folder.
// `roots` are a side's root listing (its shared folders with their kinds).

#include <string>
#include <vector>

#include "net_share.h"

// The shared folder `path` starts with ("ROMS/gba" -> "ROMS").
std::string share_paths_root(const std::string* path);

// The shared folder in `roots` named like `path`'s first part, nullptr when
// there's none.
const NetShareEntry* share_paths_find_root(const std::vector<NetShareEntry>* roots, const std::string* path);

// The other side's shared folder for `root` (one of this side's): of its
// kind, the one with its name, otherwise the first. nullptr: none of that kind.
const NetShareEntry* share_paths_matching_root(const std::vector<NetShareEntry>* to_roots, const NetShareEntry* root);

// `from` (a file on one side) at the same path under the matching shared
// folder of the other side. False when `from` isn't inside a shared folder
// of `from_roots` or the other side shares none of its kind.
bool share_paths_mirror(const std::vector<NetShareEntry>* from_roots, const std::vector<NetShareEntry>* to_roots,
                        const std::string* from, std::string* to);

// Where an arrow's copy of `from` goes: mirrored when `mirror` is on and the
// other side has a folder of its kind, otherwise into `open_folder` (the
// folder open on the other side, "" = its shared folders, where no file
// can go). False: nowhere to copy it.
bool share_paths_target(bool mirror, const std::vector<NetShareEntry>* from_roots,
                        const std::vector<NetShareEntry>* to_roots, const std::string* from,
                        const std::string* open_folder, std::string* to);
