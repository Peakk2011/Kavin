/*
    Copyright © 2025 Mint teams
    process.h
*/

#ifndef PROCESS_H
#define PROCESS_H

#include <sys/types.h>

pid_t process_start(const char *command);
void process_stop(pid_t pid); // Sends SIGTERM to the process group
void process_kill(pid_t pid); // Sends SIGKILL to the process group

// Wrapper for waitpid with WNOHANG
int process_check_status(pid_t pid, int *status);

#endif // PROCESS_H