/*
 * Zhaoxu OS - zlib/DEFLATE decompressor.
 *
 * UBIFS stores zlib-compressed data nodes, so the kernel needs a small
 * inflater that allocates nothing and never trusts a length in the stream.
 * Huffman trees are built as bit trees rather than lookup tables: decoding
 * costs a handful of branches per symbol, and the footprint stays tiny.
 */
#include <inflate.h>
#include <types.h>

#define HUFF_MAX 600

struct bit_reader {
    const u8 *data;
    u32 length;
    u32 position;
    u32 buffer;
    int bits;
    int error;
};

struct huff_node {
    i16 left;
    i16 right;
    i16 symbol;
};

struct huff {
    struct huff_node nodes[HUFF_MAX];
    int count;
};

struct inflate_state {
    struct bit_reader bits;
    u8 *out;
    u32 capacity;
    u32 length;
    int error;
};

static u32 bits_get(struct bit_reader *reader, int count)
{
    while (reader->bits < count) {
        if (reader->position >= reader->length) {
            reader->error = 1;
            return 0;
        }
        reader->buffer |= (u32)reader->data[reader->position++]
                          << reader->bits;
        reader->bits += 8;
    }
    u32 value = reader->buffer & ((1u << count) - 1);
    reader->buffer >>= count;
    reader->bits -= count;
    return value;
}

static void bits_align(struct bit_reader *reader)
{
    reader->buffer = 0;
    reader->bits = 0;
}

static int huff_new(struct huff *tree)
{
    if (tree->count >= HUFF_MAX)
        return -1;
    struct huff_node *node = &tree->nodes[tree->count];
    node->left = -1;
    node->right = -1;
    node->symbol = -1;
    return tree->count++;
}

/* Build a canonical Huffman tree from code lengths.  Codes are transmitted
 * most-significant bit first, which is the order used to grow the tree. */
static int huff_build(struct huff *tree, const u8 *lengths, int symbols)
{
    tree->count = 0;
    huff_new(tree);
    int counts[16];
    int next[16];
    for (int i = 0; i < 16; i++) {
        counts[i] = 0;
        next[i] = 0;
    }
    for (int i = 0; i < symbols; i++)
        if (lengths[i])
            counts[lengths[i]]++;

    int code = 0;
    for (int length = 1; length < 16; length++) {
        code = (code + counts[length - 1]) << 1;
        next[length] = code;
    }

    for (int symbol = 0; symbol < symbols; symbol++) {
        int length = lengths[symbol];
        if (!length)
            continue;
        int current = 0;
        int value = next[length]++;
        for (int bit = length - 1; bit >= 0; bit--) {
            int branch = (value >> bit) & 1;
            int child = branch ? tree->nodes[current].right
                               : tree->nodes[current].left;
            if (child < 0) {
                child = huff_new(tree);
                if (child < 0)
                    return -1;
                if (branch)
                    tree->nodes[current].right = (i16)child;
                else
                    tree->nodes[current].left = (i16)child;
            }
            current = child;
        }
        tree->nodes[current].symbol = (i16)symbol;
    }
    return 0;
}

static int huff_decode(struct inflate_state *state, const struct huff *tree)
{
    if (tree->count <= 0)
        return -1;
    int current = 0;
    for (int depth = 0; depth < 16; depth++) {
        int bit = (int)bits_get(&state->bits, 1);
        if (state->bits.error)
            return -1;
        current = bit ? tree->nodes[current].right
                      : tree->nodes[current].left;
        if (current < 0 || current >= tree->count)
            return -1;
        if (tree->nodes[current].symbol >= 0)
            return tree->nodes[current].symbol;
    }
    return -1;
}

static void out_byte(struct inflate_state *state, u8 value)
{
    if (state->length >= state->capacity) {
        state->error = 1;
        return;
    }
    state->out[state->length++] = value;
}

static void out_copy(struct inflate_state *state, u32 distance, u32 length)
{
    if (!distance || distance > state->length) {
        state->error = 1;
        return;
    }
    if (length > state->capacity - state->length) {
        state->error = 1;
        return;
    }
    u32 start = state->length - distance;
    for (u32 i = 0; i < length; i++)
        state->out[state->length++] = state->out[start + i];
}

static const u16 g_length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8 g_length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 g_distance_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
    6145, 8193, 12289, 16385, 24577};
static const u8 g_distance_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int inflate_codes(struct inflate_state *state, const struct huff *lit,
                         const struct huff *dist)
{
    for (;;) {
        int symbol = huff_decode(state, lit);
        if (symbol < 0)
            return -1;
        if (symbol < 256) {
            out_byte(state, (u8)symbol);
            if (state->error)
                return -1;
            continue;
        }
        if (symbol == 256)
            return 0;
        symbol -= 257;
        if (symbol >= 29)
            return -1;
        u32 length = g_length_base[symbol];
        if (g_length_extra[symbol])
            length += bits_get(&state->bits, g_length_extra[symbol]);
        int dsymbol = huff_decode(state, dist);
        if (dsymbol < 0 || dsymbol >= 30)
            return -1;
        u32 distance = g_distance_base[dsymbol];
        if (g_distance_extra[dsymbol])
            distance += bits_get(&state->bits, g_distance_extra[dsymbol]);
        if (state->bits.error)
            return -1;
        out_copy(state, distance, length);
        if (state->error)
            return -1;
    }
}

