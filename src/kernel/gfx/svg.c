/*
 * Zhaoxu OS - minimal SVG icon renderer (system API).
 *
 * Supports the subset used by desktop application icons: viewBox plus
 * <rect rx>, <circle> and <path> with M/L/H/V/C/Z and a solid fill colour.
 * Everything is integer/fixed-point so the kernel stays free of FPU state.
 */
#include <render.h>
#include <string.h>
#include <types.h>

#define SVG_MAX_POINTS 512
#define SVG_MAX_CROSS  128

static int g_vx, g_vy, g_vw = 100, g_vh = 100;
static int g_origin_x, g_origin_y, g_size;

static i32 g_px[SVG_MAX_POINTS];
static i32 g_py[SVG_MAX_POINTS];
static int g_pn;
static u32 g_fill;

static int to_x(int value)
{
    return g_origin_x + (value - g_vx) * g_size / g_vw;
}

static int to_y(int value)
{
    return g_origin_y + (value - g_vy) * g_size / g_vh;
}

static int parse_int(const char **cursor)
{
    const char *p = *cursor;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ',' || *p == '\r')
        p++;
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    int value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9')
            p++;
    }
    *cursor = p;
    return sign * value;
}

static const char *find_sub(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return NULL;
    usize length = strlen(needle);
    if (length == 0)
        return haystack;
    for (const char *p = haystack; *p; p++) {
        if (strncmp(p, needle, length) == 0)
            return p;
    }
    return NULL;
}

static const char *find_attr(const char *tag, const char *name)
{
    usize length = strlen(name);
    const char *p = tag;
    while ((p = find_sub(p, name))) {
        int boundary = p == tag || p[-1] == ' ' || p[-1] == '\t' ||
                       p[-1] == '\n' || p[-1] == '\r';
        if (boundary && p[length] == '=') {
            p += length + 1;
            while (*p == '"' || *p == '\'')
                p++;
            return p;
        }
        p += length;
    }
    return NULL;
}

static int attr_int(const char *tag, const char *name, int fallback)
{
    const char *p = find_attr(tag, name);
    if (!p)
        return fallback;
    return parse_int(&p);
}

static u32 attr_color(const char *tag, const char *name, u32 fallback)
{
    const char *p = find_attr(tag, name);
    if (!p || *p != '#')
        return fallback;
    p++;
    u32 value = 0;
    int digits = 0;
    while (digits < 6) {
        char c = *p++;
        u32 nibble;
        if (c >= '0' && c <= '9')
            nibble = (u32)(c - '0');
        else if (c >= 'a' && c <= 'f')
            nibble = (u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            nibble = (u32)(c - 'A' + 10);
        else
            break;
        value = (value << 4) | nibble;
        digits++;
    }
    if (digits == 3) {
        u32 r = (value >> 8) & 0xF;
        u32 g = (value >> 4) & 0xF;
        u32 b = value & 0xF;
        value = (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
    } else if (digits != 6) {
        return fallback;
    }
    return value;
}

static void fill_polygon(const i32 *xs, const i32 *ys, int count, u32 color)
{
    if (count < 3)
        return;

    int min_y = ys[0], max_y = ys[0];
    for (int i = 1; i < count; i++) {
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }

    i32 crossings[SVG_MAX_CROSS];
    for (int y = min_y; y <= max_y; y++) {
        int n = 0;
        for (int i = 0; i < count; i++) {
            int j = (i + 1) % count;
            i32 y0 = ys[i], y1 = ys[j];
            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                if (n >= SVG_MAX_CROSS)
                    break;
                i64 dx = (i64)xs[j] - xs[i];
                i64 dy = (i64)y1 - y0;
                crossings[n++] = (i32)((i64)xs[i] +
                                       (i64)(y - y0) * dx / dy);
            }
        }
        for (int a = 1; a < n; a++) {
            i32 key = crossings[a];
            int b = a - 1;
            while (b >= 0 && crossings[b] > key) {
                crossings[b + 1] = crossings[b];
                b--;
            }
            crossings[b + 1] = key;
        }
        for (int k = 0; k + 1 < n; k += 2)
            render_span(crossings[k], crossings[k + 1] + 1, y, color);
    }
}

static void path_flush(void)
{
    fill_polygon(g_px, g_py, g_pn, g_fill);
    g_pn = 0;
}

static void path_add(i32 x, i32 y)
{
    if (g_pn < SVG_MAX_POINTS) {
        g_px[g_pn] = x;
        g_py[g_pn] = y;
        g_pn++;
    }
}

static void cubic_to(int x0, int y0, int x1, int y1, int x2, int y2,
                     int x3, int y3)
{
    for (int step = 1; step <= 8; step++) {
        i64 t = step;
        i64 u = 8 - step;
        i64 x = (u * u * u * x0 + 3 * u * u * t * x1 +
                 3 * u * t * t * x2 + t * t * t * x3) / 512;
        i64 y = (u * u * u * y0 + 3 * u * u * t * y1 +
                 3 * u * t * t * y2 + t * t * t * y3) / 512;
        path_add((i32)x, (i32)y);
    }
}

static void draw_path(const char *d, u32 color)
{
    const char *p = d;
    int cx = 0, cy = 0;         /* viewBox coordinates */
    int start_x = 0, start_y = 0;

    g_fill = color;
    g_pn = 0;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
               *p == ',')
            p++;
        if (!*p)
            break;
        char command = *p++;
        int relative = command >= 'a' && command <= 'z';
        char op = relative ? (char)(command - 'a' + 'A') : command;

        if (op == 'M') {
            int x = parse_int(&p);
            int y = parse_int(&p);
            if (relative) {
                x += cx;
                y += cy;
            }
            path_flush();
            cx = start_x = x;
            cy = start_y = y;
            path_add(to_x(cx), to_y(cy));
        } else if (op == 'L') {
            int x = parse_int(&p);
            int y = parse_int(&p);
            if (relative) {
                x += cx;
                y += cy;
            }
            cx = x;
            cy = y;
            path_add(to_x(cx), to_y(cy));
        } else if (op == 'H') {
            int x = parse_int(&p);
            cx = relative ? cx + x : x;
            path_add(to_x(cx), to_y(cy));
        } else if (op == 'V') {
            int y = parse_int(&p);
            cy = relative ? cy + y : y;
            path_add(to_x(cx), to_y(cy));
        } else if (op == 'C') {
            int x1 = parse_int(&p), y1 = parse_int(&p);
            int x2 = parse_int(&p), y2 = parse_int(&p);
            int x3 = parse_int(&p), y3 = parse_int(&p);
            if (relative) {
                x1 += cx; y1 += cy;
                x2 += cx; y2 += cy;
                x3 += cx; y3 += cy;
            }
            cubic_to(to_x(cx), to_y(cy), to_x(x1), to_y(y1),
                     to_x(x2), to_y(y2), to_x(x3), to_y(y3));
            cx = x3;
            cy = y3;
        } else if (op == 'Z') {
            if (g_pn > 0) {
                path_add(to_x(start_x), to_y(start_y));
                path_flush();
            }
            cx = start_x;
            cy = start_y;
        } else {
            break;
        }
    }
    path_flush();
}

