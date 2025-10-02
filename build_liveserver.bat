@echo off
setlocal enabledelayedexpansion

REM # Copyright © 2025 Mint teams
REM # Build script for LiveServer GUI on Windows

REM # --- Configuration ---
set "TARGET_NAME=liveserver"
set "TARGET_EXT=.exe"

REM # Compilers and Assembler
set "CC=gcc"
set "ASM=nasm"

REM # Directories
set "SRC_DIR=liveserver\gui"
set "OBJ_DIR=obj\liveserver"
set "DIST_DIR=dist"

REM # Flags
set "AFLAGS=-f win64"
set "CFLAGS=-O3 -Wall -Wextra -I%SRC_DIR% -Wno-int-to-pointer-cast"
set "LDFLAGS=-lws2_32 -lshell32 -mwindows -ldwmapi -luxtheme"

REM # --- Build Process ---

REM # Clean previous build
if /i "%1"=="clean" (
    echo Cleaning up LiveServer build...
    if exist "%DIST_DIR%\%TARGET_NAME%%TARGET_EXT%" del "%DIST_DIR%\%TARGET_NAME%%TARGET_EXT%"
    if exist "%OBJ_DIR%" rd /s /q "%OBJ_DIR%"
    echo Clean complete.
    goto :eof
)

REM # Create output directories
if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"

set "OBJ_FILES="

REM # Compile C source files
echo Compiling LiveServer C files...
for /r "%SRC_DIR%" %%F in (*.c) do (
    set "FILE_PATH=%%F"
    set "OBJ_NAME=!FILE_PATH:%SRC_DIR%\=!"
    set "OBJ_NAME=!OBJ_NAME:.c=.o!"
    set "OBJ_PATH=%OBJ_DIR%\!OBJ_NAME:\=_!"
    echo   CC  !FILE_PATH!
    %CC% %CFLAGS% -c "%%F" -o "!OBJ_PATH!"
    if errorlevel 1 ( echo Build failed. & exit /b 1 )
    set "OBJ_FILES=!OBJ_FILES! "!OBJ_PATH!""
)

REM # Assemble ASM source files
echo Assembling LiveServer ASM files...
for /r "%SRC_DIR%" %%F in (*.asm) do (
    set "FILE_PATH=%%F"
    set "OBJ_NAME=!FILE_PATH:%SRC_DIR%\=!"
    set "OBJ_NAME=!OBJ_NAME:.asm=.o!"
    set "OBJ_PATH=%OBJ_DIR%\!OBJ_NAME:\=_!"
    echo   ASM !FILE_PATH!
    %ASM% %AFLAGS% "%%F" -o "!OBJ_PATH!"
    if errorlevel 1 ( echo Build failed. & exit /b 1 )
    set "OBJ_FILES=!OBJ_FILES! "!OBJ_PATH!""
)

REM # Link object files
echo Linking %TARGET_NAME%%TARGET_EXT%...
%CC% -o "%DIST_DIR%\%TARGET_NAME%%TARGET_EXT%" %OBJ_FILES% %LDFLAGS%

echo.
echo Build finished: %DIST_DIR%\%TARGET_NAME%%TARGET_EXT%
endlocal