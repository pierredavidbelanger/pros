#include "fpu_state.h"

#include "arch/arch.h"
#include "core/memory.h"
#include "errno.h"
#include "mm/heap.h"
#include "proc/task.h"

int arch_fpu_init(struct task *task) {
    struct fpu_state *fpu = kcalloc(1, sizeof(struct fpu_state));
    if (!fpu) return -ENOMEM;
    // zeros unmask every exception, the reset values do not
    fpu->fcw = X86_64_FCW_DEFAULT;
    fpu->mxcsr = X86_64_MXCSR_DEFAULT;
    task->fpu_state = fpu;
    return 0;
}

void arch_fpu_free(struct task *task) {
    if (!task->fpu_state) return;
    kfree(task->fpu_state);
    task->fpu_state = NULL;
}

void arch_fpu_copy(struct task *dst, const struct task *src) {
    memcpy(dst->fpu_state, src->fpu_state, sizeof(struct fpu_state));
}
