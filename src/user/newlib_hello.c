/*
 * Zhaoxu OS - newlib smoke test.
 *
 * Uses printf/malloc/strcpy from newlib, linked with libgloss and crt0.
 * Exit status 3 and the printf line are the regression markers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char *buffer = malloc(64);
    if (!buffer)
        return 4;
    strcpy(buffer, argc > 0 && argv[0] ? argv[0] : "newlib");
    printf("newlib hello: argc=%d argv0=%s heap=ok\n", argc, buffer);
    free(buffer);
    return 3;
}
