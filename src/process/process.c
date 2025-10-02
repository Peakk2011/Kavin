/*
    Copyright © 2025 Mint teams
    process.c
    The generic Node.js process watcher
*/

#include "process.h"
#include <arch/syscalls.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

pid_t process_start(const char *command) {
    char cmd_buffer[1024];
    snprintf(cmd_buffer, sizeof(cmd_buffer), "cmd.exe /C %s", command);

    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcess(NULL, cmd_buffer, NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "CreateProcess failed (%lu).\n", GetLastError());
        return 0;
    }

    CloseHandle(pi.hThread);
    // We return the handle to the process, but cast it to pid_t for compatibility
    return (pid_t)pi.hProcess;
}

void process_stop(pid_t pid) {
    // On Windows, we send a CTRL_BREAK_EVENT to the process group to simulate SIGTERM
    // This allows graceful shutdown for console applications.
    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, GetProcessId((HANDLE)pid));
}

void process_kill(pid_t pid) {
    // TerminateProcess is the equivalent of SIGKILL
    TerminateProcess((HANDLE)pid, 1);
}

int process_check_status(pid_t pid, int *status) {
    DWORD exit_code;
    if (GetExitCodeProcess((HANDLE)pid, &exit_code)) {
        if (exit_code == STILL_ACTIVE) {
            return 0; // Process is still running
        } else {
            *status = (int)exit_code;
            CloseHandle((HANDLE)pid);
            return (int)pid; // Process has exited
        }
    }
    return -1; // Error checking status
}

#else // POSIX implementation
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

pid_t process_start(const char *command) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return 0;
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
#endif