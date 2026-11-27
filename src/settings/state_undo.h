#pragma once

// UNDO SAVE STATE's files (Andrés, 2026-11-26): before a save overwrites a
// slot, its old state and thumbnail move aside on disk, so the undo survives
// a quit or a crash (which is when people notice):
//
//   "<game>.state3"      -> "<game>.state3.undo"
//   "<game>.state3.png"  -> "<game>.state3.png.undo"
//
// Undo swaps them back, so undoing twice is redo. One level per slot. A slot
// that was empty before the save keeps an empty "<game>.state3.undo" (a
// savestate is never zero bytes): undo empties the slot, and undo again
// brings the save back. The slot list and thumbnails look for the exact
// names, and FILE TRANSFER drops these files (state_undo_is_undo_file).
//
// Paths are the state file's (libretro_host_state_path); everything here is
// files only, no core, so tools/state_undo_test.cpp checks it. Not a hot path.

#include <string>

// Before a save to `state_path`: its state and thumbnail moved to the undo
// names (an empty slot: an empty undo). False (logged) if a file couldn't
// be moved; the old undo is then removed, so undo never brings back an
// older state than the one just overwritten. The save goes ahead either way.
bool state_undo_keep(const char* state_path);

// The save after state_undo_keep failed: the kept state and thumbnail go
// back into the slot (an empty slot: emptied again), and there's no undo.
void state_undo_cancel(const char* state_path);

// The slot and its undo swapped (state and thumbnail). False (logged) when
// there's nothing to undo or a file couldn't be moved.
bool state_undo_swap(const char* state_path);

// The slot has an undo.
bool state_undo_available(const char* state_path);

// A name (a file's, any folder) that is one of these files, or what a swap
// cut short left behind: "x.state3.undo", "x.state3.png.undo", "x.state3.undo.swap".
bool state_undo_is_undo_file(const std::string* name);
