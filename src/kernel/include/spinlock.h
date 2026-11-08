#ifndef ZHAOXU_SPINLOCK_H
#define ZHAOXU_SPINLOCK_H

#include <types.h>

typedef struct {
    volatile u32 lock;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

static inline void cpu_relax(void)
{
    __asm__ volatile("pause");
}

static inline u64 irq_save(void)
{
    u64 flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(u64 flags)
{
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory");
}

static inline void spin_lock(spinlock_t *lock)
{
    while (__atomic_exchange_n(&lock->lock, 1u, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock->lock, __ATOMIC_RELAXED))
            cpu_relax();
    }
}

static inline void spin_unlock(spinlock_t *lock)
{
    __atomic_store_n(&lock->lock, 0u, __ATOMIC_RELEASE);
}

static inline u64 spin_lock_irqsave(spinlock_t *lock)
{
    u64 flags = irq_save();
    spin_lock(lock);
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, u64 flags)
{
    spin_unlock(lock);
    irq_restore(flags);
}

#endif
