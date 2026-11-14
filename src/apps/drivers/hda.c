/*
 * Zhaoxu OS - Intel High Definition Audio driver.
 *
 * Initializes the controller, brings up the CORB/RIRB command rings, walks
 * the codec widget tree and configures one output path (DAC -> pin) for
 * 48 kHz/16-bit stereo playback.  A single stream descriptor is used with a
 * one-entry buffer descriptor list, which is enough for system sounds and
 * for PCM playback driven by a class service.
 */
#include <dev.h>
#include <io.h>
#include <types.h>

extern u32 pci_config_read32(u32 bus, u32 slot, u32 function, u32 offset);
extern void pci_config_write32(u32 bus, u32 slot, u32 function, u32 offset,
                               u32 value);
extern u64 pmm_alloc_page(void);
extern void buffer_zero(void *destination, u32 bytes);
extern void console_write(const char *text);

#define HDA_GCAP 0x00
#define HDA_GCTL 0x08
#define HDA_STATESTS 0x0E
#define HDA_CORBLBASE 0x40
#define HDA_CORBUBASE 0x44
#define HDA_CORBWP 0x48
#define HDA_CORBRP 0x4A
#define HDA_CORBCTL 0x4C
#define HDA_CORBSIZE 0x4E
#define HDA_RIRBLBASE 0x50
#define HDA_RIRBUBASE 0x54
#define HDA_RIRBWP 0x58
#define HDA_RINTCNT 0x5A
#define HDA_RIRBCTL 0x5C
#define HDA_RIRBSIZE 0x5E

#define HDA_GCTL_CRST 1u

#define HDA_VERB_GET_PARAMETER 0xF00u
#define HDA_VERB_SET_STREAM_FORMAT 0x200u
#define HDA_VERB_SET_AMP 0x300u
#define HDA_VERB_SET_STREAM_ID 0x706u
#define HDA_VERB_SET_PIN_CONTROL 0x707u

#define WIDGET_TYPE(caps) (((caps) >> 20) & 0xF)
#define WIDGET_TYPE_OUTPUT 0
#define WIDGET_TYPE_PIN 4

#define HDA_FORMAT_48K_16_2 0x0011u

struct hda_bdl_entry {
    u64 address;
    u32 length;
    u16 flags;
    u16 sequence;
} __attribute__((packed));

struct hda {
    volatile u8 *regs;
    u64 corb_phys;
    u64 rirb_phys;
    u64 bdl_phys;
    u64 data_phys;
    u16 corb_read;
    u16 rirb_read;
    u8 codec;
    u8 dac;
    u8 pin;
    u8 stream;
    int ready;
};

static struct hda g_hda;

static u16 rd16(u32 offset) { return *(volatile u16 *)(g_hda.regs + offset); }
static u32 rd32(u32 offset) { return *(volatile u32 *)(g_hda.regs + offset); }
static void wr8(u32 offset, u8 value)
{
    *(volatile u8 *)(g_hda.regs + offset) = value;
}
static void wr16(u32 offset, u16 value)
{
    *(volatile u16 *)(g_hda.regs + offset) = value;
}
static void wr32(u32 offset, u32 value)
{
    *(volatile u32 *)(g_hda.regs + offset) = value;
}

/* Send one 32-bit codec verb and wait for its response. */
static int hda_verb(u32 verb, u32 *response)
{
    u16 write = rd16(HDA_CORBWP) & 0xFF;
    u16 read = g_hda.corb_read;
    if (((write + 1) & 0xFF) == read)
        return -1;
    u32 *corb = (u32 *)(uintptr_t)g_hda.corb_phys;
    corb[write] = verb;
    g_hda.corb_read = (u16)((write + 1) & 0xFF);
    wr16(HDA_CORBWP, g_hda.corb_read);

    u64 *rirb = (u64 *)(uintptr_t)g_hda.rirb_phys;
    for (u32 spin = 0; spin < 1000000u; spin++) {
        u16 pointer = rd16(HDA_RIRBWP) & 0xFF;
        if (pointer == g_hda.rirb_read)
            continue;
        u32 value = (u32)rirb[g_hda.rirb_read];
        g_hda.rirb_read = (u16)((g_hda.rirb_read + 1) & 0xFF);
        if (response)
            *response = value;
        return 0;
    }
    return -1;
}

