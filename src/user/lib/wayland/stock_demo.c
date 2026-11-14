/*
 * Zhaoxu OS - stock libwayland client regression.
 *
 * Uses the upstream wl_display/wl_registry/wl_compositor/wl_shm objects and
 * protocol marshalling code, not the hand-written wire helpers.  It connects
 * to /bin/wayland, binds the globals, commits one 320x240 shm frame and
 * disconnects.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ffi.h>

#include "wayland-client.h"
#include "wayland-client-protocol.h"
#include "userlib.h"

#define WIDTH 320u
#define HEIGHT 240u
#define STRIDE (WIDTH * 4u)
#define POOL_BYTES (STRIDE * HEIGHT)

static struct wl_compositor *g_compositor;
static struct wl_shm *g_shm;

static int ffi_add(int a, int b)
{
    return a + b;
}

static int ffi_self_test(void)
{
    ffi_cif cif;
    ffi_type *types[2] = { &ffi_type_sint32, &ffi_type_sint32 };
    int a = 20, b = 22, result = 0;
    void *values[2] = { &a, &b };
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_sint32, types) !=
        FFI_OK)
        return -1;
    ffi_call(&cif, FFI_FN(ffi_add), &result, values);
    return result;
}

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    (void)data;
    (void)version;
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        g_compositor = wl_registry_bind(registry, name,
                                        &wl_compositor_interface, 1);
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        g_shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener g_registry_listener = {
    registry_global,
    registry_global_remove,
};

int user_main(long *stack)
{
    (void)stack;

    int ffi_result = ffi_self_test();
    u_puts("[wlstock] ffi ");
    u_putn(ffi_result);
    u_puts("\n");
    if (ffi_result != 42)
        return 1;

    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        u_puts("[wlstock] connect failed\n");
        return 1;
    }
    u_puts("[wlstock] connected\n");

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &g_registry_listener, NULL);
    u_puts("[wlstock] registry requested\n");
    if (wl_display_roundtrip(display) < 0) {
        u_puts("[wlstock] registry roundtrip failed\n");
        wl_display_disconnect(display);
        return 1;
    }
    if (!g_compositor || !g_shm) {
        u_puts("[wlstock] missing compositor or shm global\n");
        wl_display_disconnect(display);
        return 1;
    }
    u_puts("[wlstock] globals bound\n");

    long fd = sys_memfd_create("wlstock", 0);
    if (fd < 0 || sys_ftruncate(fd, POOL_BYTES) != 0) {
        u_puts("[wlstock] memfd failed\n");
        wl_display_disconnect(display);
        return 1;
    }
    struct zos_mmap_args mapping;
    mapping.address = 0;
    mapping.length = POOL_BYTES;
    mapping.protection = 3;
    mapping.flags = 1;
    mapping.fd = fd;
    mapping.offset = 0;
    long mapped = sys_mmap(&mapping);
    if (mapped < 0) {
        u_puts("[wlstock] mmap failed\n");
        wl_display_disconnect(display);
        return 1;
    }
    uint32_t *pixels = (uint32_t *)(uintptr_t)mapped;
    for (uint32_t i = 0; i < WIDTH * HEIGHT; i++)
        pixels[i] = 0xFF3366CCu;

    struct wl_surface *surface = wl_compositor_create_surface(g_compositor);
    struct wl_shm_pool *pool =
        wl_shm_create_pool(g_shm, (int32_t)fd, POOL_BYTES);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(
        pool, 0, WIDTH, HEIGHT, STRIDE, WL_SHM_FORMAT_XRGB8888);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, WIDTH, HEIGHT);
    wl_surface_commit(surface);

    if (wl_display_roundtrip(display) < 0) {
        u_puts("[wlstock] commit roundtrip failed\n");
        wl_display_disconnect(display);
        return 1;
    }

    u_puts("[wlstock] frame committed\n");
    wl_display_disconnect(display);
    return 0;
}
