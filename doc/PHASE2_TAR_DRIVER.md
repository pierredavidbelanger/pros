# Working Document: Phase 2 Step 13 — TAR/Initramfs Filesystem Driver [STATUS: PARTLY SUPERSEDED 🗄️]

This working document expands `PHASE2_VFS.md` Step 13 into a full design — how the archive gets loaded, how the driver is structured, and specifically how read *and* write work on a filesystem that's explicitly RAM-only and non-persistent by design.

> [!IMPORTANT]
> **Superseded by [`PHASE2_RAMFS.md`](PHASE2_RAMFS.md).** Parts 1 and 2 below (Limine module loading, the ustar block format) are still accurate and still describe the shipped code. **Parts 3 and 4 are not** — they describe a design where the TAR driver *is* the filesystem, holding the node tree and serving reads straight out of the archive blob, with writes handled by copy-on-write promotion into the heap.
>
> That design was built and then deliberately taken apart. The reason: once a tar-backed filesystem becomes writable it isn't a tar filesystem any more — it's a RAM filesystem that happens to have been kickstarted from an archive, and every node would have had to carry an "is this pointer heap-owned or blob-owned?" flag forever. The archive format was welded to the storage layer.
>
> The replacement follows Linux's `rootfs` + `unpack_to_rootfs()` split: `fs/ramfs/` owns storage, `fs/tar/` is reduced to a parser that calls the public VFS API, and neither knows anything about the other. `tar_mount()` no longer exists; it is now `tar_load(addr, size)`, which returns no node and takes no root, working purely through absolute paths.

> [!NOTE]
> **Status of what this document describes**: Parts 1-2 ✅ shipped. Parts 3-4 🗄️ superseded, kept for the reasoning trail.

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

## 🗄️ Part 3 (SUPERSEDED): Parse once at mount time, not on every access

> [!WARNING]
> Superseded. The "parse once, don't re-scan" instinct survived; putting the tree *inside the TAR driver* did not. The tree now lives in `fs/ramfs/`, and `tar_load()` builds it through `vfs_mkdir_parents()`/`vfs_create()` without ever touching a `vfs_node` field directly.

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

## 🗄️ Part 4 (SUPERSEDED): How read *and* write work, given it's RAM-only and won't persist

> [!WARNING]
> Superseded by [`PHASE2_RAMFS.md`](PHASE2_RAMFS.md) Part 2. The copy-on-write promotion below was rejected in favour of **always copying** into ramfs-owned memory at load time, which removes the per-node heap-vs-blob ownership flag entirely, and of a **sparse page list** rather than one flat buffer — the representation real `tmpfs` uses, and the one `mmap` will need in Phase 5. Kept below for the reasoning trail.

This is the actual design question. Two approaches, worth understanding both so the "why" of the recommended one is clear:

**Naive approach — mutate the loaded archive in place.** Since the TAR blob sits in ordinary RAM, you technically *can* just `memcpy` new bytes directly into it. But this only works for overwriting existing bytes up to a file's *original* size — there's no free space reserved between entries, so growing a file or creating a new one has nowhere to go. Too limited to be the real design, though it's the simplest possible first cut if you wanted to prototype read/write in an afternoon.

**Recommended approach — copy-on-write promotion into heap memory (this is genuinely how a real tmpfs works, just simplified).** The key idea: a node's data doesn't have to live in any one fixed place — it just needs a pointer and a size, and *where* that pointer points is free to change over the node's lifetime.

- **Reading** a file that's never been written: `memcpy` straight out of the original TAR blob, same as always.
- **First write** to a file: "promote" it — `kmalloc()` a fresh heap buffer (sized to whatever the write needs, which may be larger than the original), `memcpy` the existing content into it, and repoint the node's data pointer at the new heap buffer instead of the read-only archive. This is the copy-on-write moment — the original archive bytes are left untouched; the node just stops pointing at them.
- **Later writes** to an already-promoted file: operate directly on its own heap buffer. If a write needs to grow the file, allocate a bigger buffer, copy the old content over, `kfree()` the old one. *(No longer true as written: `heap.c` gained a real `krealloc()` during this work, which grows in place by absorbing free contiguous neighbours and only falls back to allocate-copy-free when it has to.)*
- **Creating a brand-new file** (once `sys_open`'s `O_CREAT` flag is actually wired up — it's already `#define`d in `syscalls.h` but unimplemented) is the same shape one level up: allocate a new `vfs_node` + private struct with a `NULL`/zero-size data pointer, link it into its parent directory's children, and let the first `write()` promote it exactly like an existing file.
- **Why this is the right shape, not over-engineering**: it reuses exactly the primitives already in this codebase (`kmalloc`/`kfree`/`memcpy`, the same allocate-copy-free pattern already used for VMM page tables), it's the standard way an actual RAM-backed filesystem separates "content" from "backing storage," and — matching what you already know — the moment the kernel halts or reboots, every heap-backed byte is gone and only the original archive shape (whatever was in the `initrd.tar` file on disk) would come back on the next boot. That's not a shortcut being taken here, it's the accurate, honest model of what "non-persistent" means for this design.

---

## 📁 Critical files

- ✅ `kernel/core/boot.c` / `kernel/include/core/boot.h` — `module_request` registered
- ✅ `limine.conf` — module directive in place (`module_path:`), confirmed working
- 🗄️ `kernel/fs/tar/tar.c` + `kernel/include/fs/tar/tar.h` — **no longer as described here**. `tar_mount()` and `struct tar_node_data` are gone, along with the driver's whole `vfs_ops` vtable; the file shrank from 232 lines to roughly 113. What remains is `struct ustar_header`, `tar_parse_octal()`, the 512-byte block walk, and `int tar_load(uint64_t addr, size_t size)`.
- 🗄️ `kernel/core/main.c` — now mounts *first*, loads second: `ramfs_create_root()` → `vfs_mount("/", ramfs)` → `tar_load(...)`. The old one-liner mounted the return value of `tar_mount()`, which is exactly the coupling the split removed.
- ✅ `kernel/fs/ramfs/ramfs.c` + `kernel/include/fs/ramfs/ramfs.h` — where the node tree lives now. See [`PHASE2_RAMFS.md`](PHASE2_RAMFS.md).
- `kernel/include/core/syscalls.h` — `O_CREAT` already exists; wiring it into `sys_open()` is still open. Not currently blocking: `vfs_create()` creates the node and the loader then opens it `O_WRONLY`.

## 🪜 Verification

🗄️ Historical — this section described the state before the split, when `cat` read directly out of the TAR blob. That path no longer exists. Today `test_vfs()` confirms the *structure* loads (`/root` resolves, `readdir` lists `hello.txt`, `/root/hello.txt` opens) but prints no content, because `ramfs_ops_read`/`ramfs_ops_write` are still stubs. Restoring `cat` is the acceptance criterion for [`PHASE2_RAMFS.md`](PHASE2_RAMFS.md) Part 5 step 2.
