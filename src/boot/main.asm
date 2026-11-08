; Zhaoxu OS - UEFI boot layer
; FASM / PE64 EFI.  Loads the flat kernel image, builds identity + higher-half
; page tables, exits boot services and transfers control to the C kernel.

format PE64 EFI
entry efi_start

include 'boot/uefi.inc'
include 'build/config.inc'

KERNEL_VMA       = 0xffffffff80000000
KERNEL_MAP_SIZE  = 0x01000000          ; 16 MiB virtual/physical window
KERNEL_STACK_TOP = 0xffffffff80f00000
BOOT_INFO_OFF    = 0x00e00000
MMAP_OFF         = 0x00c00000
MMAP_MAX         = 0x00100000
KERNEL_ALLOC     = 0x01200000          ; 18 MiB of pages, 2 MiB aligned inside
PAGE_TABLE_PAGES = 8

section '.text' code readable executable

efi_start:
        cld
        and     rsp, -16
        mov     r14, rcx                ; image handle
        mov     r15, rdx                ; system table
        mov     [image_handle], rcx
        mov     [system_table], rdx
        mov     rax, [rdx + ST_BootServices]
        mov     [boot_services], rax
        mov     rax, [rdx + ST_ConOut]
        mov     [conout], rax

        lea     rsi, [msg_banner]
        call    boot_print
        lea     rsi, [msg_probe]
        call    boot_print

; ---------------------------------------------------------------- GOP probe
if HEADLESS = 0
        lea     rcx, [gop_guid]
        xor     edx, edx
        lea     r8, [gop_ptr]
        mov     rax, [boot_services]
        sub     rsp, 0x20
        call    qword [rax + BS_LocateProtocol]
        add     rsp, 0x20
        test    rax, rax
        js      .no_gop
        mov     rax, [gop_ptr]
        test    rax, rax
        jz      .no_gop
        mov     rax, [rax + GOP_MODE]
        test    rax, rax
        jz      .no_gop
        mov     [gop_mode], rax
        mov     rcx, [rax + GOP_MODE_INFO]
        test    rcx, rcx
        jz      .no_gop
        mov     edx, [rcx + GOP_MODE_INFO_WIDTH]
        mov     [fb_width], edx
        mov     edx, [rcx + GOP_MODE_INFO_HEIGHT]
        mov     [fb_height], edx
        mov     edx, [rcx + GOP_MODE_INFO_FORMAT]
        mov     [fb_format], edx
        mov     edx, [rcx + GOP_MODE_INFO_PPSL]
        shl     edx, 2
        mov     [fb_pitch], edx
        mov     rax, [gop_mode]
        mov     rdx, [rax + GOP_MODE_FB_BASE]
        mov     [fb_base], rdx
        mov     rdx, [rax + GOP_MODE_FB_SIZE]
        mov     [fb_size], rdx
        lea     rsi, [msg_gop]
        call    boot_print
        jmp     .gop_done
.no_gop:
        lea     rsi, [msg_nogop]
        call    boot_print
.gop_done:
end if

; ---------------------------------------------------------------- ACPI RSDP
        mov     rcx, [r15 + ST_NumberOfTableEntries]
        mov     rsi, [r15 + ST_ConfigurationTable]
        test    rsi, rsi
        jz      .rsdp_done
        test    rcx, rcx
        jz      .rsdp_done
.rsdp_loop:
        mov     rax, [rsi]
        cmp     rax, qword [acpi_guid]
        jne     .rsdp_next
        mov     rax, [rsi + 8]
        cmp     rax, qword [acpi_guid + 8]
        jne     .rsdp_next
        mov     rax, [rsi + 16]
        mov     [rsdp], rax
        jmp     .rsdp_done
.rsdp_next:
        add     rsi, 24
        dec     rcx
        jnz     .rsdp_loop
.rsdp_done:

; ---------------------------------------------------------- kernel pages
        xor     ecx, ecx                ; AllocateAnyPages
        mov     edx, EFI_LOADER_DATA
        mov     r8, KERNEL_ALLOC / 0x1000
        lea     r9, [kernel_alloc]
        mov     rax, [boot_services]
        sub     rsp, 0x20
        call    qword [rax + BS_AllocatePages]
        add     rsp, 0x20
        test    rax, rax
        js      .fail_kernel_pages
        mov     rax, [kernel_alloc]
        add     rax, 0x1fffff
        and     rax, -0x200000
        mov     [kernel_phys], rax
        lea     rsi, [msg_kernel]
        call    boot_print

        ; zero the whole mapped window before copying the image
        mov     rdi, [kernel_phys]
        mov     rcx, KERNEL_MAP_SIZE / 8
        xor     eax, eax
        rep     stosq

        lea     rsi, [kernel_blob]
        mov     rdi, [kernel_phys]
        mov     rcx, kernel_blob_end - kernel_blob
        rep     movsb

