/*
    Copyright © 2025 Mint teams
    Kavin liveserver `liveserver.c`
    INFO: WORKING ONLY WINDOWS OS
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdint.h>
#include <shellapi.h>
#include <time.h>
#include <process.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h> // Keep for potential future use
#include "memory/memory.h"
#include "filesystem/filesystem.h"

// WebSocket support for updates
typedef struct WebSocketClient {
    SOCKET socket;
    int is_websocket;
    struct WebSocketClient* next;
} WebSocketClient;

static WebSocketClient* ws_clients = NULL;
static CRITICAL_SECTION ws_clients_lock;

// Server state variables
static volatile int server_running = 1;

// Include the UI interface
#include "interface/interface.h"

// Function to signal the server to stop
void stop_server() {
    server_running = 0;
}

typedef struct {
    int argc;
    char** argv;
} ThreadArgs;

typedef struct MemoryPool {
    void** blocks;
    size_t* sizes;
    int count;
    int capacity;
    size_t total_allocated;
} MemoryPool;

static MemoryPool* memory_pool = NULL;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20 19
#endif

// WebSocket utilities
void add_ws_client(SOCKET client_socket) {
    EnterCriticalSection(&ws_clients_lock);
    
    WebSocketClient* new_client = (WebSocketClient*)malloc(sizeof(WebSocketClient));
    new_client->socket = client_socket;
    new_client->is_websocket = 1;
    new_client->next = ws_clients;
    ws_clients = new_client;
    
    LeaveCriticalSection(&ws_clients_lock);
    LogWithColor("WebSocket client connected", LOG_COLOR_SUCCESS);
}

void remove_ws_client(SOCKET client_socket) {
    EnterCriticalSection(&ws_clients_lock);
    
    WebSocketClient** current = &ws_clients;
    while (*current) {
        if ((*current)->socket == client_socket) {
            WebSocketClient* to_remove = *current;
            *current = (*current)->next;
            free(to_remove);
            break;
        }
        current = &(*current)->next;
    }
    
    LeaveCriticalSection(&ws_clients_lock);
}

void broadcast_ws_message(const char* message) {
    EnterCriticalSection(&ws_clients_lock);
    
    WebSocketClient* current = ws_clients;
    while (current) {
        WebSocketClient* next = current->next;
        
        // Send WebSocket frame (simplified)
        char frame[2048];
        int message_len = strlen(message);
        int frame_len;
        
        if (message_len < 126) {
            frame[0] = 0x81; // Final frame, text
            frame[1] = message_len;
            memcpy(frame + 2, message, message_len);
            frame_len = message_len + 2;
        } else {
            frame[0] = 0x81;
            frame[1] = 126;
            frame[2] = (message_len >> 8) & 0xFF;
            frame[3] = message_len & 0xFF;
            memcpy(frame + 4, message, message_len);
            frame_len = message_len + 4;
        }
        
        if (send(current->socket, frame, frame_len, 0) == SOCKET_ERROR) {
            remove_ws_client(current->socket);
        }
        
        current = next;
    }
    
    LeaveCriticalSection(&ws_clients_lock);
}

// Memory management
MemoryPool* init_memory(int initial_capacity) {
    MemoryPool* pool = (MemoryPool*)malloc(sizeof(MemoryPool));
    if (!pool) return NULL;
    
    pool->blocks = (void**)malloc(sizeof(void*) * initial_capacity);
    pool->sizes = (size_t*)malloc(sizeof(size_t) * initial_capacity);
    pool->count = 0;
    pool->capacity = initial_capacity;
    pool->total_allocated = 0;
    
    if (!pool->blocks || !pool->sizes) {
        free(pool->blocks);
        free(pool->sizes);
        free(pool);
        return NULL;
    }
    
    memset(pool->blocks, 0, sizeof(void*) * initial_capacity);
    memset(pool->sizes, 0, sizeof(size_t) * initial_capacity);
    
    LogWithColor("Memory pool initialized: %d slots", LOG_COLOR_FUNCTION, initial_capacity);
    return pool;
}

void* tracked_malloc(size_t size) {
    if (!memory_pool) {
        memory_pool = init_memory(100);
        if (!memory_pool) return NULL;
    }
    
    void* ptr = malloc(size);
    if (!ptr) return NULL;
    
    if (memory_pool->count >= memory_pool->capacity) {
        int new_capacity = memory_pool->capacity * 2;
        void** new_blocks = (void**)realloc(memory_pool->blocks, sizeof(void*) * new_capacity);
        size_t* new_sizes = (size_t*)realloc(memory_pool->sizes, sizeof(size_t) * new_capacity);
        
        if (new_blocks && new_sizes) {
            memory_pool->blocks = new_blocks;
            memory_pool->sizes = new_sizes;
            memory_pool->capacity = new_capacity;
        }
    }
    
    if (memory_pool->count < memory_pool->capacity) {
        memory_pool->blocks[memory_pool->count] = ptr;
        memory_pool->sizes[memory_pool->count] = size;
        memory_pool->count++;
        memory_pool->total_allocated += size;
    }
    
    memset(ptr, 0, size);
    return ptr;
}

void tracked_free(void* ptr) {
    if (!ptr || !memory_pool) return;
    
    int i = find_block_index_asm(memory_pool->blocks, memory_pool->count, ptr);

    if (i != -1) {
        memory_pool->total_allocated -= memory_pool->sizes[i];
        
        // Shift remaining elements to fill the gap
        memmove(&memory_pool->blocks[i], &memory_pool->blocks[i + 1], (memory_pool->count - i - 1) * sizeof(void*));
        memmove(&memory_pool->sizes[i], &memory_pool->sizes[i + 1], (memory_pool->count - i - 1) * sizeof(size_t));
        
        memory_pool->count--;
    }
    
    free(ptr);
}

// Serve functions
void serve_hmr_script(SOCKET client) {
    // Serve the live-reload.js file from the gui directory
    serve_file(client, "live-reload.js");
}

// Initialize critical section for WebSocket clients
void init_hmr() {
    InitializeCriticalSection(&ws_clients_lock);
    LogWithColor("HMR System initialized", LOG_COLOR_FUNCTION);
}

void cleanup_hmr() {
    DeleteCriticalSection(&ws_clients_lock);
    
    WebSocketClient* current = ws_clients;
    while (current) {
        WebSocketClient* next = current->next;
        closesocket(current->socket);
        free(current);
        current = next;
    }
    ws_clients = NULL;
    
    LogWithColor("HMR System cleaned up", LOG_COLOR_WARNING);
}

// Enhanced reload endpoint with data
void serve_reload(SOCKET client) {
    // This function is now just a placeholder for the polling mechanism.
    // The actual check is done in check_file_changes() which sets a flag.
    // For simplicity, we'll just respond based on whether changes were detected in the last cycle.

    char response[512];
    int response_len = snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-cache\r\n\r\n"
        "{\"reload\":%s,\"timestamp\":%ld,\"smart_hmr\":true,\"tracked_files\":%d}",
        "false", // WebSocket is the primary method, polling is a fallback.
        (long)time(NULL),
        0); // Snapshot count is now internal to filesystem.c
    send(client, response, response_len, 0);
}

// Main server thread with WebSocket support
unsigned __stdcall server_main_thread(void* pArguments) {
    // Mark pArguments as unused to suppress compiler warnings
    (void)pArguments;
    
    // Initialize systems
    init_hmr();
    init_filesystem();
    
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    unsigned short port = 3000;
    if (__argc > 1) {
        port = (unsigned short)atoi(__argv[1]);
        if (port == 0) {
            LogWithColor("Invalid port '%s', using 3000", LOG_COLOR_WARNING, __argv[1]);
            port = 3000;
        }
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LogWithColor("Bind failed: %d", LOG_COLOR_ERROR, WSAGetLastError());
        cleanup_filesystem();
        cleanup_hmr();
        return 1;
    }

    listen(server, 10);

    LogWithColor("LiveServer Ready", LOG_COLOR_SUCCESS);
    LogWithColor("Local: http://localhost:%d", LOG_COLOR_VARIABLE, port);
    LogWithColor("Watching HTML, CSS, JS files...", LOG_COLOR_FUNCTION);

    char url[256];
    snprintf(url, sizeof(url), "http://localhost:%d", port);
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);

    while (server_running) {
        SOCKET client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) break;
        
        char *buffer = (char*)tracked_malloc(4096);
        if (!buffer) {
            closesocket(client);
            continue;
        }
        
        int bytes_received = recv(client, buffer, 4095, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            
            char method[16], path[1024];
            memset(method, 0, sizeof(method));
            memset(path, 0, sizeof(path));
            sscanf(buffer, "%15s %1023s", method, path);
            
            // Check for WebSocket upgrade
            if (strstr(buffer, "Upgrade: websocket") && strstr(buffer, "Connection: Upgrade")) {
                // WebSocket handshake (basic implementation)
                char* key = strstr(buffer, "Sec-WebSocket-Key: ");
                if (key) {
                    key += 19; // Skip "Sec-WebSocket-Key: "
                    char* key_end = strstr(key, "\r\n");
                    if (key_end) {
                        *key_end = '\0';
                        
                        const char* websocket_response = 
                            "HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
                            "\r\n";
                        
                        send(client, websocket_response, (int)strlen(websocket_response), 0);
                        add_ws_client(client);
                        LogWithColor("WebSocket connection established", LOG_COLOR_SUCCESS);

                        tracked_free(buffer);
                        continue; // Don't close the socket, keep it for WebSocket
                    }
                }
            }
            
            // Remove query parameters
            char *query_params = strchr(path, '?');
            if (query_params) *query_params = '\0';
            
            // Route handling
            if (strcmp(path, "/reload") == 0) {
                check_file_changes();
                serve_reload(client);
                LogWithColor("[%s] %s [Hot Reload System]", LOG_COLOR_SUCCESS, method, path);
            } 
            else if (strcmp(path, "/live-reload.js") == 0) {
                // No need to check for file changes when serving the script itself
                serve_hmr_script(client);
                LogWithColor("[%s] %s [HMR Script]", LOG_COLOR_SUCCESS, method, path);
            }
            else if (strncmp(path, "/api/", 5) == 0) {
                char api_response[1024];
                int api_len = snprintf(api_response, sizeof(api_response),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Access-Control-Allow-Origin: *\r\n\r\n"
                    "{\"status\":\"running\",\"smart_hmr\":true,\"files\":%d,\"memory\":%zu}",
                    0, memory_pool ? memory_pool->total_allocated : 0); // Files tracked is now internal
                send(client, api_response, api_len, 0);
                LogWithColor("[%s] %s [API]", LOG_COLOR_KEYWORD, method, path);
            }
            else {
                char *file = path + 1;
                if (strlen(file) == 0) {
                    file = "index.html";
                }
                
                const char *ext = strrchr(file, '.');
                
                // Handling based on file type
                if (ext && strcmp(ext, ".html") == 0) {
                    FILE *test_file = fopen(file, "r");
                    if (test_file) {
                        fclose(test_file);
                        LogWithColor("[%s] %s [HTML]", LOG_COLOR_STRING, method, path);
                        check_file_changes();
                        serve_html_with_hmr(client, file);
                    } else {
                        serve_404(client, file);
                    }
                }
                else if (ext && (strcmp(ext, ".css") == 0 || strcmp(ext, ".js") == 0)) {
                    LogWithColor("[%s] %s [Tracked Asset]", LOG_COLOR_TYPE, method, path);
                    check_file_changes();
                    serve_file(client, file);
                }
                else if (!ext) {
                    // Try .html extension
                    char html_file[1024];
                    snprintf(html_file, sizeof(html_file), "%s.html", file);
                    FILE *test_file = fopen(html_file, "r");
                    if (test_file) {
                        fclose(test_file);
                        LogWithColor("[%s] %s [Auto .html]", LOG_COLOR_STRING, method, path);
                        check_file_changes();
                        serve_html_with_hmr(client, html_file);
                    } else {
                        serve_file(client, file);
                    }
                }
                else {
                    LogWithColor("[%s] %s", LOG_COLOR_KEYWORD, method, path);
                    serve_file(client, file);
                }
            }
        }
        
        tracked_free(buffer);
        closesocket(client);
    }

    cleanup_filesystem();
    cleanup_hmr();
    closesocket(server);
    WSACleanup();
    return 0;
}

// Memory cleanup
void cleanup_memory_pool() {
    if (!memory_pool) return;
    
    LogWithColor("Memory cleanup started", LOG_COLOR_WARNING);
    LogWithColor("Blocks: %d, Memory: %zu bytes", LOG_COLOR_NUMBER, 
                 memory_pool->count, memory_pool->total_allocated);

    for (int i = 0; i < memory_pool->count; i++) {
        if (memory_pool->blocks[i]) free(memory_pool->blocks[i]);
    }
    
    free(memory_pool->blocks);
    free(memory_pool->sizes);
    free(memory_pool);
    memory_pool = NULL;

    LogWithColor("Memory cleanup completed", LOG_COLOR_SUCCESS);
}

/*  
    Compile using:
    gcc -O3 liveserver.c interface/interface.c -o liveserver.exe -lws2_32 -lshell32 -mwindows -ldwmapi -luxtheme -I.
    
    Then run  ./liveserver.exe [port]
    Then open http://localhost:3000

    Usage: ./liveserver.exe [port]
*/