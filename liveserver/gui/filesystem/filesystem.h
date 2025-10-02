/*
    Copyright © 2025 Mint teams
    filesystem.h - File System Component
*/

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <winsock2.h>
#include <windows.h>

// File type
typedef enum {
    FILE_TYPE_HTML,
    FILE_TYPE_CSS,
    FILE_TYPE_JS,
    FILE_TYPE_JSON,
    FILE_TYPE_OTHER
} FileType;

// Initialzation

void init_filesystem();
void cleanup_filesystem();

// Files operations
FileType get_file_type(const char* filename);
int check_file_changes();

// Serve fn
void serve_file(SOCKET client, const char *filename);
void serve_html_with_hmr(SOCKET client, const char *filename);
void serve_404(SOCKET client, const char* requested_file);

void broadcast_ws_message(const char* message);

void* tracked_malloc(size_t size);
void tracked_free(void* ptr);

#endif // FILESYSTEM_H