#include <cpu.h>
#include <kprintf.h>
#include <string.h>
#include <types.h>

static struct cpu_info info;

static inline void cpuid(u32 leaf, u32 subleaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(subleaf));
}

static void append_feature(char *buffer, usize size, usize *offset, const char *name)
{
    usize index = *offset;
    if (index && index + 1 < size) {
        buffer[index++] = ' ';
        buffer[index] = 0;
    }
    while (*name && index + 1 < size)
        buffer[index++] = *name++;
    buffer[index] = 0;
    *offset = index;
}

void cpu_features_string(char *buffer, usize size)
{
    usize offset = 0;
    if (size == 0)
        return;
    buffer[0] = 0;

    if (info.feature_edx & (1u << 0)) append_feature(buffer, size, &offset, "fpu");
    if (info.feature_edx & (1u << 4)) append_feature(buffer, size, &offset, "tsc");
    if (info.feature_edx & (1u << 5)) append_feature(buffer, size, &offset, "msr");
    if (info.feature_edx & (1u << 6)) append_feature(buffer, size, &offset, "pae");
    if (info.feature_edx & (1u << 9)) append_feature(buffer, size, &offset, "apic");
    if (info.feature_edx & (1u << 15)) append_feature(buffer, size, &offset, "cmov");
    if (info.feature_edx & (1u << 24)) append_feature(buffer, size, &offset, "fxsr");
    if (info.feature_edx & (1u << 25)) append_feature(buffer, size, &offset, "sse");
    if (info.feature_edx & (1u << 26)) append_feature(buffer, size, &offset, "sse2");
    if (info.feature_ecx & (1u << 0)) append_feature(buffer, size, &offset, "sse3");
    if (info.feature_ecx & (1u << 9)) append_feature(buffer, size, &offset, "ssse3");
    if (info.feature_ecx & (1u << 19)) append_feature(buffer, size, &offset, "sse4.1");
    if (info.feature_ecx & (1u << 20)) append_feature(buffer, size, &offset, "sse4.2");
    if (info.feature_ecx & (1u << 26)) append_feature(buffer, size, &offset, "xsave");
    if (info.feature_ecx & (1u << 28)) append_feature(buffer, size, &offset, "avx");
    if (info.feature_ecx & (1u << 31)) append_feature(buffer, size, &offset, "hypervisor");
    if (info.extended_edx & (1u << 20)) append_feature(buffer, size, &offset, "nx");
    if (info.extended_edx & (1u << 26)) append_feature(buffer, size, &offset, "pdpe1gb");
    if (info.extended_edx & (1u << 27)) append_feature(buffer, size, &offset, "rdtscp");
    if (info.extended_edx & (1u << 29)) append_feature(buffer, size, &offset, "lm");
}

u64 cpu_rdtsc(void)
{
    u32 low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}

void cpu_init(void)
{
    u32 a, b, c, d;

    /* SSE2 is used by the stb_truetype-backed font engine. Clear CR0.EM/TS
     * and advertise FXSAVE/XMM exception support before any float code. */
    __asm__ volatile(
        "mov %%cr0, %%rax\n\t"
        "and $~((1 << 2) | (1 << 3)), %%rax\n\t"
        "mov %%rax, %%cr0\n\t"
        "mov %%cr4, %%rax\n\t"
        "or $((1 << 9) | (1 << 10)), %%rax\n\t"
        "mov %%rax, %%cr4"
        ::: "rax", "memory");

    memset(&info, 0, sizeof(info));

    cpuid(0, 0, &a, &b, &c, &d);
    u32 max_leaf = a;
    memcpy(&info.vendor[0], &b, 4);
    memcpy(&info.vendor[4], &d, 4);
    memcpy(&info.vendor[8], &c, 4);
    info.vendor[12] = 0;

    cpuid(1, 0, &a, &b, &c, &d);
    info.stepping = a & 0xF;
    info.model = (a >> 4) & 0xF;
    info.family = (a >> 8) & 0xF;
    u32 ext_model = (a >> 16) & 0xF;
    u32 ext_family = (a >> 20) & 0xFF;
    if (info.family == 0xF)
        info.family += ext_family;
    if (info.family == 0x6 || info.family == 0xF)
        info.model += (ext_model << 4);
    info.feature_ecx = c;
    info.feature_edx = d;
    info.logical_cpus = (b >> 16) & 0xFF;

    cpuid(0x80000000u, 0, &a, &b, &c, &d);
    u32 max_extended = a;
    if (max_extended >= 0x80000004u) {
        u32 *brand = (u32 *)info.brand;
        for (u32 leaf = 0x80000002u; leaf <= 0x80000004u; leaf++) {
            cpuid(leaf, 0, &a, &b, &c, &d);
            *brand++ = a;
            *brand++ = b;
            *brand++ = c;
            *brand++ = d;
        }
        info.brand[48] = 0;
        while (info.brand[0] == ' ')
            memmove(info.brand, info.brand + 1, 48);
    } else {
        strcpy(info.brand, "Unknown x86_64 processor");
    }
    if (max_extended >= 0x80000001u) {
        cpuid(0x80000001u, 0, &a, &b, &c, &d);
        info.extended_edx = d;
    }
    (void)max_leaf;

    char features[256];
    cpu_features_string(features, sizeof(features));
    kprintf("[cpu ] %s\n", info.brand);
    kprintf("[cpu ] %s, family %u model %u stepping %u, %u logical CPU(s)\n",
            info.vendor, info.family, info.model, info.stepping,
            info.logical_cpus ? info.logical_cpus : 1);
    kprintf("[cpu ] %s\n", features);
}

const struct cpu_info *cpu_get_info(void)
{
    return &info;
}