static void parse_viewbox(const char *tag)
{
    const char *p = find_attr(tag, "viewBox");
    if (!p)
        return;
    int a = parse_int(&p), b = parse_int(&p);
    int c = parse_int(&p), d = parse_int(&p);
    if (c > 0 && d > 0) {
        g_vx = a;
        g_vy = b;
        g_vw = c;
        g_vh = d;
    }
}

int render_svg(const char *svg, int x, int y, int size)
{
    if (!svg || size <= 0)
        return 0;

    g_vx = 0;
    g_vy = 0;
    g_vw = 100;
    g_vh = 100;
    g_origin_x = x;
    g_origin_y = y;
    g_size = size;

    const char *svg_tag = find_sub(svg, "<svg");
    if (svg_tag)
        parse_viewbox(svg_tag);

    const char *p = svg;
    int drawn = 0;
    while ((p = strchr(p, '<'))) {
        const char *end = strchr(p, '>');
        if (!end)
            break;
        if (!strncmp(p, "<rect", 5)) {
            int rx = attr_int(p, "x", 0);
            int ry = attr_int(p, "y", 0);
            int rw = attr_int(p, "width", 0);
            int rh = attr_int(p, "height", 0);
            int radius = attr_int(p, "rx", 0);
            u32 color = attr_color(p, "fill", 0xFFFFFF);
            if (rw > 0 && rh > 0) {
                int x0 = to_x(rx), y0 = to_y(ry);
                int x1 = to_x(rx + rw), y1 = to_y(ry + rh);
                int r = radius * g_size / g_vw;
                render_rounded_rect(x0, y0, x1 - x0, y1 - y0, r, color, 255);
                drawn = 1;
            }
        } else if (!strncmp(p, "<circle", 7)) {
            int cx = attr_int(p, "cx", 0);
            int cy = attr_int(p, "cy", 0);
            int radius = attr_int(p, "r", 0);
            u32 color = attr_color(p, "fill", 0xFFFFFF);
            if (radius > 0) {
                int x0 = to_x(cx - radius), y0 = to_y(cy - radius);
                int x1 = to_x(cx + radius), y1 = to_y(cy + radius);
                render_rounded_rect(x0, y0, x1 - x0, y1 - y0,
                                    (x1 - x0) / 2, color, 255);
                drawn = 1;
            }
        } else if (!strncmp(p, "<path", 5)) {
            const char *d = find_attr(p, "d");
            u32 color = attr_color(p, "fill", 0xFFFFFF);
            if (d) {
                char buffer[384];
                usize length = 0;
                while (d[length] && d[length] != '"' && d[length] != '\'' &&
                       length + 1 < sizeof(buffer)) {
                    buffer[length] = d[length];
                    length++;
                }
                buffer[length] = 0;
                draw_path(buffer, color);
                drawn = 1;
            }
        }
        p = end + 1;
    }
    return drawn;
}
