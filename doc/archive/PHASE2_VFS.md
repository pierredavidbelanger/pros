# Working Document: Phase 2 — Disk Storage (hd0), VirtIO-Block, VFS & Filesystem Read/Write [STATUS: SUPERSEDED, SEE BANNER 🚧]

This working document breaks down **Phase 2: Disk Storage (`hd0`), VirtIO-Block, VFS & Filesystem Read/Write** into granular Steps designed for incremental implementation and verification in **PrOS**.

> [!WARNING]
> **Pivot notice**: Steps 1-9 below describe the original disk-backed FAT/VirtIO-block/PCI/ACPI stack. It was fully implemented and working, then deliberately ripped out for being "not clean enough" — none of `drivers/`, `fs/fat/` exist in the tree anymore. Steps 1-9 are kept below as a historical record (they explain a lot about how VFS/block-device concepts fit together, even though this specific implementation is gone). **Steps 10 and 12 are dead** — they refer to files that no longer exist. **Steps 11 and 13 are still the real, current plan** — see [`../ROADMAP.md`](../ROADMAP.md)'s Phase 2 entry for the up-to-date summary.

---

## 🏗️ Architectural Overview & Goals

In Phase 1, PrOS established robust Physical Memory Management (PMM), Virtual Memory Management (VMM) with 4-level paging, address space isolation, and demand paging on both `x86_64` and `aarch64`.

### What Phase 2 Adds:
1. **Linux-Like Directory Hierarchy**:
   - Organize hardware drivers into `drivers/` and filesystem subsystems into `fs/` to mirror production kernel architecture (Linux kernel layout).
2. **Modern Hardware Device Probing & VirtIO Transport**:
   - **Unified ACPI MCFG / PCIe ECAM (MMIO)**: Use ACPI RSDP (provided by Limine) to locate the `MCFG` table and access PCI Configuration Space via Memory-Mapped I/O (ECAM) on both `x86_64` and `aarch64` without legacy x86 port I/O.
   - **VirtIO MMIO Transport (Fallback/ARM)**: VirtIO MMIO slot scanning (`0x0A000000`) for simple embedded/virtualized targets.
3. **VirtIO-Block Device Driver**:
   - Virtqueue management (Descriptor Table, Available Ring, Used Ring).
   - Sector-level block read/write operations for drive `hd0`.
4. **Block Device Subsystem**:
   - Abstract `blockdev_t` interface for hardware-agnostic storage access.
5. **Virtual File System (VFS)**:
   - File system node abstractions (`vfs_node_t`), operations (`vfs_ops_t`), path lookup (`vfs_lookup`), and mount point table.
   - File descriptor table (`file_t`) and stream position management.
6. **FAT (FAT12/16/32) Filesystem Driver**:
   - BIOS Parameter Block (BPB) parsing, cluster chain walking, directory entry scanning (8.3 & LFN), file read and write.
   - Mounting QEMU's host fat partition (`fat:rw:root`) on `/`.

---

## 📁 Linux-Like Kernel Directory Structure

To keep the codebase modular, clean, and scalable as PrOS grows (adding block devices, filesystems, TTYs, framebuffers, and network interfaces in future phases), Phase 2 adopts a **Linux-like directory tree**:

```text
kernel/
├── arch/                  # Architecture-Specific Code & Exception Tables
│   ├── x86_64/            # IDT, GDT, ISRs
│   └── aarch64/           # Exception Vector Tables, EL1 Handlers
├── core/                  # Core Kernel Entry Point & Boot Infrastructure
│   ├── boot.c             # Limine Boot Requests
│   ├── main.c             # Kernel Entry Point & Unit Tests
│   ├── console.c          # Console Subsystem
│   ├── fb.c               # Framebuffer Renderer
│   ├── kprintf.c          # Formatted Printing
│   └── memory.c           # Memory Utilities
├── mm/                    # Memory Management Subsystem
│   ├── pmm.c              # Physical Memory Manager
│   ├── vmm.c              # Virtual Memory Manager
│   └── heap.c             # Dynamic Memory Heap
├── drivers/               # Hardware Device Drivers
│   ├── acpi/              # ACPI RSDP, XSDT & MCFG ECAM Parser
│   ├── block/             # Block Device Abstraction & VirtIO-Block Driver
│   ├── bus/               # Bus Controllers (PCIe ECAM MMIO, VirtIO MMIO)
│   └── virtio/            # VirtQueue Engine & Split Ring Management
├── fs/                    # Filesystems & VFS Layer
│   ├── vfs/               # Virtual File System, Path Resolver, File Descriptors
│   └── fat/               # FAT12/16/32 Filesystem Driver
└── include/               # Public Kernel Header Files
    ├── arch/              # Architecture Headers (arch.h)
    ├── core/              # Core Kernel Headers (boot.h, console.h, kprintf.h, memory.h...)
    ├── mm/                # Memory Management Headers (pmm.h, vmm.h, heap.h)
    ├── drivers/           # Driver Headers (acpi.h, pci.h, virtio.h, virtio_blk.h, blockdev.h)
    └── fs/                # Filesystem Headers (vfs.h, file.h, fat.h)
```

