/*
 * Zhaoxu OS - TrueType (glyf) font engine for CJK rendering
 *
 * Freestanding integer-only implementation: parses TTC/sfnt tables, maps
 * codepoints through cmap format 4 (format 12 fallback), reads hmtx/loca
 * metrics and rasterizes glyf outlines with a fixed-point scanline fill.
 * The font image lives in kernel pages reserved by ttf_autoload(), outside
 * the 4 MiB kmalloc heap.
 */
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <ttf.h>
#include <types.h>
#include <vfs.h>

#define TTF_TAG(a, b, c, d)                                                    \
    ((((u32)(a)) << 24) | (((u32)(b)) << 16) | (((u32)(c)) << 8) | ((u32)(d)))

#define TTF_HEAD TTF_TAG('h', 'e', 'a', 'd')
#define TTF_HHEA TTF_TAG('h', 'h', 'e', 'a')
#define TTF_MAXP TTF_TAG('m', 'a', 'x', 'p')
#define TTF_CMAP TTF_TAG('c', 'm', 'a', 'p')
#define TTF_HMTX TTF_TAG('h', 'm', 't', 'x')
#define TTF_LOCA TTF_TAG('l', 'o', 'c', 'a')
#define TTF_GLYF TTF_TAG('g', 'l', 'y', 'f')

#define TTF_MAX_FONTS 32

struct ttf_table {
    u32 off;
    u32 len;
};

static struct {
    const u8 *data;
    u64 size;
    u64 base;                       /* font offset inside a TTC */
    struct ttf_table head, hhea, maxp, cmap, hmtx, loca, glyf;
    u64 cmap_sub;                   /* absolute offset of subtable */
    int cmap_fmt;                   /* 4 or 12 */
    u32 upem;
    u32 num_glyphs;
    u32 num_hmetrics;
    int loca32;
    i32 ascender;
    i32 descender;
    int loaded;
} ttf;

static u16 ttf_be16(u64 offset)
{
    if (offset + 2 > ttf.size)
        return 0;
    const u8 *p = ttf.data + offset;
    return (u16)((p[0] << 8) | p[1]);
}

static i16 ttf_be16s(u64 offset)
{
    return (i16)ttf_be16(offset);
}

