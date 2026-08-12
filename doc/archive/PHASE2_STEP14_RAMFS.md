# Working Document: Phase 2 Step 14 — ramfs Core + TAR Loader Split [STATUS: COMPLETE ✅]

> [!NOTE]
> **Where this stands.** Done and boots on both architectures. ramfs owns the tree and the
> page storage, `tar_load()` is a pure parser that fills it through the public VFS API, and
> `/root/hello.txt` reads back the bytes that were in the archive. 31 VFS self-tests cover it,
> including the sparse-hole and page-boundary paths the loader cannot reach on its own.
>
> Deliberately not built: `truncate`, `unlink`, and `O_CREAT`/`O_TRUNC` in `sys_open`. None are
> needed until something deletes or replaces a file. `truncate` is the interesting one — it is
> the only path by which pages would return to the PMM, so today ramfs never shrinks.

This working document supersedes [`PHASE2_STEP13_TAR_DRIVER.md`](PHASE2_STEP13_TAR_DRIVER.md) **Chapter 4**
(copy-on-write promotion). Chapters 1-3 of that document still stand — how the archive reaches
the kernel and the shape of the ustar bytes hasn't changed. What changes is *who owns the
filesystem*.

> [!NOTE]
> Mentor-mode reminder (`.rules.md`): this is a design reference to code from by hand, not
> code to paste in.

---

## 🧭 Why split at all

`kernel/fs/tar/tar.c` currently **is** the filesystem: it owns the node tree
(`struct tar_node_data`), the `vfs_ops` vtable, `finddir`/`readdir`/`read`, *and* the ustar
parser — 232 lines, one file, four responsibilities.

Bolting write support onto that (the CoW design in `PHASE2_STEP13_TAR_DRIVER.md` Chapter 4) makes it
worse rather than better, because **a writable tar-fs isn't a tar-fs at all**. It's a RAM
filesystem that happens to have been kickstarted from an archive. Follow the CoW road and
every node ends up carrying an "is this pointer heap-owned or blob-owned?" flag, `read()`
and `write()` both have to reason about it, and the archive format stays welded to the
storage layer forever.

The tell that something is wrong: the archive is a *boot-time input*, but it's sitting in
the data structure's type name.

---

## 🏗️ Chapter 1: The pattern — Linux `rootfs` + initramfs unpacker

Linux does exactly this split, and it's worth knowing the real names.

- **`rootfs`** is an instance of **ramfs**, mounted at `/` before anything else exists.
  It is a completely ordinary filesystem driver — it has no idea an archive exists.
- **`unpack_to_rootfs()`** (`init/initramfs.c`) is a **cpio parser**. The critical detail is
  *how* it fills the filesystem: it calls `init_mkdir()`, `init_open()`, `init_write()`,
  `init_symlink()` — thin wrappers over the same VFS entry points userspace uses. It never
  touches a ramfs-private struct.

So the layering is:

```
ustar parser   →   generic VFS create/write API   →   ramfs driver
  (format)               (vocabulary)                   (storage)
```

Three layers, each ignorant of the others:

- Swap tar for cpio, or add gzip → the loader changes, ramfs doesn't.
- Swap ramfs for a real disk filesystem → the loader still works unmodified.
- Add devfs in Phase 5 → it plugs into the same vocabulary.

And `tar_mount()` stops being a mount. It becomes `tar_load()`, returning `int`, taking no
node and returning no node — it operates purely on absolute paths. That signature is the
proof the layers actually separated rather than just moving to another file.

### Naming: ramfs, not tmpfs

Linux draws a real distinction. **ramfs** is the dumb one: no size limit, no accounting, no
swap backing, grows until memory runs out. **tmpfs** adds a size cap, proper page-cache
integration and swap. With no swap and no cap, what this builds is honestly `ramfs`. Call it
that, and earn the `tmpfs` name later by adding a size limit.

---

## 🏗️ Chapter 2: `kernel/fs/ramfs/` — the storage layer

