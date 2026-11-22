#pragma once

// The page's touch gamepad (the browser build): buttons drawn in HTML by the
// core page (web/page/piccolo-pad.js) under or beside the picture, so the
// picture keeps the whole canvas. A controller like any other in the input
// router ("SCREEN PAD", a device that hands a RetroPad, as the on-screen pad
// and phones' pads do), on port 1 from the moment it shows without taking it
// from a gamepad (InputDevice::shares_port1); remaps, turbo, DEVICES work on it.
// In the menus it moves and accepts like any pad (input_bindings: a RetroPad
// device's interface).
//
// The page calls the exported piccolo_web_pad_show(on) when it shows or hides
// the pad (it answers 0 until main() attached the router: the page retries)
// and piccolo_web_pad_state(buttons) with the RetroPad buttons held
// (RETRO_DEVICE_ID_JOYPAD bits) plus its QUICK and MENU buttons' bits, on
// the main thread between frames. While it shows, the on-screen pad drawn in
// the canvas stays off.
//
// The other way, piccolo tells the page whether a game is being played (GAME
// mode: the page hides its strip) and the picture's shape (an upright phone
// gives the picture only the height it needs, the pad the rest), through
// multitap_page_game(playing, aspect) in web/multitap-page.js.
//
// Desktop and Android (web_pad.cpp): nothing, never active.

#include <cstdint>

struct InputSystem;

// The page's QUICK and MENU buttons, above the RetroPad's 16 buttons (never sent to a core).
constexpr int32_t WEB_PAD_QUICK_MENU_BIT = 16;
constexpr int32_t WEB_PAD_MAIN_MENU_BIT  = 17;

// Once the router exists: where the device goes.
void web_pad_attach(InputSystem* input);

// The page shows its pad (the in-canvas one stays off then).
bool web_pad_active();

// QUICK / MENU was pressed on the page's pad since the last call.
bool web_pad_take_quick_menu();
bool web_pad_take_main_menu();

// What the page shows around the game: `playing` = a game in GAME mode,
// `aspect` = the shown picture's width / height (0: none). Only a change
// reaches the page.
void web_pad_tell_page(bool playing, double aspect);

// SETTINGS' touch pad VIBRATE: the page's pad buzzes on a press only with it
// on, as piccolo's own touch pad does. Only a change reaches the page.
void web_pad_set_vibrate(bool on);