static u32 ttf_be32(u64 offset)
{
    if (offset + 4 > ttf.size)
        return 0;
    const u8 *p = ttf.data + offset;
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static struct ttf_table *find_table_slot(u32 tag)
{
    switch (tag) {
    case TTF_HEAD: return &ttf.head;
    case TTF_HHEA: return &ttf.hhea;
    case TTF_MAXP: return &ttf.maxp;
    case TTF_CMAP: return &ttf.cmap;
    case TTF_HMTX: return &ttf.hmtx;
    case TTF_LOCA: return &ttf.loca;
    case TTF_GLYF: return &ttf.glyf;
    default: return NULL;
    }
}

static int table_ok(const struct ttf_table *table, u32 need)
{
    return table->off && table->len >= need &&
           (u64)table->off + table->len <= ttf.size;
}

int ttf_load(const u8 *data, u64 size)
{
    if (!data || size < 12)
        return -1;
    memset(&ttf, 0, sizeof(ttf));
    ttf.data = data;
    ttf.size = size;

    u32 tag = ttf_be32(0);
    u64 base = 0;
    if (tag == TTF_TAG('t', 't', 'c', 'f')) {
        u32 num_fonts = ttf_be32(8);
        if (num_fonts == 0 || num_fonts > TTF_MAX_FONTS)
            return -2;
        base = ttf_be32(12);
        if (base < 12 || base + 12 > size)
            return -2;
        tag = ttf_be32(base);
    }
    if (tag != 0x00010000 && tag != TTF_TAG('t', 'r', 'u', 'e'))
        return -3; /* version 1/0.5 and 'OTTO' (CFF) unsupported */
    ttf.base = base;

    u32 num_tables = ttf_be16(base + 4);
    if (num_tables == 0 || num_tables > 64)
        return -4;
    for (u32 i = 0; i < num_tables; i++) {
        u64 entry = base + 12 + (u64)i * 16;
        if (entry + 16 > size)
            return -4;
        u32 entry_tag = ttf_be32(entry);
        struct ttf_table *slot = find_table_slot(entry_tag);
        if (!slot)
            continue;
        slot->off = ttf_be32(entry + 8);
        slot->len = ttf_be32(entry + 12);
    }
    if (!table_ok(&ttf.head, 54) || !table_ok(&ttf.hhea, 36) ||
        !table_ok(&ttf.maxp, 6) || !table_ok(&ttf.cmap, 4) ||
        !table_ok(&ttf.hmtx, 4) || !table_ok(&ttf.loca, 4) ||
        !table_ok(&ttf.glyf, 10))
        return -5;

    ttf.upem = ttf_be16(ttf.head.off + 18);
    if (ttf.upem < 16 || ttf.upem > 16384)
        return -6;
    ttf.num_glyphs = ttf_be16(ttf.maxp.off + 4);
    if (ttf.num_glyphs == 0)
        return -6;
    ttf.num_hmetrics = ttf_be16(ttf.hhea.off + 34);
    if (ttf.num_hmetrics == 0 || ttf.num_hmetrics > ttf.num_glyphs)
        ttf.num_hmetrics = ttf.num_glyphs;
    ttf.ascender = ttf_be16s(ttf.hhea.off + 4);
    ttf.descender = ttf_be16s(ttf.hhea.off + 6);
    ttf.loca32 = ttf_be16s(ttf.head.off + 50) == 1;
    u32 loca_need = (ttf.num_glyphs + 1) * (ttf.loca32 ? 4u : 2u);
    if (!table_ok(&ttf.loca, loca_need))
        return -6;

    u32 subtables = ttf_be16(ttf.cmap.off + 2);
    if (subtables == 0 || subtables > 64)
        return -7;
    int fmt4_plat = -1, fmt12_plat = -1;
    u64 fmt4_off = 0, fmt12_off = 0;
    for (u32 i = 0; i < subtables; i++) {
        u64 record = ttf.cmap.off + 4 + (u64)i * 8;
        u32 plat = ttf_be16(record);
        u32 enc = ttf_be16(record + 2);
        u64 sub = ttf.cmap.off + ttf_be32(record + 4);
        if (sub + 2 > ttf.size || sub >= ttf.cmap.off + ttf.cmap.len)
            continue;
        u32 fmt = ttf_be16(sub);
        int good_platform = (plat == 3 && (enc == 1 || enc == 10)) ||
                            (plat == 0 && (enc == 3 || enc == 4));
        if (!good_platform)
            continue;
        if (fmt == 4 && fmt4_plat < 0 && ttf_be16(sub + 2) >= 14) {
            fmt4_plat = (plat == 3) ? 3 : 0;
            fmt4_off = sub;
        }
        if (fmt == 12 && fmt12_plat < 0) {
            fmt12_plat = (plat == 3) ? 3 : 0;
            fmt12_off = sub;
        }
    }
    if (fmt4_plat >= 0) {
        ttf.cmap_sub = fmt4_off;
        ttf.cmap_fmt = 4;
    } else if (fmt12_plat >= 0) {
        ttf.cmap_sub = fmt12_off;
        ttf.cmap_fmt = 12;
    } else {
        return -8;
    }

    ttf.loaded = 1;
    return 0;
}

int ttf_available(void)
{
    return ttf.loaded;
}

u32 ttf_upem(void)
{
    return ttf.upem;
}

u32 ttf_num_glyphs(void)
{
    return ttf.num_glyphs;
}

int ttf_cmap_fmt(void)
{
    return ttf.loaded ? ttf.cmap_fmt : 0;
}

static int cmap_lookup4(u32 codepoint, u32 *glyph)
{
    if (codepoint > 0xFFFF)
        return 0;
    u64 sub = ttf.cmap_sub;
    u32 seg_count_x2 = ttf_be16(sub + 6);
    u32 seg_count = seg_count_x2 / 2;
    if (seg_count == 0 || seg_count > 4096)
        return 0;
    u64 ends = sub + 14;
    u64 starts = ends + seg_count_x2 + 2;
    u64 deltas = starts + seg_count_x2;
    u64 ranges = deltas + seg_count_x2;

    u32 lo = 0, hi = seg_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (ttf_be16(ends + (u64)mid * 2) >= codepoint)
            hi = mid;
        else
            lo = mid + 1;
    }
    if (lo >= seg_count)
        return 0;
    u32 start = ttf_be16(starts + (u64)lo * 2);
    if (start > codepoint)
        return 0;
    i16 delta = ttf_be16s(deltas + (u64)lo * 2);
    u16 range = ttf_be16(ranges + (u64)lo * 2);
    if (range == 0) {
        *glyph = (u32)(((i32)codepoint + delta) & 0xFFFF);
        return *glyph != 0 && *glyph < ttf.num_glyphs;
    }
    u64 target = ranges + (u64)lo * 2 + range +
                 (u64)(codepoint - start) * 2;
    if (target + 2 > ttf.cmap.off + ttf.cmap.len)
        return 0;
    u16 raw = ttf_be16(target);
    if (raw == 0)
        return 0;
    *glyph = (u32)(((i32)raw + delta) & 0xFFFF);
    return *glyph != 0 && *glyph < ttf.num_glyphs;
}

