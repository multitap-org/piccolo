#pragma once

// What connected devices are told and what they ask for: the game's state,
// the actions, controllers and devices, the speed test, the stream, the
// games list.
// (app_net_remote.cpp; each function's comment is at its definition.)

#include "app.h"

// The game's state
void net_send_game_state(App* app, uint32_t peer);
void net_follow_game_state(App* app);

// GO HEADLESS
void headless_tell(App* app);
void headless_back(App* app, bool pause);

// Actions
void net_send_actions(App* app, uint32_t peer, uint32_t id);

// Controllers
void net_send_controllers(App* app, uint32_t peer, uint32_t id);

// Following changes, and the remote's messages
void net_follow_remote(App* app);
void net_remote_message(App* app, const NetAccessEvent* event, const char* device);

// Speed test
void net_speed_request(App* app, const NetAccessEvent* event);
void net_speed_result(App* app, const NetAccessEvent* event, int32_t device_now);
void net_speed_update(App* app);

// The stream
void net_follow_stream_tuning(App* app);
void net_stream_message(App* app, const NetAccessEvent* event, const char* device);
void net_stream_frame(App* app);

// Games
void net_games_message(App* app, const NetAccessEvent* event, const char* device);
