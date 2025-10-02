/*
    Copyright © 2025 Mint teams
    filesystem.c - File System Implementation for LiveServer
*/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>
#include <windows.h>

#include "filesystem.h"
#include "../interface/interface.h"
#include "../memory/memory.h"

// File Snapshotting
typedef struct FileSnapshot {
    char filename[260];
    char* content;
    size_t size;
    FILETIME last_modified;
    FileType type;
} FileSnapshot;

static FILETIME last_check_time;
static int files_changed_flag = 0;
static FileSnapshot* file_snapshots = NULL;
static int snapshot_count = 0;
static int snapshot_capacity = 0;

// Forward declarations for internal functions
static void notify_file_change(const char* filename);
static void snapshot_file(const char* filename, FILETIME* write_time);

void init_filesystem() {
    GetSystemTimeAsFileTime(&last_check_time);
    LogWithColor("File System initialized.", RGB(255, 255, 0));
}

void cleanup_filesystem() {
    for (int i = 0; i < snapshot_count; i++) {
        if (file_snapshots[i].content) {
            tracked_free(file_snapshots[i].content);
        }
    }
    free(file_snapshots);
    file_snapshots = NULL;
    snapshot_count = 0;
    snapshot_capacity = 0;
    LogWithColor("File System cleaned up.", RGB(255, 165, 0));
}

FileType get_file_type(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return FILE_TYPE_OTHER;
    
    if (strcmp(ext, ".css") == 0) return FILE_TYPE_CSS;
    if (strcmp(ext, ".js") == 0 || strcmp(ext, ".ts") == 0 || 
        strcmp(ext, ".jsx") == 0 || strcmp(ext, ".tsx") == 0) return FILE_TYPE_JS;
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return FILE_TYPE_HTML;
    if (strcmp(ext, ".json") == 0) return FILE_TYPE_JSON;
    
    return FILE_TYPE_OTHER;
}

static void notify_file_change(const char* filename) {
    FileType type = get_file_type(filename);
    
    char notification[2048];
    const char* type_str;
    
    switch (type) {
        case FILE_TYPE_CSS: type_str = "css"; break;
        case FILE_TYPE_JS: type_str = "js"; break;
        case FILE_TYPE_HTML: type_str = "html"; break;
        case FILE_TYPE_JSON: type_str = "json"; break;
        default: type_str = "other"; break;
    }
    
    snprintf(notification, sizeof(notification),
        "{\"type\":\"%s\",\"filename\":\"%s\",\"timestamp\":%ld,\"action\":\"update\"}",
        type_str, filename, (long)time(NULL));
    
    broadcast_ws_message(notification);
    
    COLORREF color;
    switch (type) {
        case FILE_TYPE_CSS: color = RGB(0, 255, 255); break;
        case FILE_TYPE_JS: color = RGB(255, 255, 0); break;
        case FILE_TYPE_HTML: color = RGB(255, 165, 0); break;
        default: color = RGB(128, 128, 128); break;
    }
    
    LogWithColor("HMR: %s (%s)", color, filename, type_str);
}

static void snapshot_file(const char* filename, FILETIME* write_time) {
    FILE* file = fopen(filename, "rb");
    if (!file) return;
    
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);
    
    char* content = (char*)tracked_malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return;
    }
    
    fread(content, 1, file_size, file);
    content[file_size] = '\0';
    fclose(file);
    
    FileSnapshot* snapshot = NULL;
    for (int i = 0; i < snapshot_count; i++) {
        if (strcmp(file_snapshots[i].filename, filename) == 0) {
            snapshot = &file_snapshots[i];
            break;
        }
    }
    
    if (!snapshot) {
        if (snapshot_count >= snapshot_capacity) {
            snapshot_capacity = snapshot_capacity == 0 ? 10 : snapshot_capacity * 2;
            file_snapshots = (FileSnapshot*)realloc(file_snapshots, sizeof(FileSnapshot) * snapshot_capacity);
        }
        
        snapshot = &file_snapshots[snapshot_count++];
        strncpy(snapshot->filename, filename, sizeof(snapshot->filename) - 1);
        snapshot->filename[sizeof(snapshot->filename) - 1] = '\0';
        snapshot->content = NULL;
        snapshot->type = get_file_type(filename);
    }
    
    if (snapshot->content) {
        notify_file_change(filename);
        tracked_free(snapshot->content);
    } else {
        LogWithColor("New file tracked: %s", RGB(0, 255, 255), filename);
    }
    
    snapshot->content = content;
    snapshot->size = file_size;
    snapshot->last_modified = *write_time;
}

