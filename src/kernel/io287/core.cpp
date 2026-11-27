/*
 * Zhaoxu OS - IO/287 registry, device tree and driver matching.
 *
 * The registry is a flat array with parent/first-child/next-sibling links so
 * a device tree walk never dereferences an allocation.  Mutations take the
 * registry spinlock with interrupts masked; driver probe/attach callbacks run
 * outside the lock, which is why they must not sleep either.
 */
#include <io287.h>
#include <spinlock.h>
#include <types.h>

#include "internal.h"

static struct io287_device g_devices[IO287_MAX_DEVICES];
static const struct io287_driver *g_drivers[IO287_MAX_DRIVERS];
static u32 g_device_count;
static u32 g_driver_count;
static u32 g_initialized;
static spinlock_t g_lock = SPINLOCK_INIT;

/* Bounded string copy; dst is always NUL terminated. */
static void copy_text(char *dst, usize capacity, const char *src)
{
    usize i = 0;
    if (!capacity)
        return;
    while (src && src[i] && i + 1 < capacity) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* Compare two NUL-terminated strings without libc. */
static int text_equal(const char *a, const char *b)
{
    if (!a || !b)
        return a == b;
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/* Return the named property, or NULL.  The caller holds g_lock. */
static struct io287_prop *find_prop(struct io287_device *dev,
                                    const char *name)
{
    for (u32 i = 0; i < IO287_MAX_PROPS; i++)
        if (dev->props[i].used && text_equal(dev->props[i].name, name))
            return &dev->props[i];
    return 0;
}

/* Return the named property, creating an empty slot when absent. */
static struct io287_prop *create_prop(struct io287_device *dev,
                                      const char *name)
{
    struct io287_prop *prop = find_prop(dev, name);
    if (prop)
        return prop;
    for (u32 i = 0; i < IO287_MAX_PROPS; i++) {
        if (dev->props[i].used)
            continue;
        dev->props[i].used = 1;
        dev->props[i].type = 0;
        dev->props[i].number = 0;
        dev->props[i].text[0] = 0;
        copy_text(dev->props[i].name, sizeof(dev->props[i].name), name);
        dev->prop_count++;
        return &dev->props[i];
    }
    return 0;
}

u32 io287_add_device(const char *name, u32 parent, u8 bus_kind,
                     u32 bus, u32 slot, u32 function,
                     u32 vendor, u32 device, u32 class_code)
{
    u64 flags = spin_lock_irqsave(&g_lock);
    if (g_device_count >= IO287_MAX_DEVICES || parent >= g_device_count ||
        !g_devices[parent].used) {
        spin_unlock_irqrestore(&g_lock, flags);
        return IO287_NONE;
    }

    u32 id = g_device_count;
    struct io287_device *dev = &g_devices[id];
    dev->used = 1;
    dev->id = id;
    dev->parent = parent;
    dev->first_child = IO287_NONE;
    dev->next_sibling = IO287_NONE;
    dev->refs = 1;
    dev->driver = IO287_NONE;
    dev->bus = bus;
    dev->slot = slot;
    dev->function = function;
    dev->vendor = vendor;
    dev->device = device;
    dev->class_code = class_code;
    dev->prop_count = 0;
    dev->bus_kind = bus_kind;
    dev->matched = 0;
    copy_text(dev->name, sizeof(dev->name), name);

    struct io287_device *owner = &g_devices[parent];
    if (owner->first_child == IO287_NONE) {
        owner->first_child = id;
    } else {
        u32 last = owner->first_child;
        while (g_devices[last].next_sibling != IO287_NONE)
            last = g_devices[last].next_sibling;
        g_devices[last].next_sibling = id;
    }

    g_device_count = id + 1;
    spin_unlock_irqrestore(&g_lock, flags);
    return id;
}

void io287_device_prop_u64(u32 id, const char *name, u64 value)
{
    u64 flags = spin_lock_irqsave(&g_lock);
    if (id < g_device_count) {
        struct io287_prop *prop = create_prop(&g_devices[id], name);
        if (prop) {
            prop->type = IO287_PROP_U64;
            prop->number = value;
        }
    }
    spin_unlock_irqrestore(&g_lock, flags);
}

void io287_device_prop_text(u32 id, const char *name, const char *text)
{
    u64 flags = spin_lock_irqsave(&g_lock);
    if (id < g_device_count) {
        struct io287_prop *prop = create_prop(&g_devices[id], name);
        if (prop) {
            prop->type = IO287_PROP_TEXT;
            copy_text(prop->text, sizeof(prop->text), text);
        }
    }
    spin_unlock_irqrestore(&g_lock, flags);
}

int io287_init(void)
{
    u64 flags = spin_lock_irqsave(&g_lock);
    for (u32 i = 0; i < IO287_MAX_DEVICES; i++) {
        g_devices[i].used = 0;
        g_devices[i].first_child = IO287_NONE;
        g_devices[i].next_sibling = IO287_NONE;
        g_devices[i].driver = IO287_NONE;
        g_devices[i].prop_count = 0;
        for (u32 p = 0; p < IO287_MAX_PROPS; p++)
            g_devices[i].props[p].used = 0;
    }
    g_device_count = 1;
    g_driver_count = 0;
    g_initialized = 1;

    struct io287_device *root = &g_devices[0];
    root->used = 1;
    root->id = 0;
    root->parent = IO287_NONE;
    root->first_child = IO287_NONE;
    root->next_sibling = IO287_NONE;
    root->refs = 1;
    root->driver = IO287_NONE;
    root->bus = 0;
    root->slot = 0;
    root->function = 0;
    root->vendor = 0;
    root->device = 0;
    root->class_code = 0;
    root->prop_count = 0;
    root->bus_kind = IO287_BUS_ROOT;
    root->matched = 0;
    copy_text(root->name, sizeof(root->name), "io287");
    spin_unlock_irqrestore(&g_lock, flags);

    io287_pci_enumerate();
    return 0;
}

u32 io287_device_count(void)
{
    return g_device_count;
}

struct io287_device *io287_device_at(u32 index)
{
    if (index >= g_device_count || !g_devices[index].used)
        return 0;
    return &g_devices[index];
}

struct io287_device *io287_root(void)
{
    if (!g_initialized)
        return 0;
    return &g_devices[0];
}

int io287_get_u64(const struct io287_device *dev, const char *name,
                  u64 *value)
{
    int found = -1;
    if (!dev || !value)
        return -1;
    u64 flags = spin_lock_irqsave(&g_lock);
    struct io287_prop *prop = find_prop((struct io287_device *)dev, name);
    if (prop && prop->type == IO287_PROP_U64) {
        *value = prop->number;
        found = 0;
    }
    spin_unlock_irqrestore(&g_lock, flags);
    return found;
}

const char *io287_get_text(const struct io287_device *dev, const char *name)
{
    const char *text = 0;
    if (!dev || dev->id >= g_device_count)
        return 0;
    u64 flags = spin_lock_irqsave(&g_lock);
    struct io287_prop *prop = find_prop((struct io287_device *)dev, name);
    if (prop && prop->type == IO287_PROP_TEXT)
        text = prop->text;
    spin_unlock_irqrestore(&g_lock, flags);
    return text;
}

int io287_register_driver(const struct io287_driver *driver)
{
    if (!driver)
        return -1;
    u64 flags = spin_lock_irqsave(&g_lock);
    if (g_driver_count >= IO287_MAX_DRIVERS) {
        spin_unlock_irqrestore(&g_lock, flags);
        return -1;
    }
    u32 index = g_driver_count++;
    g_drivers[index] = driver;
    spin_unlock_irqrestore(&g_lock, flags);
    return (int)index;
}

u32 io287_driver_count(void)
{
    return g_driver_count;
}

/* Class/vendor/device match; the class mask selects which class bits count. */
static int match_device(const struct io287_device *dev,
                        const struct io287_match *match)
{
    if (match->class_mask) {
        if ((dev->class_code & match->class_mask) != match->class_value)
            return 0;
    } else if (match->class_value && dev->class_code != match->class_value) {
        return 0;
    }
    if (match->vendor != IO287_ANY_ID && dev->vendor != match->vendor)
        return 0;
    if (match->device != IO287_ANY_ID && dev->device != match->device)
        return 0;
    return 1;
}

int io287_probe_all(void)
{
    int attached = 0;
    for (u32 i = 1; i < g_device_count; i++) {
        u64 flags = spin_lock_irqsave(&g_lock);
        struct io287_device *dev = &g_devices[i];
        if (!dev->used || dev->matched || dev->driver != IO287_NONE) {
            spin_unlock_irqrestore(&g_lock, flags);
            continue;
        }
        u32 chosen = IO287_NONE;
        for (u32 d = 0; d < g_driver_count; d++) {
            if (match_device(dev, &g_drivers[d]->match)) {
                chosen = d;
                break;
            }
        }
        spin_unlock_irqrestore(&g_lock, flags);
        if (chosen == IO287_NONE)
            continue;

        const struct io287_driver *driver = g_drivers[chosen];
        if (driver->probe && driver->probe(dev) != 0)
            continue;
        if (driver->attach && driver->attach(dev) != 0)
            continue;

        flags = spin_lock_irqsave(&g_lock);
        dev->driver = chosen;
        dev->matched = 1;
        dev->refs++;
        spin_unlock_irqrestore(&g_lock, flags);
        attached++;
    }
    return attached;
}

/* A class-6 host bridge is always present on x86 firmware. */
static int selftest_probe(struct io287_device *dev)
{
    return dev && dev->bus_kind == IO287_BUS_PCI ? 0 : -1;
}

static int selftest_attach(struct io287_device *dev)
{
    return dev ? 0 : -1;
}

int io287_selftest(void)
{
    static const struct io287_driver selftest = {
        "io287-selftest",
        { 0xFFFF00u, 0x060000u, IO287_ANY_ID, IO287_ANY_ID },
        selftest_probe, selftest_attach, 0
    };
    static u32 registered;

    if (!g_initialized)
        return -1;
    if (g_device_count < 2)
        return -2;
    if (g_devices[0].parent != IO287_NONE)
        return -3;

    for (u32 i = 1; i < g_device_count; i++) {
        if (!g_devices[i].used)
            return -4;
        if (g_devices[i].parent >= g_device_count ||
            g_devices[i].parent == i)
            return -5;
        u32 child = g_devices[i].first_child;
        u32 guard = 0;
        while (child != IO287_NONE) {
            if (child >= g_device_count || ++guard > IO287_MAX_DEVICES)
                return -6;
            if (g_devices[child].parent != i)
                return -7;
            child = g_devices[child].next_sibling;
        }
    }

    if (!registered) {
        if (io287_register_driver(&selftest) < 0)
            return -8;
        registered = 1;
    }
    if (io287_probe_all() <= 0)
        return -9;
    return 0;
}
