/* Minimal locale shim for the vendored regex: Zhaoxu OS has no locale
 * collation or multibyte rune tables yet, so ranges compare by code point
 * and the single-byte path is always taken. */
#ifndef ZHAOXU_COLLATE_SHIM_H
#define ZHAOXU_COLLATE_SHIM_H

#ifndef XLC_COLLATE
#define XLC_COLLATE 0
#endif

struct xlocale_collate {
    int __collate_load_error;
};

static inline int __collate_range_cmp(int c1, int c2)
{
    return c1 - c2;
}

static inline int __wcollate_range_cmp(unsigned int c1, unsigned int c2)
{
    return (int)c1 - (int)c2;
}

struct zx_locale {
    void *components[1];
};

static struct xlocale_collate zx_collate_state = { 1 };

static inline struct zx_locale *__get_locale(void)
{
    static struct zx_locale locale = { { &zx_collate_state } };
    return &locale;
}

struct zx_rune_locale {
    const char *__encoding;
};

static const struct zx_rune_locale zx_rune_ascii = { "ASCII" };
#define _CurrentRuneLocale (&zx_rune_ascii)

#endif
