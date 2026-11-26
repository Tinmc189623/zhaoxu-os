/*
 * Zhaoxu OS - variadic exec wrappers and PATH lookup.
 *
 * execve itself lives in libgloss; these add the argument-list forms and
 * the PATH search that execvp promises.
 */
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EXEC_ARG_MAX 32

int execl(const char *path, const char *argument, ...)
{
    char *arguments[EXEC_ARG_MAX];
    int count = 0;
    arguments[count++] = (char *)argument;
    va_list list;
    va_start(list, argument);
    while (count < EXEC_ARG_MAX) {
        char *next = va_arg(list, char *);
        if (!next) {
            arguments[count] = NULL;
            break;
        }
        arguments[count++] = next;
    }
    va_end(list);
    return execve(path, arguments, environ);
}

int execv(const char *path, char *const arguments[])
{
    return execve(path, arguments, environ);
}

/* Search PATH for an executable named by argv[0]. */
int execvp(const char *file, char *const arguments[])
{
    if (!file || !*file) {
        errno = ENOENT;
        return -1;
    }
    if (strchr(file, '/'))
        return execve(file, arguments, environ);

    const char *path = getenv("PATH");
    if (!path || !*path)
        path = "/bin:/usr/bin";
    char candidate[256];
    const char *segment = path;
    while (segment && *segment) {
        const char *end = strchr(segment, ':');
        size_t length = end ? (size_t)(end - segment) : strlen(segment);
        if (length == 0) {
            /* An empty element means the current directory. */
            snprintf(candidate, sizeof(candidate), "%s", file);
        } else if (length + 1 + strlen(file) < sizeof(candidate)) {
            memcpy(candidate, segment, length);
            candidate[length] = '/';
            snprintf(candidate + length + 1, sizeof(candidate) - length - 1,
                     "%s", file);
        } else {
            segment = end ? end + 1 : NULL;
            continue;
        }
        /* execve keeps errno from the last failed candidate. */
        execve(candidate, arguments, environ);
        segment = end ? end + 1 : NULL;
    }
    errno = ENOENT;
    return -1;
}
