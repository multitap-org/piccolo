// Checks UNDO SAVE STATE's files (state_undo.h) in a scratch folder: a save
// over an empty slot and over a used one, undo, undo twice (redo), the
// thumbnails following their states, a failed save put back, and the names
// FILE TRANSFER drops.
//
// Usage: piccolo_state_undo_test <scratch directory>
// Exit code 0 = all checks passed. Each failure is printed.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "host_fs.h"
#include "host_log.h"
#include "state_undo.h"

static int g_failures = 0;

static void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", what);
        g_failures++;
    }
}

static void write_text(const std::string* path, const char* text)
{
    host_fs_write_file(path->c_str(), text, std::strlen(text));
}

// The file's contents ("" when missing, "<empty>" when zero bytes).
static std::string read_text(const std::string* path)
{
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!host_fs_exists(path->c_str()) || !host_fs_read_file(path->c_str(), &data, &size))
    {
        return "";
    }
    std::string text = size == 0 ? "<empty>" : std::string(reinterpret_cast<const char*>(data), size);
    delete[] data;
    return text;
}

// What a save does after state_undo_keep: the state and its picture written.
static void save(const std::string* state, const char* contents, bool picture)
{
    check(state_undo_keep(state->c_str()), "keep before a save");
    write_text(state, contents);
    if (picture)
    {
        std::string png = *state + ".png";
        std::string text = std::string("picture of ") + contents;
        write_text(&png, text.c_str());
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <scratch directory>\n", argv[0]);
        return 2;
    }
    host_log_set_min_level(LogLevel::error);
    std::string folder = argv[1];
    host_fs_make_directories(folder.c_str());
    std::string state = folder + "/Game (USA).state3";
    std::string png = state + ".png";
    std::string undo = state + ".undo";
    std::string png_undo = png + ".undo";
    host_fs_remove_file(state.c_str());
    host_fs_remove_file(png.c_str());
    host_fs_remove_file(undo.c_str());
    host_fs_remove_file(png_undo.c_str());

    // Nothing saved yet: nothing to undo.
    check(!state_undo_available(state.c_str()), "a new slot has no undo");
    check(!state_undo_swap(state.c_str()), "a new slot can't undo");

    // A save to the empty slot: undo empties it, undo again brings it back.
    save(&state, "A", true);
    check(state_undo_available(state.c_str()), "a save to an empty slot can be undone");
    check(read_text(&undo) == "<empty>", "the empty slot's undo is empty");
    check(!host_fs_exists(png_undo.c_str()), "no picture undo for an empty slot");
    check(state_undo_swap(state.c_str()), "undo the first save");
    check(!host_fs_exists(state.c_str()) && !host_fs_exists(png.c_str()), "the slot is empty again");
    check(read_text(&undo) == "A" && read_text(&png_undo) == "picture of A", "the save kept as the undo");
    check(state_undo_swap(state.c_str()), "redo the first save");
    check(read_text(&state) == "A" && read_text(&png) == "picture of A", "the first save is back");
    check(read_text(&undo) == "<empty>" && !host_fs_exists(png_undo.c_str()), "its undo is empty again");

    // An overwrite: the old state and picture move aside.
    save(&state, "B", true);
    check(read_text(&state) == "B" && read_text(&png) == "picture of B", "overwritten");
    check(read_text(&undo) == "A" && read_text(&png_undo) == "picture of A", "the old state and picture kept");
    check(state_undo_swap(state.c_str()), "undo the overwrite");
    check(read_text(&state) == "A" && read_text(&png) == "picture of A", "the old state is back");
    check(read_text(&undo) == "B" && read_text(&png_undo) == "picture of B", "the overwrite is the undo now");
    check(state_undo_swap(state.c_str()), "undo twice");
    check(read_text(&state) == "B" && read_text(&png) == "picture of B", "undo twice is redo");
    check(read_text(&undo) == "A", "and the old state is the undo again");

    // One level: another save drops the oldest.
    save(&state, "C", false);
    check(read_text(&state) == "C" && !host_fs_exists(png.c_str()), "a save without a picture");
    check(read_text(&undo) == "B" && read_text(&png_undo) == "picture of B", "one level: B is the undo");
    check(state_undo_swap(state.c_str()), "undo a save without a picture");
    check(read_text(&state) == "B" && read_text(&png) == "picture of B", "B and its picture back");
    check(read_text(&undo) == "C" && !host_fs_exists(png_undo.c_str()), "C without a picture is the undo");
    check(!host_fs_exists((undo + ".swap").c_str()) && !host_fs_exists((png_undo + ".swap").c_str()),
          "no spare files left");

    // The slot emptied outside piccolo: the undo comes back, and undoing again empties it.
    host_fs_remove_file(state.c_str());
    host_fs_remove_file(png.c_str());
    check(state_undo_swap(state.c_str()) && read_text(&state) == "C" && read_text(&undo) == "<empty>",
          "a slot emptied elsewhere gets its undo back");
    check(state_undo_swap(state.c_str()) && !host_fs_exists(state.c_str()) && read_text(&undo) == "C",
          "and undoing again empties it");

    // A failed save puts the slot back as it was, without an undo.
    host_fs_remove_file(undo.c_str());
    write_text(&state, "D");
    write_text(&png, "picture of D");
    check(state_undo_keep(state.c_str()), "keep before a save that fails");
    write_text(&state, "half");
    state_undo_cancel(state.c_str());
    check(read_text(&state) == "D" && read_text(&png) == "picture of D", "a failed save: the slot as it was");
    check(!state_undo_available(state.c_str()), "a failed save leaves no undo");
    host_fs_remove_file(state.c_str());
    host_fs_remove_file(png.c_str());
    check(state_undo_keep(state.c_str()), "keep before a save to an empty slot that fails");
    write_text(&state, "half");
    state_undo_cancel(state.c_str());
    check(!host_fs_exists(state.c_str()) && !state_undo_available(state.c_str()), "a failed save: empty again");

    // FILE TRANSFER's filter.
    std::string names[] = {"Game.state3.undo", "Game.state3.png.undo", "Game.state3.undo.swap"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        check(state_undo_is_undo_file(&names[i]), "an undo file is dropped");
    }
    std::string kept[] = {"Game.state3", "Game.state3.png", "Game.srm", "Undo.sfc", "Game.undone"};
    for (size_t i = 0; i < sizeof(kept) / sizeof(kept[0]); i++)
    {
        check(!state_undo_is_undo_file(&kept[i]), "other files are kept");
    }

    host_fs_remove_file(state.c_str());
    host_fs_remove_file(png.c_str());
    host_fs_remove_file(undo.c_str());
    host_fs_remove_file(png_undo.c_str());
    if (g_failures == 0)
    {
        std::printf("state undo: all checks passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
}
