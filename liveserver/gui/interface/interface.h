/*
    Copyright © 2025 Mint teams
    interface.h - Win32 UI Interface for LiveServer
*/

#ifndef INTERFACE_H
#define INTERFACE_H

#include <windows.h>

// VSCode-like Log Colors
#define LOG_COLOR_COMMENT    RGB(106, 153, 85)   // #6a9955
#define LOG_COLOR_KEYWORD    RGB(86, 156, 214)   // #569cd6
#define LOG_COLOR_STRING     RGB(206, 145, 120)  // #ce9178
#define LOG_COLOR_NUMBER     RGB(181, 206, 168)  // #b5cea8
#define LOG_COLOR_FUNCTION   RGB(220, 220, 170)  // #dcdcaa
#define LOG_COLOR_VARIABLE   RGB(156, 220, 254)  // #9cdcfe
#define LOG_COLOR_TYPE       RGB(78, 201, 176)   // #4ec9b0
#define LOG_COLOR_ERROR      RGB(244, 71, 71)    // Bright Red
#define LOG_COLOR_SUCCESS    RGB(110, 200, 120)  // Bright Green
#define LOG_COLOR_WARNING    RGB(255, 180, 80)   // Orange/Yellow

// Functions for the server logic to log messages to the UI
void Log(const char* format, ...);
void LogWithColor(const char* format, COLORREF color, ...);
void stop_server();

#endif // INTERFACE_H