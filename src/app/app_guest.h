#pragma once

// REMOTE PLAY's guest links: the link, its card on the beacon, the window.
// (app_guest.cpp; each function's comment is at its definition.)

#include "app.h"

// The link and its card
void open_guest_window(App* app);
void guest_make_link(App* app);
void guest_end(App* app, NetAccess* access);
void guest_update_locked(App* app);
