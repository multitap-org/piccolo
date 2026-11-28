#pragma once

// Messages, a HUD widget (Andrés, testing round 3: "any messages are
// popups"; there is no status line any more): what piccolo reports (a state
// saved, a screenshot taken, a device paired, a core's own message) shows
// as a notification card in the HUD's bottom right stack for a few seconds,
// then fades out. A message already showing is kept (shown longer), not
// repeated. Which messages there are is the app's (set_status), chosen by
// Andrés one by one: failures always, successes only where nothing else
// shows them (a hotkey's SAVE STATE, not the menu's).

#include <cstdint>

struct HudStack;

constexpr uint32_t MESSAGE_WIDGET_MAX = 4;          // the oldest goes when a fifth comes
constexpr uint64_t MESSAGE_WIDGET_SHOW_MS = 4000;   // each one's time, its fade included
constexpr uint64_t MESSAGE_WIDGET_FADE_MS = 500;
constexpr uint32_t MESSAGE_WIDGET_TEXT = 96;

struct MessageEntry
{
    char     text[MESSAGE_WIDGET_TEXT] = {};
    uint64_t since_ms = 0;   // when it came (the card's slide-in)
    uint64_t until_ms = 0;   // when it's gone
};

struct MessageWidget
{
    MessageEntry entries[MESSAGE_WIDGET_MAX];
    uint32_t     count = 0;
};

// A new message (UTF-8, cut to fit). Empty: nothing.
void message_widget_post(MessageWidget* widget, const char* text, uint64_t now_ms);

// Drops what's gone, then its cards into the frame's HUD stack.
void message_widget_add_hud(MessageWidget* widget, HudStack* stack, uint64_t now_ms);
