#pragma once

// FILE TRANSFER and TOOLS > PAIR: what this device shares and says about
// itself, the connection both windows use, and their glue.
// (app_share.cpp; each function's comment is at its definition.)

#include "app.h"

// This device, and the connection both windows use
void share_folder_paths(const App* app, NetShareFolderPaths* paths);
const NetDeviceInfo* net_device_info(App* app);
bool start_share_link(App* app);

// TOOLS > PAIR and pairing requests
void pair_update(App* app);
void pair_apply(App* app, const PairWindowAction* action);
void pair_expire(App* app);
bool pair_request_in_game(const App* app);
bool pair_accept_held(const App* app);

// FILE TRANSFER
void open_share_window(App* app);
void share_update(App* app, ShareTransferLine* line);
void share_pair(App* app, bool internet);
