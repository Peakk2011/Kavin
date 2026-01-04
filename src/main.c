/*
    Copyright © 2025 Mint teams
    main.c | Main entry point for Kavin
*/

#include <stdio.h>
#include <signal.h>
#include <string.h> // Added for string manipulation
#include <stdlib.h> // Added for malloc and free
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

#include <watcher/watcher.h>

// Global flag to control the main loop, accessible by the signal handler.
static volatile sig_atomic_t g_running = 1;

static void signal_handler(int signum) {
    (void)signum;
    g_running = 0;
}

int main(int argc, char *argv[]) {
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <kavin_command> [-- <child_command_and_args>]\n", argv[0]);
        fprintf(stderr, "       %s <child_command> <file1> [file2] ...\n", argv[0]);
        fprintf(stderr, "Example (new): %s rs -- \"electron-forge start\"\n", argv[0]);
        fprintf(stderr, "Example (old): %s \"npm start\" src/main.js src/utils.js\n", argv[0]);
        return 1;
    }

    char *kavin_command = NULL;
    char *child_command = NULL;
    char **files_to_watch = NULL;
    int file_count = 0;
    int delimiter_idx = -1;

    // Find the "--" delimiter
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--") == 0) {
            delimiter_idx = i;
            break;
        }
    }

    if (delimiter_idx != -1) {
        // New usage: kavin <kavin_command> -- <child_command_and_args>
        kavin_command = argv[1];

        if (strcmp(kavin_command, "rs") != 0) {
             fprintf(stderr, "Error: Unknown Kavin command '%s'. Only 'rs' is supported with '--' delimiter.\n", kavin_command);
             return 1;
        }

        if (delimiter_idx + 1 >= argc) {
            fprintf(stderr, "Error: No child command provided after '--'.\n");
            return 1;
        }

        // Concatenate child command and its arguments
        size_t total_len = 0;
        for (int i = delimiter_idx + 1; i < argc; ++i) {
            total_len += strlen(argv[i]) + 1; // +1 for space or null terminator
        }

        child_command = (char *)malloc(total_len);
        if (child_command == NULL) {
            fprintf(stderr, "Error: Memory allocation failed.\n");
            return 1;
        }
        child_command[0] = '\0'; // Initialize as empty string

        for (int i = delimiter_idx + 1; i < argc; ++i) {
            strcat(child_command, argv[i]);
            if (i < argc - 1) {
                strcat(child_command, " ");
            }
        }

        // For "rs" mode with "--", no explicit files to watch are passed
        files_to_watch = NULL;
        file_count = 0;

    } else {
        // Old usage: kavin <command> <file1> [file2] ...
        if (argc < 3) {
            fprintf(stderr, "Error: Insufficient arguments for old usage. Expected at least: %s <command> <file1>\n", argv[0]);
            fprintf(stderr, "Example (old): %s \"npm start\" src/main.js\n", argv[0]);
            return 1;
        }
        child_command = argv[1];
        files_to_watch = &argv[2];
        file_count = argc - 2;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Clear the console screen while run
    printf("\033[2J\033[H");

    Watcher watcher;
    // Pass the command.
    watcher_init(&watcher, child_command, files_to_watch, file_count);

    /*
        The main logic is now encapsulated in watcher_run.
        We pass the global running flag to it.
    */
    watcher_run(&watcher, &g_running);

    // Cleanup message
    printf("\n[Kavin] Watcher stopped. Total restarts: %lu\n", watcher.restart_count);

    // Free dynamically allocated child_command if it was used
    if (delimiter_idx != -1 && child_command != NULL) {
        free(child_command);
    }
}

/*

    To compile all components together:
    gcc -O3 -march=native -flto -o kavin src/main.c src/watcher/watcher.c src/watcher/watcher_actions.c src/process/process.c -Isrc

    Usage: ./kavin <command> <file1> <file2> <file3> ...
    Example: ./kavin "npm start" src/main.js
    Or use many of file examole: ./kavin "npm start" src/main.js src/index.js ... rest of the file

*/