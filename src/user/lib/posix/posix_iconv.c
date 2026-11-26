/*
 * Zhaoxu OS - iconv over UTF-8, ASCII and ISO-8859-1.
 *
 * These three encodings cover the terminal and the configuration files the
 * image ships.  Conversion stops with EILSEQ on a character the target
 * cannot represent, which is the standard's required behaviour.
 */
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef void *iconv_t;

enum zos_encoding {
    ZOS_ENC_UTF8 = 0,
    ZOS_ENC_ASCII = 1,
    ZOS_ENC_LATIN1 = 2
};

struct zos_converter {
    enum zos_encoding from;
    enum zos_encoding to;
};

static const char *encoding_name(const char *name)
{
    return name ? name : "";
}

/* Map an encoding name onto the three supported ones. */
static int encoding_lookup(const char *name, enum zos_encoding *encoding)
{
    const char *text = encoding_name(name);
    if (!strcasecmp(text, "UTF-8") || !strcasecmp(text, "UTF8")) {
        *encoding = ZOS_ENC_UTF8;
        return 0;
    }
    if (!strcasecmp(text, "ASCII") || !strcasecmp(text, "US-ASCII") ||
        !strcasecmp(text, "ANSI_X3.4-1968")) {
        *encoding = ZOS_ENC_ASCII;
        return 0;
    }
    if (!strcasecmp(text, "ISO-8859-1") || !strcasecmp(text, "LATIN1") ||
        !strcasecmp(text, "ISO8859-1")) {
        *encoding = ZOS_ENC_LATIN1;
        return 0;
    }
    return -1;
}

iconv_t iconv_open(const char *target, const char *source)
{
    enum zos_encoding to, from;
    if (encoding_lookup(target, &to) != 0 ||
        encoding_lookup(source, &from) != 0) {
        errno = EINVAL;
        return (iconv_t)-1;
    }
    struct zos_converter *converter =
        (struct zos_converter *)malloc(sizeof(*converter));
    if (!converter) {
        errno = ENOMEM;
        return (iconv_t)-1;
    }
    converter->from = from;
    converter->to = to;
    return (iconv_t)converter;
}

/* Decode one UTF-8 code point; returns bytes consumed or -1. */
static int utf8_decode(const unsigned char *input, size_t available,
                       unsigned int *code_point)
{
    unsigned char first = input[0];
    if (first < 0x80) {
        *code_point = first;
        return 1;
    }
    int length;
    unsigned int value;
    if ((first & 0xE0) == 0xC0) {
        length = 2;
        value = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        length = 3;
        value = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        length = 4;
        value = first & 0x07;
    } else {
        return -1;
    }
    if ((size_t)length > available)
        return -1;
    for (int i = 1; i < length; i++) {
        if ((input[i] & 0xC0) != 0x80)
            return -1;
        value = (value << 6) | (input[i] & 0x3F);
    }
    *code_point = value;
    return length;
}

/* Encode one code point as UTF-8; returns bytes written or -1. */
static int utf8_encode(unsigned int code_point, unsigned char *output,
                       size_t available)
{
    if (code_point < 0x80) {
        if (available < 1)
            return -1;
        output[0] = (unsigned char)code_point;
        return 1;
    }
    if (code_point < 0x800) {
        if (available < 2)
            return -1;
        output[0] = (unsigned char)(0xC0 | (code_point >> 6));
        output[1] = (unsigned char)(0x80 | (code_point & 0x3F));
        return 2;
    }
    if (code_point < 0x10000) {
        if (available < 3)
            return -1;
        output[0] = (unsigned char)(0xE0 | (code_point >> 12));
        output[1] = (unsigned char)(0x80 | ((code_point >> 6) & 0x3F));
        output[2] = (unsigned char)(0x80 | (code_point & 0x3F));
        return 3;
    }
    if (available < 4)
        return -1;
    output[0] = (unsigned char)(0xF0 | (code_point >> 18));
    output[1] = (unsigned char)(0x80 | ((code_point >> 12) & 0x3F));
    output[2] = (unsigned char)(0x80 | ((code_point >> 6) & 0x3F));
    output[3] = (unsigned char)(0x80 | (code_point & 0x3F));
    return 4;
}

size_t iconv(iconv_t descriptor, char **input, size_t *input_left,
             char **output, size_t *output_left)
{
    struct zos_converter *converter = (struct zos_converter *)descriptor;
    if (!converter || !input || !input_left || !output || !output_left) {
        errno = EINVAL;
        return (size_t)-1;
    }
    size_t converted = 0;
    while (*input_left > 0) {
        unsigned int code_point;
        int consumed;
        if (converter->from == ZOS_ENC_UTF8) {
            consumed = utf8_decode((unsigned char *)*input, *input_left,
                                   &code_point);
            if (consumed < 0) {
                errno = EILSEQ;
                return (size_t)-1;
            }
        } else {
            code_point = (unsigned char)**input;
            consumed = 1;
        }

        unsigned char encoded[4];
        int produced;
        if (converter->to == ZOS_ENC_UTF8) {
            produced = utf8_encode(code_point, encoded, sizeof(encoded));
            if (produced < 0) {
                errno = E2BIG;
                return (size_t)-1;
            }
        } else if (converter->to == ZOS_ENC_ASCII) {
            if (code_point > 0x7F) {
                errno = EILSEQ;
                return (size_t)-1;
            }
            encoded[0] = (unsigned char)code_point;
            produced = 1;
        } else {
            if (code_point > 0xFF) {
                errno = EILSEQ;
                return (size_t)-1;
            }
            encoded[0] = (unsigned char)code_point;
            produced = 1;
        }

        if ((size_t)produced > *output_left) {
            errno = E2BIG;
            return (size_t)-1;
        }
        memcpy(*output, encoded, (size_t)produced);
        *output += produced;
        *output_left -= (size_t)produced;
        *input += consumed;
        *input_left -= (size_t)consumed;
        converted += (size_t)produced;
    }
    return converted;
}

int iconv_close(iconv_t descriptor)
{
    free(descriptor);
    return 0;
}
