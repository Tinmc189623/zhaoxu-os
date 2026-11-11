; ZhaoxuBootloader - legacy BIOS stage 2
;
; Loaded at 0x8000 by stage 1.  Runs in real mode, collects VBE framebuffer
; information, an E820 memory map and the ACPI RSDP, loads the flat kernel
; image (addresses passed in a patched parameter block), copies it to its
; physical window, builds identity + higher-half page tables and enters long
; mode with the same boot_info contract as the UEFI layer.

org 0x8000
use16

; HEADLESS=1 tells the kernel there is no framebuffer, so a serial-only guest
; (headless install regression) boots the Ring3 csh instead of ADE.  The VBE
; probe itself still runs: on q35/SeaBIOS an SMI that arrives during the first
; CD read hangs the firmware's own SMM handler unless the VGA was initialised,
; and that read is what boots the kernel.  The Makefile passes
; -dHEADLESS=$(HEADLESS); a plain `fasm bios_stage2.asm` knows no HEADLESS
; symbol and assembles the normal (framebuffer) path - fasm treats an
; undefined symbol in `defined X & X` as zero and does not evaluate the rest.

KERNEL_PHYS      = 0x02000000           ; 2 MiB aligned kernel window
KERNEL_VMA       = 0xffffffff80000000
KERNEL_MAP_SIZE  = 0x01000000
BOOT_INFO_OFF    = 0x00e00000
MMAP_OFF         = 0x00c00000
BOOT_MAGIC       = 0x5a4f5331
PT_BASE          = 0x00100000
E820_BUF         = 0x5000
E820_MAX         = 64
VBE_INFO         = 0x5c00
VBE_MODEINFO     = 0x5e00
MMAP_DESC_SIZE   = 48

stage2_start:
        cli
        xor     ax, ax
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     sp, 0x7C00
        sti
        cld
        mov     [boot_drive], dl

        call    serial_init
        mov     si, msg_banner
        call    print

        call    enable_a20
        mov     word [0x3000], 0x1234
        mov     word [0x103000], 0xABCD
        cmp     word [0x3000], 0x1234
        je      .a20_on
        mov     si, msg_a20_off
        call    print
        jmp     halt
.a20_on:
        mov     si, msg_a20
        call    print

if defined HEADLESS & HEADLESS
        ; q35 + SeaBIOS hangs inside its own SMM handler when the first CD
        ; read happens without the VGA ever having been initialised (the
        ; frame buffer probe is what does it), so the probe still runs here.
        ; The kernel is told otherwise: dropping the frame buffer sends it to
        ; the serial csh instead of ADE.
        call    vbe_probe
        xor     eax, eax
        mov     dword [fb_base], eax
        mov     dword [fb_size], eax
        mov     dword [fb_width], eax
        mov     dword [fb_height], eax
        mov     dword [fb_pitch], eax
        mov     byte [fb_format], al
        mov     si, msg_headless
        call    print
else
        call    vbe_probe
        cmp     byte [fb_format], 0
        je      .no_vbe
        mov     si, msg_vbe
        call    print
        jmp     .vbe_done
.no_vbe:
        mov     si, msg_novbe
        call    print
.vbe_done:
end if

        call    collect_e820
        mov     si, msg_e820
        call    print

        call    find_rsdp
        test    eax, eax
        jz      .no_rsdp
        mov     [rsdp], eax
        mov     si, msg_rsdp
        call    print
        jmp     .rsdp_done
.no_rsdp:
        mov     si, msg_norsdp
        call    print
.rsdp_done:

        mov     eax, dword [param_magic]
        cmp     eax, 0x4F4F425A          ; 'ZBOO'
        jne     .bad_params
        mov     eax, dword [param_magic + 4]
        cmp     eax, 0x4D525054          ; 'TPRM'
        jne     .bad_params
        cmp     dword [param_kernel_lba], 0
        je      .bad_params
        cmp     dword [param_kernel_bytes], 0
        je      .bad_params

        call    probe_sector_size
        jc      .sector_fail
        call    load_kernel
        jc      .disk_fail

        mov     si, msg_kernel
        call    print
        jmp     pm_enter

.bad_params:
        mov     si, msg_params
        call    print
        jmp     halt
.sector_fail:
        mov     si, msg_sector
        call    print
        jmp     halt
.disk_fail:
        mov     si, msg_disk
        call    print
halt:
        cli
.halt_loop:
        hlt
        jmp     .halt_loop

