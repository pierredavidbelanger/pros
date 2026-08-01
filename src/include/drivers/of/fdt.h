#ifndef PROS_DRIVERS_OF_FDT_H
#define PROS_DRIVERS_OF_FDT_H

#include <stdint.h>
#include <stddef.h>

int fdt_get_memory(const void *fdt_ptr, uintptr_t *out_base, size_t *out_size);
void fdt_dump(const void *fdt_ptr);

#endif // PROS_DRIVERS_OF_FDT_H
