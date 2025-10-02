; Copyright © 2025 Mint teams
; memory.asm - Memory optimizations

section .text
    global find_block_index_asm

; int find_block_index_asm(void** block, int count, int count, void** ptr);
; search for a pointer "ptr" within "blocks" array
; Return the index if found -1 otherwise

; Windows x64 ABI:
; RCX = blocks (pointer to array of pointer)
; RDX = count (number of elements)
; R8 = ptr (pointer to find)

find_block_index_asm:
    push    rbx                ; Pre-serve RBX register
    xor     rax, rax           ; rax = count index(i) = 0
    mov     rbx, rdx           ; rbx = cound
    test    rbx, rbx           ; Check if count is = 0
    jle     .not_found         ; If count <= 0, Nothing.

    ; Load the target pointer into XMM register
    ; We can compare 2 pointer
    movq    xmm0, r8           ; xmm0 = [ 0 | ptr_to_find ]
    pshufd  xmm0, xmm0, 0      ; xmm0 = [ ptr_to_find | ptr_to_find ]

.search_loop:
    ; Check if we have at least 2 elements left to process
    cmp     rax, rbx
    jge     .cleanup_loop      ; If i =< 1 count

    ; Load 2 pointer (16 bytes) from the array into xmm1
    ; blocks[i] and blocks[i+1]
    movdqa  xmm1, [rcx + rax * 8]

    ; Compare the target pointer
    ; pcmpeqq performs a 64bit comparison
    pcmpeqq xmm1, xmm0

    ; Create a bitmask from the comparison result
    pmovmskb edx, xmm1
    test    edx, edx
    jnz     .found             ; Match found, jump to calc index
    
    ; Not match
    add     rax, 2
    jmp     .search_loop

.found:
    ; A match was found in the last 2 elements 
    ; The mask in edx register tells us which element
    ; If the first byte is set (mask & 0x01), it's the first pointer
    ; If the ninth byte is set (mask & 0x100), it's the second pointer
    ; We can find the first set bit
    bsf    rdx, rdx            ; Find bit scan found
    shr    rdx, 3              ; Divide by 8 byte per pointer
    add    rdx, rax            ; Add offset to base index
    jmp    .done               ; Return the index

.cleanup_loop:
    ; Handle the last element if count is odd
    cmp    rax, rbx
    jge    .not_found          ; If i >= count, we are done
    mov    rbx, [rcx + rax * 8]
    cmp    rbx, r8
    je     .done               ; Found it, rax is the correct index
    inc    rax
    jmp    .cleanup_loop

.not_found:
    mov     rax, -1            ; Return -1

.done:
    pop     rbx                ; Restore RBX register
    ret