#ifndef ZHAOXU_IO287_INTERNAL_H
#define ZHAOXU_IO287_INTERNAL_H

#include <io287.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bus backends register their devices through these; callers do not hold
 * the registry lock. */
u32 io287_add_device(const char *name, u32 parent, u8 bus_kind,
                     u32 bus, u32 slot, u32 function,
                     u32 vendor, u32 device, u32 class_code);
void io287_device_prop_u64(u32 id, const char *name, u64 value);
void io287_device_prop_text(u32 id, const char *name, const char *text);

void io287_pci_enumerate(void);

#ifdef __cplusplus
}
#endif

#endif