New files: `kernel/fs/ramfs/ramfs.c` and `kernel/include/fs/ramfs/ramfs.h`.
`kernel/Makefile:30` globs `find core mm drivers fs -name '*.c'`, so no build change needed.

The public surface is deliberately one function:

```c
struct vfs_node *ramfs_create_root(void);
```

Everything else goes through `vfs_ops`. Nothing outside `ramfs.c` should ever see
`struct ramfs_node` — if something does, the seam has leaked.

```c
struct ramfs_node {
    // directory tree — lifted almost verbatim from struct tar_node_data
    struct vfs_node *first_child;
    struct vfs_node *next_sibling;
    struct vfs_node *parent;

    // file content: page list, sparse-capable. A NULL entry is a hole, reads as zeros.
    void **pages;            // virtual addrs (pmm_phys_to_virt of pmm_alloc(1))
    uint64_t page_count;     // number of entries in `pages`, NOT a byte count
};
```

Logical file length stays in `vfs_node->size` (the field already exists);
`page_count * PAGE_SIZE` is the capacity. Keeping the tree links inside `ramfs_node` rather
than promoting them into `vfs_node` is deliberate — see Chapter 6 on the dentry/inode split.

### Data is always copied — no zero-copy blob pointers

Decision taken: `tar_load` writes every byte through the normal VFS write path, so all file
content lands in ramfs-owned pages. The alternative (attach blob pointers, promote on first
write) saves RAM but reintroduces the ownership flag, which is precisely the un-seamless
thing this refactor exists to delete. Real tmpfs has no such flag.

The RAM cost is recoverable and should be treated as a follow-up, not a blocker — see Chapter 6.

### Storage: page list, not a flat buffer

A flat `kmalloc` buffer with a capacity field would be simpler, but the page list is what
real tmpfs uses and it's the representation `mmap` needs for `/dev/fb0` (Phase 5) and for
the ELF loader (Phase 3). It also needs no contiguous allocation — `heap.c` is first-fit
with forward-only coalescing, so asking it for one 4 MiB block is optimistic.

**Two independent growth axes.** Keeping these straight is most of the work:

1. **The `pages` array itself** grows by doubling. This originally read "allocate-copy-free,
   since `heap.c` has no `krealloc`" — no longer true: `krealloc()` was built during this work
   and is the right call here. It grows in place when the array's own split remainder is free
   and physically contiguous (the common case for a repeatedly doubled array) and falls back
   to allocate-copy-free only when it must. Note `krealloc` does **not** zero the grown tail,
   so new slots still need clearing before they can be read as holes. The array stays small:
   512 entries is 4 KiB and covers a 2 MiB file.
2. **Individual pages** come from `pmm_alloc(1)`, and **must be `memset` to 0** on
   allocation. Forget this and a new file reads back whatever the last process left in that
   page.

### The three operations

**`read(node, offset, size, buf)`** — clamp `size` against `node->size`, then loop:

```
page_index = off / PAGE_SIZE
page_off   = off % PAGE_SIZE
chunk      = min(PAGE_SIZE - page_off, remaining)
```

If `pages[page_index] == NULL`, that's a hole: `memset(buf, 0, chunk)` instead of `memcpy`.

**`write(node, offset, size, buf)`** — grow the array to cover
`(offset + size + PAGE_SIZE - 1) / PAGE_SIZE`, run the same chunk loop but allocate any
`NULL` page on demand, then:

```c
if (offset + size > node->size) node->size = offset + size;
```

Note what the page list buys here: `lseek` past EOF followed by a write produces a genuine
sparse file with no special case at all. A flat buffer would have needed an explicit
zero-fill of the gap.

**`truncate(node, new_size)`** — free every page past the new end, **and zero the tail of
the new last partial page**. That zeroing is not optional: skip it and shrink-then-grow
resurrects stale bytes, which is the classic ramfs bug.

### The rest of the vtable