static u32 hda_get_parameter(u8 codec, u8 node, u8 parameter)
{
    u32 response = 0;
    u32 verb = ((u32)codec << 28) | (HDA_VERB_GET_PARAMETER << 16) |
               ((u32)parameter << 8) | node;
    if (hda_verb(verb, &response) != 0)
        return 0xFFFFFFFFu;
    return response;
}

/* Find an audio output converter and an output-capable pin complex. */
static int hda_configure_path(void)
{
    u8 codec = g_hda.codec;
    u32 root = hda_get_parameter(codec, 0, 0x04);
    if (root == 0xFFFFFFFFu)
        return -1;
    u8 start = (u8)((root >> 16) & 0xFF);
    u8 count = (u8)(root & 0xFF);
    if (!count)
        return -1;

    u8 group = 0;
    for (u8 node = start; node < (u8)(start + count); node++) {
        u32 type = hda_get_parameter(codec, node, 0x05) & 0xFF;
        if (type == 1) { /* audio function group */
            group = node;
            break;
        }
    }
    if (!group)
        return -1;

    u32 group_nodes = hda_get_parameter(codec, group, 0x04);
    u8 node_start = (u8)((group_nodes >> 16) & 0xFF);
    u8 node_count = (u8)(group_nodes & 0xFF);
    for (u8 node = node_start; node < (u8)(node_start + node_count);
         node++) {
        u32 capabilities = hda_get_parameter(codec, node, 0x04);
        if (capabilities == 0xFFFFFFFFu)
            continue;
        u32 type = WIDGET_TYPE(capabilities);
        if (type == WIDGET_TYPE_OUTPUT && !g_hda.dac)
            g_hda.dac = node;
        else if (type == WIDGET_TYPE_PIN && !g_hda.pin) {
            u32 config = hda_get_parameter(codec, node, 0x0C);
            u32 device = (config >> 20) & 0xF;
            if (device != 0)
                g_hda.pin = node;
        }
    }
    return (g_hda.dac && g_hda.pin) ? 0 : -1;
}

/* Configure the pins, converter and stream descriptor for playback. */
static int hda_prepare_stream(void)
{
    u8 codec = g_hda.codec;
    /* Unmute the output amplifier at maximum gain. */
    hda_verb(((u32)codec << 28) | (HDA_VERB_SET_AMP << 16) |
             ((u32)g_hda.dac << 8), NULL);
    /* Enable the pin output and its EAPD. */
    hda_verb(((u32)codec << 28) | (HDA_VERB_SET_PIN_CONTROL << 16) |
             (0x40u << 8) | g_hda.pin, NULL);
    /* Long-form SET_CONVERTER_FORMAT: command 0x2, 16-bit payload. */
    hda_verb(((u32)codec << 28) | (0x2u << 24) |
             ((u32)HDA_FORMAT_48K_16_2 << 8) | g_hda.dac, NULL);
    /* Route stream tag 1 to the DAC. */
    hda_verb(((u32)codec << 28) | (HDA_VERB_SET_STREAM_ID << 16) |
             (1u << 8) | g_hda.dac, NULL);

    u32 base = 0x80 + (u32)g_hda.stream * 0x20;
    wr8(base + 0x00, 0);
    wr32(base + 0x08, 4096);
    wr16(base + 0x0C, 0);
    wr16(base + 0x12, HDA_FORMAT_48K_16_2);
    wr32(base + 0x18, (u32)g_hda.bdl_phys);
    wr32(base + 0x1C, (u32)(g_hda.bdl_phys >> 32));
    wr32(base + 0x00, (1u << 1) | (1u << 4));
    return 0;
}

