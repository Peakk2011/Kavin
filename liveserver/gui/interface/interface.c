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
#include <dwmapi.h>

#include "interface.h"

#define APP_BG           RGB(24, 24, 24)
#define APP_TITLEBAR     RGB(24, 24, 24)
#define APP_TEXT         RGB(255, 255, 255)
 
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20 19
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

unsigned __stdcall server_main_thread(void* pArguments);
void cleanup_memory_pool();
void stop_server();

HWND g_hEdit = NULL;
static HBRUSH g_bgBrush = NULL;
static HFONT g_hFont = NULL;
static HINSTANCE hRichEdit = NULL;

void EnableDarkModeTitleBar(HWND hwnd) {
    if (!hwnd) return;
    
    BOOL useDarkMode = TRUE;
    BOOL darkModeEnabled = FALSE;
    
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, 
                                        &useDarkMode, sizeof(useDarkMode)))) {
        darkModeEnabled = TRUE;
    }
    else if (SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20, 
                                             &useDarkMode, sizeof(useDarkMode)))) {
        darkModeEnabled = TRUE;
    }
    
    if (darkModeEnabled) {
        COLORREF titleBarColor = APP_TITLEBAR;
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &titleBarColor, sizeof(titleBarColor));
        
        COLORREF textColor = APP_TEXT;
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
    }
    
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, 
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

void Log(const char* format, ...) {
    if (!g_hEdit) return;

    char buffer[4096];
    va_list args;
    va_start(args, format);
    int count = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (count < 0) return;

    int len = GetWindowTextLength(g_hEdit);
    SendMessage(g_hEdit, EM_SETSEL, len, len);

    CHARFORMAT2 cf = {0};
    cf.cbSize = sizeof(CHARFORMAT2);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = APP_TEXT;  // default white color
    SendMessage(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    SendMessage(g_hEdit, EM_REPLACESEL, 0, (LPARAM)buffer);
    SendMessage(g_hEdit, EM_REPLACESEL, 0, (LPARAM)"\r\n");
}


void LogWithColor(const char* format, COLORREF color, ...) {
    if (!g_hEdit) return;
    
    char buffer[4096];
    va_list args;
    va_start(args, format);
    int count = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (count < 0) return;

    int len = GetWindowTextLength(g_hEdit);
    SendMessage(g_hEdit, EM_SETSEL, len, len);

    CHARFORMAT2 cf = {0};
    cf.cbSize = sizeof(CHARFORMAT2);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;

    SendMessage(g_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessage(g_hEdit, EM_REPLACESEL, 0, (LPARAM)buffer);
    SendMessage(g_hEdit, EM_REPLACESEL, 0, (LPARAM)"\r\n");
}

void ApplyTheme(HWND hwnd) {
    (void)hwnd;

    if (g_bgBrush) {
        DeleteObject(g_bgBrush);
    }

    g_bgBrush = CreateSolidBrush(APP_BG);

    if (g_hEdit) {
        // background
        SendMessage(g_hEdit, EM_SETBKGNDCOLOR, 0, APP_BG);

        // default for text
        CHARFORMAT2 cf = {0};
        cf.cbSize = sizeof(CHARFORMAT2);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = APP_TEXT;
        SendMessage(g_hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
        SendMessage(g_hEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);

        InvalidateRect(g_hEdit, NULL, TRUE);
        UpdateWindow(g_hEdit);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            hRichEdit = LoadLibrary("MsFtedit.dll");
            if (!hRichEdit) {
                MessageBox(hwnd, "Failed to load Rich Edit library", "Error", MB_OK | MB_ICONERROR);
                return -1;
            }

            g_hEdit = CreateWindowExA(0, "RICHEDIT50W", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                0, 0, 0, 0, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);

            if (!g_hEdit) {
                MessageBox(hwnd, "Failed to create Rich Edit control", "Error", MB_OK | MB_ICONERROR);
                return -1;
            }

            g_hFont = CreateFontA(12, 0, 0, 0, 400, FALSE, FALSE, FALSE, 
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

            if (g_hFont) {
                SendMessage(g_hEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            }

            CHARFORMAT2 cf = {0};
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
            cf.crTextColor = APP_TEXT;
            cf.yHeight = 12 * 20;  // FontSize
            strcpy(cf.szFaceName, "Consolas");
            SendMessage(g_hEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);

            // Background Settings
            SendMessage(g_hEdit, EM_SETBKGNDCOLOR, 0, APP_BG);

            ApplyTheme(hwnd);
            EnableDarkModeTitleBar(hwnd);
            break;
        }
    
        case WM_SIZE:
            if (g_hEdit) {
                if (wParam == SIZE_MAXIMIZED) {
                    MoveWindow(g_hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
                } else {
                    int padding = 8;
                    MoveWindow(g_hEdit, padding, 0, LOWORD(lParam) - (padding * 2), HIWORD(lParam) - padding, TRUE);
                }
            }
            break;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, APP_TEXT);
            SetBkColor(hdcStatic, APP_BG);
            return (INT_PTR)g_bgBrush;
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rect;
            GetClientRect(hwnd, &rect);
            FillRect(hdc, &rect, g_bgBrush);
            return 1;
        }

        case WM_CLOSE:
            if (MessageBox(hwnd, "Stop Kavin Liveserver?", "Kavin Liveserver", 
                          MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
                stop_server();
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    const char CLASS_NAME[] = "KavinLiveserverClass";

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = g_bgBrush ? g_bgBrush : (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    // Load application icon from assets folder
    HICON hAppIcon = (HICON)LoadImage(
        NULL,
        "assets\\Logo.ico", 
        IMAGE_ICON,
        0, 0, 
        LR_LOADFROMFILE | LR_DEFAULTSIZE
    );
    wc.hIcon = hAppIcon ? hAppIcon : LoadIcon(NULL, IDI_APPLICATION);     // Large icon
    wc.hIconSm = hAppIcon ? hAppIcon : LoadIcon(NULL, IDI_APPLICATION);   // Small icon

    // Register the window class
    if (!RegisterClassEx(&wc)) { 
        MessageBox(NULL, "Window Registration Failed!", "Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        "Kavin Liveserver",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (!hwnd) {
        MessageBox(NULL, "Window Creation Failed!", "Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    HANDLE hServerThread = (HANDLE)_beginthreadex(NULL, 0, server_main_thread, NULL, 0, NULL);
    if (!hServerThread) {
        MessageBox(hwnd, "Failed to start server thread", "Error", MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd);
        return 0;
    }

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    WaitForSingleObject(hServerThread, INFINITE);
    CloseHandle(hServerThread);

    return (int)msg.wParam;
}