; ---------------------------------------------------------- page tables
        xor     ecx, ecx
        mov     edx, EFI_LOADER_DATA
        mov     r8, PAGE_TABLE_PAGES
        lea     r9, [pt_alloc]
        mov     rax, [boot_services]
        sub     rsp, 0x20
        call    qword [rax + BS_AllocatePages]
        add     rsp, 0x20
        test    rax, rax
        js      .fail_pt_pages
        mov     rdi, [pt_alloc]
        mov     rcx, PAGE_TABLE_PAGES * 512
        xor     eax, eax
        rep     stosq

        ; PML4[0] -> low PDPT, PML4[511] -> high PDPT
        mov     rdi, [pt_alloc]
        lea     rax, [rdi + 0x1000]
        or      rax, 3
        mov     [rdi], rax
        lea     rax, [rdi + 0x6000]
        or      rax, 3
        mov     [rdi + 511 * 8], rax

        ; low PDPT entries -> four page directories (identity, first 4 GiB)
        lea     rdi, [rdi + 0x1000]
        xor     r8, r8
.pdpt_low:
        lea     rax, [rdi + 0x1000]     ; pd0 (plus r8 pages)
        mov     rcx, r8
        shl     rcx, 12
        add     rax, rcx
        or      rax, 3
        mov     [rdi + r8 * 8], rax
        inc     r8
        cmp     r8, 4
        jb      .pdpt_low

        ; fill four 2 MiB huge-page directories
        xor     r8, r8
.pd_fill:
        mov     rdi, [pt_alloc]
        add     rdi, 0x2000
        mov     rax, r8
        shl     rax, 12
        add     rdi, rax
        xor     r9, r9
.pd_entry:
        mov     rax, r8
        shl     rax, 30
        mov     rcx, r9
        shl     rcx, 21
        add     rax, rcx
        or      rax, 0x83
        mov     [rdi + r9 * 8], rax
        inc     r9
        cmp     r9, 512
        jb      .pd_entry
        inc     r8
        cmp     r8, 4
        jb      .pd_fill

        ; high PDPT[510] -> kernel PD
        mov     rdi, [pt_alloc]
        add     rdi, 0x6000
        mov     rax, [pt_alloc]
        add     rax, 0x7000
        or      rax, 3
        mov     [rdi + 510 * 8], rax

        ; map the 16 MiB kernel window
        mov     rdi, [pt_alloc]
        add     rdi, 0x7000
        xor     r8, r8
.kmap:
        mov     rax, [kernel_phys]
        mov     rcx, r8
        shl     rcx, 21
        add     rax, rcx
        or      rax, 0x83
        mov     [rdi + r8 * 8], rax
        inc     r8
        cmp     r8, KERNEL_MAP_SIZE / 0x200000
        jb      .kmap

; ---------------------------------------------------------- boot info
        mov     rdi, [kernel_phys]
        add     rdi, BOOT_INFO_OFF
        mov     rax, 0x5a4f5331
        mov     [rdi + 0], rax
        mov     rax, [fb_base]
        mov     [rdi + 8], rax
        mov     rax, [fb_size]
        mov     [rdi + 16], rax
        mov     eax, [fb_width]
        mov     [rdi + 24], eax
        mov     eax, [fb_height]
        mov     [rdi + 28], eax
        mov     eax, [fb_pitch]
        mov     [rdi + 32], eax
        mov     eax, [fb_format]
        mov     [rdi + 36], eax
        mov     rax, KERNEL_VMA
        add     rax, MMAP_OFF
        mov     [rdi + 40], rax
        mov     rax, [rsdp]
        mov     [rdi + 72], rax
        mov     rax, [kernel_phys]
        mov     [rdi + 80], rax
        mov     rax, KERNEL_VMA
        mov     [rdi + 88], rax
        mov     rax, kernel_blob_end - kernel_blob
        mov     [rdi + 96], rax
        mov     rax, KERNEL_MAP_SIZE
        mov     [rdi + 104], rax
        mov     rax, [pt_alloc]
        mov     [rdi + 112], rax
        mov     qword [rdi + 120], 1

; ---------------------------------------------------------- memory map
        mov     qword [exit_retries], 4
.refresh_mmap:
        mov     qword [mmap_size], MMAP_MAX
        mov     rcx, mmap_size
        mov     rdx, [kernel_phys]
        add     rdx, MMAP_OFF
        lea     r8, [mmap_key]
        lea     r9, [mmap_desc_size]
        mov     rax, [boot_services]
        sub     rsp, 0x30
        lea     r10, [mmap_desc_ver]
        mov     [rsp + 0x20], r10
        call    qword [rax + BS_GetMemoryMap]
        add     rsp, 0x30
        test    rax, rax
        jz      .mmap_ok
        mov     rcx, 0x8000000000000005
        cmp     rax, rcx
        je      .refresh_mmap
        lea     rsi, [msg_mmap_fail]
        call    boot_print
        jmp     .fail_halt

