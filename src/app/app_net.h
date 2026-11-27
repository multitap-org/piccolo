#pragma once

// The network service: starting and stopping it, and the loop that follows
// its events (devices arriving and leaving, pairing, what they ask to load).
// (app_net.cpp; each function's comment is at its definition.)

#include "app.h"

// Starting and stopping
bool net_prepare(App* app);
void net_shutdown_all(App* app);

// A device asked for a game
void net_answer_load(App* app, NetContentStatus status);
void net_answer_load_refused(App* app);

// Once per loop
void net_update(App* app);
