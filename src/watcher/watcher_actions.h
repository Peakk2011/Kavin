/*
    Copyright © 2025 Mint teams
    watcher_actions.h
    The generic Node.js process watcher
*/

#ifndef WATCHER_ACTIONS_H
#define WATCHER_ACTIONS_H

#include <watcher/watcher.h>

void handle_state_running(Watcher *watcher);
void handle_state_shutting_down(Watcher *watcher);
void handle_state_force_killing(Watcher *watcher);
void handle_state_restarting(Watcher *watcher);

// Helper
void watcher_initiate_shutdown(Watcher *watcher);

#endif // WATCHER_ACTIONS_H