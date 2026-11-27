#ifndef ZHAOXU_IO287_H
#define ZHAOXU_IO287_H

/*
 * IO/287 - the OS_5P in-kernel device framework.
 *
 * A device tree with a registry, typed properties and driver matching over
 * a class/vendor/device descriptor.  The first slice enumerates the PCI
 * bus through mechanism #1; the personality and kexts attach drivers with
 * io287_register_driver() and io287_probe_all().
 */

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IO287_NONE 0xFFFFFFFFu
#define IO287_ANY_ID 0xFFFFFFFFu

#define IO287_MAX_DEVICES 128
#define IO287_MAX_DRIVERS 32
#define IO287_NAME_MAX 32
#define IO287_MAX_PROPS 4
#define IO287_TEXT_MAX 32

enum io287_bus_kind {
    IO287_BUS_ROOT = 0,
    IO287_BUS_PCI = 1
};

enum io287_prop_type {
    IO287_PROP_U64 = 1,
    IO287_PROP_TEXT = 2
};

struct io287_prop {
    char name[IO287_NAME_MAX];
    u64 number;
    char text[IO287_TEXT_MAX];
    u8 type;
    u8 used;
};

struct io287_device {
    u32 used;
    u32 id;
    u32 parent;
    u32 first_child;
    u32 next_sibling;
    u32 refs;
    u32 driver;
    u32 bus;
    u32 slot;
    u32 function;
    u32 vendor;
    u32 device;
    u32 class_code;
    u32 prop_count;
    u8 bus_kind;
    u8 matched;
    char name[IO287_NAME_MAX];
    struct io287_prop props[IO287_MAX_PROPS];
};

struct io287_match {
    u32 class_mask;
    u32 class_value;
    u32 vendor;
    u32 device;
};

struct io287_driver {
    const char *name;
    struct io287_match match;
    int (*probe)(struct io287_device *dev);
    int (*attach)(struct io287_device *dev);
    void (*detach)(struct io287_device *dev);
};

int io287_init(void);
u32 io287_device_count(void);
struct io287_device *io287_device_at(u32 index);
struct io287_device *io287_root(void);
int io287_get_u64(const struct io287_device *dev, const char *name, u64 *value);
const char *io287_get_text(const struct io287_device *dev, const char *name);
int io287_register_driver(const struct io287_driver *driver);
u32 io287_driver_count(void);
int io287_probe_all(void);
int io287_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
