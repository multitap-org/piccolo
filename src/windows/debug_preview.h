#pragma once

// The debug window's previews (debug_window.h; Andrés: "anytime a new dialog
// is added, or a pop-up it needs to be added to the debug window so I can
// see it live"). The real notification cards and windows, drawn by their own
// functions from fake state kept here, never the App's: the real windows
// don't know, and nothing reaches the network, the settings or the game.
//
// One window preview at a time (DebugPreview). What's pressed in it is
// ignored, except closing it (its X, CLOSE, NO / YES, Escape: main.cpp's
// back), which ends it. The real window opening (a real pairing request,
// say) ends its preview too: both would be the toolkit's same window.
//
// Split like every window: debug_preview_show / _update / _state fill and
// follow the fake state (no GUI), debug_preview_draw draws the window shown
// with its own draw function, and debug_preview_apply ends the preview when
// it was closed (no GUI). The cards go into the HUD with the windows' own
// *_add_hud.

#include <cstdint>

#include "about_window.h"
#include "debug_window.h"
#include "dvr_view.h"
#include "guest_window.h"
#include "hud_view.h"
#include "localization.h"
#include "mobile_home.h"
#include "ports_widget.h"
#include "net_share_link.h"
#include "onscreen_pad.h"
#include "pair_window.h"
#include "quick_menu.h"
#include "settings_error_window.h"

#include "retro_ui_font.h"

// The cards the debug window brings up for a few seconds.
enum class DebugCard
{
    link_copied,      // REMOTE PLAY's LINK COPIED TO THE CLIPBOARD
    pair_timed_out,   // PAIRING TIMED OUT
    pair_failed,      // PAIRING FAILED
};

struct DebugPreviews
{
    // The cards, each from a window's own state that only they use.
    GuestWindow card_guest;            // LINK COPIED: its copied_ms
    PairWindow  card_pair;             // PAIRING TIMED OUT / FAILED, and a request in the game
    bool        request_card = false;  // the fake request's card is up

    // The window shown, and the fake state its draw function gets.
    DebugPreview       shown = DebugPreview::none;
    PairWindow         pair;
    NetShareLinkStatus pair_link;      // the devices PAIR WITH A HOST finds
    AboutWindow        about;
    GuestWindow        guest;          // REMOTE PLAY (its picture: none)
    QuickMenu          quick;          // its slots: a few used, no pictures
    MobileHome         phone;          // the phone's HOME and ☰
    DvrView            phone_dvr;      // the DVR's phone panel: five empty pictures, a kept lane
    PortsWidget        devices;        // DEVICES: its board open, fake controllers
};

// What the preview shows this frame (UiView).
struct DebugPreviewView
{
    DebugPreview     shown = DebugPreview::none;
    PairWindowState  pair = {};
    AboutWindowState about = {};
    GuestWindowState guest = {};
    QuickMenuState   quick = {};
    MobileHomeState  phone = {};
    DvrViewText      dvr_text = {};
    PortsWidgetState devices = {};
    SettingsErrorWindowState settings_error = {};
    OnscreenPadState pad_menu = {};   // the burger menu: its labels and the pad's look
};

// What was pressed in it (UiActions): only closing it counts.
struct DebugPreviewActions
{
    PairWindowAction  pair = {};
    AboutWindowAction about = {};
    GuestWindowAction guest = {};
    QuickMenuAction   quick = {};
    MobileHomeAction  phone = {};
    DvrViewInput      dvr = {};
    PortsWidgetAction devices = {};
    SettingsErrorWindowAction settings_error = {};
    OnscreenPadMenuPress pad_menu = OnscreenPadMenuPress::none;   // a pick is ignored; a tap outside ends it
};

// Shows `which` (another one shown ends), from its first step's fake state.
void debug_preview_show(DebugPreviews* previews, DebugPreview which);
void debug_preview_end(DebugPreviews* previews);
bool debug_preview_shown(const DebugPreviews* previews);
// A phone screen is shown: it covers the whole screen, so the debug window
// (and the phone's own HOME) step aside until it ends.
bool debug_preview_covers_screen(const DebugPreviews* previews);
// Before the view, every frame: the real window open ends its preview
// (`devices_open`: the ports widget's DEVICES board; `dvr_open`: the DVR).
void debug_preview_update(DebugPreviews* previews, bool pair_open, bool about_open, bool guest_open, bool quick_open,
                          bool settings_error_open, bool devices_open, bool dvr_open);
// `version`: ABOUT's (PICCOLO_VERSION). `menus`: the menu bar's, for the
// phone's ☰ (the real items; only the games and the paused game are fake).
// `phone_upright`: the phone UI held upright (DEVICES' rows). `pad`: the
// touch gamepad's state (onscreen_pad_state), for the burger menu's look.
void debug_preview_state(DebugPreviewView* view, const DebugPreviews* previews, const Localization* strings,
                         const char* version, const RetroUiMenu* menus, int32_t menu_count, bool phone_upright,
                         const OnscreenPadState* pad);
// Draws the window shown, if any.
void debug_preview_draw(DebugPreviews* previews, const DebugPreviewView* view, RetroUiFont* pixel_font,
                        int32_t scale, DebugPreviewActions* actions);
// Ends the preview when it was closed; everything else pressed is ignored
// (the phone's HOME: ☰ moves on to the sidebar's preview).
void debug_preview_apply(DebugPreviews* previews, const DebugPreviewActions* actions);

void debug_preview_show_card(DebugPreviews* previews, DebugCard card, uint64_t now_ms);
// The fake pairing request's card ("<NAME> WANTS TO PAIR / HOLD START TO
// ACCEPT"): up until taken down, or until START held on it.
void debug_preview_request_card(DebugPreviews* previews, bool shown, uint64_t now_ms);
// Every frame: START held (`held`, pair_window_hold_accept's) while the
// player is in the game (`in_game`) fills the fake card's key as on the real
// one; once full the card goes and nothing else happens (no request is
// there to accept: no window opens).
void debug_preview_hold_request(DebugPreviews* previews, bool in_game, bool held, uint64_t now_ms);
// The fake cards, with the real ones (the request's shows with the menu up
// too, so it can be seen from the debug window).
void debug_preview_add_hud(const DebugPreviews* previews, const Localization* strings, HudStack* stack,
                           uint64_t now_ms);
