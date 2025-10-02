// Copyright © 2025 Mint teams
// Licensed under the MIT License

#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <atomic>
#include <memory>
#include <queue>
#include <iomanip>
#include <csignal>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <shellapi.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <sys/inotify.h>
    #include <poll.h>
#endif

namespace fs = std::filesystem;

namespace Config {
    constexpr size_t MAX_REQUEST_SIZE = 8192;
    constexpr size_t MAX_RESPONSE_SIZE = 1024 * 1024; 
    constexpr size_t MAX_FILE_SIZE = 512 * 1024; 
    constexpr size_t MAX_CACHE_SIZE = 4 * 1024 * 1024; 
    constexpr size_t MAX_CACHE_ENTRIES = 50;
    constexpr uint32_t MAX_CONNECTIONS = 10;
    constexpr uint32_t MAX_REQUESTS_PER_SECOND = 20;
    constexpr std::chrono::milliseconds POLL_INTERVAL{500};
    constexpr std::chrono::seconds HEALTH_CHECK_INTERVAL{30};
}

class PathValidator {
private:
    static std::unordered_set<std::string> allowedExtensions;
    
public:
    static bool isValidPath(std::string_view path) {
        if (path.empty() || path.size() > 255) return false;
        
        std::string pathStr{path};
        
        try {
            auto canonical = fs::weakly_canonical(fs::current_path() / pathStr);
            auto base = fs::current_path();
            
            auto relative = fs::relative(canonical, base);
            if (relative.string().starts_with("..")) {
                return false;
            }
            
            std::string ext = canonical.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            return allowedExtensions.contains(ext);
            
        } catch (const std::exception&) {
            return false;
        }
    }
    
    static std::string sanitizePath(std::string_view path) {
        if (!isValidPath(path)) return "";
        
        std::string result;
        result.reserve(path.size());
        
        for (char c : path) {
            if (std::isalnum(c) || c == '.' || c == '/' || c == '-' || c == '_') {
                result += c;
            }
        }
        
        return result;
    }
};

std::unordered_set<std::string> PathValidator::allowedExtensions = {
    ".html", ".css", ".js", ".json", ".txt", ".ico", ".png", ".jpg", ".jpeg", ".svg", ".webp"
};

class SignalHandler {
private:
    static std::atomic<bool> shutdownRequested;
    static void signalCallback(int signal) {
        shutdownRequested.store(true);
    }
    
public:
    static void setup() {
        std::signal(SIGINT, signalCallback);
        std::signal(SIGTERM, signalCallback);
#ifndef _WIN32
        std::signal(SIGPIPE, SIG_IGN);
#endif
    }
    
    static bool shouldShutdown() {
        return shutdownRequested.load();
    }
};

std::atomic<bool> SignalHandler::shutdownRequested{false};

// Lightweight buffer management
class BufferPool {
private:
    std::queue<std::unique_ptr<char[]>> available;
    std::mutex mutex;
    std::atomic<size_t> totalBuffers{0};
    static constexpr size_t MAX_BUFFERS = 20;
    static constexpr size_t BUFFER_SIZE = Config::MAX_REQUEST_SIZE;
    
public:
    std::unique_ptr<char[]> acquire() {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (!available.empty()) {
            auto buffer = std::move(available.front());
            available.pop();
            return buffer;
        }
        
        if (totalBuffers < MAX_BUFFERS) {
            totalBuffers++;
            return std::make_unique<char[]>(BUFFER_SIZE);
        }
        
        return nullptr;
    }
    
    void release(std::unique_ptr<char[]> buffer) {
        if (!buffer) return;
        
        std::lock_guard<std::mutex> lock(mutex);
        if (available.size() < MAX_BUFFERS / 2) {
            available.push(std::move(buffer));
        } else {
            totalBuffers--;
        }
    }
    
    size_t size() const { return totalBuffers.load(); }
};

// Rate limiting
class RateLimiter {
private:
    std::mutex mutex;
    std::queue<std::chrono::steady_clock::time_point> requests;
    
public:
    bool allowRequest() {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex);
        
        // Clean old requests
        while (!requests.empty() && 
               std::chrono::duration_cast<std::chrono::seconds>(now - requests.front()).count() >= 1) {
            requests.pop();
        }
        
        if (requests.size() >= Config::MAX_REQUESTS_PER_SECOND) {
            return false;
        }
        
        requests.push(now);
        return true;
    }
};

// Efficient file cache with strict limits
class FileCache {
private:
    struct CacheEntry {
        std::string content;
        fs::file_time_type lastModified;
        std::chrono::steady_clock::time_point lastAccessed;
        size_t size;
        
        CacheEntry(std::string c, fs::file_time_type mod) 
            : content(std::move(c)), lastModified(mod), 
              lastAccessed(std::chrono::steady_clock::now()),
              size(content.size()) {}
    };
    
