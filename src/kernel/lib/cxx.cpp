/*
 * C++ runtime glue for the Yuanshuo kernel.
 * The kernel is built freestanding and without exceptions, so only the
 * allocation hooks and the pure-virtual trap are required.
 */
extern "C" {
#include <io.h>
#include <mem.h>
#include <types.h>
}

void *operator new(usize size)
{
    return kmalloc(size);
}

void *operator new[](usize size)
{
    return kmalloc(size);
}

void operator delete(void *pointer) noexcept
{
    kfree(pointer);
}

void operator delete[](void *pointer) noexcept
{
    kfree(pointer);
}

void operator delete(void *pointer, usize) noexcept
{
    kfree(pointer);
}

void operator delete[](void *pointer, usize) noexcept
{
    kfree(pointer);
}

extern "C" void __cxa_pure_virtual(void)
{
    for (;;)
        hlt();
}
