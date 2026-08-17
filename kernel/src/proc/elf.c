#include "proc/elf.h"

#include "core/kprintf.h"
#include "core/memory.h"
#include "fs/vfs/vfs.h"
#include "mm/pmm.h"
#include "mm/vmm.h"

#define EI_NIDENT 16

// e_ident[] indices
#define EI_MAG0    0
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4

// e_ident[] magic bytes, one per EI_MAG* index
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

// e_ident[EI_CLASS]
#define ELFCLASS64 2

// e_type
#define ET_EXEC 2 // what we load
#define ET_DYN  3 // PIE, rejected explicitly

// e_machine, only these two ever match this kernel
#define EM_X86_64  62
#define EM_AARCH64 183

#define PT_LOAD 1

// p_flags bits
#define PF_X (1u << 0)
#define PF_W (1u << 1)
#define PF_R (1u << 2)

struct elf64_ehdr {
    uint8_t e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

int elf_load(const char *path, struct vmm_context *ctx, struct elf_load_result *out) {
    if (!path || !ctx || !out) return -1;

    struct vfs_node *node = vfs_lookup(path);
    if (!node) return -1;

    struct elf64_ehdr ehdr;
    if (node->ops->read(node, 0, sizeof(struct elf64_ehdr), &ehdr) != sizeof(struct elf64_ehdr)) {
        kprintf("ELF", "%s: too short for an ELF header\n", path);
        return -1;
    }

    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 || ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        kprintf("ELF", "%s: bad magic\n", path);
        return -1;
    }

    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        kprintf("ELF", "%s: not ELFCLASS64\n", path);
        return -1;
    }

    if (ehdr.e_type != ET_EXEC) {
        kprintf("ELF", "%s: e_type %u, want ET_EXEC (PIE/ET_DYN not supported)\n", path, ehdr.e_type);
        return -1;
    }

    // TODO: move somewhere in an arch_ specific function
#ifdef __x86_64__
    uint16_t want_machine = EM_X86_64;
#else
    uint16_t want_machine = EM_AARCH64;
#endif
    if (ehdr.e_machine != want_machine) {
        kprintf("ELF", "%s: wrong e_machine: got %u, want %u\n", path, ehdr.e_machine, want_machine);
        return -1;
    }

    if (ehdr.e_phentsize != sizeof(struct elf64_phdr)) {
        kprintf("ELF", "%s: e_phentsize %u != sizeof(elf64_phdr)\n", path, ehdr.e_phentsize);
        return -1;
    }

    // pass 1: every PT_LOAD segment gets its pages allocated, zeroed, filled
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        struct elf64_phdr phdr;
        uint64_t off = ehdr.e_phoff + (uint64_t) i * ehdr.e_phentsize;
        if (node->ops->read(node, off, sizeof(phdr), &phdr) != sizeof(phdr)) {
            kprintf("ELF", "%s: short read on phdr %u\n", path, i);
            return -1;
        }

        if (phdr.p_type != PT_LOAD) continue;

        uint64_t vaddr_lo = phdr.p_vaddr & ~(PAGE_SIZE - 1);
        uint64_t vaddr_hi = (phdr.p_vaddr + phdr.p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        for (uint64_t page = vaddr_lo; page < vaddr_hi; page += PAGE_SIZE) {
            uint64_t phys = vmm_virt_to_phys(ctx, page);
            if (!phys) {
                // fresh page, alloc + zero it
                phys = pmm_alloc(1);
                if (!phys) {
                    kprintf("ELF", "%s: OOM mapping 0x%016lx\n", path, page);
                    return -1;
                }
                // kprintf("ELF", "phdr[%u] map page 0x%016lx (flags 0b%b)\n", i, phys, VMM_USER | VMM_WRITABLE);
                if (vmm_map_page(ctx, page, phys, VMM_USER | VMM_WRITABLE) != 0) {
                    kprintf("ELF", "%s: vmm_map_page failed at 0x%016lx\n", path, page);
                    return -1;
                }
                memset(pmm_phys_to_virt(phys), 0, PAGE_SIZE);
            }
            // overlap between this page and the segment range [p_vaddr, p_vaddr+p_filesz)
            uint64_t seg_file_end = phdr.p_vaddr + phdr.p_filesz;
            uint64_t start = page > phdr.p_vaddr ? page : phdr.p_vaddr;
            uint64_t end = (page + PAGE_SIZE) < seg_file_end ? (page + PAGE_SIZE) : seg_file_end;
            if (end > start) {
                uint64_t file_off = phdr.p_offset + (start - phdr.p_vaddr);
                void *dest = (uint8_t *) pmm_phys_to_virt(phys) + (start - page);
                // kprintf("ELF", "phdr[%u] read at file offset 0x%016lx\n", i, file_off);
                if (node->ops->read(node, file_off, end - start, dest) != (int64_t)(end - start)) {
                    kprintf("ELF", "%s: short read on segment %u\n", path, i);
                    return -1;
                }
            }
        }
    }

    // pass 2: now that every segment are loaded, set real permissions
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        struct elf64_phdr phdr;
        uint64_t off = ehdr.e_phoff + (uint64_t) i * ehdr.e_phentsize;
        // we already know its readable from pass 1
        node->ops->read(node, off, sizeof(phdr), &phdr);

        if (phdr.p_type != PT_LOAD) continue;

        uint64_t vaddr_lo = phdr.p_vaddr & ~(PAGE_SIZE - 1);
        uint64_t vaddr_hi = (phdr.p_vaddr + phdr.p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        uint64_t flags = VMM_USER;
        if (phdr.p_flags & PF_W) flags |= VMM_WRITABLE;
        if (!(phdr.p_flags & PF_X)) flags |= VMM_NO_EXECUTE;

        for (uint64_t page = vaddr_lo; page < vaddr_hi; page += PAGE_SIZE) {
            uint64_t phys = vmm_virt_to_phys(ctx, page);
            // kprintf("ELF", "phdr[%u] map page 0x%016lx (flags 0b%b)\n", i, phys, flags);
            vmm_map_page(ctx, page, phys, flags);
        }
    }

    out->entry = ehdr.e_entry;
    return 0;
}

