/* Zhaoxu OS - newlib C startup.
 *
 * The ZXE loader enters with rsp pointing at the argument block built by
 * build_user_stack(): [argc][argv...][NULL].  Call main(argc, argv, envp)
 * and hand the return value to _exit.
 */
.section .text
.code64

.global _start
.extern main
.extern _exit

_start:
    xorq    %rbp, %rbp
    movq    (%rsp), %rdi           /* argc */
    leaq    8(%rsp), %rsi          /* argv */
    leaq    16(%rsp,%rdi,8), %rdx  /* envp (points at the NULL terminator) */
    andq    $-16, %rsp
    call    main
    movl    %eax, %edi
    call    _exit
1:
    hlt
    jmp     1b
