#ifndef ZHAOXU_FREETYPE_STDIO_H
#define ZHAOXU_FREETYPE_STDIO_H
/* FreeType's upstream ftstdlib.h includes stdio.h even when stream support
 * is disabled.  This freestanding declaration keeps the third-party source
 * unchanged and makes that dead file API harmless under --gc-sections. */
typedef struct zhaoxu_unused_FILE FILE;
int snprintf(char *, unsigned long, const char *, ...);
#endif
