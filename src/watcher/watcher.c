/*
    Copyright © 2025 Mint teams
    watcher.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void watcher_init(Watcher *watcher, const char *cmd, char **paths, int path_count) {
    watcher->cmd = cmd;
    watcher->files_to_watch = NULL;
    watcher->dirs_to_watch = NULL;
    watcher->file_count = 0;
    watcher->dir_count = 0;
    watcher->process_id = 0;
    watcher->running = 1;
    watcher->state = STATE_RESTARTING;
    watcher->restart_count = 0;
    watcher->last_mtimes = NULL;

    // Allocate space for file and directory pointers
    watcher->files_to_watch = malloc(sizeof(char*) * path_count);
    watcher->dirs_to_watch = malloc(sizeof(char*) * path_count);

    if (!watcher->files_to_watch || !watcher->dirs_to_watch) {
        perror("Failed to allocate memory for paths");
        exit(1);
    }

    for (int i = 0; i < path_count; ++i) {
        struct stat st;
        if (stat(paths[i], &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                watcher->dirs_to_watch[watcher->dir_count++] = paths[i];
            } else if (S_ISREG(st.st_mode)) {
                watcher->files_to_watch[watcher->file_count++] = paths[i];
            }
        } else {
            fprintf(stderr, "[Watcher warning] Path not found and will be ignored: %s\n", paths[i]);
        }
    }

    // Trim the arrays to actual size
    watcher->files_to_watch = realloc(watcher->files_to_watch, sizeof(char*) * watcher->file_count);
    watcher->dirs_to_watch = realloc(watcher->dirs_to_watch, sizeof(char*) * watcher->dir_count);
    watcher->last_mtimes = malloc(sizeof(time_t) * watcher->file_count);
    if (!watcher->last_mtimes) {
        perror("Failed to allocate memory for mtimes");
        exit(1);
    }
}

void watcher_run(Watcher *watcher, volatile sig_atomic_t *running_flag) {
    // Initial check and population of modification times
    for (int i = 0; i < watcher->file_count; ++i) {
        watcher->last_mtimes[i] = get_mtime(watcher->files_to_watch[i]);
        printf("[Watcher info] Watching: %s\n", watcher->files_to_watch[i]);
    }
    for (int i = 0; i < watcher->dir_count; ++i) {
        printf("[Watcher info] Watching directory: %s\n", watcher->dirs_to_watch[i]);
    }

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

    if (watcher->process_id > 0) {
        printf("\n[Watcher info] Shutting down process (PID: %d)...\n", watcher->process_id);
        watcher_initiate_shutdown(watcher);
        waitpid(watcher->process_id, NULL, 0);
    }

    // Free allocated memory
    for (int i = 0; i < watcher->file_count; ++i) {
        /*
            Note: This assumes original paths from argv are not freed elsewhere
            A solution would strdup all paths in init
        */
        free(watcher->files_to_watch[i]);
    }
    free(watcher->files_to_watch);
    free(watcher->dirs_to_watch);
    free(watcher->last_mtimes);
}