static int cmap_lookup12(u32 codepoint, u32 *glyph)
{
    u64 sub = ttf.cmap_sub;
    u32 groups = ttf_be32(sub + 12);
    if (groups == 0 || groups > 100000)
        return 0;
    u64 limit = ttf.cmap.off + ttf.cmap.len;
    u32 lo = 0, hi = groups - 1;
    while (lo <= hi) {
        u32 mid = lo + (hi - lo) / 2;
        u64 group = sub + 16 + (u64)mid * 12;
        if (group + 12 > limit)
            return 0;
        u32 start = ttf_be32(group);
        u32 end = ttf_be32(group + 4);
        if (codepoint < start) {
            if (mid == 0)
                return 0;
            hi = mid - 1;
        } else if (codepoint > end) {
            lo = mid + 1;
        } else {
            u32 gid = ttf_be32(group + 8) + (codepoint - start);
            *glyph = gid;
            return gid < ttf.num_glyphs;
        }
    }
    return 0;
}

int ttf_glyph_for(u32 codepoint, u32 *glyph)
{
    if (!ttf.loaded || !glyph)
        return 0;
    return ttf.cmap_fmt == 4 ? cmap_lookup4(codepoint, glyph)
                             : cmap_lookup12(codepoint, glyph);
}

u32 ttf_glyph_advance(u32 glyph)
{
    if (!ttf.loaded || glyph >= ttf.num_glyphs)
        return 0;
    u32 index = glyph < ttf.num_hmetrics ? glyph : ttf.num_hmetrics - 1;
    return ttf_be16(ttf.hmtx.off + (u64)index * 4);
}

static u64 ttf_glyph_offset(u32 glyph);

static u64 ttf_glyph_offset(u32 glyph)
{
    if (!ttf.loaded || glyph >= ttf.num_glyphs)
        return 0;
    u64 start, end;
    if (ttf.loca32) {
        start = ttf_be32(ttf.loca.off + (u64)glyph * 4);
        end = ttf_be32(ttf.loca.off + (u64)(glyph + 1) * 4);
    } else {
        start = (u64)ttf_be16(ttf.loca.off + (u64)glyph * 2) * 2;
        end = (u64)ttf_be16(ttf.loca.off + (u64)(glyph + 1) * 2) * 2;
    }
    if (end <= start || ttf.glyf.off + end > ttf.size)
        return 0;
    return ttf.glyf.off + start;
}

/* Space and other blank glyphs have an empty loca range, not an outline. */
static int ttf_glyph_is_empty(u32 glyph)
{
    if (!ttf.loaded || glyph >= ttf.num_glyphs)
        return 1;
    u64 start, end;
    if (ttf.loca32) {
        start = ttf_be32(ttf.loca.off + (u64)glyph * 4);
        end = ttf_be32(ttf.loca.off + (u64)(glyph + 1) * 4);
    } else {
        start = (u64)ttf_be16(ttf.loca.off + (u64)glyph * 2) * 2;
        end = (u64)ttf_be16(ttf.loca.off + (u64)(glyph + 1) * 2) * 2;
    }
    return end <= start;
}

