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
    LogWithColor("File System initialized.", LOG_COLOR_FUNCTION);
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
    LogWithColor("File System cleaned up.", LOG_COLOR_WARNING);
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
        case FILE_TYPE_CSS:  color = LOG_COLOR_TYPE; break;
        case FILE_TYPE_JS:   color = LOG_COLOR_FUNCTION; break;
        case FILE_TYPE_HTML: color = LOG_COLOR_STRING; break;
        case FILE_TYPE_JSON: color = LOG_COLOR_VARIABLE; break;
        default:             color = LOG_COLOR_COMMENT; break;
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
        LogWithColor("New file tracked: %s", LOG_COLOR_KEYWORD, filename);
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
    
    LogWithColor("Served HMR HTML: %s", LOG_COLOR_STRING, filename);
}

void serve_404(SOCKET client, const char* requested_file) {
    LogWithColor("404: %s", LOG_COLOR_WARNING, requested_file);

    const char* html_content =
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "    <meta charset=\"UTF-8\">\n"
        "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "    <title>404 - Not Found</title>\n"
        "    <style>\n"
        "        * {\n"
        "            margin: 0;\n"
        "            padding: 0;\n"
        "            box-sizing: border-box;\n"
        "        }\n"
        "        \n"
        "        body {\n"
        "            font-family: 'SF Mono', 'Monaco', 'Consolas', monospace;\n"
        "            background: #ffffff;\n"
        "            color: #333333;\n"
        "            min-height: 100vh;\n"
        "            display: flex;\n"
        "            align-items: center;\n"
        "            justify-content: center;\n"
        "            line-height: 1.5;\n"
        "        }\n"
        "        \n"
        "        .container {\n"
        "            background: #ffffff;\n"
        "            border-radius: 4px;\n"
        "            padding: 40px;\n"
        "            max-width: 600px;\n"
        "            width: 90%;\n"
        "            box-shadow: 0 2px 8px rgba(0,0,0,0.05);\n"
        "        }\n"
        "        \n"
        "        .error-code {\n"
        "            font-size: 3rem;\n"
        "            font-weight: 300;\n"
        "            color: #666666;\n"
        "            text-align: center;\n"
        "            margin-bottom: 16px;\n"
        "            letter-spacing: -1px;\n"
        "        }\n"
        "        \n"
        "        .error-message {\n"
        "            font-size: 1.1rem;\n"
        "            text-align: center;\n"
        "            margin-bottom: 32px;\n"
        "            color: #666666;\n"
        "        }\n"
        "        \n"
        "        .chat-container {\n"
        "            border: 1px solid #e1e5e9;\n"
        "            border-radius: 10px;\n"
        "            overflow: hidden;\n"
        "        }\n"
        "        \n"
        "        .chat-header {\n"
        "            background: #f8f9fa;\n"
        "            padding: 12px 16px;\n"
        "            border-bottom: 1px solid #e1e5e9;\n"
        "            font-size: 14px;\n"
        "            color: #666666;\n"
        "        }\n"
        "        \n"
        "        .chat-area {\n"
        "            background: #ffffff;\n"
        "            padding: 16px;\n"
        "            height: 280px;\n"
        "            overflow-y: auto;\n"
        "            font-size: 14px;\n"
        "        }\n"
        "        \n"
        "        .chat-area::-webkit-scrollbar {\n"
        "            width: 6px;\n"
        "        }\n"
        "        \n"
        "        .chat-area::-webkit-scrollbar-track {\n"
        "            background: #f8f9fa;\n"
        "        }\n"
        "        \n"
        "        .chat-area::-webkit-scrollbar-thumb {\n"
        "            background: #dee2e6;\n"
        "            border-radius: 3px;\n"
        "        }\n"
        "        \n"
        "        .message {\n"
        "            margin-bottom: 12px;\n"
        "            word-wrap: break-word;\n"
        "        }\n"
        "        \n"
        "        .user-msg {\n"
        "            color: #333333;\n"
        "        }\n"
        "        \n"
        "        .bot-msg {\n"
        "            color: #666666;\n"
        "        }\n"
        "        \n"
        "        .system-msg {\n"
        "            color: #999999;\n"
        "            font-style: italic;\n"
        "        }\n"
        "        \n"
        "        .typing-indicator {\n"
        "            color: #999999;\n"
        "            font-style: italic;\n"
        "        }\n"
        "        \n"
        "        .input-container {\n"
        "            background: #f8f9fa;\n"
        "            padding: 12px 16px;\n"
        "            border-top: 1px solid #e1e5e9;\n"
        "            display: flex;\n"
        "            gap: 8px;\n"
        "        }\n"
        "        \n"
        "        .chat-input {\n"
        "            flex: 1;\n"
        "            background: #ffffff;\n"
        "            border: 1px solid #dee2e6;\n"
        "            border-radius: 100vmax;\n"
        "            padding: 8px 12px;\n"
        "            color: #333333;\n"
        "            font-family: 'SF Mono', 'Monaco', 'Consolas', monospace;\n"
        "            font-size: 14px;\n"
        "            outline: none;\n"
        "            transition: border-color 0.2s ease;\n"
        "        }\n"
        "        \n"
        "        .chat-input:focus {\n"
        "            border-color: #007bff;\n"
        "        }\n"
        "        \n"
        "        .send-btn {\n"
        "            background: #333333;\n"
        "            color: #ffffff;\n"
        "            border: none;\n"
        "            border-radius: 100vmax;\n"
        "            padding: 8px 16px;\n"
        "            cursor: pointer;\n"
        "            font-family: 'SF Mono', 'Monaco', 'Consolas', monospace;\n"
        "            font-size: 14px;\n"
        "            transition: background-color 0.2s ease;\n"
        "        }\n"
        "        \n"
        "        .send-btn:hover {\n"
        "            background: #555555;\n"
        "        }\n"
        "        \n"
        "        .cursor {\n"
        "            animation: blink 1s infinite;\n"
        "        }\n"
        "        \n"
        "        @keyframes blink {\n"
        "            0%, 50% { opacity: 1; }\n"
        "            51%, 100% { opacity: 0; }\n"
        "        }\n"
        "        \n"
        "        @media (max-width: 768px) {\n"
        "            .container { padding: 24px; }\n"
        "            .error-code { font-size: 2.5rem; }\n"
        "            .chat-area { height: 240px; }\n"
        "        }\n"
        "        \n"
        "        @media (prefers-color-scheme: dark) {\n"
        "            body {\n"
        "                background: #1e1e1e;\n"
        "                color: #d4d4d4;\n"
        "            }\n"
        "            .container {\n"
        "                background: transparent;\n"
        "            }\n"
        "            .error-code, .error-message, .bot-msg {\n"
        "                color: #cccccc;\n"
        "            }\n"
        "            .user-msg {\n"
        "                color: #d4d4d4;\n"
        "            }\n"
        "            .system-msg, .typing-indicator {\n"
        "                color: #888888;\n"
        "            }\n"
        "            .chat-container {\n"
        "                border-color: #3c3c3c;\n"
        "            }\n"
        "            .chat-header {\n"
        "                background: #333333;\n"
        "                border-bottom-color: #3c3c3c;\n"
        "                color: #cccccc;\n"
        "            }\n"
        "            .chat-area {\n"
        "                background: #252526;\n"
        "            }\n"
        "            .chat-area::-webkit-scrollbar-track {\n"
        "                background: #333333;\n"
        "            }\n"
        "            .chat-area::-webkit-scrollbar-thumb {\n"
        "                background: #555555;\n"
        "            }\n"
        "            .input-container {\n"
        "                background: #333333;\n"
        "                border-top-color: #3c3c3c;\n"
        "            }\n"
        "            .chat-input {\n"
        "                background: #3c3c3c;\n"
        "                border-color: #555555;\n"
        "                color: #d4d4d4;\n"
        "            }\n"
        "            .chat-input:focus {\n"
        "                border-color: #007acc;\n"
        "            }\n"
        "            .send-btn {\n"
        "                background: #f0f0f0;\n"
        "                color: #1e1e1e;\n"
        "            }\n"
        "            .send-btn:hover {\n"
        "                background: #ffffff;\n"
        "            }\n"
        "        }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <div class=\"error-code\">404</div>\n"
        "        <div class=\"error-message\">Page not found</div>\n"
        "        \n"
        "        <div class=\"chat-container\">\n"
        "            <div class=\"chat-header\">Assistant</div>\n"
        "            \n"
        "            <div class=\"chat-area\" id=\"chatArea\">\n"
        "                <div class=\"message system-msg\">Connection established</div>\n"
        "                <div class=\"message bot-msg\">Hello! The page you're looking for wasn't found, but I'm here to help.</div>\n"
        "                <div class=\"message bot-msg\">Type 'help' to see available commands.</div>\n"
        "            </div>\n"
        "            \n"
        "            <div class=\"input-container\">\n"
        "                <input type=\"text\" class=\"chat-input\" id=\"chatInput\" placeholder=\"Type a message...\" autocomplete=\"off\">\n"
        "                <button class=\"send-btn\" onclick=\"sendMessage()\">Send</button>\n"
        "            </div>\n"
        "        </div>\n"
        "    </div>\n"
        "\n"
        "    <script>\n"
        "        const chatArea = document.getElementById('chatArea');\n"
        "        const chatInput = document.getElementById('chatInput');\n"
        "        \n"
        "        let isTyping = false;\n"
        "        \n"
        "        const commands = {\n"
        "            'help': 'Available commands:\\n- help: Show all commands\\n- clear: Clear screen\\n- time: Show current time\\n- joke: Tell a joke\\n- status: System status',\n"
        "            'clear': 'CLEAR_SCREEN',\n"
        "            'time': () => {\n"
        "                const now = new Date();\n"
        "                return `Current time: ${now.toLocaleString()}`;\n"
        "            },\n"
        "            'joke': () => {\n"
        "                const jokes = [\n"
        "                    'Why do programmers prefer dark mode? Because light attracts bugs!',\n"
        "                    '404: Joke not found... wait, this is the joke!',\n"
        "                    'HTML and CSS are a happy couple, but JavaScript is the third wheel.',\n"
        "                    'Why did the developer go broke? Because he used up all his cache!'\n"
        "                ];\n"
        "                return jokes[Math.floor(Math.random() * jokes.length)];\n"
        "            },\n"
        "            'status': 'System Status:\\n- CPU: OK\\n- Memory: Available\\n- Network: Connected\\n- Error: 404 Not Found'\n"
        "        };\n"
        "        \n"
        "        const responses = {\n"
        "            greetings: ['Hello! How can I help you?', 'Hi there! Type help to see commands.'],\n"
        "            questions: ['Interesting. Tell me more.', 'I see. Anything else?'],\n"
        "            thanks: ['You\\'re welcome! Happy to help.', 'No problem at all.'],\n"
        "            confused: ['I don\\'t understand. Try typing help.', 'Command not recognized. Type help for available commands.']\n"
        "        };\n"
        "        \n"
        "        function addMessage(text, type = 'bot', showTyping = false) {\n"
        "            if (showTyping) {\n"
        "                const typingMsg = document.createElement('div');\n"
        "                typingMsg.className = 'message typing-indicator';\n"
        "                typingMsg.innerHTML = 'typing<span class=\"cursor\">...</span>';\n"
        "                chatArea.appendChild(typingMsg);\n"
        "                chatArea.scrollTop = chatArea.scrollHeight;\n"
        "                \n"
        "                setTimeout(() => {\n"
        "                    typingMsg.remove();\n"
        "                    const messageDiv = document.createElement('div');\n"
        "                    messageDiv.className = `message ${type}-msg`;\n"
        "                    messageDiv.innerHTML = text.replace(/\\n/g, '<br>');\n"
        "                    chatArea.appendChild(messageDiv);\n"
        "                    chatArea.scrollTop = chatArea.scrollHeight;\n"
        "                    isTyping = false;\n"
        "                }, 800 + Math.random() * 800);\n"
        "            } else {\n"
        "                const messageDiv = document.createElement('div');\n"
        "                messageDiv.className = `message ${type}-msg`;\n"
        "                messageDiv.innerHTML = text.replace(/\\n/g, '<br>');\n"
        "                chatArea.appendChild(messageDiv);\n"
        "                chatArea.scrollTop = chatArea.scrollHeight;\n"
        "            }\n"
        "        }\n"
        "        \n"
        "        function getResponse(input) {\n"
        "            const msg = input.toLowerCase().trim();\n"
        "            \n"
        "            if (commands[msg]) {\n"
        "                if (msg === 'clear') {\n"
        "                    chatArea.innerHTML = '<div class=\"message system-msg\">Screen cleared</div>';\n"
        "                    return null;\n"
        "                }\n"
        "                return typeof commands[msg] === 'function' ? commandsmsg : commands[msg];\n"
        "            }\n"
        "            \n"
        "            if (msg.match(/(hello|hi|hey|good)/)) {\n"
        "                return responses.greetings[Math.floor(Math.random() * responses.greetings.length)];\n"
        "            }\n"
        "            if (msg.match(/(thanks|thank you|thx)/)) {\n"
        "                return responses.thanks[Math.floor(Math.random() * responses.thanks.length)];\n"
        "            }\n"
        "            if (msg.match(/(404|not found|missing)/)) {\n"
        "                return 'Yes, the page you were looking for is not here. But you found me instead!';\n"
        "            }\n"
        "            if (msg.match(/(name|who are you|what are you)/)) {\n"
        "                return 'I\\'m a 404 Assistant - here to help when pages go missing.';\n"
        "            }\n"
        "            if (msg.match(/(time|clock|hour)/)) {\n"
        "                return commands.time();\n"
        "            }\n"
        "            \n"
        "            return responses.confused[Math.floor(Math.random() * responses.confused.length)];\n"
        "        }\n"
        "        \n"
        "        function sendMessage() {\n"
        "            const input = chatInput.value.trim();\n"
        "            if (!input || isTyping) return;\n"
        "            \n"
        "            addMessage(input, 'user');\n"
        "            chatInput.value = '';\n"
        "            \n"
        "            isTyping = true;\n"
        "            const response = getResponse(input);\n"
        "            \n"
        "            if (response) {\n"
        "                addMessage(response, 'bot', true);\n"
        "            } else {\n"
        "                isTyping = false;\n"
        "            }\n"
        "        }\n"
        "        \n"
        "        chatInput.addEventListener('keypress', (e) => {\n"
        "            if (e.key === 'Enter') {\n"
        "                sendMessage();\n"
        "            }\n"
        "        });\n"
        "        \n"
        "        chatInput.focus();\n"
        "    </script>\n"
        "</body>\n"
        "</html>";

    size_t content_len = strlen(html_content);
    char header[256];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n", content_len);

    send(client, header, header_len, 0);
    send(client, html_content, content_len, 0);
}
