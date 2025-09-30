/*
    Copyright © 2025 Mint teams
    watcher_actions.c
    The generic Node.js process watcher
*/

#include "watcher_actions.h"
#include "../process/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>

#include <arch/syscalls.h>

static void add_watched_file(Watcher *watcher, const char *filepath) {
    // Check if file is already watched
    for (int i = 0; i < watcher->file_count; ++i) {
        if (strcmp(watcher->files_to_watch[i], filepath) == 0) {
            return;
        }
    }

    // Expand arrays if needed
    int new_count = watcher->file_count + 1;
    char **new_files = realloc(watcher->files_to_watch, sizeof(char *) * new_count);
    time_t *new_mtimes = realloc(watcher->last_mtimes, sizeof(time_t) * new_count);

    if (!new_files || !new_mtimes) {
        perror("Failed to reallocate memory for new file");
        return;
    }

    watcher->files_to_watch = new_files;
    watcher->last_mtimes = new_mtimes;

    watcher->files_to_watch[watcher->file_count] = strdup(filepath);
    if (!watcher->files_to_watch[watcher->file_count]) {
        perror("Failed to duplicate filepath string");
        return;
    }
    
    watcher->last_mtimes[watcher->file_count] = get_mtime_asm(filepath);
    watcher->file_count = new_count;

    printf("[Watcher info] Now watching new file: %s\n", filepath);
}

static void rescan_directories(Watcher *watcher) {
    for (int i = 0; i < watcher->dir_count; ++i) {
        DIR *d = opendir(watcher->dirs_to_watch[i]);
        if (!d) {
            continue;
        }

        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) { // Regular file
                /*
                    This example doesn't filter by extension,
                    But you could add `strstr(dir->d_name, ".js")` here.
                */
                char filepath[1024];
                snprintf(filepath, sizeof(filepath), "%s/%s", watcher->dirs_to_watch[i], dir->d_name);
                add_watched_file(watcher, filepath);
            }
        }
        closedir(d);
    }
}

int check_for_file_changes(Watcher *watcher) {
    // Rescan directories for new files.
    rescan_directories(watcher);

    for (int i = 0; i < watcher->file_count; ++i) {
        time_t current_mtime = get_mtime_asm(watcher->files_to_watch[i]);
        if (current_mtime != watcher->last_mtimes[i] && watcher->last_mtimes[i] != 0) {
            printf("[Watcher info] Change detected in %s! Restarting...\n", watcher->files_to_watch[i]);
            for (int j = 0; j < watcher->file_count; ++j) {
                watcher->last_mtimes[j] = get_mtime_asm(watcher->files_to_watch[j]);
            }
            return 1; // Change detected
        }
        // Handle deleted files
        if (current_mtime == 0 && watcher->last_mtimes[i] != 0) {
             printf("[Watcher info] File deleted: %s! Restarting...\n", watcher->files_to_watch[i]);
             return 1; // Deletion detected
        }
    }
    return 0;
}

void watcher_restart(Watcher *watcher) {
    printf("[Watcher info] Starting application\n");
    watcher->process_id = process_start(watcher->cmd);
    
    if (watcher->process_id > 0) {
        printf("[Watcher info] Started [PID: %d]\n", watcher->process_id);
    } else {
        fprintf(stderr, "[Watcher error] Failed to start process\n");
    }
}

void watcher_initiate_shutdown(Watcher *watcher) {
    if (watcher->process_id > 0) {
        process_stop(watcher->process_id);
        clock_gettime(CLOCK_MONOTONIC, &watcher->shutdown_start_time);
    }
}

void handle_state_running(Watcher *watcher) {
    int status;
    if (watcher->process_id > 0 && process_check_status(watcher->process_id, &status) == watcher->process_id) {
        printf("[Watcher info] Process died unexpectedly\n");
        watcher->state = STATE_RESTARTING;
        return;
    }

    if (check_for_file_changes(watcher)) {
        watcher_initiate_shutdown(watcher);
        watcher->state = STATE_SHUTTING_DOWN;
    }
}

void handle_state_shutting_down(Watcher *watcher) {
    if (watcher->process_id <= 0) {
        watcher->state = STATE_RESTARTING;
        return;
    }
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
    if (watcher->process_id <= 0) {
        watcher->state = STATE_RESTARTING;
        return;
    }
    int status;
    if (process_check_status(watcher->process_id, &status) == watcher->process_id) {
        watcher->state = STATE_RESTARTING;
    }
}

void handle_state_restarting(Watcher *watcher) {
    if (watcher->process_id > 0) {
        waitpid(watcher->process_id, NULL, 0);
        watcher->process_id = 0;
    }
    watcher_restart(watcher);
    if (watcher->process_id > 0) watcher->restart_count++;
    watcher->state = STATE_RUNNING;
}