; ------------------------------------------------------------------ helpers
enable_a20:
        in      al, 0x92
        or      al, 2
        and     al, 0xFE
        out     0x92, al
        ret

serial_init:
        mov     dx, 0x3F9
        xor     al, al
        out     dx, al
        mov     dx, 0x3FB
        mov     al, 0x80
        out     dx, al
        mov     dx, 0x3F8
        mov     al, 1
        out     dx, al
        mov     dx, 0x3F9
        xor     al, al
        out     dx, al
        mov     dx, 0x3FB
        mov     al, 3
        out     dx, al
        mov     dx, 0x3FA
        mov     al, 0xC7
        out     dx, al
        mov     dx, 0x3FC
        mov     al, 3
        out     dx, al
        ret

; SI -> ASCIIZ, BIOS console + COM1.
print:
        push    ax
        push    bx
        push    si
        push    dx
.loop:
        lodsb
        test    al, al
        jz      .done
        push    si
        push    ax
        mov     ah, 0x0E
        mov     bx, 7
        int     0x10
        pop     ax
        pop     si
        mov     dx, 0x3F8
        out     dx, al
        jmp     .loop
.done:
        pop     dx
        pop     si
        pop     bx
        pop     ax
        ret

; The device sector size decides the unit of param_*_lba.  A no-emulation
; El Torito CD reports 2048-byte sectors; a disk reports 512.  Only those two
; are representable: shift 9 or 11.  Anything else (or a failed AH=48h) is an
; error rather than a silent guess, since the wrong unit would read garbage.
probe_sector_size:
        push    ax
        push    dx
        push    si
        mov     word [dap_geom], 0x1E   ; returned length 30 (EDD 3.0 table)
        mov     si, dap_geom
        mov     ah, 0x48
        mov     dl, [boot_drive]
        int     0x13
        jc      .fail
        mov     ax, word [dap_geom + 0x18]   ; bytes per logical sector
        mov     byte [disk_shift], 9
        cmp     ax, 512
        je      .ok
        mov     byte [disk_shift], 11
        cmp     ax, 2048
        je      .ok
.fail:
        pop     si
        pop     dx
        pop     ax
        stc
        ret
.ok:
        pop     si
        pop     dx
        pop     ax
        clc
        ret

; ES:BX <- CX device sectors from 64-bit LBA in EDX:EAX (INT 13h extensions).
; The kernel lives on ext4 past the first CHS-addressable region, so reads
; must use EDD LBA48 rather than 16-bit CHS.  Every chunk stops at the next
; 64 KiB linear boundary because the destination is a real-mode ES:BX pair.
; When param_cd is set the LBA is expressed in 2048-byte units (ISO9660).
read_lba:
        pusha
        push    es
        mov     dword [io_lba64], eax
        mov     dword [io_lba64+4], edx
        mov     word [io_rem], cx
        cmp     dword [param_cd], 0
        je      .no_cd
        mov     cl, 11
        sub     cl, byte [disk_shift]   ; 2048 >> shift
        jz      .no_cd
        mov     eax, dword [io_lba64]
        mov     edx, dword [io_lba64+4]
        shld    edx, eax, cl
        shl     eax, cl
        mov     dword [io_lba64], eax
        mov     dword [io_lba64+4], edx
.no_cd:
        mov     [io_buf], bx
        mov     [io_seg], es
.chunk:
        mov     ax, [io_rem]
        test    ax, ax
        jz      .ok
        ; max sectors that fit before the next 64 KiB boundary
        push    ax
        mov     ax, [io_seg]
        shl     ax, 4
        add     ax, [io_buf]
        neg     ax                      ; bytes left in this 64 KiB page
        mov     cl, byte [disk_shift]
        shr     ax, cl
        test    ax, ax
        jnz     .cap
        mov     ax, 1
        mov     cl, 16
        sub     cl, byte [disk_shift]
        shl     ax, cl                  ; 0x10000 >> shift
.cap:
        cmp     ax, 0x7F                ; EDD transfers are 7-bit (128 max)
        jbe     .cap_ok
        mov     ax, 0x7F
.cap_ok:
        mov     cx, ax
        pop     ax
        cmp     ax, cx
        jbe     .size_ok
        mov     ax, cx