`finddir` and `readdir` port over from `tar.c:196-232` essentially unchanged, and `create`
reuses the tail-append tree-linking block at `tar.c:155-166`. `open`/`close` stay `NULL` —
there's still nothing to acquire per-entry.

`readdir` stays O(n) per call, so `ls` stays O(n²). Fine at this scale; noted so it isn't a
surprise later.

---

## 🏗️ Chapter 3: The VFS creation vocabulary

`struct vfs_ops` currently has **no way to create anything**. That's the missing vocabulary
the loader needs, and it's needed for Phase 3's `mkdir`/`O_CREAT` regardless.

`kernel/include/fs/vfs/vfs.h`:

```c
int (*create)(struct vfs_node *dir, const char *name, uint32_t flags, struct vfs_node **out);
int (*truncate)(struct vfs_node *node, uint64_t size);
// later, not needed for this refactor:
int (*unlink)(struct vfs_node *dir, const char *name);
```

One `create` taking `VFS_FILE` / `VFS_DIRECTORY` in `flags` rather than separate
`create`/`mkdir` — fewer vtable slots, and the loader just forwards the ustar typeflag.

`kernel/fs/vfs/vfs.c` — path-level helpers alongside the existing `vfs_lookup`:

```c
struct vfs_node *vfs_lookup_parent(const char *path, char *name_out, size_t name_out_size);
struct vfs_node *vfs_create(const char *path, uint32_t flags);
struct vfs_node *vfs_mkdir_parents(const char *path);
```

`vfs_lookup_parent` copies the path into a stack buffer, splits at the last `/`, looks up
the prefix and hands back the basename. Worth noticing *why* `vfs_lookup` hand-rolls its own
tokenizer today instead of using `strtokr`: `strtokr` writes `'\0'` over the delimiters and
`vfs_lookup`'s path is `const`. The local copy is exactly what makes `strtokr` usable here.

This needs a **`VFS_PATH_MAX`** (256 is fine) — `VFS_NAME_SIZE` is per *component*, not per
path, and the two are being conflated in a few places already.

Keep `vfs_create` **strict** — fail if the parent directory doesn't exist. That's POSIX
`O_CREAT` behaviour; `open("/a/b/c", O_CREAT)` does not create `/a/b`. `vfs_mkdir_parents`
is the separate, explicit `mkdir -p` that the loader calls.

`kernel/fs/vfs/file.c:23` — wire `O_CREAT` and `O_TRUNC` into `sys_open`. `O_CREAT` has been
`#define`d in `kernel/include/core/syscalls.h:12` and unhandled since the beginning. This is
a **prerequisite** of the loader, not a follow-up. The fact that the design forces it is a
good sign the seam is in the right place.

---

## 🏗️ Chapter 4: `tar.c` becomes a loader

`kernel/include/fs/tar/tar.h` shrinks to:

```c
int tar_load(uint64_t tar_addr, size_t tar_size);
```

**Keep**: `struct ustar_header`, `tar_parse_octal()`, the 512-byte block walk and the
`addr += TAR_BLOCK_SIZE + ...` advance (`tar.c:174`).

**Delete**: `struct tar_node_data`, `tar_ops`, `tar_ops_read`, `tar_ops_finddir`,
`tar_ops_readdir`, and the whole `strtokr` tree-building loop — roughly 120 of the 232 lines.

Per entry, build `"/" + header->name` into a `VFS_PATH_MAX` buffer, then:

- `TAR_TYPE_DIR` → strip the trailing `/`, `vfs_mkdir_parents(path)`
- regular file → `sys_open(path, O_CREAT | O_WRONLY)`,
  `sys_write(fd, (uint8_t *) addr + TAR_BLOCK_SIZE, size)`, `sys_close(fd)`

Going through the **fd path** rather than reaching for `node->ops->write` directly is
deliberate. It's what Linux's `init_open`/`init_write` do, and it means the loader exercises
and validates the exact code path userspace will use — including the `O_CREAT` wiring that
was just added. Reaching around the fd layer would leave that untested until Phase 3.

