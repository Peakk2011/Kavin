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
    char **files_to_watch;
    char **dirs_to_watch;
    int file_count;
    int dir_count;
    time_t *last_mtimes;
    pid_t process_id;
    volatile sig_atomic_t running;
    WatcherState state;
    struct timespec shutdown_start_time;
    unsigned long restart_count;
} Watcher;

void watcher_init(Watcher *watcher, const char *cmd, char **paths, int path_count);
void watcher_run(Watcher *watcher, volatile sig_atomic_t *running_flag);

#endif // WATCHER_H