int hda_init(void)
{
    u64 bar = 0;
    for (u32 bus = 0; bus < 256 && !bar; bus++) {
        for (u32 slot = 0; slot < 32 && !bar; slot++) {
            u32 id = pci_config_read32(bus, slot, 0, 0);
            if (id == 0xFFFFFFFFu || id == 0)
                continue;
            u32 class = pci_config_read32(bus, slot, 0, 8) >> 8;
            if (class != 0x040300u)
                continue;
            u32 low = pci_config_read32(bus, slot, 0, 0x10);
            if (low & 1)
                continue;
            bar = low & 0xFFFFFFF0u;
            if (!bar || bar >= 0x100000000ull) {
                bar = 0;
                continue;
            }
            u32 command = pci_config_read32(bus, slot, 0, 4);
            pci_config_write32(bus, slot, 0, 4, command | 0x6);
        }
    }
    if (!bar)
        return -1;

    g_hda.regs = (volatile u8 *)(uintptr_t)bar;
    wr32(HDA_GCTL, 0);
    for (u32 spin = 0; spin < 1000000u; spin++)
        if ((rd32(HDA_GCTL) & HDA_GCTL_CRST) == 0)
            break;
    wr32(HDA_GCTL, HDA_GCTL_CRST);
    for (u32 spin = 0; spin < 1000000u; spin++)
        if (rd32(HDA_GCTL) & HDA_GCTL_CRST)
            break;

    u16 states = rd16(HDA_STATESTS);
    if (!states) {
        console_write("[hda ] no codec detected\n");
        return -1;
    }
    for (u8 codec = 0; codec < 15; codec++) {
        if (states & (1u << codec)) {
            g_hda.codec = codec;
            break;
        }
    }

    g_hda.corb_phys = pmm_alloc_page();
    g_hda.rirb_phys = pmm_alloc_page();
    g_hda.bdl_phys = pmm_alloc_page();
    g_hda.data_phys = pmm_alloc_page();
    if (!g_hda.corb_phys || !g_hda.rirb_phys || !g_hda.bdl_phys ||
        !g_hda.data_phys)
        return -1;
    buffer_zero((void *)(uintptr_t)g_hda.corb_phys, 4096);
    buffer_zero((void *)(uintptr_t)g_hda.rirb_phys, 4096);
    buffer_zero((void *)(uintptr_t)g_hda.bdl_phys, 4096);
    buffer_zero((void *)(uintptr_t)g_hda.data_phys, 4096);

    wr32(HDA_CORBLBASE, (u32)g_hda.corb_phys);
    wr32(HDA_CORBUBASE, (u32)(g_hda.corb_phys >> 32));
    wr32(HDA_RIRBLBASE, (u32)g_hda.rirb_phys);
    wr32(HDA_RIRBUBASE, (u32)(g_hda.rirb_phys >> 32));
    wr8(HDA_CORBSIZE, 2); /* 256 entries */
    wr8(HDA_RIRBSIZE, 2);
    wr16(HDA_RINTCNT, 1);
    wr16(HDA_CORBWP, 0);
    wr16(HDA_CORBRP, 0x8000);
    for (u32 spin = 0; spin < 1000000u; spin++)
        if (rd16(HDA_CORBRP) & 0x8000)
            break;
    wr16(HDA_CORBRP, 0);
    wr16(HDA_RIRBWP, 0x8000); /* reset the read pointer */
    wr8(HDA_CORBCTL, 1);
    wr8(HDA_RIRBCTL, 1);

    if (hda_configure_path() != 0) {
        console_write("[hda ] no usable output path\n");
        return -1;
    }

    struct hda_bdl_entry *bdl =
        (struct hda_bdl_entry *)(uintptr_t)g_hda.bdl_phys;
    bdl[0].address = g_hda.data_phys;
    bdl[0].length = 4096;
    bdl[0].flags = 1;
    bdl[0].sequence = 0;
    if (hda_prepare_stream() != 0)
        return -1;

    g_hda.stream = 0;
    g_hda.ready = 1;
    console_write("[hda ] output stream configured\n");
    return 0;
}

/* Queue one 4 KiB buffer of signed 16-bit stereo PCM. */
int hda_play(const u16 *samples, u32 sample_count)
{
    if (!g_hda.ready || !samples)
        return -1;
    if (sample_count > 1024)
        sample_count = 1024;
    u16 *buffer = (u16 *)(uintptr_t)g_hda.data_phys;
    for (u32 i = 0; i < sample_count * 2; i++)
        buffer[i] = samples[i];

    struct hda_bdl_entry *bdl =
        (struct hda_bdl_entry *)(uintptr_t)g_hda.bdl_phys;
    bdl[0].length = sample_count * 4;
    bdl[0].flags = 1;
    u32 base = 0x80 + (u32)g_hda.stream * 0x20;
    wr32(base + 0x08, bdl[0].length);
    wr16(base + 0x0C, 0);
    wr32(base + 0x00, (1u << 1) | (1u << 4));
    return 0;
}

int hda_present(void)
{
    return g_hda.ready;
}