### This deletes a live bug for free

`tar.c:138-152` gives every intermediate directory node the **leaf's** typeflag, size and
data pointer. An archive containing `a/b/c.txt` with no explicit `a/` or `a/b/` entry
produces `a` and `b` as `VFS_FILE` nodes pointing at the file's bytes. It only works today
because GNU tar happens to emit `root/` before `root/hello.txt`.

With `vfs_mkdir_parents`, the parent-creation decision lives in exactly one place and has
nothing to inherit from.

### Wiring

`kernel/core/main.c:57-64` becomes:

```c
struct vfs_node *root = ramfs_create_root();
if (root && vfs_mount("/", root) == 0) {
    if (module_request.response && module_request.response->module_count > 0) {
        tar_load(module_request.response->modules[0]->address,
                 module_request.response->modules[0]->size);
    }
    if (tests_enabled) test_vfs();
}
```

Note the mount now succeeds even with no initrd present — an empty `/` is a valid state,
which it never was before. While in there: `vfs_init()` and `file_init()` are declared and
never called.

---

## 🪜 Chapter 5: Order of work

Each step boots green on its own — no long broken window.

1. ✅ **ramfs tree only** — node struct, `ramfs_create_root`, `create`, `finddir`, `readdir`.
2. ✅ **ramfs page storage** — `read` and `write` done. `truncate` deliberately deferred: it is
   only needed once `O_TRUNC` is wired, and it is the one path by which pages return to the PMM
   (`unlink` will reuse it as `truncate(node, 0)`).
   The storage tests went into `core/test/test_vfs.c` rather than a separate `test_ramfs.c` —
   driving them through `sys_open`/`sys_write`/`sys_lseek` tests the whole stack rather than
   ramfs in isolation, and needs no new entry point. **They still involve no archive at all**,
   which was the point: they build their own files under `/test/scratch`.
3. ✅ **VFS verbs** — `vfs_ops.create`, `vfs_create`/`vfs_mkdir_parents`, `VFS_PATH_MAX`.
   Two deviations from the plan: `vfs_lookup_parent` was not needed (`vfs_lookup`,
   `vfs_create` and `vfs_mkdir_parents` all share one resolver, `vfs_inner_find_and_create`,
   parameterised by *what it is permitted to create*), and `O_CREAT`/`O_TRUNC` in `sys_open`
   turned out not to be a prerequisite — the loader calls `vfs_create()` and then opens the
   existing node. `truncate` is not in the vtable yet.
4. ✅ **Rewrite `tar.c` as loader**, swap `main.c`. Acceptance criterion met: `ls /root` and
   `cat /root/hello.txt` both produce what they did before the split. `main.c` now also
   `kpanic`s if `tar_load` returns non-zero, after a silent `-1` cost real debugging time.

---

## 🔨 Implementation brief — `ramfs_ops_read` / `ramfs_ops_write`

Chapter 2 above sketches the storage model. This section is the actionable version of Chapter 2,
written against the code as it actually stands today.

> [!NOTE]
> Mentor-mode reminder (`.rules.md`): a design reference to code from by hand, not code to
> paste in.

### What changed since Chapter 2 was written

- **`krealloc()` now exists.** Chapter 2 says "allocate-copy-free with doubling, since `heap.c`
  has no `krealloc`". Use `krealloc` instead. It does **not** zero the grown tail, so new
  slots still need clearing before they can read as holes.
- **`pmm_alloc()` returns 0 on exhaustion** instead of `kpanic`-ing, so the failure branches
  in the write path are genuinely reachable now, not theoretical.
- **`kmalloc`/`kcalloc` can return NULL**, since they propagate that 0.

### Pin the invariant first

Every bug in this kind of code is an invariant violation. Worth writing as a comment on
`struct ramfs_node_data` before any code:

- `pages == NULL` ⟺ `page_count == 0`
- `page_count` is **capacity** — entries in the array, not a byte count, and not "pages
  actually allocated"
- every slot `< page_count` is either a valid virtual pointer **or** `NULL`, meaning a hole
  that reads as zeros
- `node->size` is the *only* source of truth for EOF, always `<= page_count * PAGE_SIZE`

That last one is what lets `read` clamp without consulting the array at all.

**Decide deliberately** whether `pages[]` holds virtual or physical addresses. The current
comment says virtual, which is fine — but `pmm_free` takes a physical address, so
`truncate`/`unlink` will need `pmm_virt_to_phys()` on the way back out. Mixing the two is
the bug that only surfaces when something is finally freed.

### One page walker, parameterised by whether it may allocate

```c
// Resolves `index` to a page. When `allocates` is false it never grows the array and never
// materialises a page — a NULL return means "hole, read as zeros". When `allocates` is true
// a NULL return can only mean allocation failure, because a hole would have been filled.
static void *ramfs_walk_page(struct ramfs_node_data *node_data, uint64_t index, bool allocates);
```

The hard rule this has to protect: **`read` must never allocate.** If it does, reading a
sparse file materialises every hole, and reading a 1 GiB sparse file eats 1 GiB of RAM. Two
guards carry that:

```c
static void *ramfs_walk_page(struct ramfs_node_data *node_data, uint64_t index, bool allocates) {
    // grow the array, but only when we are allowed to
    if (index >= node_data->page_count) {
        if (!allocates) return NULL;
        // ... grow, see below ...
    }

    // materialize the page, but only when we are allowed to
    if (node_data->pages[index] == NULL && allocates) {
        // ... allocate, see below ...
    }

    return node_data->pages[index];
}
```

The early `return NULL` also stops the next line from indexing past the array. Both branches
read as "do the thing, but only when permitted", which is what keeps the merged version
honest. This is the shape Linux uses too — `pagecache_get_page()` takes an `FGP_CREAT` flag
deciding whether a miss allocates, rather than splitting into two functions.

The one thing the merged signature costs is that `NULL` is ambiguous in the abstract — hole
or failure? It is never ambiguous at a call site, because `allocates == true` rules out the
hole case. Worth stating in the comment above the function, as done here.

**Growing the array** — double from a floor, then zero only the newly added slots:

```c
uint64_t new_count = node_data->page_count ? node_data->page_count * 2 : 4;
while (new_count <= index) new_count *= 2;

void **new_pages = krealloc(node_data->pages, new_count * sizeof(void *));
if (!new_pages) return NULL;
memset(&new_pages[node_data->page_count], 0, (new_count - node_data->page_count) * sizeof(void *));
```

Doubling matters: growing by one turns a 1 MiB file into 256 reallocations instead of 8.

**Allocating a page**: `pmm_alloc(1)` returns a **physical** address — `pmm_phys_to_virt()`
before touching it — and returns **0** on exhaustion. Then `memset(page, 0, PAGE_SIZE)`: the
PMM does not zero, and a fresh page holds whatever the last owner left in it.

### The chunk loop

Identical shape in both directions, and this is where `ramfs_walk_page` gets called. The
index math per iteration:

```c
uint64_t index    = off / PAGE_SIZE;
uint64_t page_off = off % PAGE_SIZE;
uint64_t chunk    = PAGE_SIZE - page_off;
if (chunk > remaining) chunk = remaining;
```

**Read** — `allocates` is false, and `NULL` means hole:

```c
while (remaining > 0) {
    // ... index math ...
    void *page = ramfs_walk_page(node_data, index, false);
    if (page) {
        memcpy(buf, (uint8_t *) page + page_off, chunk);
    } else {
        memset(buf, 0, chunk);
    }
    off += chunk; buf += chunk; remaining -= chunk;
}
```

**Write** — `allocates` is true, and `NULL` can only mean out of memory:

```c
while (remaining > 0) {
    // ... index math ...
    void *page = ramfs_walk_page(node_data, index, true);
    if (!page) break;   // short write, return what was actually copied
    memcpy((uint8_t *) page + page_off, buf, chunk);
    off += chunk; buf += chunk; remaining -= chunk;
}
```

`buf` is `uint8_t *` for the byte arithmetic — `void *` arithmetic is a GCC extension, and
the explicit cast matches the style used everywhere else in the tree. On the read side it is
`uint8_t *buf = buffer;`, on the write side `const uint8_t *buf = buffer;`, since
`ramfs_ops_write` takes a `const void *`.

### `read`

The skeleton is already right; uncomment and use `actual_size`:

- `offset >= node->size` → return 0 (already there)
- clamp `size` against `node->size - offset`
- run the chunk loop, `pages[i] == NULL` → `memset(buf, 0, chunk)` rather than `memcpy`
- return bytes copied

### `write`

Order of operations is the whole game:

1. grow the array and allocate every page the write touches
2. copy
3. **only then** `if (offset + size > node->size) node->size = offset + size;`

Bump `size` first and a failed `pmm_alloc` leaves a file claiming bytes with no backing —
every later read of that range returns whatever junk the caller's buffer already held.

On partial failure return the count actually written, not `-1`. POSIX allows a short write,
and `sys_write` (`fs/vfs/file.c:137`) advances `f->offset` by exactly the return value, so
short writes compose correctly for free.

### Guards both functions need

- `buffer` NULL check and `size == 0` early return
- **reject `VFS_DIRECTORY` nodes.** Neither stub checks `flags`. A directory's `pages` is
  meant to stay `NULL` forever, sitting right next to live `first_child`/`next_sibling`
  pointers — so this is memory safety, not merely POSIX `EISDIR`
- **overflow**: `offset + size` can wrap. `sys_lseek` (`fs/vfs/file.c:143`) puts `offset`
  wherever a caller asks, with no upper bound
- a sanity ceiling on `offset` is worth considering: a large `lseek` followed by a write
  currently asks `pmm_alloc` for a preposterous page count and takes the kernel down

### Closing the loop in `tar.c`

`fs/tar/tar.c:104`, replacing `// TODO write data`. `vfs_create` already made the node, so
this needs no `O_CREAT`:

```c
int fd = sys_open(path, O_WRONLY);
sys_write(fd, (uint8_t *) addr + TAR_BLOCK_SIZE, size);
sys_close(fd);
```

`sys_write` requires `O_WRONLY` or `O_RDWR` (`fs/vfs/file.c:127`) — `O_RDONLY` is 0 and gets
rejected. Needs `#include "core/syscalls.h"` in `tar.c`.

Staying on the public syscall API rather than reaching for `file->ops->write` is deliberate:
it mirrors Linux's `init_open`/`init_write`, and it means the loader exercises the same path
userland will.

### Fix before `read` starts returning data

`core/test/test_vfs.c:28-32` — `char buffer[2000]`, `sys_read(fd, buffer, 2000)`, then
`buffer[bytes_read] = '\0'`. A file of exactly 2000 bytes writes `buffer[2000]`, one past the
end of a stack array. Dormant while `read` returns 0; live the moment it works. Read 1999, or
size the buffer 2001.

---

## 🔭 Chapter 6: Known follow-ups, deliberately out of scope

- **Reclaim the initrd blob.** `kernel/mm/pmm.c:40` only reclaims `LIMINE_MEMMAP_USABLE`, so
  the `BOOTLOADER_RECLAIMABLE` region holding the archive is never handed to the PMM — it is
  wasted today regardless of this refactor. Reclaiming it after `tar_load` returns makes the
  always-copy decision cost nothing. Small, self-contained, good next side quest.
- **`vfs_get_mountpoint` prefix bug** (`vfs.c:35`): `strncmp(*path, current->path, mount_len)`
  is a raw prefix compare with no component-boundary check, so a mount at `/foo` also matches
  `/foobar`. Harmless with one mount; bites the day devfs lands.