int ttf_glyph_bbox(u32 glyph, i32 box[4])
{
    u64 glyf = ttf_glyph_offset(glyph);
    if (!glyf)
        return -1;
    if (glyf + 10 > ttf.size)
        return -1;
    i32 contours = ttf_be16s(glyf);
    if (contours >= 0) {
        box[0] = ttf_be16s(glyf + 2);
        box[1] = ttf_be16s(glyf + 4);
        box[2] = ttf_be16s(glyf + 6);
        box[3] = ttf_be16s(glyf + 8);
        return 0;
    }
    /* composite glyphs store the box at offset 10 */
    if (glyf + 18 > ttf.size)
        return -1;
    box[0] = ttf_be16s(glyf + 10);
    box[1] = ttf_be16s(glyf + 12);
    box[2] = ttf_be16s(glyf + 14);
    box[3] = ttf_be16s(glyf + 16);
    return 0;
}

int ttf_autoload(const char *path)
{
    int is_dir = 0;
    u64 size = 0;
    if (!path || vfs_stat(path, &is_dir, &size) != 0 || is_dir)
        return -1;
    if (size < 12 || size > 64u * 1024 * 1024)
        return -1;

    u32 pages = (u32)((size + PAGE_SIZE - 1) / PAGE_SIZE);
    kprintf("[ttf ] %s: %llu bytes, %u pages\n", path, size, pages);
    u64 va = vmm_alloc_pages(pages);
    if (!va)
        return -2;

    u64 read = 0;
    if (vfs_read_into(path, (u8 *)(uintptr_t)va,
                      (u64)pages * PAGE_SIZE, &read) != 0) {
        for (u32 i = 0; i < pages; i++)
            vmm_unmap_page(va + (u64)i * PAGE_SIZE);
        return -3;
    }

    int status = ttf_load((const u8 *)(uintptr_t)va, read);
    if (status != 0) {
        for (u32 i = 0; i < pages; i++)
            vmm_unmap_page(va + (u64)i * PAGE_SIZE);
        return -4;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* outline extraction                                                  */
/* ------------------------------------------------------------------ */

#define TTF_MAX_POINTS 1024
#define TTF_MAX_CONTOURS 64
#define TTF_MAX_LINES 4096
#define TTF_MAX_CROSS 256
#define TTF_CACHE_SLOTS 1024

struct ttf_line {
    i32 x0, y0, x1, y1; /* 26.6 pixel space, y down */
};

static i32 g_x[TTF_MAX_POINTS];   /* font units */
static i32 g_y[TTF_MAX_POINTS];
static u8 g_on[TTF_MAX_POINTS];
static u16 g_end[TTF_MAX_CONTOURS];
static u32 g_point_total;
static u32 g_contour_count;

static struct ttf_line g_lines[TTF_MAX_LINES];
static u32 g_line_count;

static i32 g_box_x0, g_box_y0, g_box_x1, g_box_y1; /* 26.6 */

static u32 g_px = 20; /* active em size, set via ttf_set_pixel_size() */

static i64 scale_font_to266(i64 value)
{
    return value * ((i64)g_px << 6) / (i64)ttf.upem;
}

static i64 baseline266(void)
{
    return scale_font_to266(ttf.ascender);
}

static u8 s_flags[TTF_MAX_POINTS];

static int extract_simple(u64 glyf)
{
    if (glyf + 12 > ttf.size)
        return -1;
    i32 contours = ttf_be16s(glyf);
    if (contours <= 0 || contours > TTF_MAX_CONTOURS)
        return -1;
    u64 ends_at = glyf + 10;
    if (ends_at + (u64)contours * 2 + 2 > ttf.size)
        return -1;
    for (u32 i = 0; i < (u32)contours; i++) {
        u32 e = ttf_be16(ends_at + (u64)i * 2);
        if (e + 1 > TTF_MAX_POINTS || (i && e < g_end[i - 1]))
            return -1;
        g_end[i] = (u16)e;
    }
    u32 total = g_end[contours - 1] + 1;
    u64 ilen_at = ends_at + (u64)contours * 2;
    u32 ilen = ttf_be16(ilen_at);
    u64 flags_at = ilen_at + 2 + ilen;
    if (flags_at + 1 > ttf.size)
        return -1;

    /* decode the flag stream (with REPEAT runs) into s_flags */
    u64 cursor = flags_at;
    for (u32 i = 0; i < total; i++) {
        if (cursor + 1 > ttf.size)
            return -1;
        u8 flag = ttf.data[cursor++];
        s_flags[i] = flag;
        g_on[i] = flag & 1;
        if (flag & 8) {
            if (cursor + 1 > ttf.size)
                return -1;
            u32 extra = ttf.data[cursor++];
            while (extra-- > 0 && i + 1 < total) {
                s_flags[++i] = flag;
                g_on[i] = flag & 1;
            }
        }
    }

    for (u32 axis = 0; axis < 2; axis++) {
        i32 previous = 0;
        for (u32 i = 0; i < total; i++) {
            u8 flag = s_flags[i];
            u8 short_bit = axis == 0 ? 2 : 4;
            u8 same_bit = axis == 0 ? 16 : 32;
            i32 delta = 0;
            if (flag & short_bit) {
                if (cursor + 1 > ttf.size)
                    return -1;
                delta = ttf.data[cursor++];
                if (!(flag & same_bit))
                    delta = -delta;
            } else if (!(flag & same_bit)) {
                if (cursor + 2 > ttf.size)
                    return -1;
                delta = ttf_be16s(cursor);
                cursor += 2;
            }
            previous += delta;
            if (axis == 0)
                g_x[i] = previous;
            else
                g_y[i] = previous;
        }
    }
    g_point_total = total;
    g_contour_count = (u32)contours;
    return 0;
}

/* ------------------------------------------------------------------ */
/* outline flattening                                                  */
/* ------------------------------------------------------------------ */

static int g_box_valid;

static i32 g_tcx[TTF_MAX_POINTS];
static i32 g_tcy[TTF_MAX_POINTS];
static u8 g_ton[TTF_MAX_POINTS];

static void add_line(i32 x0, i32 y0, i32 x1, i32 y1)
{
    if (x0 == x1 && y0 == y1)
        return;
    if (g_line_count >= TTF_MAX_LINES)
        return;
    struct ttf_line *line = &g_lines[g_line_count++];
    line->x0 = x0;
    line->y0 = y0;
    line->x1 = x1;
    line->y1 = y1;

    if (!g_box_valid) {
        g_box_x0 = g_box_x1 = x0 < x1 ? x0 : x1;
        g_box_y0 = g_box_y1 = y0 < y1 ? y0 : y1;
        g_box_valid = 1;
    }
    if (x0 < g_box_x0) g_box_x0 = x0;
    if (x1 < g_box_x0) g_box_x0 = x1;
    if (x0 > g_box_x1) g_box_x1 = x0;
    if (x1 > g_box_x1) g_box_x1 = x1;
    if (y0 < g_box_y0) g_box_y0 = y0;
    if (y1 < g_box_y0) g_box_y0 = y1;
    if (y0 > g_box_y1) g_box_y1 = y0;
    if (y1 > g_box_y1) g_box_y1 = y1;
}

static void quad_flatten(i32 x0, i32 y0, i32 cx, i32 cy, i32 x1, i32 y1,
                         u32 depth)
{
    if (depth > 0) {
        /* flat enough: straight segment (control point within ~0.25px) */
        i64 cross = (i64)(cx - x0) * (y1 - y0) - (i64)(cy - y0) * (x1 - x0);
        if (cross < 0)
            cross = -cross;
        i64 len2 = (i64)(x1 - x0) * (x1 - x0) + (i64)(y1 - y0) * (y1 - y0);
        if (cross * cross * 4 <= len2 * 16) /* dev^2 = cross^2/(4*len2) */
            depth = 0;
    }
    if (depth == 0) {
        add_line(x0, y0, x1, y1);
        return;
    }
    i32 ax = (x0 + cx) / 2, ay = (y0 + cy) / 2;
    i32 bx = (cx + x1) / 2, by = (cy + y1) / 2;
    i32 mx = (ax + bx) / 2, my = (ay + by) / 2;
    quad_flatten(x0, y0, ax, ay, mx, my, depth - 1);
    quad_flatten(mx, my, bx, by, x1, y1, depth - 1);
}

/* Flatten one closed contour. Points are already in 26.6 pixel space.
 *
 * Every source point must be consumed exactly once: the previous version
 * counted an on-curve endpoint twice when it followed a control point,
 * which exhausted the point budget early and silently dropped the tail of
 * the contour (e.g. the right stem of 'u' and 'd'). */
static void flatten_contour(const i32 *xs, const i32 *ys, const u8 *on,
                            u32 n)
{
    if (n < 2)
        return;
    u32 first_on = 0;
    while (first_on < n && !on[first_on])
        first_on++;

    i32 start_x, start_y, cx, cy;
    u32 idx;
    u32 done;
    if (first_on == n) {
        /* every point is a control point: implicit on-curve midpoint */
        cx = (xs[n - 1] + xs[0]) / 2;
        cy = (ys[n - 1] + ys[0]) / 2;
        idx = n - 1;
        done = 0;
    } else {
        cx = xs[first_on];
        cy = ys[first_on];
        idx = first_on;
        done = 1;
    }
    start_x = cx;
    start_y = cy;

    while (done < n) {
        u32 next = idx + 1;
        if (next >= n)
            next = 0;
        if (on[next]) {
            add_line(cx, cy, xs[next], ys[next]);
            cx = xs[next];
            cy = ys[next];
            idx = next;
            done++;
        } else {
            u32 after = next + 1;
            if (after >= n)
                after = 0;
            if (on[after]) {
                quad_flatten(cx, cy, xs[next], ys[next], xs[after],
                             ys[after], 4);
                cx = xs[after];
                cy = ys[after];
                idx = after;
                done += 2;
            } else {
                i32 mx = (xs[next] + xs[after]) / 2;
                i32 my = (ys[next] + ys[after]) / 2;
                quad_flatten(cx, cy, xs[next], ys[next], mx, my, 4);
                cx = mx;
                cy = my;
                idx = next;
                done++;
            }
        }
        if (idx == first_on && done < n)
            break; /* malformed contour wrapped early */
    }
    if (cx != start_x || cy != start_y)
        add_line(cx, cy, start_x, start_y);
}

/* ------------------------------------------------------------------ */
/* glyph extraction (simple + composite, with affine composition)      */
/* ------------------------------------------------------------------ */

#define GLYF_ARG_WORDS 0x0001u
#define GLYF_ARGS_XY   0x0002u
#define GLYF_A_SCALE   0x0008u
#define GLYF_MORE      0x0020u
#define GLYF_XY_SCALE  0x0040u
#define GLYF_2X2       0x0080u

struct ttf_aff {
    i32 sx, sy; /* f2dot14, 16384 = identity */
    i32 dx, dy; /* font units */
};

static int extract_glyph(u32 gid, const struct ttf_aff *aff, u32 depth);

static int extract_composite(u64 glyf, const struct ttf_aff *aff, u32 depth)
{
    u64 cursor = glyf + 10;
    u32 components = 0;
    for (;;) {
        if (cursor + 4 > ttf.size)
            return -1;
        u32 flags = ttf_be16(cursor);
        u32 child_gid = ttf_be16(cursor + 2);
        cursor += 4;

        int drawable = (flags & GLYF_ARGS_XY) != 0 &&
                       !(flags & GLYF_2X2);
        i32 dx = 0, dy = 0;
        if (flags & GLYF_ARG_WORDS) {
            dx = ttf_be16s(cursor);
            dy = ttf_be16s(cursor + 2);
            cursor += 4;
        } else {
            if (cursor + 2 > ttf.size)
                return -1;
            dx = (i8)ttf.data[cursor];
            dy = (i8)ttf.data[cursor + 1];
            cursor += 2;
        }
        i32 sx = 16384, sy = 16384;
        if (flags & GLYF_A_SCALE) {
            sx = sy = ttf_be16s(cursor);
            cursor += 2;
        } else if (flags & GLYF_XY_SCALE) {
            sx = ttf_be16s(cursor);
            sy = ttf_be16s(cursor + 2);
            cursor += 4;
        } else if (flags & GLYF_2X2) {
            cursor += 8; /* unsupported rotation: skip the component */
        }
        if (cursor > ttf.size)
            return -1;

        if (drawable) {
            struct ttf_aff child;
            child.sx = (i32)(((i64)aff->sx * sx) / 16384);
            child.sy = (i32)(((i64)aff->sy * sy) / 16384);
            child.dx = (i32)(((i64)aff->sx * dx) / 16384) + aff->dx;
            child.dy = (i32)(((i64)aff->sy * dy) / 16384) + aff->dy;
            int status = extract_glyph(child_gid, &child, depth + 1);
            if (status < 0)
                return status;
        }
        if (!(flags & GLYF_MORE))
            break;
        if (++components >= 64)
            return -1;
    }
    return 0;
}

static int extract_glyph(u32 gid, const struct ttf_aff *aff, u32 depth)
{
    if (depth > 8)
        return -1;
    u64 glyf = ttf_glyph_offset(gid);
    if (!glyf)
        return -1;
    i32 contours = ttf_be16s(glyf);
    if (contours == 0)
        return 0; /* empty glyph */
    if (contours > 0) {
        if (extract_simple(glyf) != 0)
            return -1;
        i64 scale_num = (i64)g_px << 6;
        i64 baseline = baseline266();
        u32 base = 0;
        for (u32 c = 0; c < g_contour_count; c++) {
            u32 end = g_end[c];
            u32 n = end - base + 1;
            for (u32 i = 0; i < n; i++) {
                i32 fx =
                    (i32)(((i64)g_x[base + i] * aff->sx) / 16384) + aff->dx;
                i32 fy =
                    (i32)(((i64)g_y[base + i] * aff->sy) / 16384) + aff->dy;
                g_tcx[i] = (i32)(((i64)fx * scale_num) / (i64)ttf.upem);
                g_tcy[i] =
                    (i32)(baseline - ((i64)fy * scale_num) / (i64)ttf.upem);
                g_ton[i] = g_on[base + i];
            }
            flatten_contour(g_tcx, g_tcy, g_ton, n);
            base = end + 1;
        }
        return 0;
    }
    return extract_composite(glyf, aff, depth);
}

/* ------------------------------------------------------------------ */
/* scanline rasterizer                                                 */
/* ------------------------------------------------------------------ */

#define TTF_ROWS (TTF_MAX_PX + 6) /* cell height: ascender + descender */

static u32 g_cov[TTF_ROWS][TTF_MAX_PX];
static i32 g_cross_x[TTF_MAX_CROSS];
static i8 g_cross_w[TTF_MAX_CROSS];

static void accumulate_span(i32 row, i32 xa, i32 xb, u32 weight)
{
    if (xb <= xa)
        return;
    i32 col = xa >> 6;
    if (col < 0)
        col = 0;
    i32 last = (xb + 63) >> 6;
    if (last > (i32)g_px)
        last = (i32)g_px;
    for (; col < last; col++) {
        i32 lo = xa > col * 64 ? xa : col * 64;
        i32 hi = xb < col * 64 + 64 ? xb : col * 64 + 64;
        if (hi > lo)
            g_cov[row][col] += (u32)((hi - lo) * weight);
    }
}

static void rasterize(struct ttf_glyph *out)
{
    memset(out->alpha, 0, sizeof(out->alpha));
    out->w = 0;
    out->h = 0;
    out->bx = 0;
    out->by = 0;
    if (!g_box_valid || g_line_count == 0)
        return;

    i32 cx0 = g_box_x0 >> 6;
    i32 cx1 = (g_box_x1 + 63) >> 6;
    i32 cy0 = g_box_y0 >> 6;
    i32 cy1 = (g_box_y1 + 63) >> 6;
    if (cx0 < 0)
        cx0 = 0;
    if (cy0 < 0)
        cy0 = 0;
    if (cx1 > (i32)g_px)
        cx1 = (i32)g_px;
    if (cy1 > TTF_ROWS)
        cy1 = TTF_ROWS;
    if (cx1 <= cx0 || cy1 <= cy0)
        return;

    for (u32 i = 0; i < TTF_ROWS; i++)
        for (u32 j = 0; j < TTF_MAX_PX; j++)
            g_cov[i][j] = 0;

    for (i32 row = cy0; row < cy1; row++) {
        for (u32 sub = 0; sub < 8; sub++) {
            i32 sy = row * 64 + (i32)(sub * 8 + 4);
            u32 count = 0;
            for (u32 l = 0; l < g_line_count; l++) {
                const struct ttf_line *line = &g_lines[l];
                i32 dir = 0;
                if (line->y0 <= sy && sy < line->y1)
                    dir = 1;
                else if (line->y1 <= sy && sy < line->y0)
                    dir = -1;
                else
                    continue;
                if (count >= TTF_MAX_CROSS)
                    break;
                i64 x = (i64)line->x0 +
                        ((i64)(line->x1 - line->x0) * (sy - line->y0)) /
                            (line->y1 - line->y0);
                g_cross_x[count] = (i32)x;
                g_cross_w[count] = (i8)dir;
                count++;
            }
            for (u32 i = 1; i < count; i++) {
                i32 xv = g_cross_x[i];
                i8 wv = g_cross_w[i];
                u32 j = i;
                while (j > 0 && g_cross_x[j - 1] > xv) {
                    g_cross_x[j] = g_cross_x[j - 1];
                    g_cross_w[j] = g_cross_w[j - 1];
                    j--;
                }
                g_cross_x[j] = xv;
                g_cross_w[j] = wv;
            }
            i32 winding = 0;
            i32 span_start = 0;
            int in_span = 0;
            for (u32 i = 0; i < count; i++) {
                winding += g_cross_w[i];
                if (!in_span && winding != 0) {
                    span_start = g_cross_x[i];
                    in_span = 1;
                } else if (in_span && winding == 0) {
                    accumulate_span(row, span_start, g_cross_x[i], 8);
                    in_span = 0;
                }
            }
        }
    }

    out->bx = (i8)cx0;
    out->by = (i8)cy0;
    out->w = (u8)(cx1 - cx0);
    out->h = (u8)(cy1 - cy0);
    for (i32 row = cy0; row < cy1; row++) {
        for (i32 col = cx0; col < cx1; col++) {
            u32 cov = g_cov[row][col];
            u8 alpha = 0;
            if (cov >= 512) {
                alpha = 255;
            } else if (cov) {
                /* 8x vertical supersampling plus a gamma-2.0 coverage
                 * curve; linear coverage makes thin CJK strokes break up
                 * into gray dots at small sizes. */
                u32 value = (cov * 255 + 256) / 512;
                value = 255 - ((255 - value) * (255 - value) + 127) / 255;
                alpha = (u8)value;
            }
            out->alpha[(row - cy0) * out->w + (col - cx0)] = alpha;
        }
    }
}

/* ------------------------------------------------------------------ */
/* glyph cache + public render entry                                   */
/* ------------------------------------------------------------------ */

struct ttf_cache_entry {
    u32 codepoint;
    int used;
    struct ttf_glyph glyph;
};

static struct ttf_cache_entry g_cache[TTF_CACHE_SLOTS];

int ttf_set_pixel_size(int px)
{
    if (px < 12)
        px = 12;
    if (px > TTF_MAX_PX)
        px = TTF_MAX_PX;
    g_px = (u32)px;
    memset(g_cache, 0, sizeof(g_cache));
    return px;
}

int ttf_pixel_size(void)
{
    return (int)g_px;
}

int ttf_line_height(void)
{
    return (int)g_px + 4;
}

static u32 cache_hash(u32 codepoint)
{
    return (codepoint * 2654435761u) >> 22;
}

int ttf_render(u32 codepoint, struct ttf_glyph *out)
{
    if (!ttf.loaded || !out)
        return -1;

    u32 slot = cache_hash(codepoint);
    int free_slot = -1;
    for (u32 i = 0; i < 8; i++) {
        struct ttf_cache_entry *entry =
            &g_cache[(slot + i) % TTF_CACHE_SLOTS];
        if (entry->used && entry->codepoint == codepoint) {
            *out = entry->glyph;
            return 0;
        }
        if (!entry->used && free_slot < 0)
            free_slot = (int)((slot + i) % TTF_CACHE_SLOTS);
    }

    u32 gid;
    if (!ttf_glyph_for(codepoint, &gid))
        return -2;

    g_line_count = 0;
    g_box_valid = 0;
    if (ttf_glyph_is_empty(gid)) {
        memset(out->alpha, 0, sizeof(out->alpha));
        out->w = 0;
        out->h = 0;
        out->bx = 0;
        out->by = 0;
    } else {
        struct ttf_aff identity = {16384, 16384, 0, 0};
        if (extract_glyph(gid, &identity, 0) < 0)
            return -3;
        rasterize(out);
    }

    u32 advance_units = ttf_glyph_advance(gid);
    i32 advance_px = (i32)(((i64)advance_units * g_px) / (i64)ttf.upem);
    if (advance_px < 1)
        advance_px = 1;
    if (advance_px > 255)
        advance_px = 255;
    out->adv = (u8)advance_px;

    struct ttf_cache_entry *entry =
        &g_cache[free_slot >= 0 ? (u32)free_slot : slot];
    entry->used = 1;
    entry->codepoint = codepoint;
    entry->glyph = *out;
    return 0;
}
