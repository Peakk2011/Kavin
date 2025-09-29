/*
    Copyright © 2025 Mint teams
    watcher.h
*/

#ifndef WATCHER_H
#define WATCHER_H

#include <signal.h>
#include <sys/types.h>
#include <time.h>

extern const useconds_t FILE_INTERVAL;

typedef enum {
    STATE_RUNNING,
    STATE_SHUTTING_DOWN,
    STATE_FORCE_KILLING,
    STATE_RESTARTING
} WatcherState;

typedef struct {
    const char *cmd;
    const char *file_to_watch;
    pid_t process_id;
    time_t last_mtime;
    volatile sig_atomic_t running;
    WatcherState state;
    struct timespec shutdown_start_time;
    unsigned long restart_count;
} Watcher;

void watcher_init(Watcher *watcher, const char *cmd, const char *file);
void watcher_run(Watcher *watcher, volatile sig_atomic_t *running_flag);

#endif // WATCHER_H