.size_ok:
        mov     word [io_chunk], ax
        mov     byte [disk_dap], 0x10
        mov     byte [disk_dap+1], 0
        mov     word [disk_dap+2], ax
        mov     ax, word [io_buf]
        mov     word [disk_dap+4], ax
        mov     ax, [io_seg]
        mov     word [disk_dap+6], ax
        mov     eax, dword [io_lba64]
        mov     dword [disk_dap+8], eax
        mov     eax, dword [io_lba64+4]
        mov     dword [disk_dap+12], eax
        mov     si, disk_dap
        mov     dl, byte [boot_drive]
        mov     ah, 0x42
        int     0x13
        jc      .error
        movzx   eax, word [io_chunk]
        add     dword [io_lba64], eax
        adc     dword [io_lba64+4], 0
        mov     ax, [io_chunk]
        mov     cl, byte [disk_shift]
        sub     cl, 4                   ; sectors * (1 << shift) / 16
        shl     ax, cl
        add     word [io_seg], ax
        mov     ax, [io_chunk]
        sub     word [io_rem], ax
        jmp     .chunk
.ok:
        pop     es
        popa
        clc
        ret
.error:
        pop     es
        popa
        stc
        ret

; ------------------------------------------------------------- kernel load
; Real mode cannot address memory above 1 MiB, so the whole kernel is read
; once into the low buffer at 0x10000 with EDD reads (ES advances) and copied
; to KERNEL_PHYS later from flat protected mode.  The buffer must stay below
; the EBDA, hence the 448 KiB size cap.  The sector count is derived from the
; byte size because it depends on the device sector size.
MAX_KERNEL_BYTES = 0x70000
load_kernel:
        mov     si, msg_load
        call    print
        cmp     dword [param_kernel_bytes + 4], 0
        jne     .error
        mov     eax, dword [param_kernel_bytes]
        cmp     eax, MAX_KERNEL_BYTES
        ja      .error
        mov     cl, byte [disk_shift]
        mov     edx, 1
        shl     edx, cl                 ; device sector size
        dec     edx
        add     eax, edx
        shr     eax, cl                 ; device sectors, rounded up
        mov     cx, ax
        mov     ax, 0x1000
        mov     es, ax
        xor     bx, bx
        mov     eax, dword [param_kernel_lba]
        mov     edx, dword [param_kernel_lba+4]
        call    read_lba
        jc      .error
        mov     si, msg_loaded
        call    print
        clc
        ret
.error:
        stc
        ret

; ---------------------------------------------------------------- VBE probe
vbe_probe:
        mov     word [best_mode], 0xFFFF
        mov     dword [best_area], 0
        mov     byte [fb_format], 0
        mov     dword [fb_base], 0

        mov     dword [VBE_INFO], 'VBE2'
        mov     ax, 0x4F00
        mov     di, VBE_INFO
        int     0x10
        cmp     ax, 0x004F
        jne     .none

        mov     si, [VBE_INFO + 0x0E]
        mov     ax, [VBE_INFO + 0x10]
        mov     [list_seg], ax
.next_mode:
        mov     es, [list_seg]
        mov     cx, [es:si]
        add     si, 2
        cmp     cx, 0xFFFF
        je      .pick
        mov     [cur_mode], cx
        mov     [list_si], si
        mov     ax, es
        mov     [list_seg], ax

        xor     ax, ax
        mov     es, ax
        mov     di, VBE_MODEINFO
        mov     cx, [cur_mode]
        mov     ax, 0x4F01
        int     0x10
        mov     si, [list_si]
        mov     es, [list_seg]
        cmp     ax, 0x004F
        jne     .next_mode

        mov     ax, [VBE_MODEINFO]
        and     ax, 0x81
        cmp     ax, 0x81                ; supported + LFB
        jne     .next_mode
        cmp     byte [VBE_MODEINFO + 0x19], 32
        jne     .next_mode
        cmp     byte [VBE_MODEINFO + 0x1B], 6
        jne     .next_mode
        mov     ax, [VBE_MODEINFO + 0x12]
        test    ax, ax
        jz      .next_mode
        mov     bx, [VBE_MODEINFO + 0x14]
        test    bx, bx
        jz      .next_mode

        cmp     ax, 1920
        jne     .try_1280
        cmp     bx, 1080
        jne     .try_1280
        call    adopt_mode
        jmp     .pick

.try_1280:
        cmp     ax, 1280
        jne     .score
        cmp     bx, 800
        jne     .score
        call    adopt_mode
        jmp     .pick

.score:
        cmp     ax, 1024
        jb      .next_mode
        cmp     bx, 768
        jb      .next_mode
        movzx   eax, ax
        movzx   ebx, bx
        imul    eax, ebx
        cmp     eax, [best_area]
        jbe     .next_mode
        call    adopt_mode
        jmp     .next_mode