---

## 🪜 Architecture Diagram & Data Flow

```text
  ┌────────────────────────────────────────────────────────────────────────┐
  │                 Kernel Storage System Call / API Layer                 │
  │                  (vfs_open, vfs_read, vfs_write, etc.)                 │
  └───────────────────────────────────┬────────────────────────────────────┘
                                      │
                                      ▼
  ┌────────────────────────────────────────────────────────────────────────┐
  │                 Virtual File System Layer (fs/vfs/)                    │
  │            Path Lookup Engine, Mount Table & File Descriptors           │
  └───────────────────────────────────┬────────────────────────────────────┘
                                      │
                                      ▼
  ┌────────────────────────────────────────────────────────────────────────┐
  │               Filesystem Driver Layer (fs/fat/fat.c)                   │
  │           BPB Parser, Cluster Chain Walker & Dir Entry Lookup          │
  └───────────────────────────────────┬────────────────────────────────────┘
                                      │
                                      ▼
  ┌────────────────────────────────────────────────────────────────────────┐
  │          Block Device Subsystem Layer (drivers/block/blockdev.c)       │
  │               Unified Block Read / Write Sector Interface              │
  └───────────────────────────────────┬────────────────────────────────────┘
                                      │
                                      ▼
  ┌────────────────────────────────────────────────────────────────────────┐
  │         VirtIO-Block Driver Layer (drivers/block/virtio_blk.c)          │
  │               Virtqueue (Descriptors, Available & Used Ring)           │
  └─────────────────┬──────────────────────────────────┬───────────────────┘
                    │ (PCIe MMIO BARs)                 │ (VirtIO MMIO)
                    ▼                                  ▼
  ┌──────────────────────────────────┐┌────────────────────────────────────┐
  │  ACPI MCFG / PCIe ECAM (MMIO)    ││  VirtIO Direct MMIO Transport      │
  │  (drivers/bus/pci.c)             ││  (drivers/bus/virtio_mmio.c)       │
  └──────────────────────────────────┘└────────────────────────────────────┘
```

---

## 🪜 Steps

```text
  [✅] Step 1: Block Device Abstraction (drivers/block/blockdev.c)
  [✅] Step 2: ACPI MCFG / PCIe ECAM & Device Probe (drivers/bus)
  [✅] Step 3: Core VirtQueue Ring & Buffer Engine (drivers/virtio)
  [✅] Step 4: VirtIO-Block Driver & hd0 Sector I/O (drivers/block)
  [✅] Step 5: Virtual File System Node & Mount Manager (fs/vfs/)
  [✅] Step 6: File Descriptor Management & Path Resolution
  [✅] Step 7: FAT Filesystem Driver (Read-Only)
  [✅] Step 8: Mount FAT Volume to VFS Root "/"
  [✅] Step 9: Runtime Verification & Integration Unit Tests
  [⛔] Step 10: Refactor PCI Subsystem for Multi-Device Support (MOOT — pci.c removed)
  [✅] Step 11: Implement Iterative VFS Path Resolution (vfs_lookup and friends)
  [⛔] Step 12: Implement FAT Subdirectory Support (MOOT — fat.c removed)
  [✅] Step 13: Implement Initramfs — built as ramfs + a tar loader, see PHASE2_STEP14_RAMFS.md
```