    std::unordered_map<std::string, std::unique_ptr<CacheEntry>> cache;
    mutable std::mutex mutex;
    std::atomic<size_t> totalSize{0};
    
    void evictLRU() {
        if (cache.empty()) return;
        
        auto oldest = cache.begin();
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->second->lastAccessed < oldest->second->lastAccessed) {
                oldest = it;
            }
        }
        
        totalSize -= oldest->second->size;
        cache.erase(oldest);
    }
    
public:
    std::string getFile(const std::string& filename) {
        if (!PathValidator::isValidPath(filename)) {
            return "";
        }
        
        std::lock_guard<std::mutex> lock(mutex);
        
        try {
            if (!fs::exists(filename) || !fs::is_regular_file(filename)) {
                return "";
            }
            
            auto fileSize = fs::file_size(filename);
            if (fileSize > Config::MAX_FILE_SIZE) {
                return "";
            }
            
            auto lastWrite = fs::last_write_time(filename);
            auto it = cache.find(filename);
            
            // Check if cached version is valid
            if (it != cache.end() && it->second->lastModified == lastWrite) {
                it->second->lastAccessed = std::chrono::steady_clock::now();
                return it->second->content;
            }
            
            // Read file
            std::ifstream file(filename, std::ios::binary);
            if (!file) return "";
            
            std::string content;
            content.resize(fileSize);
            
            if (!file.read(content.data(), fileSize)) {
                return "";
            }
            
            // Ensure cache limits
            while (totalSize + content.size() > Config::MAX_CACHE_SIZE || 
                   cache.size() >= Config::MAX_CACHE_ENTRIES) {
                evictLRU();
            }
            
            // Add to cache
            auto entry = std::make_unique<CacheEntry>(content, lastWrite);
            totalSize += entry->size;
            cache[filename] = std::move(entry);
            
            return content;
            
        } catch (const std::exception&) {
            return "";
        }
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        cache.clear();
        totalSize = 0;
    }
    
    size_t getCacheSize() const { return totalSize.load(); }
    size_t getCacheEntries() const { 
        std::lock_guard<std::mutex> lock(mutex);
        return cache.size(); 
    }
};

// Safe socket wrapper
class Socket {
private:
#ifdef _WIN32
    SOCKET sock;
    static constexpr auto INVALID_SOCK = INVALID_SOCKET;
#else
    int sock;
    static constexpr int INVALID_SOCK = -1;
#endif
    
public:
    Socket() : sock(INVALID_SOCK) {}
    
    Socket(Socket&& other) noexcept : sock(other.sock) {
        other.sock = INVALID_SOCK;
    }
    
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            sock = other.sock;
            other.sock = INVALID_SOCK;
        }
        return *this;
    }
    
    ~Socket() { close(); }
    
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    bool create() {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCK) return false;
        
        // Set socket options
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));
        
        // Set timeouts
#ifdef _WIN32
        DWORD timeout = 5000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
#else
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
        return true;
    }
    
    bool bind(int port) {
        if (sock == INVALID_SOCK) return false;
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        addr.sin_addr.s_addr = INADDR_ANY;
        
        return ::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    }
    
    bool listen(int backlog = 5) {
        return sock != INVALID_SOCK && ::listen(sock, backlog) == 0;
    }
    
    Socket accept() {
        Socket client;
        if (sock != INVALID_SOCK) {
            client.sock = ::accept(sock, nullptr, nullptr);
        }
        return client;
    }
    
    int recv(char* buffer, int size) {
        if (sock == INVALID_SOCK) return -1;
        return ::recv(sock, buffer, size, 0);
    }
    
    bool send(std::string_view data) {
        if (sock == INVALID_SOCK || data.empty()) return false;
        
        size_t sent = 0;
        while (sent < data.size()) {
            int result = ::send(sock, data.data() + sent, data.size() - sent, 0);
            if (result <= 0) return false;
            sent += result;
        }
        return true;
    }
    
    void close() {
        if (sock != INVALID_SOCK) {
#ifdef _WIN32
            shutdown(sock, SD_BOTH);
            closesocket(sock);
#else
            shutdown(sock, SHUT_RDWR);
            ::close(sock);
#endif
            sock = INVALID_SOCK;
        }
    }
    
    bool isValid() const { return sock != INVALID_SOCK; }
};

// Event-driven file watcher
class FileWatcher {
private:
#ifdef _WIN32
    std::unordered_map<std::string, fs::file_time_type> fileStates;
    std::mutex mutex;
    