.pick:
        cmp     word [best_mode], 0xFFFF
        je      .none
        mov     ax, 0x4F02
        mov     bx, [best_mode]
        or      bx, 0x4000
        int     0x10
        cmp     ax, 0x004F
        jne     .none

        mov     eax, [best_fb]
        test    eax, eax
        jz      .none
        mov     [fb_base], eax
        mov     ax, [best_w]
        movzx   eax, ax
        mov     [fb_width], eax
        mov     ax, [best_h]
        movzx   eax, ax
        mov     [fb_height], eax
        mov     ax, [best_pitch]
        movzx   eax, ax
        mov     [fb_pitch], eax
        mov     eax, [best_fb]
        mov     [fb_base], eax
        mov     eax, [fb_pitch]
        imul    eax, [fb_height]
        mov     [fb_size], eax
        mov     byte [fb_format], 1
.none:
        ret

adopt_mode:
        mov     ax, [cur_mode]
        mov     [best_mode], ax
        movzx   eax, word [VBE_MODEINFO + 0x12]
        movzx   ebx, word [VBE_MODEINFO + 0x14]
        mov     [best_w], ax
        mov     [best_h], bx
        imul    eax, ebx
        mov     [best_area], eax
        mov     ax, [VBE_MODEINFO + 0x10]
        mov     [best_pitch], ax
        mov     eax, [VBE_MODEINFO + 0x28]
        mov     [best_fb], eax
        ret

; ---------------------------------------------------------------- E820 map
collect_e820:
        xor     ax, ax
        mov     es, ax
        mov     word [e820_count], 0
        xor     ebx, ebx
.loop:
        movzx   eax, word [e820_count]
        mov     cx, 24
        mul     cx
        add     ax, E820_BUF
        mov     di, ax
        mov     eax, 0xE820
        mov     edx, 0x534D4150
        mov     ecx, 24
        int     0x15
        jc      .done
        cmp     eax, 0x534D4150
        jne     .done
        inc     word [e820_count]
        cmp     word [e820_count], E820_MAX
        jae     .done
        test    ebx, ebx
        jnz     .loop
.done:
        ret

; ------------------------------------------------------------- RSDP search
; Firmware may place the RSDP anywhere in the first MiB (SeaBIOS keeps it
; in a low malloc zone), so walk 16 x 64 KiB segments instead of only the
; EBDA and the 0xE0000..0xFFFFF window (SI wraps at 64 KiB).
find_rsdp:
        push    bx
        push    dx
        xor     bx, bx
        mov     dx, 16
.seg_loop:
        mov     es, bx
        xor     si, si
        mov     cx, 0x1000
        push    bx
        call    scan_area
        pop     bx
        test    eax, eax
        jnz     .done
        add     bx, 0x1000
        dec     dx
        jnz     .seg_loop
        xor     eax, eax
.done:
        pop     dx
        pop     bx
        ret

; ES:SI start, CX slots of 16 bytes.  Returns EAX = physical RSDP or 0.
scan_area:
        push    bx
        push    si
.loop:
        mov     eax, [es:si]
        cmp     eax, 0x20445352         ; 'RSD '
        jne     .next
        mov     eax, [es:si + 4]
        cmp     eax, 0x20525450         ; 'PTR '
        jne     .next
        xor     eax, eax
        mov     ax, es
        shl     eax, 4
        movzx   ebx, si
        add     eax, ebx
        jmp     .found
.next:
        add     si, 16
        dec     cx
        jnz     .loop
        xor     eax, eax
.found:
        pop     si
        pop     bx
        ret

; ---------------------------------------------------------- protected mode
pm_enter:
        cli
        lgdt    [gdtr]
        mov     eax, cr0
        or      al, 1
        mov     cr0, eax
        db      0EAh
        dw      pm16_entry
        dw      0x0008

use16
pm16_entry:
        mov     ax, 0x20
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     esp, 0x4F00
        db      66h, 0EAh
        dd      pm32_entry
        dw      0x0010

