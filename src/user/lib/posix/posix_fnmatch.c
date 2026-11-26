/*
 * Zhaoxu OS - fnmatch.
 *
 * newlib does not ship a pattern matcher; this implements the POSIX rules
 * for * ? [...] and backslash escapes, including FNM_PATHNAME and
 * FNM_PERIOD handling.
 */
#include <ctype.h>
#include <fnmatch.h>
#include <stddef.h>
#include <string.h>

/* Case-insensitive matching is a BSD extension some libcs omit. */
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0x10
#endif

/* Compare one character under FNM_CASEFOLD. */
static int same(int a, int b, int flags)
{
    if (flags & FNM_CASEFOLD)
        return tolower(a) == tolower(b);
    return a == b;
}

static int match_here(const char *pattern, const char *text, int flags,
                      int at_start);

/* Match one bracket expression starting after '['. */
static int match_bracket(const char **pattern, int character, int flags)
{
    const char *cursor = *pattern;
    int negate = 0;
    if (*cursor == '!' || *cursor == '^') {
        negate = 1;
        cursor++;
    }
    int matched = 0;
    int first = 1;
    while (*cursor && (*cursor != ']' || first)) {
        first = 0;
        int low = (unsigned char)*cursor++;
        if (low == '\\' && !(flags & FNM_NOESCAPE) && *cursor)
            low = (unsigned char)*cursor++;
        int high = low;
        if (*cursor == '-' && cursor[1] && cursor[1] != ']') {
            cursor++;
            high = (unsigned char)*cursor++;
            if (high == '\\' && !(flags & FNM_NOESCAPE) && *cursor)
                high = (unsigned char)*cursor++;
        }
        int candidate = (unsigned char)character;
        if (flags & FNM_CASEFOLD) {
            int low_folded = tolower(low);
            int high_folded = tolower(high);
            int value = tolower(candidate);
            if (value >= low_folded && value <= high_folded)
                matched = 1;
        } else if (candidate >= low && candidate <= high) {
            matched = 1;
        }
    }
    if (*cursor == ']')
        cursor++;
    *pattern = cursor;
    return matched != negate;
}

/* Match '*' by trying every split point. */
static int match_star(const char *pattern, const char *text, int flags,
                      int at_start)
{
    for (;;) {
        if (match_here(pattern, text, flags, at_start) == 0)
            return 0;
        if (!*text)
            return FNM_NOMATCH;
        if ((flags & FNM_PATHNAME) && *text == '/')
            return FNM_NOMATCH;
        text++;
        at_start = 0;
    }
}

/* Compare the pattern from its first byte. */
static int match_here(const char *pattern, const char *text, int flags,
                      int at_start)
{
    while (*pattern) {
        int character = (unsigned char)*pattern;
        if (character == '*') {
            while (*pattern == '*')
                pattern++;
            return match_star(pattern, text, flags, at_start);
        }
        if (!*text)
            return FNM_NOMATCH;
        if ((flags & FNM_PERIOD) && at_start && *text == '.' &&
            character != '.' && character != '?') {
            return FNM_NOMATCH;
        }
        if (character == '?') {
            if ((flags & FNM_PATHNAME) && *text == '/')
                return FNM_NOMATCH;
            pattern++;
            text++;
        } else if (character == '[') {
            pattern++;
            if ((flags & FNM_PATHNAME) && *text == '/')
                return FNM_NOMATCH;
            if (!match_bracket(&pattern, (unsigned char)*text, flags))
                return FNM_NOMATCH;
            text++;
        } else if (character == '\\' && !(flags & FNM_NOESCAPE)) {
            pattern++;
            if (!*pattern)
                return FNM_NOMATCH;
            if (!same((unsigned char)*pattern, (unsigned char)*text, flags))
                return FNM_NOMATCH;
            pattern++;
            text++;
        } else {
            if (!same(character, (unsigned char)*text, flags))
                return FNM_NOMATCH;
            pattern++;
            text++;
        }
        at_start = (flags & FNM_PATHNAME) && text[-1] == '/';
    }
    return *text ? FNM_NOMATCH : 0;
}

int fnmatch(const char *pattern, const char *text, int flags)
{
    if (!pattern || !text)
        return FNM_NOMATCH;
    return match_here(pattern, text, flags, 1);
}
