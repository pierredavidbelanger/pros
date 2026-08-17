#ifndef PROS_ELF_H
#define PROS_ELF_H

#include "mm/vmm.h"

#include "stdc.h"

struct elf_load_result {
    uint64_t entry;
};

int elf_load(const char *path, struct vmm_context *ctx, struct elf_load_result *out);

uint64_t elf_build_user_stack(struct vmm_context *ctx, uint64_t stack_top, int argc, char *const argv[], char *const envp[]);

#endif //PROS_ELF_H