- **dentry/inode split.** `struct vfs_node` conflates three things — the name, the tree
  position, and the content — which is why it carries both a `name[128]` and an `inode` field
  that is declared and never set anywhere. Linux separates `dentry` (name + parent + children)
  from `inode` (content + size + mode + refcount). That split is what hard links, `.`/`..`,
  and pointer-move `rename` require. Much bigger refactor, not needed yet. Keeping the tree
  links inside `struct ramfs_node` is precisely what keeps that door open to a one-file change.
- **Filesystem type registration** — `register_filesystem(struct filesystem_type {name, mount})`
  and `vfs_mount("/", "ramfs", opts)`. Pure ceremony with exactly one filesystem; what makes it
  worth building is the *second* one. Revisit when devfs arrives in Phase 5.
- **ustar gaps** still unhandled: the `prefix` field (paths > 100 chars), hard/symlinks, and
  the two trailing zero blocks (the loop relies on `header->name[0] == '\0'`).

---

## 📁 Critical files

- ✅ `kernel/fs/ramfs/ramfs.c` + `kernel/include/fs/ramfs/ramfs.h` — the whole storage layer,
  read and write included. `ramfs_walk_page(node_data, index, alloc)` is the single page
  resolver; `alloc` is false on the read path so a read can never materialise a hole.
- ✅ `kernel/include/fs/vfs/vfs.h` — `create` in `vfs_ops`, `VFS_PATH_MAX`. `truncate` not added.
- ✅ `kernel/fs/vfs/vfs.c` — `vfs_create`, `vfs_mkdir_parents`, both over one shared resolver
- ⬜ `kernel/fs/vfs/file.c` — `O_CREAT` / `O_TRUNC` in `sys_open` still unwired (not blocking)
- ✅ `kernel/fs/tar/tar.c` + `kernel/include/fs/tar/tar.h` — reduced to `tar_load()`
- ✅ `kernel/core/main.c` — ramfs root mounted, then `tar_load`, whose failure now `kpanic`s.
  `vfs_init()`/`file_init()` are still never called; harmless while both only zero
  already-zero statics, worth fixing.
- ✅ `kernel/core/test/test_vfs.c` — the archive-free storage tests landed here rather than in
  a separate `test_ramfs.c`, so they exercise the syscall layer at the same time.

## 📝 Docs to update alongside the code

- ✅ `ROADMAP.md` — Phase 2 marked complete, tasks rewritten around the split.
- ✅ `PHASE2_STEP13_TAR_DRIVER.md` — Chapters 3 and 4 marked superseded (Chapters 1-2 still stand).
- ✅ `PHASE2_VFS.md` — Steps 11 and 13 updated to what was actually built.
- ✅ `README.md` — ramfs/initrd subsystem, `krealloc`, tagged `kprintf`, `core/test/` layout.

## 🪜 Verification

Done. `make` builds both AArch64 and x86_64; booting with `cmdline: pros.tests` runs 64 checks,
all passing on both, of which 31 are VFS.

The tests that matter for this document, all in `core/test/test_vfs.c`:

- initrd content compared with `memcmp` rather than eyeballed as printed text
- create, write, read back through `sys_open`/`sys_write`/`sys_read`
- an overwrite *inside* a file that must not extend it — the regression guard for the size
  double-count that this work introduced and then fixed
- 300 bytes written at offset 4000, straddling two pages, which is the only coverage of the
  chunk loop's `page_offset` arithmetic
- `lseek` to 8192, write, then assert **both skipped pages read back as zeros** and the data
  after the hole survives. The tar loader only ever writes sequentially from offset 0, so
  nothing else in the tree can reach the hole path.
- read of a `VFS_DIRECTORY` node returns -1, write to one likewise

Still uncovered, and honestly so: `truncate` (shrink then grow, asserting no stale bytes in the
tail of the new last partial page) does not exist yet, so neither does its test.
