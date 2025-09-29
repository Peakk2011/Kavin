/*
    Copyright © 2025 Mint teams
    watcher.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <watcher/watcher.h>
#include <watcher/watcher_actions.h>

const useconds_t FILE_INTERVAL = 100000; // 0.1s

static time_t get_mtime(const char *filepath) {
    struct stat st;
    return (stat(filepath, &st) == 0) ? st.st_mtime : 0;
}

void watcher_init(Watcher *watcher, const char *cmd, const char *file) {
    watcher->cmd = cmd;
    watcher->file_to_watch = file;
    watcher->process_id = 0;
    watcher->last_mtime = 0;
    watcher->running = 1; // This is now controlled by the caller
    watcher->state = STATE_RESTARTING; // Initial state
    watcher->restart_count = 0;
}

void watcher_run(Watcher *watcher, volatile sig_atomic_t *running_flag) {
    watcher->last_mtime = get_mtime(watcher->file_to_watch);
    if (watcher->last_mtime == 0) {
        fprintf(stderr, "[Watcher error] File not found: %s\n", watcher->file_to_watch);
        return;
    }

    printf("[Watcher info] Watching: %s\n", watcher->file_to_watch);
    printf("[Watcher info] Command: %s\n", watcher->cmd);

    while (*running_flag) {
        switch (watcher->state) {
            case STATE_RUNNING:
                handle_state_running(watcher);
                break;

            case STATE_SHUTTING_DOWN:
                handle_state_shutting_down(watcher);
                break;

            case STATE_FORCE_KILLING:
                handle_state_force_killing(watcher);
                break;

            case STATE_RESTARTING:
                handle_state_restarting(watcher);
                break;
        }

        if (*running_flag) {
            usleep(FILE_INTERVAL);
        }
    }

    // Graceful shutdown initiated by signal
    if (watcher->process_id > 0) {
        printf("\n[Watcher info] Shutting down process (PID: %d)...\n", watcher->process_id);
        watcher_initiate_shutdown(watcher);
        waitpid(watcher->process_id, NULL, 0); // Wait for termination
    }
}