---

### Step 1: Block Device Subsystem (`include/drivers/blockdev.h`, `drivers/block/blockdev.c`)
* **Status**: ✅ Completed
* **Target Files**: `kernel/include/drivers/blockdev.h`, `kernel/drivers/block/blockdev.c`
* **Goal**: Provide a generic block device interface so filesystems can read/write sectors without knowing whether the underlying disk is VirtIO, NVMe, or IDE.
* **Tasks**:
  - [x] Define `blockdev_t` structure.
  - [x] Implement global block device registration (`blockdev_register`, `blockdev_get_by_name`).
  - [x] Implement sector bounds-checking and helper wrappers (`blockdev_read`, `blockdev_write`).

---

### Step 2: Modern ACPI MCFG / PCIe ECAM (MMIO) Bus Probing
* **Status**: ✅ Completed
* **Target Files**: 
  - `kernel/include/drivers/acpi.h`, `kernel/drivers/acpi/acpi.c`
  - `kernel/include/drivers/pci.h`, `kernel/drivers/bus/pci.c`
  - `kernel/arch/aarch64/virtio_mmio.h`, `kernel/drivers/bus/virtio_mmio.c`
* **Goal**: Discover PCI devices across architectures (`x86_64` and `aarch64`) using memory-mapped ACPI MCFG (PCIe ECAM).
* **Tasks**:
  - [x] ACPI Table Parsing (RSDP $\rightarrow$ XSDT $\rightarrow$ MCFG).
  - [x] PCIe ECAM (MMIO) Config Space Access.
  - [x] PCI Bus Enumeration (Find VirtIO Block 0x1001/0x1042).
  - [x] AArch64 VirtIO MMIO Fallback.

---

### Step 3: VirtQueue Core Engine (`include/drivers/virtio.h`, `drivers/virtio/virtio.c`)
* **Status**: ✅ Completed
* **Target Files**: `kernel/include/drivers/virtio.h`, `kernel/drivers/virtio/virtio.c`
* **Goal**: Implement standard VirtIO split virtqueue ring buffers.
* **Tasks**:
  - [x] Define VirtQueue structures.
  - [x] Implement VirtQueue initialization function (`virtq_init`).
  - [x] Implement VirtIO Device Initialization Handshake.

---

### Step 4: VirtIO-Block Driver (`include/drivers/virtio_blk.h`, `drivers/block/virtio_blk.c`)
* **Status**: ✅ Completed
* **Target Files**: `kernel/include/drivers/virtio_blk.h`, `kernel/drivers/block/virtio_blk.c`
* **Goal**: Send sector read and write commands over VirtQueue to drive `hd0` and expose them through `blockdev_t`.
* **Tasks**:
  - [x] Define VirtIO-Block Request Header.
  - [x] Implement `virtio_blk_rw(dev, lba, count, buffer, write)`.
  - [x] Register VirtIO-Block device as `"hd0"`.

---

### Step 5: Virtual File System (VFS) Core Abstractions (`include/fs/vfs.h`, `fs/vfs/vfs.c`)
* **Status**: ✅ Completed
* **Target Files**: `kernel/include/fs/vfs.h`, `kernel/fs/vfs/vfs.c`
* **Goal**: Define file system nodes, directory structures, mount tables, and core operations.
* **Tasks**:
  - [x] Define node types and flags (e.g. `VFS_FILE`, `VFS_DIRECTORY`).
  - [x] Define `vfs_node_t` and `vfs_ops_t`.
  - [x] Implement Mount Table (`vfs_mount_t`, `vfs_mount`, `vfs_get_mountpoint`).
  - [x] **Define `struct vfs_dirent`**: Needed to pass directory listing data back to the user.
    - *Plan*: Create `struct vfs_dirent { char name[128]; uint32_t flags; uint64_t inode; }` in `vfs.h`. Add `int (*readdir)(struct vfs_node *node, uint32_t index, struct vfs_dirent *out)` to `vfs_ops_t`.

---

