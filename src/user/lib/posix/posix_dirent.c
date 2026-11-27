/* Zhaoxu OS - newlib directory streams.
 *
 * The kernel readdir syscall returns "d name\n" and "f name size\n" lines;
 * this file turns that text into a POSIX DIR/readdir stream. */
#include "posix_glue.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct zos_dir_stream {
    char *buffer;
    size_t length;
    size_t offset;
    struct dirent entry;
};

/* Open a directory and snapshot its listing. */
DIR *opendir(const char *path)
{
    char *buffer = malloc(65536);
    if (!buffer)
        return NULL;
    long length = zos_syscall3(ZOS_READDIR, (long)(uintptr_t)path,
                               (long)(uintptr_t)buffer, 65536);
    if (length <= 0) {
        free(buffer);
        errno = ENOENT;
        return NULL;
    }
    struct zos_dir_stream *stream = malloc(sizeof(*stream));
    if (!stream) {
        free(buffer);
        return NULL;
    }
    stream->buffer = buffer;
    stream->length = (size_t)length;
    stream->offset = 0;
    return (DIR *)stream;
}

/* Return the next name from the snapshot. */
struct dirent *readdir(DIR *directory)
{
    struct zos_dir_stream *stream = (struct zos_dir_stream *)directory;
    if (!stream || stream->offset >= stream->length)
        return NULL;

    size_t start = stream->offset;
    size_t end = start;
    while (end < stream->length && stream->buffer[end] != '\n')
        end++;
    size_t next = end < stream->length ? end + 1 : end;

    char type = stream->buffer[start];
    size_t name_start = start + 2;
    if (name_start > end)
        name_start = end;
    size_t name_length = 0;
    while (name_start + name_length < end &&
           stream->buffer[name_start + name_length] != ' ')
        name_length++;
    if (name_length >= sizeof(stream->entry.d_name))
        name_length = sizeof(stream->entry.d_name) - 1;

    for (size_t i = 0; i < sizeof(stream->entry); i++)
        ((unsigned char *)&stream->entry)[i] = 0;
    for (size_t i = 0; i < name_length; i++)
        stream->entry.d_name[i] = stream->buffer[name_start + i];
    stream->entry.d_name[name_length] = '\0';
    stream->entry.d_type = type == 'd' ? DT_DIR : DT_REG;
    stream->offset = next;
    return &stream->entry;
}

/* Restart the snapshot from the beginning (the listing is a static buffer). */
void rewinddir(DIR *directory)
{
    struct zos_dir_stream *stream = (struct zos_dir_stream *)directory;
    if (stream)
        stream->offset = 0;
}

/* Release the snapshot. */
int closedir(DIR *directory)
{
    struct zos_dir_stream *stream = (struct zos_dir_stream *)directory;
    if (!stream)
        return -1;
    free(stream->buffer);
    free(stream);
    return 0;
}
