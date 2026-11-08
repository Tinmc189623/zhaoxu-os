/* ZhaoxuBootloader - embeds the flat Yuanshuo kernel image (GAS) */
    .section .rdata,"dr"
    .globl kernel_blob
    .globl kernel_blob_end
    .balign 16
kernel_blob:
    .incbin "out/kernel.bin"
kernel_blob_end:
