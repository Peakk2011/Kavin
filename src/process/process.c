/*
    Copyright © 2025 Mint teams
    process.c
    The generic Node.js process watcher
*/

#include "process.h"
#include <arch/syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

pid_t process_start(const char *command) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return -1;
    } else if (pid == 0) {
        // Child process
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        perror("execl failed"); // execl only returns on error
        exit(127);
    }
    // Parent process
    return pid;
}

void process_stop(pid_t pid) {
    process_stop_asm(pid);
}

void process_kill(pid_t pid) {
    kill(-pid, SIGKILL); // We can convert this to assembly next
}

int process_check_status(pid_t pid, int *status) {
    return waitpid(pid, status, WNOHANG);
}