int check_file_changes() {
    WIN32_FIND_DATA findFileData;
    HANDLE hFind;
    int changed = 0;
    
    const char* patterns[] = {"*.html", "*.css", "*.js", "*.json", "*.tsx", "*.jsx", "*.ts"};
    int pattern_count = sizeof(patterns) / sizeof(patterns[0]);
    
    for (int i = 0; i < pattern_count; i++) {
        hFind = FindFirstFile(patterns[i], &findFileData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (CompareFileTime(&findFileData.ftLastWriteTime, &last_check_time) > 0) {
                    snapshot_file(findFileData.cFileName, &findFileData.ftLastWriteTime);
                    changed = 1;
                }
            } while (FindNextFile(hFind, &findFileData) != 0);
            FindClose(hFind);
        }
    }
    
    if (changed) {
        GetSystemTimeAsFileTime(&last_check_time);
        files_changed_flag = 1;
    }
    
    return changed;
}

void serve_file(SOCKET client, const char *filename) {
    const char *file_to_open = (filename[0] == '/') ? filename + 1 : filename;
    
    FILE *file = fopen(file_to_open, "rb");
    if (!file) {
        serve_404(client, filename);
        return;
    }

    const char *ext = strrchr(filename, '.');
    const char *mime = "text/plain";
    if (ext) {
        if (strcmp(ext, ".html") == 0) mime = "text/html";
        else if (strcmp(ext, ".js") == 0 || strcmp(ext, ".ts") == 0 || strcmp(ext, ".tsx") == 0 || strcmp(ext, ".jsx") == 0) mime = "application/javascript";
        else if (strcmp(ext, ".css") == 0) mime = "text/css";
        else if (strcmp(ext, ".png") == 0) mime = "image/png";
        else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) mime = "image/jpeg";
        else if (strcmp(ext, ".svg") == 0) mime = "image/svg+xml";
        else if (strcmp(ext, ".json") == 0) mime = "application/json";
        else if (strcmp(ext, ".ico") == 0) mime = "image/x-icon";
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    
    char *buffer = (char*)tracked_malloc(size);
    if (!buffer) {
        fclose(file);
        return;
    }

    fread(buffer, 1, size, file);
    fclose(file);

    char *header = (char*)tracked_malloc(512);
    int header_len = snprintf(header, 512,
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nCache-Control: no-cache\r\n\r\n",
        mime, size);

    send(client, header, header_len, 0);
    send(client, buffer, size, 0);
    
    tracked_free(buffer);
    tracked_free(header);
}

void serve_html_with_hmr(SOCKET client, const char *filename) {
    const char *file_to_open = (filename[0] == '/') ? filename + 1 : filename;
    
    FILE *file = fopen(file_to_open, "rb");
    if (!file) {
        serve_404(client, filename);
        return;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    
    char *buffer = (char*)tracked_malloc(size + 100); // Extra space for script
    if (!buffer) {
        fclose(file);
        return;
    }
    
    size_t bytes_read = fread(buffer, 1, size, file);
    buffer[bytes_read] = '\0';
    fclose(file);
    
    char *body_end = strstr(buffer, "</body>");
    if (body_end) {
        const char *smart_script = "<script src=\"live-reload.js\"></script>";
        size_t script_len = strlen(smart_script);
        memmove(body_end + script_len, body_end, strlen(body_end) + 1);
        memcpy(body_end, smart_script, script_len);
        bytes_read += script_len;
    }
    
    char *header = (char*)tracked_malloc(512);
    int header_len = snprintf(header, 512,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nCache-Control: no-cache\r\n\r\n",
        bytes_read);
    
    send(client, header, header_len, 0);
    send(client, buffer, bytes_read, 0);
    
    tracked_free(buffer);
    tracked_free(header);
    
    LogWithColor("HMR HTML: %s", RGB(0, 255, 128), filename);
}

void serve_404(SOCKET client, const char* requested_file) {
    LogWithColor("404: %s", RGB(255, 165, 0), requested_file);

    const char* html_template = "<!DOCTYPE html><html><head><title>404 Not Found</title>"
        "<style>body{font-family:system-ui;text-align:center;padding:50px;}"
        "h1{font-size:4rem;margin:0;color:#ef4444;}p{font-size:1.2rem;}</style>"
        "</head><body><h1>404</h1><p>File not found: %s</p></body></html>";
    
    char html_buffer[512];
    snprintf(html_buffer, sizeof(html_buffer), html_template, requested_file);

    size_t content_len = strlen(html_buffer);
    char header[256];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n", content_len);

    send(client, header, header_len, 0);
    send(client, html_buffer, content_len, 0);
}
