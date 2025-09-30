/*
    Copyright © 2025 Mint teams
    syscalls.h
    The generic Node.js process watcher
*/

#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <time.h>

extern time_t get_mtime_asm(const char *filepath);
extern int process_stop_asm(pid_t pid);

#endif // SYSCALLS_H