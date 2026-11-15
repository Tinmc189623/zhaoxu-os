/*
 * Zhaoxu OS - minimal POSIX glob for newlib programs.
 *
 * Supports literal paths plus '*' and '?' in the final component, which is
 * enough for nano's command-line file arguments.  Brace and tilde expansion
 * are deliberately absent and report GLOB_NOMATCH.
 */
#include <dirent.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int match_component(const char *pattern, const char *text)
{
    while (*pattern) {
        if (*pattern == '*') {
            while (*pattern == '*')
                pattern++;
            if (!*pattern)
                return 1;
            while (*text) {
                if (match_component(pattern, text))
                    return 1;
                text++;
            }
            return match_component(pattern, text);
        }
        if (!*text)
            return 0;
        if (*pattern != '?' && *pattern != *text)
            return 0;
        pattern++;
        text++;
    }
    return *text == '\0';
}

int fnmatch(const char *pattern, const char *string, int flags)
{
    (void)flags;
    return match_component(pattern, string) ? 0 : 1;
}

static int add_path(glob_t *result, const char *path)
{
    size_t count = result->gl_pathc;
    char **paths = realloc(result->gl_pathv, (count + 2) * sizeof(char *));
    if (!paths)
        return GLOB_NOSPACE;
    char *copy = malloc(strlen(path) + 1);
    if (!copy)
        return GLOB_NOSPACE;
    strcpy(copy, path);
    paths[count] = copy;
    paths[count + 1] = NULL;
    result->gl_pathv = paths;
    result->gl_pathc = count + 1;
    return 0;
}

int glob(const char *pattern, int flags, int (*errfunc)(const char *, int),
         glob_t *result)
{
    (void)flags;
    (void)errfunc;
    if (!pattern || !result)
        return GLOB_NOMATCH;
    if (!result->gl_pathv) {
        result->gl_pathc = 0;
        result->gl_pathv = NULL;
    }

    const char *slash = strrchr(pattern, '/');
    if (!slash || !strpbrk(pattern, "*?"))
        return add_path(result, pattern) == 0 ? 0 : GLOB_NOSPACE;

    char directory[256];
    size_t length = (size_t)(slash - pattern);
    if (length == 0)
        length = 1;   /* root */
    if (length >= sizeof(directory))
        return GLOB_NOMATCH;
    memcpy(directory, pattern, length);
    directory[length] = '\0';
    const char *base = slash + 1;

    DIR *stream = opendir(directory);
    if (!stream)
        return GLOB_NOMATCH;

    int matched = 0;
    struct dirent *entry;
    while ((entry = readdir(stream)) != NULL) {
        if (entry->d_name[0] == '.' && base[0] != '.')
            continue;
        if (!match_component(base, entry->d_name))
            continue;
        char path[512];
        int written = snprintf(path, sizeof(path), "%s%s%s", directory,
                               directory[length - 1] == '/' ? "" : "/",
                               entry->d_name);
        if (written <= 0 || (size_t)written >= sizeof(path))
            continue;
        if (add_path(result, path) != 0)
            break;
        matched++;
    }
    closedir(stream);
    return matched ? 0 : GLOB_NOMATCH;
}

void globfree(glob_t *result)
{
    if (!result || !result->gl_pathv)
        return;
    for (size_t i = 0; i < result->gl_pathc; i++)
        free(result->gl_pathv[i]);
    free(result->gl_pathv);
    result->gl_pathv = NULL;
    result->gl_pathc = 0;
}
