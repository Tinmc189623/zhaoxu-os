/*
 * Zhaoxu OS - getline/getdelim over newlib's buffered stdio.
 *
 * newlib does not carry these two POSIX functions; the growth policy here
 * matches the standard one: the buffer doubles until the delimiter or EOF.
 */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

/* Read one delimited line, growing *line as needed. */
ssize_t getdelim(char **line, size_t *capacity, int delimiter, FILE *file)
{
    if (!line || !capacity || !file) {
        errno = EINVAL;
        return -1;
    }
    if (!*line || *capacity == 0) {
        *capacity = 128;
        *line = malloc(*capacity);
        if (!*line)
            return -1;
    }
    size_t used = 0;
    for (;;) {
        int character = getc(file);
        if (character == EOF) {
            if (used == 0)
                return -1;
            break;
        }
        if (used + 1 >= *capacity) {
            size_t grown = *capacity * 2;
            char *bigger = realloc(*line, grown);
            if (!bigger) {
                errno = ENOMEM;
                return -1;
            }
            *line = bigger;
            *capacity = grown;
        }
        (*line)[used++] = (char)character;
        if (character == delimiter)
            break;
    }
    (*line)[used] = '\0';
    return (ssize_t)used;
}

/* Read one newline-terminated line. */
ssize_t getline(char **line, size_t *capacity, FILE *file)
{
    return getdelim(line, capacity, '\n', file);
}
