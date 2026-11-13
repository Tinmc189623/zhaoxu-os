/*
 * ZXE dynamic-link fixture: imports ztest_value from libztest.zxe and exits
 * with 0 only when the loader resolved the import and the call returned 42.
 */
#include "userlib.h"

__asm__(
    ".section .zxe.deps,\"\",@progbits\n"
    ".ascii \"[{\\\"name\\\":\\\"libztest.zxe\\\",\\\"abi_major\\\":1}]\"\n"
    ".previous\n");

__asm__(
    ".section .zxe.imports,\"\",@progbits\n"
    ".ascii \"[{\\\"dependency_index\\\":0,\\\"name\\\":\\\"ztest_value\\\","
    "\\\"abi_major\\\":1}]\"\n"
    ".previous\n");

extern int ztest_value(void);

int user_main(long *stack)
{
    (void)stack;
    int value = ztest_value();
    u_puts("[ztest] imported ztest_value() = ");
    u_putn((long)value);
    u_puts("\n");
    return value == 42 ? 0 : 1;
}
