# Working Document: Phase 2 Step 13 — TAR/Initramfs Filesystem Driver [STATUS: READ PATH WORKING ✅, WRITE STILL OPEN 🚧]

This working document expands `PHASE2_VFS.md` Step 13 into a full design — how the archive gets loaded, how the driver is structured, and specifically how read *and* write work on a filesystem that's explicitly RAM-only and non-persistent by design.

> [!NOTE]
> **Status**: Parts 1-3 are implemented and verified end-to-end at boot — `module_request` loads the archive, `tar_mount()` builds a real `vfs_node` tree (first-child/next-sibling, see `kernel/fs/tar/tar.c`), and `finddir`/`readdir`/`read` all work (`ls /` and `cat` against the mounted archive both confirmed working). Part 4 (write support / copy-on-write promotion into heap memory) is still just a design below, not yet built.

> [!NOTE]
> Mentor-mode reminder (`.rules.md`): this is a design reference to code from by hand, not code to paste in.

---

## 🏗️ Part 1: How the archive gets into the kernel at all

Confirmed by reading the vendored `kernel/libs/limine-protocol/include/limine.h` + `PROTOCOL.md`:

```c
struct limine_file {
    uint64_t revision;
    void *address;   // where Limine loaded this module in physical memory
    uint64_t size;    // its size in bytes
    char *path;       // identifying path
    char *string;     // identifying cmdline/string
    ... (media/partition fields, irrelevant here)
};

struct limine_module_response {
    uint64_t revision;
    uint64_t module_count;
    struct limine_file **modules;   // array of pointers, one per loaded module
};

struct limine_module_request {
    uint64_t id[4];
    uint64_t revision;
    struct limine_module_response *response;
    uint64_t internal_module_count;      // revision >= 1 only
    struct limine_internal_module **internal_modules;
};
```

Two ways to get a module loaded — **recommended: config-declared**, matching how this project already does everything else (`path:`/`cmdline:` are already `limine.conf` directives, not embedded in the kernel binary):

