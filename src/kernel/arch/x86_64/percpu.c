#include <percpu.h>
#include <types.h>

#define LAPIC_ID_REGISTER 0xFEE00020ULL

struct percpu g_percpu[MAX_CPUS];
u32 g_cpu_count = 1;

static struct percpu *g_by_apic[256];
static volatile u32 g_lapic_lookup;

static u32 hardware_apic_id(void)
{
    return (*(volatile u32 *)(uintptr_t)LAPIC_ID_REGISTER) >> 24;
}

void percpu_init_bsp(u32 apic_id)
{
    g_percpu[0].index = 0;
    g_percpu[0].apic_id = apic_id & 0xFF;
    g_by_apic[apic_id & 0xFF] = &g_percpu[0];
    g_cpu_count = 1;
    __atomic_store_n(&g_lapic_lookup, 1u, __ATOMIC_RELEASE);
}

void percpu_register(u32 index, u32 apic_id)
{
    if (index >= MAX_CPUS)
        return;
    g_percpu[index].index = index;
    g_percpu[index].apic_id = apic_id & 0xFF;
    g_by_apic[apic_id & 0xFF] = &g_percpu[index];
    if (index + 1 > g_cpu_count)
        g_cpu_count = index + 1;
}

struct percpu *percpu_this(void)
{
    if (!__atomic_load_n(&g_lapic_lookup, __ATOMIC_RELAXED))
        return &g_percpu[0];

    u32 id = hardware_apic_id() & 0xFF;
    struct percpu *cpu = g_by_apic[id];
    return cpu ? cpu : &g_percpu[0];
}

u32 cpu_index(void)
{
    return percpu_this()->index;
}