### Step 6: Path Resolution & File Descriptor System (`include/fs/file.h`, `fs/vfs/file.c`)
* **Status**: ✅ Completed
* **Target Files**: `kernel/include/fs/file.h`, `kernel/fs/vfs/file.c`
* **Goal**: Implement standard open file descriptor operations.
* **Tasks**:
  - [x] Implement File Descriptor Table (`file_t`, `allocate_fd`).
  - [x] Implement Kernel File System API wrappers (`sys_open`, `sys_read`, `sys_close`, `sys_lseek`).
  - [x] **Implement `sys_readdir`**:
    - *Plan*: Create `int sys_readdir(int fd, struct vfs_dirent *out)`. It should use `f->offset` as the index, call `f->node->ops->readdir`, and increment `f->offset` by 1 if successful so subsequent calls return the next file.
  - [x] **Implement `sys_write`**:
    - *Plan*: Hook it up to call `f->node->ops->write`.
  - [ ] *Deferred Tech Debt*: Recursive Path Resolution Engine (`vfs_lookup`) moved to Step 11.

---

### Step 7: FAT Filesystem Driver (Read-Only) (`include/fs/fat.h`, `fs/fat/fat.c`)
* **Status**: ✅ Completed (Read-Only Experimentation)
* **Target Files**: `kernel/include/fs/fat.h`, `kernel/fs/fat/fat.c`
* **Goal**: Implement BIOS Parameter Block parsing, FAT cluster lookup, directory entry scanning, and basic file reading. *Note: Write support and full cluster-chain walking are explicitly abandoned in favor of moving to a clean Initramfs.*
* **Tasks**:
  - [x] Define FAT Boot Sector & BPB structures.
  - [x] Calculate FAT Layout Parameters via `fat_node_data`.
  - [x] Implement `fat_finddir` (with 8.3 name trimming support).
  - [x] Implement `fat_close` (correctly handling the `bpb` lifetime vs `entry` lifetime).
  - [x] **Implement Directory Entry Scanning (`fat_readdir`)**:
    - *Plan*: Read the root directory sectors (or the subdirectory cluster chain later). Jump to the `index`th valid (non-deleted, non-LFN, non-VolumeID) entry. Copy the name into `out->name` and set `out->flags` based on attributes. Return 1 on success, 0 when no more files exist.
  - [❌] **Implement File Content Reading (`fat_read`) handling `offset` and Cluster Chains**:
    - *Plan*: Abandoned. Kept simple read-only for first cluster only for experimentation purposes.
  - [❌] **Implement File Content Writing (`fat_write`)**:
    - *Plan*: Abandoned. Write support will not be implemented for FAT16.

---

### Step 8: Mount FAT Volume to VFS Root (`/`)
* **Status**: ✅ Completed
* **Target Files**: `kernel/src/main.c`, `kernel/fs/vfs/vfs.c`
* **Goal**: Initialize storage subsystems at boot and mount the `hd0` FAT volume onto the root VFS mount point.
* **Tasks**:
  - [x] Boot sequence additions in `main.c` (`acpi_init`, `pci_init`, `virtio_blk_init`, `vfs_init`).
  - [x] Mount `hd0p0` to `/` via `fat_mount` and `vfs_mount`.

---

### Step 9: Runtime Verification & Integration Unit Tests in `main.c`
* **Status**: ✅ Completed
* **Target Files**: `kernel/src/main.c`
* **Goal**: Provide empirical proof that disk reading, VFS navigation, and FAT parsing work correctly.
* **Tasks**:
  - [x] Test 1: Block Device Sector 0 Inspection.
  - [x] Test 3: File Lookup & Open (`sys_open`).
  - [x] Test 4: File Read (`sys_read`).
  - [x] **Test 2: Root Directory Enumeration (`sys_readdir`)**:
    - *Plan*: Open `/` (`fd = sys_open("/")`), loop calling `sys_readdir` until it returns 0, printing out every file and directory name found.
  - [❌] **Test 5: File Creation & Readback Verification (`sys_write`)**:
    - *Plan*: Abandoned. FAT write support cancelled. Write tests will be reintroduced when a native read-write filesystem is implemented.

---