    bool pollChanges() {
        std::lock_guard<std::mutex> lock(mutex);
        bool changed = false;
        
        try {
            // Only scan current directory to avoid deep recursion
            for (const auto& entry : fs::directory_iterator(".")) {
                if (!entry.is_regular_file()) continue;
                
                std::string path = entry.path().string();
                if (!PathValidator::isValidPath(path)) continue;
                
                auto writeTime = fs::last_write_time(entry.path());
                auto it = fileStates.find(path);
                
                if (it == fileStates.end() || it->second != writeTime) {
                    fileStates[path] = writeTime;
                    changed = true;
                }
            }
        } catch (const std::exception&) {}
        
        return changed;
    }
#else
    int inotifyFd;
    int watchFd;
    
public:
    FileWatcher() : inotifyFd(-1), watchFd(-1) {}
    
    bool initialize() {
        inotifyFd = inotify_init1(IN_NONBLOCK);
        if (inotifyFd == -1) return false;
        
        watchFd = inotify_add_watch(inotifyFd, ".", IN_MODIFY | IN_CREATE | IN_DELETE);
        return watchFd != -1;
    }
    
    bool checkChanges() {
        if (inotifyFd == -1) return pollChanges();
        
        char buffer[1024];
        ssize_t length = read(inotifyFd, buffer, sizeof(buffer));
        
        if (length <= 0) return false;
        
        size_t i = 0;
        while (i < static_cast<size_t>(length)) {
            auto* event = reinterpret_cast<inotify_event*>(&buffer[i]);
            if (event->len > 0 && PathValidator::isValidPath(event->name)) {
                return true;
            }
            i += sizeof(inotify_event) + event->len;
        }
        
        // Remove entries for files that no longer exist
        auto it = fileStates.begin();
        while (it != fileStates.end()) {
            if (!fs::exists(it->first)) {
                it = fileStates.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    ~FileWatcher() {
        if (watchFd != -1) close(watchFd);
        if (inotifyFd != -1) close(inotifyFd);
    }
#endif

#ifdef _WIN32
public:
    bool initialize() { return true; }
    bool checkChanges() { return pollChanges(); }
#endif
};

// HTTP response builder
class ResponseBuilder {
private:
    static const std::unordered_map<std::string, std::string> mimeTypes;
    
public:
    static std::string getMimeType(std::string_view filename) {
        auto dotPos = filename.find_last_of('.');
        if (dotPos == std::string_view::npos) return "text/plain";
        
        auto ext = filename.substr(dotPos);
        auto it = mimeTypes.find(std::string(ext));
        return it != mimeTypes.end() ? it->second : "text/plain";
    }
    
    static std::string buildResponse(int code, std::string_view contentType, std::string_view body) {
        std::string response;
        response.reserve(200 + body.size());
        
        response += "HTTP/1.1 ";
        response += std::to_string(code);
        response += code == 200 ? " OK\r\n" : " Error\r\n";
        response += "Content-Type: ";
        response += contentType;
        response += "\r\nContent-Length: ";
        response += std::to_string(body.size());
        response += "\r\nConnection: close\r\n";
        response += "Cache-Control: no-cache\r\n\r\n";
        response += body;
        
        return response;
    }
    
    static std::string notFound() {
        return buildResponse(404, "text/html", 
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
            "                return typeof commands[msg] === 'function' ? commands[msg]() : commands[msg];\n"
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
            "</html>"
        );
    }
    
    static std::string rateLimited() {
        return buildResponse(429, "application/json", "{\"error\":\"rate_limited\"}");
    }
    
    static std::string serverError() {
        return buildResponse(500, "application/json", "{\"error\":\"server_error\"}");
    }
};

const std::unordered_map<std::string, std::string> ResponseBuilder::mimeTypes = {
    {".html", "text/html"}, {".css", "text/css"}, {".js", "application/javascript"},
    {".json", "application/json"}, {".png", "image/png"}, {".jpg", "image/jpeg"},
    {".ico", "image/x-icon"}, {".svg", "image/svg+xml"}
};

// Main server class
class LiveServer {
private:
    Socket serverSocket;
    std::unique_ptr<FileWatcher> fileWatcher;
    std::unique_ptr<FileCache> fileCache;
    std::unique_ptr<BufferPool> bufferPool;
    std::unique_ptr<RateLimiter> rateLimiter;
    
    std::atomic<bool> running{false};
    std::atomic<uint32_t> activeConnections{0};
    std::atomic<bool> filesChanged{false};
    
    std::thread serverThread;
    std::thread watcherThread;
    
    int port;
    // Live reload script (minimal)
    static constexpr std::string_view liveReloadScript = R"(
(function(){
    let f=()=>{fetch('/reload').then(r=>r.json()).then(d=>{if(d.reload)location.reload()}).catch(()=>{})};
    setInterval(f,1000);
})();
)";
    
public:
    explicit LiveServer(int p = 3000) 
        : fileWatcher(std::make_unique<FileWatcher>())
        , fileCache(std::make_unique<FileCache>())
        , bufferPool(std::make_unique<BufferPool>())
        , rateLimiter(std::make_unique<RateLimiter>())
        , port(p) {}
    
    ~LiveServer() { stop(); }
    
    bool start() {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
        
        if (!serverSocket.create() || !serverSocket.bind(port) || !serverSocket.listen()) {
            return false;
        }
        
        if (!fileWatcher->initialize()) {
            std::cerr << "Warning: File watching unavailable\n";
        }
        
        running = true;
        
        // Server thread
        serverThread = std::thread([this]() {
            while (running && !SignalHandler::shouldShutdown()) {
                auto client = serverSocket.accept();
                if (client.isValid() && activeConnections < Config::MAX_CONNECTIONS) {
                    activeConnections++;
                    std::thread(&LiveServer::handleClient, this, std::move(client)).detach();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        
        // File watcher thread
        watcherThread = std::thread([this]() {
            while (running && !SignalHandler::shouldShutdown()) {
                if (fileWatcher->checkChanges()) {
                    filesChanged = true;
                    fileCache->clear();
                }
                std::this_thread::sleep_for(Config::POLL_INTERVAL);
            }
        });
        
        return true;
    }
    
    void stop() {
        running = false;
        serverSocket.close();
        
        if (serverThread.joinable()) serverThread.join();
        if (watcherThread.joinable()) watcherThread.join();
        
#ifdef _WIN32
        WSACleanup();
#endif
    }
    
    void run() {
        std::cout << "Server: http://localhost:" << port << "\n";
        std::cout << "Press Ctrl+C to stop\n";
        
        while (running && !SignalHandler::shouldShutdown()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        stop();
    }
    
private:
    void handleClient(Socket client) {
        activeConnections--;  // Ensure decrement on exit
        
        if (!rateLimiter->allowRequest()) {
            client.send(ResponseBuilder::rateLimited());
            return;
        }
        
        auto buffer = bufferPool->acquire();
        if (!buffer) {
            client.send(ResponseBuilder::serverError());
            return;
        }
        
        int bytesRead = client.recv(buffer.get(), Config::MAX_REQUEST_SIZE - 1);
        if (bytesRead <= 0) return;
        
        buffer[bytesRead] = '\0';
        std::string_view request(buffer.get(), bytesRead);
        
        // Parse request
        auto firstSpace = request.find(' ');
        auto secondSpace = request.find(' ', firstSpace + 1);
        
        if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos) {
            return;
        }
        
        auto method = request.substr(0, firstSpace);
        auto path = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
        
        if (method != "GET") return;
        
        // Route request
        if (path == "/reload") {
            serveReload(client);
        } else if (path == "/live-reload.js") {
            client.send(ResponseBuilder::buildResponse(200, "application/javascript", liveReloadScript));
        } else {
            serveFile(client, path == "/" ? "index.html" : std::string(path.substr(1)));
        }
        
        bufferPool->release(std::move(buffer));
    }
    
    void serveFile(Socket& client, const std::string& filename) {
        auto content = fileCache->getFile(filename);
        if (content.empty()) {
            client.send(ResponseBuilder::notFound());
            return;
        }
        
        // Inject live reload for HTML
        if (filename.ends_with(".html")) {
            auto pos = content.find("</body>");
            if (pos != std::string::npos) {
                content.insert(pos, "<script src=\"/live-reload.js\"></script>");
            }
        }
        
        auto mimeType = ResponseBuilder::getMimeType(filename);
        client.send(ResponseBuilder::buildResponse(200, mimeType, content));
    }
    
    void serveReload(Socket& client) {
        bool reload = filesChanged.exchange(false);
        std::string json = reload ? "{\"reload\":true}" : "{\"reload\":false}";
        client.send(ResponseBuilder::buildResponse(200, "application/json", json));
    }
};

int main(int argc, char* argv[]) {
    SignalHandler::setup();
    
    int port = 3000;
    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535) port = 3000;
    }
    
    try {
        LiveServer server(port);
        if (!server.start()) {
            std::cerr << "Failed to start server\n";
            return 1;
        }
        
        server.run();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
/*    
    COMPILATION COMMANDS:
    
    WINDOWS (MinGW):
    g++ -x c++ -std=c++20 -static -O3 -DNDEBUG -flto -march=native LiveServer.cpp -o LiveServer.exe -lstdc++ -lws2_32
    
    LINUX:
    g++ -std=c++20 -O3 -DNDEBUG -flto -march=native LiveServer.cpp -o LiveServer -lpthread
    
    macOS:
    g++ -std=c++20 -O3 -DNDEBUG -flto -march=native LiveServer.cpp -o LiveServer
    
    FLAGS:
    - O3: Maximum optimization
    - DNDEBUG: Disable debug assertions
    - flto: Link-time optimization
    - march=native: CPU-specific optimizations
*/