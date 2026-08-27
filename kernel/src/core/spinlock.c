#include "core/spinlock.h"

#include "arch/arch.h"
#include "stdc.h"

void spinlock_lock(struct spinlock *lock) {
    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE)) {
        arch_cpu_relax();
    }
}

void spinlock_unlock(struct spinlock *lock) {
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
}

uint64_t spinlock_lock_irqsave(struct spinlock *lock) {
    // mask before we acquire, the other order deadlocks against our own isr
    uint64_t flags = arch_irq_save();
    spinlock_lock(lock);
    return flags;
}

void spinlock_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
    spinlock_unlock(lock);
    arch_irq_restore(flags);
}