### Step 10: Refactor PCI Subsystem for Multi-Device Support
* **Status**: ⛔ Moot — `kernel/drivers/bus/pci.c` was removed in the Phase 2 rip-out; nothing to refactor
* **Target Files**: `kernel/include/drivers/pci.h`, `kernel/drivers/bus/pci.c`, `kernel/drivers/block/virtio_blk.c`
* **Goal**: Transition from a single-device `pci_find_device` model to a callback-driven or index-based enumeration model to support multiple hard drives (e.g., `hd1`, `hd2`).
* **Tasks**:
  - [ ] Refactor `pci_find_device` to accept an `index` parameter (to find the Nth matching device).
  - [ ] Generate dynamic names (`"hd0"`, `"hd1"`) for each registered block device.

---

### Step 11: Implement Iterative VFS Path Resolution (`vfs_lookup`)
* **Status**: ✅ Completed — and since extended. `vfs_lookup()` is one of three entry points onto a shared resolver (`vfs_inner_find_and_create` in `kernel/fs/vfs/vfs.c`) parameterised by what it is permitted to create; the others are `vfs_create()` and `vfs_mkdir_parents()`. The tokenizer works on a stack copy of the path, because `strtokr` mutates its input.
* **Target Files**: `kernel/include/fs/vfs.h`, `kernel/fs/vfs/vfs.c`, `kernel/fs/vfs/file.c`
* **Goal**: Replace the shortcut in `sys_open` with a proper VFS path tokenization engine that can traverse subdirectories natively (e.g., resolving `/boot/limine.conf`).
* **Tasks**:
  - [ ] Implement `vfs_node_t *vfs_lookup(const char *path)`.
  - [ ] Split the incoming path string into directory tokens using `/` as a delimiter.
  - [ ] Iteratively call `node->ops->finddir(node, token)` traversing down the tree.

---

### Step 12: Implement FAT Subdirectory Support
* **Status**: ⛔ Moot — `kernel/fs/fat/fat.c` was removed in the Phase 2 rip-out; no FAT driver left to extend
* **Target Files**: `kernel/fs/fat/fat.c`
* **Goal**: Update `fat_vfs_ops_finddir` to properly search inside subdirectories instead of hardcoding the search to the Root Directory.
* **Tasks**:
  - [ ] Check if the incoming `dir_node` is the root node or a subdirectory node.
  - [ ] If it is a subdirectory, extract the starting cluster from `dir_node->priv_data->entry->fst_clus_lo`.
  - [ ] Walk the cluster chain of that subdirectory to search for 32-byte directory entries, rather than reading `root_start_sector`.

---

### Step 13: Implement Initramfs (TAR Filesystem)
* **Status**: ✅ Completed — and built differently from the sketch below. There is no "TAR VFS driver" with its own `vfs_ops`: storage lives in `fs/ramfs/` (read *and* write, over a sparse page list) and `fs/tar/` is only a parser calling the public VFS API. The "Memory-Mapped Read" task below never happened — bytes are copied into ramfs-owned pages at load time rather than read in place, which is what makes the filesystem writable. Current design in [`PHASE2_STEP14_RAMFS.md`](PHASE2_STEP14_RAMFS.md); [`PHASE2_STEP13_TAR_DRIVER.md`](PHASE2_STEP13_TAR_DRIVER.md) Chapters 3-4 are superseded.
* **Target Files**: `kernel/fs/tar/tar.c`, `kernel/core/boot.c`
* **Goal**: Mount an in-memory TAR archive provided by the Limine bootloader to serve as the initial RAM disk, bypassing disk I/O for early boot files.
* **Tasks**:
  - [ ] **Limine Module Parsing**: Update `boot.c` to parse the Limine module request and locate the physical memory address and size of the loaded `initrd.tar`.
  - [ ] **TAR VFS Driver**: Create `fs/tar/tar.c` with its own `vfs_ops_t` (close, read, finddir, readdir).
  - [ ] **TAR Header Parsing**: Implement logic to read `ustar` TAR headers, extract file names, and calculate file sizes to satisfy `vfs_node_t` creation.
  - [ ] **Memory-Mapped Read**: Implement `tar_read` which simply copies bytes from the RAM address of the file's data block into the user buffer (no disk I/O needed).
  - [ ] **Mount to VFS**: Mount this TAR filesystem to `/initrd` (or use it as root `/`) during boot.

---

## 🛠️ Summary of Planned File Locations (Linux-Like Layout)

*(Table remains the same)*
