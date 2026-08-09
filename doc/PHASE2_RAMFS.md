# Working Document: Phase 2 Step 14 — ramfs Core + TAR Loader Split [STATUS: SPLIT DONE ✅, PAGE STORAGE OPEN 🚧]

> [!NOTE]
> **Where this stands.** The structural half is built and boots on both architectures: ramfs
> owns the tree, `tar_load()` is a pure parser, and `/root/hello.txt` resolves through the
> mount table. What remains is the page storage in Part 2 — `ramfs_ops_read` and
> `ramfs_ops_write` are still stubs, so file *content* is not stored yet and `cat` prints
> nothing. See Part 5 for the per-step status.

This working document supersedes [`PHASE2_TAR_DRIVER.md`](PHASE2_TAR_DRIVER.md) **Part 4**
(copy-on-write promotion). Parts 1-3 of that document still stand — how the archive reaches
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

Bolting write support onto that (the CoW design in `PHASE2_TAR_DRIVER.md` Part 4) makes it
worse rather than better, because **a writable tar-fs isn't a tar-fs at all**. It's a RAM
filesystem that happens to have been kickstarted from an archive. Follow the CoW road and
every node ends up carrying an "is this pointer heap-owned or blob-owned?" flag, `read()`
and `write()` both have to reason about it, and the archive format stays welded to the
storage layer forever.

The tell that something is wrong: the archive is a *boot-time input*, but it's sitting in
the data structure's type name.

---

## 🏗️ Part 1: The pattern — Linux `rootfs` + initramfs unpacker

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

## 🏗️ Part 2: `kernel/fs/ramfs/` — the storage layer

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
than promoting them into `vfs_node` is deliberate — see Part 6 on the dentry/inode split.

### Data is always copied — no zero-copy blob pointers

Decision taken: `tar_load` writes every byte through the normal VFS write path, so all file
content lands in ramfs-owned pages. The alternative (attach blob pointers, promote on first
write) saves RAM but reintroduces the ownership flag, which is precisely the un-seamless
thing this refactor exists to delete. Real tmpfs has no such flag.

The RAM cost is recoverable and should be treated as a follow-up, not a blocker — see Part 6.

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

## 🏗️ Part 3: The VFS creation vocabulary

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

## 🏗️ Part 4: `tar.c` becomes a loader

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

## 🪜 Part 5: Order of work

Each step boots green on its own — no long broken window.

1. ✅ **ramfs tree only** — node struct, `ramfs_create_root`, `create`, `finddir`, `readdir`.
2. 🚧 **ramfs page storage** — `read`, `write`, `truncate`. **This is the remaining work.**
   `truncate` was deferred: it is only needed once `O_TRUNC` is wired, and it is the one path
   by which pages return to the PMM (`unlink` will reuse it as `truncate(node, 0)`).
   Add `test_ramfs.c` under `core/test/`: create a file, write, read back; `lseek` past EOF and
   write, assert the hole reads as zeros; truncate down then grow, assert no stale bytes.
   **This tests the filesystem with zero archive involvement** — the main payoff of the split,
   and something that was literally impossible with the pre-split design.
3. ✅ **VFS verbs** — `vfs_ops.create`, `vfs_create`/`vfs_mkdir_parents`, `VFS_PATH_MAX`.
   Two deviations from the plan: `vfs_lookup_parent` was not needed (`vfs_lookup`,
   `vfs_create` and `vfs_mkdir_parents` all share one resolver, `vfs_inner_find_and_create`,
   parameterised by *what it is permitted to create*), and `O_CREAT`/`O_TRUNC` in `sys_open`
   turned out not to be a prerequisite — the loader calls `vfs_create()` and then opens the
   existing node. `truncate` is not in the vtable yet.
4. ✅ **Rewrite `tar.c` as loader**, swap `main.c`. The acceptance criterion was
   "`test_vfs()` passes unmodified" — **partially met**: the `ls` half matches exactly, the
   `cat` half cannot until step 2 lands. That gap is the honest measure of what is left.

---

## 🔭 Part 6: Known follow-ups, deliberately out of scope

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

- ✅ `kernel/fs/ramfs/ramfs.c` + `kernel/include/fs/ramfs/ramfs.h` — the whole storage layer.
  Tree, `create`, `finddir`, `readdir` done; `ramfs_ops_read`/`ramfs_ops_write` still stubs.
- ✅ `kernel/include/fs/vfs/vfs.h` — `create` in `vfs_ops`, `VFS_PATH_MAX`. `truncate` not added.
- ✅ `kernel/fs/vfs/vfs.c` — `vfs_create`, `vfs_mkdir_parents`, both over one shared resolver
- ⬜ `kernel/fs/vfs/file.c` — `O_CREAT` / `O_TRUNC` in `sys_open` still unwired (not blocking)
- ✅ `kernel/fs/tar/tar.c` + `kernel/include/fs/tar/tar.h` — reduced to `tar_load()`
- ✅ `kernel/core/main.c` — ramfs root mounted, then `tar_load`. `vfs_init()`/`file_init()` are
  still never called; harmless while both only zero already-zero statics, worth fixing.
- ⬜ `kernel/core/test/test_ramfs.c` — the archive-free storage test, once step 2 lands

## 📝 Docs to update alongside the code

- ✅ `ROADMAP.md` — Phase 2 tasks 3-6 rewritten around the split, pointing here.
- ✅ `PHASE2_TAR_DRIVER.md` — Parts 3 and 4 marked superseded (Parts 1-2 still stand).
- ✅ `README.md` — ramfs/initrd subsystem, `krealloc`, tagged `kprintf`, `core/test/` layout.
- ⬜ Once page storage lands: flip this document's status banner and Part 5 step 2.
- `README.md:39` and `README.md:64` are **already stale**: they claim no filesystem is mounted
  at `/` and that the initrd tmpfs "is the next thing being built", but the tar read path has
  been mounted and working since commit `13b8ced`. Fix as part of this work.
- `README.md:37` enumerates the `vfs_ops` members — add the new verbs.

## 🪜 Verification

- `make` for both AArch64 and x86_64; boot with `cmdline: pros.tests` and read
  `logs/qemu-<arch>.log`.
- Step 2's `test_ramfs()` is the real unit test — it exercises the filesystem end to end with
  no tar involved at all.
- Step 4's acceptance criterion: `test_vfs()` **unchanged** still prints the same `ls /` listing
  and the same `hello.txt` contents as today.
- Worth adding once write works: open `/root/hello.txt` `O_RDWR`, write past its original size,
  read it back, confirm the growth — the thing the old zero-copy design could never do.