use32
pm32_entry:
        ; ---- copy the kernel from the low buffer into its physical window
        ; (zero the whole window first: the kernel .bss is not cleared)
        mov     edi, KERNEL_PHYS
        xor     eax, eax
        mov     ecx, KERNEL_MAP_SIZE / 4
        rep     stosd
        mov     esi, 0x10000
        mov     edi, KERNEL_PHYS
        mov     ecx, dword [param_kernel_bytes]
        add     ecx, 3
        shr     ecx, 2
        rep     movsd

        ; ---- zero page tables
        mov     edi, PT_BASE
        xor     eax, eax
        mov     ecx, 8192
        rep     stosd

        ; ---- page tables: PML4[0] and PML4[511]
        mov     dword [PT_BASE], 0x00101003
        mov     dword [PT_BASE + 511 * 8], 0x00106003

        ; low PDPT -> four page directories
        mov     edi, 0x00101000
        mov     eax, 0x00102003
        mov     ecx, 4
.pdpt_low:
        mov     [edi], eax
        add     eax, 0x1000
        add     edi, 8
        dec     ecx
        jnz     .pdpt_low

        ; identity map the first 4 GiB with 2 MiB pages
        mov     edi, 0x00102000
        mov     eax, 0x83
        mov     ecx, 2048
.pd_fill:
        mov     [edi], eax
        add     eax, 0x200000
        add     edi, 8
        dec     ecx
        jnz     .pd_fill

        ; high PDPT[510] -> kernel page directory
        mov     dword [0x00106000 + 510 * 8], 0x00107003

        ; map the 16 MiB kernel window
        mov     edi, 0x00107000
        mov     eax, KERNEL_PHYS or 0x83
        mov     ecx, KERNEL_MAP_SIZE / 0x200000
.kmap:
        mov     [edi], eax
        add     eax, 0x200000
        add     edi, 8
        dec     ecx
        jnz     .kmap

        ; ---- E820 -> UEFI descriptors (48-byte stride)
        mov     esi, E820_BUF
        mov     edi, KERNEL_PHYS + MMAP_OFF
        movzx   ecx, word [e820_count]
        mov     eax, ecx
        imul    eax, MMAP_DESC_SIZE
        mov     [mmap_bytes], eax
        test    ecx, ecx
        jz      .mmap_done
.mmap_loop:
        mov     eax, [esi + 16]
        cmp     eax, 1
        jne     .mmap_other
        mov     eax, 7
        jmp     .mmap_type
.mmap_other:
        mov     eax, 2
.mmap_type:
        mov     [edi], eax
        mov     dword [edi + 4], 0
        mov     eax, [esi]
        mov     [edi + 8], eax
        mov     eax, [esi + 4]
        mov     [edi + 12], eax
        mov     dword [edi + 16], 0
        mov     dword [edi + 20], 0
        mov     eax, [esi + 8]
        mov     ebx, [esi + 12]
        mov     edx, eax
        shr     edx, 12
        mov     eax, ebx
        shl     eax, 20
        or      edx, eax
        mov     [edi + 24], edx
        shr     ebx, 12
        mov     [edi + 28], ebx
        mov     dword [edi + 32], 0
        mov     dword [edi + 36], 0
        mov     dword [edi + 40], 0
        mov     dword [edi + 44], 0
        add     esi, 24
        add     edi, MMAP_DESC_SIZE
        dec     ecx
        jnz     .mmap_loop
.mmap_done:

        ; ---- boot_info
        mov     edi, KERNEL_PHYS + BOOT_INFO_OFF
        mov     dword [edi + 0], BOOT_MAGIC
        mov     dword [edi + 4], 0
        mov     eax, [fb_base]
        mov     [edi + 8], eax
        mov     dword [edi + 12], 0
        mov     eax, [fb_size]
        mov     [edi + 16], eax
        mov     dword [edi + 20], 0
        mov     eax, [fb_width]
        mov     [edi + 24], eax
        mov     eax, [fb_height]
        mov     [edi + 28], eax
        mov     eax, [fb_pitch]
        mov     [edi + 32], eax
        mov     eax, [fb_format]
        mov     [edi + 36], eax
        mov     dword [edi + 40], 0x80C00000
        mov     dword [edi + 44], 0xFFFFFFFF
        mov     eax, [mmap_bytes]
        mov     [edi + 48], eax
        mov     dword [edi + 52], 0
        mov     dword [edi + 56], MMAP_DESC_SIZE
        mov     dword [edi + 60], 0
        mov     dword [edi + 64], 1
        mov     dword [edi + 68], 0
        mov     eax, [rsdp]
        mov     [edi + 72], eax
        mov     dword [edi + 76], 0
        mov     dword [edi + 80], KERNEL_PHYS
        mov     dword [edi + 84], 0
        mov     dword [edi + 88], 0x80000000
        mov     dword [edi + 92], 0xFFFFFFFF
        mov     eax, dword [param_kernel_bytes]
        mov     [edi + 96], eax
        mov     dword [edi + 100], 0
        mov     dword [edi + 104], KERNEL_MAP_SIZE
        mov     dword [edi + 108], 0
        mov     dword [edi + 112], PT_BASE
        mov     dword [edi + 116], 0
        mov     dword [edi + 120], 1
        mov     dword [edi + 124], 0

        mov     esi, msg_pm
        call    serial_print32

        ; ---- enter long mode
        mov     eax, cr4
        or      eax, 0x20               ; PAE
        mov     cr4, eax
        mov     eax, PT_BASE
        mov     cr3, eax
        mov     ecx, 0xC0000080
        rdmsr
        or      eax, 0x100              ; LME
        wrmsr
        mov     eax, cr0
        or      eax, 0x80000001         ; PG | PE
        mov     cr0, eax
        db      0EAh
        dd      long_entry
        dw      0x0018

