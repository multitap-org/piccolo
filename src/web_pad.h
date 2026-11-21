#pragma once

// The page's touch gamepad (the browser build): buttons drawn in HTML by the
// core page (web/page/piccolo-pad.js) under or beside the picture, so the
// picture keeps the whole canvas. A controller like any other in the input
// router ("SCREEN PAD", a device that hands a RetroPad, as the on-screen pad
// and phones' pads do): START joins a port; remaps, turbo, DEVICES work on it.
//
// The page calls the exported piccolo_web_pad_show(on) when it shows or hides
// the pad (it answers 0 until main() attached the router: the page retries) and piccolo_web_pad_state(buttons) with the RetroPad buttons held
// (RETRO_DEVICE_ID_JOYPAD bits), on the main thread between frames. While it
// shows, the on-screen pad drawn in the canvas stays off.
//
// Desktop and Android (web_pad.cpp): nothing, never active.

struct InputSystem;

// Once the router exists: where the device goes.
void web_pad_attach(InputSystem* input);

// The page shows its pad (the in-canvas one stays off then).
bool web_pad_active();
