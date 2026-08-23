#include "core/spinlock.h"

#include "arch/arch.h"
#include "stdc.h"

uint64_t spinlock_lock_irqsave(struct spinlock *lock) {
    // mask before we acquire, the other order deadlocks against our own isr
    uint64_t flags = arch_irq_save();
    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE)) {
        arch_cpu_relax();
    }
    return flags;
}

void spinlock_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
    arch_irq_restore(flags);
}
