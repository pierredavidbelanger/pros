#ifndef PROS_BOOT_H
#define PROS_BOOT_H

#include <limine.h>

void _start(void);

extern volatile uint64_t limine_base_revision[];

extern volatile struct limine_hhdm_request hhdm_request;
extern volatile struct limine_executable_cmdline_request cmdline_request;
extern volatile struct limine_framebuffer_request framebuffer_request;
extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_paging_mode_request paging_mode_request;
extern volatile struct limine_dtb_request dtb_request;
extern volatile struct limine_rsdp_request rsdp_request;
extern volatile struct limine_stack_size_request stack_size_request;
extern volatile struct limine_entry_point_request entry_point_request;
extern volatile struct limine_executable_address_request executable_address_request;
extern volatile struct limine_module_request module_request;

#endif //PROS_BOOT_H