serial_print32:
        push    eax
        push    edx
.loop:
        mov     al, [esi]
        test    al, al
        jz      .done
        mov     dx, 0x3F8
        out     dx, al
        inc     esi
        jmp     .loop
.done:
        pop     edx
        pop     eax
        ret

use64
long_entry:
        mov     ax, 0x20
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     rsp, KERNEL_VMA + 0xF00000
        xor     ebp, ebp
        mov     rdi, KERNEL_VMA + BOOT_INFO_OFF
        mov     rax, KERNEL_VMA
        jmp     rax

; ------------------------------------------------------------------- data
use16
boot_drive      db 0
disk_shift      db 9
io_buf          dw 0
io_seg          dw 0
io_rem          dw 0
io_lba64        dq 0
disk_dap        db 16 dup 0    ; INT 13h AH=42h disk address packet
dap_geom        db 30 dup 0    ; INT 13h AH=48h drive parameter table
io_chunk        dw 0

list_seg        dw 0
list_si         dw 0
cur_mode        dw 0
best_mode       dw 0xFFFF
best_area       dd 0
best_w          dw 0
best_h          dw 0
best_pitch      dw 0
best_fb         dd 0
e820_count      dw 0
rsdp            dd 0

fb_base         dd 0
fb_size         dd 0
fb_width        dd 0
fb_height       dd 0
fb_pitch        dd 0
fb_format       dd 0
mmap_bytes      dd 0

align 8
param_magic     dq 0x4D5250544F4F425A   ; 'ZBOOTPRM', patched by the image builder
param_kernel_lba dq 0
param_kernel_bytes dq 0
param_kernel_sectors dq 0               ; informational (installer only)
param_cd        dq 0                    ; 1 = LBAs are ISO9660 2 KiB units
param_font_lba  dq 0
param_font_bytes dq 0

align 8
gdt:
        dq      0
        dq      0x008F9A000000FFFF     ; 0x08 code16
        dq      0x00CF9A000000FFFF     ; 0x10 code32
        dq      0x00AF9A000000FFFF     ; 0x18 code64
        dq      0x00CF92000000FFFF     ; 0x20 data32
gdt_end:
gdtr:
        dw      gdt_end - gdt - 1
        dd      gdt

msg_banner      db 13,10,'ZhaoxuBootloader stage2 (legacy BIOS)',13,10,0
msg_a20         db 'A20 gate enabled.',13,10,0
msg_a20_off     db 'stage2: A20 gate still disabled.',13,10,0
msg_vbe         db 'VESA framebuffer selected.',13,10,0
msg_novbe       db 'No 32-bit VESA mode, serial console only.',13,10,0
msg_headless    db 'Headless build: no framebuffer, kernel uses the serial console.',13,10,0
msg_e820        db 'E820 memory map collected.',13,10,0
msg_rsdp        db 'ACPI RSDP located.',13,10,0
msg_norsdp      db 'ACPI RSDP not found.',13,10,0
msg_kernel      db 'Kernel image loaded, entering long mode.',13,10,0
msg_load        db 'Reading kernel image.',13,10,0
msg_loaded      db 'Kernel image read.',13,10,0
msg_params      db 'stage2: parameter block missing.',13,10,0
msg_sector      db 'stage2: unsupported sector size.',13,10,0
msg_disk        db 'stage2: disk read failed.',13,10,0
msg_pm          db 'ZhaoxuBootloader handing off to Yuanshuo kernel.',13,10,0

assert ($ - $$) <= 0x8000
