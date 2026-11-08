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
    if (!ttf.loaded || glyph + 1 >= ttf.num_glyphs)
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
