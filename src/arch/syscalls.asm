; Copyright © 2025 Mint teams
; syscalls.asm - x86-64 syscall wrappers

section .data
    ; stat struct buffer, size is 144 bytes on x86-64 Linux
    stat_buf: times 144 db 0

section .text
    global get_mtime_asm
    global process_stop_asm

; time_t get_mtime_asm(const char *filepath)
; Returns the modification time of a file.
; On error or if file not found, returns 0.
; C ABI: RDI = filepath
get_mtime_asm:
    push    rbp
    mov     rbp, rsp

    ; Syscall: stat(const char *pathname, struct stat *statbuf)
    mov     rax, STAT_SYSCALL   ; Syscall number for stat (4 on x86-64)
    ; RDI already contains the filepath (1st argument)
    lea     rsi, [rel stat_buf] ; 2nd argument: pointer to stat_buf
    syscall

    ; Check for error (syscall returns negative on error)
    test    rax, rax
    js      .error

    ; Success: The modification time (st_mtime) is at offset 104 (0x68)
    ; in the stat struct for x86-64. It's a 64-bit value (timespec.tv_sec).
    mov     rax, [rel stat_buf + 104]
    jmp     .done

.error:
    xor     rax, rax            ; Return 0 on error

.done:
    pop     rbp
    ret

; void process_stop_asm(pid_t pid)
; Sends SIGTERM (15) to the process group.
; C ABI: RDI = pid
process_stop_asm:
    push    rbp
    mov     rbp, rsp

    ; Syscall: kill(pid_t pid, int sig)
    mov     rax, KILL_SYSCALL   ; Syscall number for kill (62 on x86-64)
    ; RDI already contains the pid (1st argument)
    neg     rdi                 ; Negate PID to target the whole process group
    mov     rsi, 15             ; 2nd argument: signal number for SIGTERM
    syscall

    ; No return value needed (void function)
    pop     rbp
    ret