- **Config-declared (recommended)**: add a module directive to each entry in `limine.conf` (Limine's own config syntax — not documented in this repo's vendored `PROTOCOL.md`, which only covers the wire structs, so **verify the exact directive name against the actual `bin/limine-binary` tool's docs when implementing** — the well-known Limine convention is `module_path:`, but confirm rather than assume). With this approach the kernel-side request can stay at `revision = 0` and just reads whatever Limine hands back in `module_response.modules[0]` — no path logic in the kernel at all, same simplicity as `cmdline_request`.
- **Kernel-declared (`internal_modules`)**: fully self-contained in `boot.c`, no `limine.conf` change needed, but requires `revision = 1`, an embedded path *relative to the kernel executable's own location*, and staging the tar file at that exact relative path under `root/` in the Makefile. More coupling, only worth it if the config directive turns out to be awkward.

Either way, registration in `boot.c`/`boot.h` follows the exact existing pattern (`paging_mode_request`, `cmdline_request`):
```c
// boot.c
volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
};
// boot.h
extern volatile struct limine_module_request module_request;
```

---

## 🏗️ Part 2: The TAR (ustar) format — the shape of the bytes

A `.tar` file is just a flat sequence of 512-byte blocks:

```
[512-byte header][file data, padded to a multiple of 512][512-byte header][data]...[two 512-byte zero blocks = end]
```

Each header (fixed-offset ASCII text fields, this is the whole format) has, among others:
- `name` — path relative to archive root, e.g. `"etc/passwd"`, or `"etc/"` (trailing slash) for a directory
- `size` — file size, as **octal digits in ASCII text**, not binary (e.g. the bytes `"00000000173\0"` mean 0173 octal = 123 decimal)
- `typeflag` — one byte: `'0'` or `'\0'` = regular file, `'5'` = directory

That's genuinely most of what's needed to read every entry out of the archive.

---

## 🏗️ Part 3: Parse once at mount time, not on every access

The natural design — and the one that sets up cleanly for write support in Part 4 — is: walk the whole archive **once**, when it's mounted, and build a real in-memory tree of `struct vfs_node`s (same struct every other filesystem in this codebase uses), rather than re-scanning the raw 512-byte blocks on every `finddir()`/`readdir()` call.

- For each header found: split its `name` on `/` (same idea as `vfs_lookup()`'s segment-walking, just building the tree instead of walking an existing one) — create/reuse directory nodes for each intermediate segment, then create the leaf node (file or dir) for the last segment.
- Each node's `priv_data` points at a small driver-private struct holding at least: a pointer to where this entry's *data* currently lives, and its current size. Initially, that data pointer just points **directly into the loaded TAR blob** — zero-copy, exactly like the old FAT driver's read-only approach.
- `finddir()`/`readdir()` become simple, fast in-memory list/tree lookups — no archive re-parsing, ever, after mount.

`vfs_ops` mapping, once the tree exists:
- `open`/`close` — likely no-ops (nothing to acquire/release per-entry)
- `read(node, offset, size, buf)` — `memcpy` out of wherever the node's data pointer currently points
- `finddir`/`readdir` — walk the already-built children list
- `write` — see Part 4, this is the interesting one

---

## 🏗️ Part 4: How read *and* write work, given it's RAM-only and won't persist

This is the actual design question. Two approaches, worth understanding both so the "why" of the recommended one is clear:

**Naive approach — mutate the loaded archive in place.** Since the TAR blob sits in ordinary RAM, you technically *can* just `memcpy` new bytes directly into it. But this only works for overwriting existing bytes up to a file's *original* size — there's no free space reserved between entries, so growing a file or creating a new one has nowhere to go. Too limited to be the real design, though it's the simplest possible first cut if you wanted to prototype read/write in an afternoon.

**Recommended approach — copy-on-write promotion into heap memory (this is genuinely how a real tmpfs works, just simplified).** The key idea: a node's data doesn't have to live in any one fixed place — it just needs a pointer and a size, and *where* that pointer points is free to change over the node's lifetime.

- **Reading** a file that's never been written: `memcpy` straight out of the original TAR blob, same as always.
- **First write** to a file: "promote" it — `kmalloc()` a fresh heap buffer (sized to whatever the write needs, which may be larger than the original), `memcpy` the existing content into it, and repoint the node's data pointer at the new heap buffer instead of the read-only archive. This is the copy-on-write moment — the original archive bytes are left untouched; the node just stops pointing at them.
- **Later writes** to an already-promoted file: operate directly on its own heap buffer. If a write needs to grow the file, allocate a bigger buffer, copy the old content over, `kfree()` the old one (this codebase's `heap.c` has no `krealloc`, so "grow" means "allocate new, copy, free old" — same pattern used elsewhere in this codebase, e.g. `vmm_create_context`'s table allocations).
- **Creating a brand-new file** (once `sys_open`'s `O_CREAT` flag is actually wired up — it's already `#define`d in `syscalls.h` but unimplemented) is the same shape one level up: allocate a new `vfs_node` + private struct with a `NULL`/zero-size data pointer, link it into its parent directory's children, and let the first `write()` promote it exactly like an existing file.
- **Why this is the right shape, not over-engineering**: it reuses exactly the primitives already in this codebase (`kmalloc`/`kfree`/`memcpy`, the same allocate-copy-free pattern already used for VMM page tables), it's the standard way an actual RAM-backed filesystem separates "content" from "backing storage," and — matching what you already know — the moment the kernel halts or reboots, every heap-backed byte is gone and only the original archive shape (whatever was in the `initrd.tar` file on disk) would come back on the next boot. That's not a shortcut being taken here, it's the accurate, honest model of what "non-persistent" means for this design.

---

## 📁 Critical files

- ✅ `kernel/core/boot.c` / `kernel/include/core/boot.h` — `module_request` registered
- ✅ `limine.conf` — module directive in place (`module_path:`), confirmed working
- ✅ `kernel/fs/tar/tar.c` + `kernel/include/fs/tar/tar.h` — `tar_mount()`, `vfs_ops` (`read`/`finddir`/`readdir` — `write`/`open`/`close` not yet implemented), tree built as `struct tar_node_data { next_sibling, first_child, data }` (first-child/next-sibling, not the array-of-children shape originally sketched below — simpler, no `krealloc` needed at all)
- ✅ `kernel/core/main.c` — `vfs_mount("/", tar_mount(module_request.response->modules[0]->address, ...->size))` wired in, running before `test_vfs()`
- `kernel/include/core/syscalls.h` — `O_CREAT` already exists; wiring it into `sys_open()` is still open, needed once file creation (Part 4) is built

## 🪜 Verification

✅ Done — `test_vfs()` in `main.c` ran unmodified against the mounted archive and confirmed the whole read path: `ls /` correctly lists the real directory structure from the archive, `cat` reads real file content back out of the loaded TAR blob. A write test (open, write, read back, confirm) would need a small addition to `test_vfs()` or a new `test_tar_write()`, once Part 4 is built.
