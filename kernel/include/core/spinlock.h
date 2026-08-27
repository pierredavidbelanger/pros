#ifndef PROS_SPINLOCK_H
#define PROS_SPINLOCK_H

#include "stdc.h"

struct spinlock {
    int locked;
};

#define SPINLOCK_INIT {0}

void spinlock_lock(struct spinlock *lock);
void spinlock_unlock(struct spinlock *lock);

// mask irqs then take the lock, hand the returned flags back to unlock
uint64_t spinlock_lock_irqsave(struct spinlock *lock);
void spinlock_unlock_irqrestore(struct spinlock *lock, uint64_t flags);

#endif  // PROS_SPINLOCK_H
