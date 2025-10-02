/*
    Copyright © 2025 Mint teams
    interface.h - Win32 UI Interface for LiveServer
*/

#ifndef INTERFACE_H
#define INTERFACE_H

#include <windows.h>

// Functions for the server logic to log messages to the UI
void Log(const char* format, ...);
void LogWithColor(const char* format, COLORREF color, ...);

#endif // INTERFACE_H