.mmap_ok:
        mov     rdi, [kernel_phys]
        add     rdi, BOOT_INFO_OFF
        mov     rax, [mmap_size]
        mov     [rdi + 48], rax
        mov     rax, [mmap_desc_size]
        mov     [rdi + 56], rax
        mov     eax, [mmap_desc_ver]
        mov     [rdi + 64], rax

        ; No further ConOut calls: firmware may allocate while printing and
        ; invalidate the memory-map key required by ExitBootServices.
        lea     rsi, [msg_mmap_ok]
        call    serial_print
        lea     rsi, [msg_exitbs]
        call    serial_print

; ---------------------------------------------------------- exit services
.exit_retry:
        mov     rcx, r14
        mov     rdx, [mmap_key]
        mov     rax, [boot_services]
        sub     rsp, 0x20
        call    qword [rax + BS_ExitBootServices]
        add     rsp, 0x20
        test    rax, rax
        jz      .exited
        dec     qword [exit_retries]
        jz      .fail_exit
        jmp     .refresh_mmap

.exited:
        cli
        lea     rsi, [msg_jump]
        call    serial_print
        mov     rax, [pt_alloc]
        mov     cr3, rax
        mov     rdi, KERNEL_VMA + BOOT_INFO_OFF
        mov     rsp, KERNEL_STACK_TOP
        xor     ebp, ebp
        mov     rax, KERNEL_VMA
        jmp     rax

.fail_kernel_pages:
        lea     rsi, [msg_kernel_fail]
        call    boot_print
        jmp     .fail_halt
.fail_pt_pages:
        lea     rsi, [msg_pt_fail]
        call    boot_print
        jmp     .fail_halt
.fail_exit:
        lea     rsi, [msg_exit_fail]
        call    serial_print
.fail_halt:
        cli
.halt:
        hlt
        jmp     .halt

; ---------------------------------------------------------------- helpers
; rsi -> ASCIIZ.  Writes to COM1 and (unless headless) UEFI ConOut.
boot_print:
        push    rbx
        push    r12
        mov     r12, rsi
.loop:
        mov     al, [r12]
        test    al, al
        jz      .done
        mov     dx, 0x3f8
        out     dx, al
if HEADLESS = 0
        movzx   eax, al
        mov     [utf16_buf], ax
        mov     word [utf16_buf + 2], 0
        mov     rcx, [conout]
        test    rcx, rcx
        jz      .next
        lea     rdx, [utf16_buf]
        sub     rsp, 0x28
        call    qword [rcx + 8]
        add     rsp, 0x28
end if
.next:
        inc     r12
        jmp     .loop
.done:
        pop     r12
        pop     rbx
        ret

; rsi -> ASCIIZ, COM1 only.
serial_print:
        push    rsi
.loop:
        lodsb
        test    al, al
        jz      .done
        mov     dx, 0x3f8
        out     dx, al
        jmp     .loop
.done:
        pop     rsi
        ret

section '.data' data readable writeable
        align   8
gop_guid        db 0deh,0a9h,42h,90h,0dch,23h,38h,4ah,96h,0fbh,7ah,0deh,0d0h,80h,51h,6ah
acpi_guid       db 71h,0e8h,68h,88h,0f1h,0e4h,0d3h,11h,0bch,22h,00h,80h,0c7h,3ch,88h,81h

system_table    dq 0
image_handle    dq 0
boot_services   dq 0
conout          dq 0
gop_ptr         dq 0
gop_mode        dq 0
fb_base         dq 0
fb_size         dq 0
fb_width        dd 0
fb_height       dd 0
fb_pitch        dd 0
fb_format       dd 0
rsdp            dq 0
kernel_alloc    dq 0
kernel_phys     dq 0
pt_alloc        dq 0
mmap_size       dq 0
mmap_key        dq 0
mmap_desc_size  dq 0
mmap_desc_ver   dd 0
exit_retries    dq 0
utf16_buf       dw 0, 0

msg_banner      db 13,10,'ZhaoxuBootloader (FASM, UEFI x86_64)',13,10,0
msg_probe       db 'Probing UEFI services...',13,10,0
msg_gop         db 'GOP framebuffer acquired.',13,10,0
msg_nogop       db 'No GOP framebuffer; serial console only.',13,10,0
msg_kernel      db 'Loading Yuanshuo kernel...',13,10,0
msg_mmap_fail   db 'GetMemoryMap failed.',13,10,0
msg_mmap_ok     db 'Memory map acquired.',13,10,0
msg_exitbs      db 'Exiting UEFI boot services...',13,10,0
msg_jump        db 'Handing off to Yuanshuo kernel.',13,10,0
msg_kernel_fail db 'AllocatePages for kernel failed.',13,10,0
msg_pt_fail     db 'AllocatePages for page tables failed.',13,10,0
msg_exit_fail   db 'ExitBootServices failed.',13,10,0

; The generated include embeds out/kernel.bin into the data section.
include 'build/kernel_blob.inc'
