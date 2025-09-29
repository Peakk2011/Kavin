/*
    Copyright © 2025 Mint teams
    process.c
*/

#include <process/process.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>

pid_t process_start(const char *command) {
    pid_t pid = fork();

    if (pid == 0) {
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        perror("execl failed");
        exit(127);
    }
    // Parent or error. The caller should check if pid > 0.
    return pid;
}

void process_stop(pid_t pid) {
    if (pid > 0) {
        kill(-pid, SIGTERM); // Send signal to the entire process group
    }
}

void process_kill(pid_t pid) {
    if (pid > 0) {
        kill(-pid, SIGKILL); // Send signal to the entire process group
    }
}

int process_check_status(pid_t pid, int *status) {
    return waitpid(pid, status, WNOHANG);
}