; Copyright © 2025 Mint teams
; syscalls.asm
; The generic Node.js process watcher
 
section .text
    global _get_mtime_asm
    
    ; extern time_t get_mtime_asm(const char *filepath);
    ; Input:
    ;   rdi: const char* (pointer to filepath str)
    ; Output:
    ;   rax: time_t (modification time) or 0 on error

    _get_mtime_asm:
        ; To the system V AMD64 ABI
        sub rsp, 144

        ; Prepare args for the 'stat' system call
        ; Syscall number 4 on x86-64 Linux.
        ; int stat(const char *pathname, struct stat *statbuf);
        mov rax, STAT_SYSCALL   ; This value is passed from the Makefile via -D
        ; rdi already contains the first args
        mov rsi, rsp
        
        ; Make the syscall
        syscall

        ; The system call returns the result in rax reg
        test rax, rax           ; Check if rax register is negative
        js .error               ; If so, jump to error handler

        ; If successful, extract st_mtine.tv_sec from the stat struct
        ; x86-64 Linux - the st_mtine field 
        mov rax, [rsp + 88]     ; rax = st_mtine.tv_sec

        ; Clean up the stack and return
        add rsp, 144
        ret

    .error:
        ; On error, will return 0
        xor rax, rax           ; rax = 0
        add rsp, 144           ; Clean up the stack
        ret

    ; Process
    global _process_stop_asm

    _process_stop_asm:
        ; syscall: kill(pid_t pid, int sig)
        ; On macOS target the process group by passing a negative PID.
        neg rdi                ; rdi = -rdi
        mov rax, KILL_SYSCALL  ; This value is passed from the Makefile
        mov rsi, 15            ; rsi = signal number for SIGTERM
        syscall
        ret