uint64_t elf_build_user_stack(struct vmm_context *ctx, uint64_t stack_top, int argc, char *const argv[], char *const envp[]) {
    int envc = 0;
    while (envp && envp[envc]) envc++;

    uint64_t phys = pmm_alloc(1);
    if (!phys) return 0;

    uint8_t *base = pmm_phys_to_virt(phys);
    memset(base, 0, PAGE_SIZE);

    uint64_t page_vaddr = stack_top - PAGE_SIZE;

    // offset within the page; everything builds downward from here
    uint64_t cursor = PAGE_SIZE;

    // 1) string bytes, remember each one's resulting *user* vaddr for the pointer tables below
    // TODO: grows on the stack, maybe switch to kmalloc/kfree someday
    uint64_t argv_vaddrs[argc > 0 ? argc : 1];
    uint64_t envp_vaddrs[envc > 0 ? envc : 1];

    for (int i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]) + 1;
        cursor -= len;
        memcpy(base + cursor, argv[i], len);
        argv_vaddrs[i] = page_vaddr + cursor;
    }
    for (int i = 0; i < envc; i++) {
        size_t len = strlen(envp[i]) + 1;
        cursor -= len;
        memcpy(base + cursor, envp[i], len);
        envp_vaddrs[i] = page_vaddr + cursor;
    }
    // 8-byte align the boundary between strings and the pointer table
    cursor &= ~7ULL;

    // slots from argc through AT_NULL's two words, inclusive
    uint64_t slots = 1 + (uint64_t)(argc + 1) + (uint64_t)(envc + 1) + 2;
    // pad here, not after, so argc lands 16-aligned by construction
    if ((cursor - slots * 8) % 16 != 0) cursor -= 8;

    // 2) AT_NULL — already zero from the memset, just move
    cursor -= 16;

    // 3) envp[] pointers, high to low, NULL terminator first since we write top-down
    // NULL terminator (already zero), just move
    cursor -= 8;
    for (int i = envc - 1; i >= 0; i--) {
        cursor -= 8;
        *(uint64_t *)(base + cursor) = envp_vaddrs[i];
    }

    // 4) argv[] pointers, same shape
    // NULL terminator, just move
    cursor -= 8;
    for (int i = argc - 1; i >= 0; i--) {
        cursor -= 8;
        *(uint64_t *)(base + cursor) = argv_vaddrs[i];
    }

    // 5) argc
    cursor -= 8;
    *(uint64_t *)(base + cursor) = (uint64_t) argc;

    // map this page
    if (vmm_map_page(ctx, page_vaddr, phys, VMM_USER | VMM_WRITABLE | VMM_NO_EXECUTE) != 0) {
        return 0;
    }

    return page_vaddr + cursor;
}
