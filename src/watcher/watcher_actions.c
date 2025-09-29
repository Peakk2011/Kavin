/*
    Copyright © 2025 Mint teams
    watcher_actions.c
*/

#include "watcher_actions.h"
#include "../process/process.h"
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static time_t get_mtime(const char *filepath) {
    struct stat st;
    return (stat(filepath, &st) == 0) ? st.st_mtime : 0;
}

static void watcher_restart(Watcher *watcher) {
    printf("[Watcher info] Starting application\n");
    watcher->process_id = process_start(watcher->cmd);
    
    if (watcher->process_id > 0) {
        printf("[Watcher info] Started [PID: %d]\n", watcher->process_id);
    } else {
        fprintf(stderr, "[Watcher error] Failed to start process\n");
    }
}

void watcher_initiate_shutdown(Watcher *watcher) {
    process_stop(watcher->process_id);
    clock_gettime(CLOCK_MONOTONIC, &watcher->shutdown_start_time);
}

void handle_state_running(Watcher *watcher) {
    int status;
    if (process_check_status(watcher->process_id, &status) == watcher->process_id) {
        printf("[Watcher info] Process died unexpectedly. Restarting...\n");
        watcher->state = STATE_RESTARTING;
        return; // Return early to switch state immediately
    }

    time_t current_mtime = get_mtime(watcher->file_to_watch);
    if (current_mtime != watcher->last_mtime && watcher->last_mtime != 0) {
        printf("[Watcher info] Change detected! Restarting...\n");
        watcher->last_mtime = current_mtime;
        watcher_initiate_shutdown(watcher);
        watcher->state = STATE_SHUTTING_DOWN;
    }
}

void handle_state_shutting_down(Watcher *watcher) {
    int status;
    if (process_check_status(watcher->process_id, &status) == watcher->process_id) {
        watcher->state = STATE_RESTARTING;
    } else {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - watcher->shutdown_start_time.tv_sec >= 2) {
            printf("[Watcher info] Process did not respond to SIGTERM, sending SIGKILL...\n");
            process_kill(watcher->process_id);
            watcher->state = STATE_FORCE_KILLING;
        }
    }
}

void handle_state_force_killing(Watcher *watcher) {
    int status;
    if (process_check_status(watcher->process_id, &status) == watcher->process_id) {
        watcher->state = STATE_RESTARTING;
    }
}

void handle_state_restarting(Watcher *watcher) {
    if (watcher->process_id > 0) {
        waitpid(watcher->process_id, NULL, 0); // Ensure previous process is cleaned up
        watcher->process_id = 0;
    }
    watcher_restart(watcher);
    if (watcher->process_id > 0) watcher->restart_count++;
    watcher->state = STATE_RUNNING;
}