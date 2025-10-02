/*
    Copyright © 2025 Mint teams
    interface.c - Win32 UI Implementation for LiveServer
*/

#include <stdio.h>
#include <windows.h>
#include <process.h>
#include <richedit.h>
#include <commctrl.h>
#include <uxtheme.h>

#include "interface.h"

// Forward declaration
unsigned __stdcall server_main_thread(void* pArguments);
void cleanup_memory_pool();

// User-interface global vars
static HWND hLogEdit = NULL;
static HBRUSH g_bgBrush = NULL;
static HFONT g_hFont = NULL;
static HINSTANCE hRichEdit = NULL;
static volatile int* p_server_running_flag = NULL;

// User-interface functions

void Log(const char* format, ...) {
    if (!hLogEdit) return;
    
    char buffer[4096];
    va_list args;
    va_start(args,format);
    int count = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (count < 0) return;

    int len = GetWindowTextLength(hLogEdit);
    SendMessage(hLogEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hLogEdit, EM_REPLACESEL, 0, (LPARAM)buffer);
    SendMessage(hLogEdit, EM_REPLACESEL, 0, (LPARAM)"\r\n");
}

void LogWithColor(const char* format, COLORREF color, ...) {
    if (!hLogEdit) return;
    
    char buffer[4096];
    va_list args;
    va_start(args, color);
    int count = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (count < 0) return;

    int len = GetWindowTextLength(hLogEdit);
    SendMessage(hLogEdit, EM_SETSEL, len, len);

    CHARFORMAT2 cf = {0};
    cf.cbSize = sizeof(CHARFORMAT2);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;

    SendMessage(hLogEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessage(hLogEdit, EM_REPLACESEL, 0, (LPARAM)buffer);
    SendMessage(hLogEdit, EM_REPLACESEL, 0, (LPARAM)"\r\n");
}

void ApplyTheme(HWND hwnd) {
    (void)hwnd;
    COLORREF g_bgColor = RGB(18,18,18);

    if (g_bgBrush) {
        DeleteObject(g_bgBrush);
    }

    g_bgBrush = CreateSolidBrush(g_bgColor);
    
    // BOOL dark = TRUE;
    // DwmSetWindowAttribute is not include let's me create a simple
    // It would be dark mode titlebar

    if (hLogEdit) {
        SendMessage(hLogEdit, EM_SETBKGNDCOLOR, 0, g_bgColor);
        InvalidateRect(hLogEdit, NULL, TRUE);
        UpdateWindow(hLogEdit);
    }
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            hRichEdit = LoadLibrary("MsFtedit.dll");
            if (!hRichEdit) {
                MessageBox(hwnd, "Failed to load Rich Edit library", "Error", MB_OK);
                return -1;
            }

            hLogEdit = CreateWindowExA(0, "RICHEDIT50W", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                0, 0, 0, 0, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);

            if (!hLogEdit) {
                MessageBox(hwnd, "Failed to create Rich Edit control", "Error", MB_OK);
                return -1;
            }

            g_hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

            if (g_hFont) SendMessage(hLogEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            ApplyTheme(hwnd);
            break;
    
        case WM_SIZE:
            MoveWindow(hLogEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
            break;

        case WM_CLOSE:
            if (MessageBox(hwnd, "Stop Kavin Liveserver?", "Kavin LiveServer", MB_OKCANCEL) == IDOK) {
                if (p_server_running_flag) {
                    *p_server_running_flag = 0; // Signal the server thread to stop
                }
                DestroyWindow(hwnd);
            }

            break;

        case WM_DESTROY:
            if (g_hFont) DeleteObject(g_hFont);
            if (g_bgBrush) DeleteObject(g_bgBrush);
            if (hRichEdit) FreeLibrary(hRichEdit);
            cleanup_memory_pool();
            PostQuitMessage(0);
            break;
        
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    const char CLASS_NAME[] = "KavinLiveserverClass";

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    // The server thread need to access command line args
    HANDLE hServerThread = (HANDLE)_beginthreadex(NULL, 0, *server_main_thread, NULL, 0, NULL);

    MSG msg = {0};

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    WaitForSingleObject(hServerThread, INFINITE);
    CloseHandle(hServerThread);

    return (int)msg.wParam;
}