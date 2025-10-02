@echo off
setlocal enabledelayedexpansion

REM # Copyright © 2025 Mint teams
REM # Build script for Kavin on Windows

REM # Configuration
set "TARGET_NAME=kavin"
set "TARGET_EXT=.exe"

REM # Compilers and Assembler
set "CC=gcc"
set "ASM=nasm"

REM # Directories
set "SRC_DIR=src"
set "OBJ_DIR=obj"
set "DIST_DIR=dist"

REM # Flags
set "AFLAGS=-f win64"
set "ASM_DEFINES=-DSTAT_SYSCALL=4 -DKILL_SYSCALL=0"
set "CFLAGS=-O3 -march=native -flto -Wall -Wextra -I%SRC_DIR% -D_WIN32"
set "LDFLAGS=-flto -lwinmm"

REM # Build Process
set "CURRENT_DIR=%cd%"
REM # Clean previous build
if /i "%1"=="clean" (
    echo Cleaning up...
    if exist "%DIST_DIR%" rd /s /q "%DIST_DIR%"
    if exist "%OBJ_DIR%" rd /s /q "%OBJ_DIR%"
    echo Clean complete.
    goto :eof
)

REM # Create output directories
if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"

set "OBJ_FILES="

REM # Compile C source files
echo Compiling C files...
for /r "%SRC_DIR%" %%F in (*.c) do (
    echo   CC  %%F
    set "FULL_PATH=%%F"
    set "REL_PATH=!FULL_PATH:%CURRENT_DIR%\%SRC_DIR%\=!"
    set "OBJ_SUBDIR=%OBJ_DIR%\!REL_PATH!\.."
    set "OBJ_PATH=%OBJ_DIR%\!REL_PATH:.c=.o!"
    if not exist "!OBJ_SUBDIR!" mkdir "!OBJ_SUBDIR!"
    %CC% %CFLAGS% -c -o "!OBJ_PATH!" "%%F"
    set "OBJ_FILES=!OBJ_FILES! "!OBJ_PATH!""
)

REM # Assemble ASM source files
echo Assembling ASM files...
for /r "%SRC_DIR%" %%F in (*.asm) do (
    echo   ASM %%F
    set "FULL_PATH=%%F"
    set "REL_PATH=!FULL_PATH:%CURRENT_DIR%\%SRC_DIR%\=!"
    set "OBJ_SUBDIR=%OBJ_DIR%\!REL_PATH!\.."
    set "OBJ_PATH=%OBJ_DIR%\!REL_PATH:.asm=.o!"
    if not exist "!OBJ_SUBDIR!" mkdir "!OBJ_SUBDIR!"
    %ASM% %AFLAGS% %ASM_DEFINES% -o "!OBJ_PATH!" "%%F"
    set "OBJ_FILES=!OBJ_FILES! "!OBJ_PATH!""
)

REM # Link object files
echo Linking...
%CC% %LDFLAGS% -o "%DIST_DIR%\%TARGET_NAME%%TARGET_EXT%" %OBJ_FILES%

echo Build finished: %DIST_DIR%\%TARGET_NAME%%TARGET_EXT%
endlocal