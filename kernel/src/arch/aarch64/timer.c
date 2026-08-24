#include "timer.h"

#include "core/kprintf.h"

// CNTV_CTL_EL0 bits. The interrupt output is asserted when ISTATUS is set and IMASK is clear.
#define ARM64_CNTV_CTL_ENABLE (1ULL << 0)
#define ARM64_CNTV_CTL_IMASK (1ULL << 1)
#define ARM64_CNTV_CTL_ISTATUS (1ULL << 2)

// Ticks of CNTFRQ_EL0 per interrupt.
static uint32_t arm_timer_interval;

void arm_timer_init(uint32_t hz) {
    if (hz == 0) {
        kpanic("arm_timer_init: hz must not be zero");
    }

    // the tick rate is not hardcoded, it is whatever the firmware put there.
    uint64_t cntfrq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));

    // writing TVAL sets CVAL = CNTVCT + interval, so interval must fit in a signed 32 bit value
    uint64_t interval = cntfrq / hz;
    if (interval == 0 || interval > 0x7FFFFFFF) {
        kpanic("arm_timer_init: hz outside the virtual timer's usable range");
    }
    arm_timer_interval = (uint32_t)interval;

    kprintf("ARM64", "cntfrq: %u Hz, %u ticks per interrupt\n", (uint32_t)cntfrq, arm_timer_interval);

    arm_timer_rearm();

    // start the counter
    asm volatile("msr cntv_ctl_el0, %0\n\tisb" ::"r"(ARM64_CNTV_CTL_ENABLE));
}

// The Generic Timer is a one-shot, nothing reloads it, so this has to happen on every interrupt or it never fires again.
void arm_timer_rearm(void) {
    asm volatile("msr cntv_tval_el0, %0" ::"r"((uint64_t)arm_timer_interval));
}
