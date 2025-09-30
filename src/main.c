/*
    Copyright © 2025 Mint teams
    main.c | Main entry point for Kavin
*/

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include <watcher/watcher.h>

// Global flag to control the main loop, accessible by the signal handler.
static volatile sig_atomic_t g_running = 1;

static void signal_handler(int signum) {
    g_running = 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <command> <file1> [file2] ...\n", argv[0]);
        fprintf(stderr, "Example: %s \"npm start\" src/main.js src/utils.js\n", argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    Watcher watcher;
    // Pass the command.
    watcher_init(&watcher, argv[1], &argv[2], argc - 2);

    /*
        The main logic is now encapsulated in watcher_run.
        We pass the global running flag to it.
    */
    watcher_run(&watcher, &g_running);

    // Cleanup message
    printf("\n[Kavin] Watcher stopped. Total restarts: %lu\n", watcher.restart_count);

    return 0;
}

/*

    To compile all components together:
    gcc -O3 -march=native -flto -o kavin src/main.c src/watcher/watcher.c src/watcher/watcher_actions.c src/process/process.c -Isrc

    Usage: ./kavin <command> <file1> <file2> <file3> ...
    Example: ./kavin "npm start" src/main.js
    Or use many of file examole: ./kavin "npm start" src/main.js src/index.js ... rest of the file

*/