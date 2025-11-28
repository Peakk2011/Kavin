# Kavin Project Structure

## Overview
Kavin is a Node.js process watcher written in C. It monitors file changes and automatically restarts the application when modifications are detected.

## Directory Structure

```
Kavin/
├── build_liveserver.bat      # Build script for liveserver
├── build.bat                 # Main build script
├── Makefile                  # Build configuration
├── README.MD                 # Project documentation
├── LICENSE                   # License file
│
├── assets/                   # Static assets
│
├── liveserver/              # Live reload server implementation
│   ├── liveserver.cpp       # Main C++ implementation
│   ├── gui/
│   │   ├── live-reload.js   # Client-side reload script
│   │   └── liveserver.c     # GUI server code
│   ├── filesystem/          # Filesystem operations
│   │   ├── filesystem.c
│   │   └── filesystem.h
│   ├── interface/           # Interface layer
│   │   ├── interface.c
│   │   └── interface.h
│   └── memory/              # Memory management
│       ├── memory.asm
│       └── memory.h
│
├── obj/                     # Build output directory
│   ├── arch/
│   ├── liveserver/
│   ├── process/
│   └── watcher/
│
└── src/                     # Main source code
    ├── main.c               # Entry point
    ├── arch/                # Architecture-specific code
    │   ├── memory.h         # Memory utilities
    │   ├── syscalls.asm     # Assembly syscalls
    │   └── syscalls.h       # Syscall headers
    ├── process/             # Process management
    │   ├── process.c
    │   └── process.h
    └── watcher/             # File watcher module
        ├── watcher.c        # Main watcher implementation
        ├── watcher.h        # Watcher header
        ├── watcher_actions.c # Watcher state handlers
        └── watcher_actions.h # Action handlers header
```

## Technical Architecture

### Core Components

#### 1. **Watcher Module** (`src/watcher/`)
The main file-watching system that monitors changes and manages application restarts.

**Key Files:**
- `watcher.h/c` - Core watcher structure and initialization
- `watcher_actions.h/c` - State machine handlers

**Key Structures:**
```c
typedef struct {
    const char *cmd;                    // Command to execute
    char **files_to_watch;              // Array of watched file paths
    char **dirs_to_watch;               // Array of watched directory paths
    int file_count;                     // Number of files
    int dir_count;                      // Number of directories
    time_t *last_mtimes;                // Last modification times
    pid_t process_id;                   // Process ID
    volatile sig_atomic_t running;      // Running flag
    WatcherState state;                 // Current state (RUNNING, SHUTTING_DOWN, etc.)
    ULONGLONG/timespec shutdown_start_time;  // Shutdown timeout tracker
    ULONGLONG/timespec last_dir_scan_time;   // Directory scan debounce timer
    unsigned long restart_count;        // Restart counter
} Watcher;
```

**State Machine:**
- `STATE_RUNNING` - Monitoring files and running the process
- `STATE_SHUTTING_DOWN` - Gracefully stopping the process (2-second timeout)
- `STATE_FORCE_KILLING` - Force killing unresponsive process
- `STATE_RESTARTING` - Starting/restarting the application

#### 2. **Process Management** (`src/process/`)
Handles cross-platform process creation, monitoring, and termination.

**Responsibilities:**
- Starting processes with `process_start()`
- Checking process status with `process_check_status()`
- Graceful shutdown with `process_stop()`
- Force kill with `process_kill()`

#### 3. **Architecture Layer** (`src/arch/`)
Platform-specific implementations for system calls and memory operations.

**Files:**
- `syscalls.asm` - Low-level assembly syscalls
- `syscalls.h` - Syscall declarations
- `memory.h` - Memory utilities

#### 4. **LiveServer** (`liveserver/`)
Optional live reload server with GUI and filesystem watching capabilities.

**Components:**
- `filesystem/` - Directory and file operations
- `interface/` - Server interface
- `memory/` - Memory management with assembly
- `gui/live-reload.js` - Browser-side reload script

---

## Performance Optimizations

### Recent Improvements (Performance Fix)

1. **Directory Scanning Throttling**
   - Changed: Every 100ms → Every 1 second (10x reduction)
   - Added debounce timer: `last_dir_scan_time`
   - Reduces CPU usage significantly for large directories

2. **Redundant mtime Calls Eliminated**
   - Before: Updated ALL files' mtimes on ANY change
   - After: Only update the specific file that changed
   - Reduces system calls by ~90%

3. **Efficient File Deduplication**
   - Checks for existing watches before adding new files
   - Prevents duplicate monitoring overhead

### Intervals
- **File Check Interval**: 100ms - Responsive to file changes
- **Directory Scan Interval**: 1000ms - Low-overhead new file detection

---

## Build System

### Build Scripts
- `build.bat` - Compiles the main Kavin watcher
- `build_liveserver.bat` - Compiles the liveserver component
- `Makefile` - GNU Make configuration

### Output Directory
All compiled objects go to `obj/` following the source structure.

---

## Cross-Platform Support

The project supports both **Windows** and **Unix-like systems** (Linux, macOS):

- `#ifdef _WIN32` preprocessor conditionals handle platform differences
- Windows: Uses `GetTickCount64()` and Windows API
- Unix: Uses `clock_gettime()` and POSIX APIs
- Process management abstracted for platform compatibility

---

## Workflow

1. **Initialization** → Parse watched files/directories
2. **Main Loop** → Monitor file changes every 100ms
3. **Detection** → When change detected, trigger restart
4. **Shutdown** → Graceful 2-second shutdown timeout
5. **Force Kill** → Kill unresponsive process if needed
6. **Restart** → Start process again
7. **Repeat** → Continue monitoring