static int inflate_fixed(struct inflate_state *state)
{
    u8 lit_lengths[288];
    u8 dist_lengths[32];
    for (int i = 0; i < 144; i++)
        lit_lengths[i] = 8;
    for (int i = 144; i < 256; i++)
        lit_lengths[i] = 9;
    for (int i = 256; i < 280; i++)
        lit_lengths[i] = 7;
    for (int i = 280; i < 288; i++)
        lit_lengths[i] = 8;
    for (int i = 0; i < 32; i++)
        dist_lengths[i] = 5;
    struct huff lit;
    struct huff dist;
    if (huff_build(&lit, lit_lengths, 288) ||
        huff_build(&dist, dist_lengths, 32))
        return -1;
    return inflate_codes(state, &lit, &dist);
}

static int inflate_dynamic(struct inflate_state *state)
{
    static const u8 order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11,
                                 4, 12, 3, 13, 2, 14, 1, 15};
    u32 hlit = bits_get(&state->bits, 5) + 257;
    u32 hdist = bits_get(&state->bits, 5) + 1;
    u32 hclen = bits_get(&state->bits, 4) + 4;
    if (hlit > 286 || hdist > 32)
        return -1;

    u8 code_lengths[19];
    for (int i = 0; i < 19; i++)
        code_lengths[i] = 0;
    for (u32 i = 0; i < hclen; i++)
        code_lengths[order[i]] = (u8)bits_get(&state->bits, 3);
    if (state->bits.error)
        return -1;

    struct huff code_tree;
    if (huff_build(&code_tree, code_lengths, 19))
        return -1;

    u8 lengths[286 + 32];
    u32 total = hlit + hdist;
    u32 index = 0;
    while (index < total) {
        int symbol = huff_decode(state, &code_tree);
        if (symbol < 0)
            return -1;
        if (symbol < 16) {
            lengths[index++] = (u8)symbol;
            continue;
        }
        u32 repeat;
        u8 value;
        if (symbol == 16) {
            if (!index)
                return -1;
            value = lengths[index - 1];
            repeat = 3 + bits_get(&state->bits, 2);
        } else if (symbol == 17) {
            value = 0;
            repeat = 3 + bits_get(&state->bits, 3);
        } else if (symbol == 18) {
            value = 0;
            repeat = 11 + bits_get(&state->bits, 7);
        } else {
            return -1;
        }
        if (index + repeat > total)
            return -1;
        while (repeat--)
            lengths[index++] = value;
    }

    struct huff lit;
    struct huff dist;
    if (huff_build(&lit, lengths, (int)hlit) ||
        huff_build(&dist, lengths + hlit, (int)hdist))
        return -1;
    return inflate_codes(state, &lit, &dist);
}

static int inflate_stored(struct inflate_state *state)
{
    /* Alignment discards the partial byte left in the bit buffer; the byte
     * position already points at the next unconsumed byte. */
    bits_align(&state->bits);

    if (state->bits.position + 4 > state->bits.length)
        return -1;
    const u8 *header = state->bits.data + state->bits.position;
    u32 length = (u32)header[0] | ((u32)header[1] << 8);
    u32 complement = (u32)header[2] | ((u32)header[3] << 8);
    state->bits.position += 4;
    if ((length ^ 0xFFFFu) != complement)
        return -1;
    if (state->bits.position + length > state->bits.length)
        return -1;
    if (state->length + length > state->capacity)
        return -1;
    for (u32 i = 0; i < length; i++)
        state->out[state->length++] = state->bits.data[state->bits.position++];
    return 0;
}

int zlib_decompress(const u8 *source, u32 source_len, u8 *destination,
                    u32 capacity, u32 *out_len)
{
    if (!source || source_len < 6 || !destination)
        return -1;
    u8 cmf = source[0];
    u8 flg = source[1];
    if ((cmf & 0x0F) != 8)
        return -1;
    if ((u32)(((u32)cmf << 8) | flg) % 31 != 0)
        return -1;
    if (flg & 0x20)
        return -1; /* preset dictionary is not produced by UBIFS */

    struct inflate_state state;
    state.bits.data = source + 2;
    state.bits.length = source_len - 6;
    state.bits.position = 0;
    state.bits.buffer = 0;
    state.bits.bits = 0;
    state.bits.error = 0;
    state.out = destination;
    state.capacity = capacity;
    state.length = 0;
    state.error = 0;

    int final = 0;
    while (!final) {
        final = (int)bits_get(&state.bits, 1);
        u32 type = bits_get(&state.bits, 2);
        if (state.bits.error)
            return -1;
        int result;
        if (type == 0)
            result = inflate_stored(&state);
        else if (type == 1)
            result = inflate_fixed(&state);
        else if (type == 2)
            result = inflate_dynamic(&state);
        else
            return -1;
        if (result != 0)
            return -1;
    }
    if (state.bits.error || state.error)
        return -1;
    if (out_len)
        *out_len = state.length;
    return 0;
}
