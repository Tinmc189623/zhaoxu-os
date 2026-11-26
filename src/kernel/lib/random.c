/*
 * Zhaoxu OS - kernel entropy for getrandom() and stack canaries.
 *
 * RDRAND is used when CPUID reports it; every call is mixed with the TSC so a
 * hypervisor that returns a constant still yields moving output.  Without
 * RDRAND the pool falls back to a seeded xorshift.
 */
#include <random.h>
#include <timer.h>
#include <types.h>

#define CPUID_RDRAND_BIT (1u << 30)

static u64 g_pool_state;

/* Read the time-stamp counter. */
static u64 read_tsc(void)
{
    u32 low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((u64)high << 32) | low;
}

/* CPUID leaf 1 ECX bit 30: RDRAND is available. */
static int rdrand_available(void)
{
    u32 eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1), "c"(0));
    return (ecx & CPUID_RDRAND_BIT) != 0;
}

/* Try to read one hardware random word; CF=0 means no entropy this time. */
static int rdrand64(u64 *value)
{
    u8 ok;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(*value), "=qm"(ok));
    return ok != 0;
}

/* Advance the software pool when hardware entropy is unavailable. */
static u64 pool_next(u64 *state)
{
    u64 x = *state ? *state : 0x9e3779b97f4a7c15ULL;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    x ^= read_tsc();
    *state = x;
    return x;
}

void random_bytes(void *buffer, usize length)
{
    u8 *out = (u8 *)buffer;
    int hardware = rdrand_available();

    g_pool_state ^= read_tsc() ^ timer_ticks() ^ (u64)(uintptr_t)buffer;
    if (g_pool_state == 0)
        g_pool_state = 0x2545f4914f6cdd1dULL;

    while (length) {
        u64 word;
        int got = 0;
        if (hardware) {
            for (int attempt = 0; attempt < 10 && !got; attempt++)
                got = rdrand64(&word);
        }
        if (!got)
            word = pool_next(&g_pool_state);

        usize chunk = length < sizeof(word) ? length : sizeof(word);
        for (usize i = 0; i < chunk; i++)
            out[i] = (u8)(word >> (8 * i));
        out += chunk;
        length -= chunk;
    }
}
