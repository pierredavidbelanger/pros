#include "core/kprintf.h"
#include "core/memory.h"
#include "core/syscalls.h"
#include "core/test/test.h"
#include "errno.h"
#include "fs/vfs/file.h"
#include "fs/vfs/vfs.h"
#include "mm/pmm.h"
#include "mm/vmm.h"

#define TEST_VFS_TAG "VFS"

// what the root Makefile puts in initrd.tar
#define TEST_VFS_INITRD_DIR "/root"
#define TEST_VFS_INITRD_FILE "/root/hello.txt"
#define TEST_VFS_INITRD_TEXT "HelloWorld\n"
#define TEST_VFS_INITRD_SIZE 11

// scratch area the write tests build for themselves, so they never depend on the archive
#define TEST_VFS_SCRATCH_DIR "/test/scratch"

// a directory the getdents64 tests own, so the entry count and the names are known
#define TEST_VFS_DENTS_DIR TEST_VFS_SCRATCH_DIR "/dents"

// where d_name starts in a packed record, and the biggest record the kernel can ever build
#define TEST_VFS_DENT_NAME_OFFSET (offsetof(struct linux_dirent64, d_name))
#define TEST_VFS_DENT_MAX_RECLEN ((TEST_VFS_DENT_NAME_OFFSET + VFS_NAME_SIZE + 7) & ~7ULL)

#define TEST_VFS_BUF_SIZE 512

// sys_read/sys_write now validate the buffer is real user memory,
// so the tests need one below VMM_ADDR_SPLIT instead of a kernel stack array
#define TEST_VFS_BUF_VIRT 0x50000000ULL

static bool test_vfs_is_zeroed(const uint8_t *buf, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (buf[i] != 0x00) return false;
    }
    return true;
}

static bool test_vfs_is_filled(const uint8_t *buf, size_t size, uint8_t value) {
    for (size_t i = 0; i < size; i++) {
        if (buf[i] != value) return false;
    }
    return true;
}

// creates every missing directory on the path, then the leaf file itself
static struct vfs_node *test_vfs_make_file(const char *path) {
    if (!vfs_mkdir_parents(path)) return NULL;
    return vfs_create(path, VFS_FILE);
}

// walks a packed getdents64 buffer end to end, counting entries and looking for `name`.
// well_formed stays true only if every record is 8 aligned, stays inside len, holds its own
// name, and the walk lands exactly on len -- landing anywhere else means a d_reclen lied
static uint32_t test_vfs_walk_dents(const uint8_t *buf, int64_t len, const char *name, bool *found, bool *well_formed) {
    *found = false;
    *well_formed = true;

    uint32_t count = 0;
    int64_t pos = 0;
    while (pos < len) {
        const struct linux_dirent64 *dent = (const struct linux_dirent64 *)(buf + pos);
        // not 8 aligned means the next record starts misaligned
        if (dent->d_reclen == 0 || dent->d_reclen % 8 != 0) break;
        // and a record must not claim more than the kernel said it wrote
        if (pos + dent->d_reclen > len) break;
        // the name plus its NUL has to fit in the record holding it
        if (TEST_VFS_DENT_NAME_OFFSET + strlen(dent->d_name) + 1 > dent->d_reclen) break;
        if (name && strncmp(dent->d_name, name, VFS_NAME_SIZE) == 0) *found = true;
        pos += dent->d_reclen;
        count++;
    }

    if (pos != len) *well_formed = false;
    return count;
}

// finds one entry by name, NULL if this buffer does not hold it
static const struct linux_dirent64 *test_vfs_find_dent(const uint8_t *buf, int64_t len, const char *name) {
    int64_t pos = 0;
    while (pos < len) {
        const struct linux_dirent64 *dent = (const struct linux_dirent64 *)(buf + pos);
        if (dent->d_reclen == 0 || pos + dent->d_reclen > len) return NULL;
        if (strncmp(dent->d_name, name, VFS_NAME_SIZE) == 0) return dent;
        pos += dent->d_reclen;
    }
    return NULL;
}

// seeks then reads, returning whatever sys_read reported
static int64_t test_vfs_read_at(int fd, uint64_t offset, void *buf, uint64_t size) {
    if (sys_lseek(fd, offset, SEEK_SET) < 0) return -1;
    return sys_read(fd, buf, size);
}

static void test_vfs_initrd(uint8_t *buf) {
    // the directory the loader built out of the archive must open and enumerate
    int fd = sys_open(TEST_VFS_INITRD_DIR, O_RDONLY);
    test_report(TEST_VFS_TAG, "open initrd directory", fd >= 0);
    if (fd < 0) return;

    // look for a specific name rather than a fixed count, so adding entries to initrd.tar
    // does not break this test. the loop also covers the multi call path if it ever grows
    bool found_hello = false;
    bool well_formed = true;
    uint32_t entry_count = 0;
    int64_t len;
    while ((len = sys_getdents64(fd, buf, TEST_VFS_BUF_SIZE)) > 0) {
        bool found, ok;
        entry_count += test_vfs_walk_dents(buf, len, "hello.txt", &found, &ok);
        found_hello = found_hello || found;
        well_formed = well_formed && ok;
    }
    sys_close(fd);
    test_report(TEST_VFS_TAG, "getdents64 enumerates entries", entry_count > 0);
    test_report(TEST_VFS_TAG, "getdents64 records are well formed", well_formed);
    test_report(TEST_VFS_TAG, "getdents64 finds hello.txt", found_hello);
    // draining a directory ends on a clean zero, not an error
    test_report(TEST_VFS_TAG, "getdents64 stops at the end", len == 0);

    // resolving a path that was never created must fail rather than inventing a node
    test_report(TEST_VFS_TAG, "open of a missing path fails", sys_open("/root/nope.txt", O_RDONLY) < 0);

    fd = sys_open(TEST_VFS_INITRD_FILE, O_RDONLY);
    test_report(TEST_VFS_TAG, "open initrd file", fd >= 0);
    if (fd < 0) return;

    // the whole point of the tar loader: archive bytes reach the filesystem intact
    int64_t count = sys_read(fd, buf, TEST_VFS_BUF_SIZE);
    test_report(TEST_VFS_TAG, "read clamps to the file size", count == TEST_VFS_INITRD_SIZE);
    test_report(TEST_VFS_TAG, "read returns the file content", count == TEST_VFS_INITRD_SIZE && memcmp(buf, TEST_VFS_INITRD_TEXT, TEST_VFS_INITRD_SIZE) == 0);

    // a read that starts at EOF is not an error, it is simply empty
    test_report(TEST_VFS_TAG, "read at EOF returns zero", test_vfs_read_at(fd, TEST_VFS_INITRD_SIZE, buf, TEST_VFS_BUF_SIZE) == 0);

    // seeking mid file must return the tail, clamped to what is actually there
    count = test_vfs_read_at(fd, 5, buf, TEST_VFS_BUF_SIZE);
    test_report(TEST_VFS_TAG, "read from an offset returns the tail", count == 6 && memcmp(buf, "World\n", 6) == 0);

    // SEEK_END is how a caller discovers the size without a stat syscall
    test_report(TEST_VFS_TAG, "lseek SEEK_END reports the size", sys_lseek(fd, 0, SEEK_END) == TEST_VFS_INITRD_SIZE);

    sys_close(fd);

    // a descriptor must stop working once it is closed
    test_report(TEST_VFS_TAG, "read on a closed fd fails", sys_read(fd, buf, TEST_VFS_BUF_SIZE) < 0);
}

static void test_vfs_directories(uint8_t *buf) {
    // a directory is not a byte stream, and its private data holds live child pointers
    int fd = sys_open(TEST_VFS_INITRD_DIR, O_RDONLY);
    if (fd >= 0) {
        test_report(TEST_VFS_TAG, "read of a directory fails", sys_read(fd, buf, 16) < 0);
        sys_close(fd);
    }

    // mkdir -p has to build every missing level, not just the last one
    struct vfs_node *parent = vfs_mkdir_parents(TEST_VFS_SCRATCH_DIR "/deep/file.txt");
    test_report(TEST_VFS_TAG, "mkdir_parents builds the chain", parent != NULL);

    struct vfs_node *mid = vfs_lookup(TEST_VFS_SCRATCH_DIR "/deep");
    test_report(TEST_VFS_TAG, "intermediate directory is created", mid != NULL && (mid->flags & VFS_DIRECTORY) != 0);

    // and must not create the leaf itself, that is what open(O_CREAT) is for
    test_report(TEST_VFS_TAG, "mkdir_parents leaves the leaf alone", vfs_lookup(TEST_VFS_SCRATCH_DIR "/deep/file.txt") == NULL);
}

static void test_vfs_write(uint8_t *buf) {
    // a file this test owns, so nothing here depends on the archive's contents
    struct vfs_node *node = test_vfs_make_file(TEST_VFS_SCRATCH_DIR "/rw.bin");
    test_report(TEST_VFS_TAG, "create a file through the VFS", node != NULL);
    if (!node) return;

    int fd = sys_open(TEST_VFS_SCRATCH_DIR "/rw.bin", O_RDWR);
    test_report(TEST_VFS_TAG, "open the created file", fd >= 0);
    if (fd < 0) return;

    // the simplest round trip there is: what goes in comes back out
    memset(buf, 0x3C, 200);
    test_report(TEST_VFS_TAG, "write returns the byte count", sys_write(fd, buf, 200) == 200);
    test_report(TEST_VFS_TAG, "write updates the file size", node->size == 200);

    memset(buf, 0x00, TEST_VFS_BUF_SIZE);
    int64_t count = test_vfs_read_at(fd, 0, buf, 200);
    test_report(TEST_VFS_TAG, "read back what was written", count == 200 && test_vfs_is_filled(buf, 200, 0x3C));

    // overwriting inside the file must not extend it
    sys_lseek(fd, 100, SEEK_SET);
    memset(buf, 0x7E, 50);
    sys_write(fd, buf, 50);
    test_report(TEST_VFS_TAG, "overwrite does not grow the file", node->size == 200);

    count = test_vfs_read_at(fd, 100, buf, 50);
    test_report(TEST_VFS_TAG, "overwritten bytes read back", count == 50 && test_vfs_is_filled(buf, 50, 0x7E));

    sys_close(fd);

    // writing to a directory would allocate pages into a node whose page list must stay empty
    int dir_fd = sys_open(TEST_VFS_SCRATCH_DIR, O_RDWR);
    if (dir_fd >= 0) {
        test_report(TEST_VFS_TAG, "write to a directory fails", sys_write(dir_fd, buf, 16) < 0);
        sys_close(dir_fd);
    }
}

static void test_vfs_page_boundary(uint8_t *buf) {
    // 300 bytes starting at 4000 straddles the end of page 0 and the start of page 1, the only
    // thing that exercises the chunk loop's page_offset arithmetic
    struct vfs_node *node = test_vfs_make_file(TEST_VFS_SCRATCH_DIR "/cross.bin");
    if (!node) return;

    int fd = sys_open(TEST_VFS_SCRATCH_DIR "/cross.bin", O_RDWR);
    if (fd < 0) return;

    memset(buf, 0xC3, 300);

    sys_lseek(fd, 4000, SEEK_SET);
    test_report(TEST_VFS_TAG, "write across a page boundary", sys_write(fd, buf, 300) == 300);
    test_report(TEST_VFS_TAG, "cross page write sets the size", node->size == 4300);

    memset(buf, 0x00, TEST_VFS_BUF_SIZE);
    int64_t count = test_vfs_read_at(fd, 4000, buf, 300);
    test_report(TEST_VFS_TAG, "read across a page boundary", count == 300 && test_vfs_is_filled(buf, 300, 0xC3));

    sys_close(fd);
}

static void test_vfs_sparse(uint8_t *buf) {
    // seek far past EOF and write: everything skipped over must read back as zeros without ever
    // being allocated. The tar loader only writes sequentially from offset 0, so this hole path
    // has no other coverage anywhere in the tree.
    struct vfs_node *node = test_vfs_make_file(TEST_VFS_SCRATCH_DIR "/sparse.bin");
    if (!node) return;

    int fd = sys_open(TEST_VFS_SCRATCH_DIR "/sparse.bin", O_RDWR);
    if (fd < 0) return;

    sys_lseek(fd, 2 * PAGE_SIZE, SEEK_SET);
    memcpy(buf, "DATA", 4);  // sys_write needs a real user buffer, not a rodata pointer
    test_report(TEST_VFS_TAG, "write past EOF", sys_write(fd, buf, 4) == 4);
    test_report(TEST_VFS_TAG, "sparse write sets the size", node->size == 2 * PAGE_SIZE + 4);

    // page 0 was never touched
    memset(buf, 0xFF, TEST_VFS_BUF_SIZE);
    int64_t count = test_vfs_read_at(fd, 0, buf, TEST_VFS_BUF_SIZE);
    test_report(TEST_VFS_TAG, "hole at page 0 reads as zeros", count == TEST_VFS_BUF_SIZE && test_vfs_is_zeroed(buf, TEST_VFS_BUF_SIZE));

    // neither was page 1
    memset(buf, 0xFF, TEST_VFS_BUF_SIZE);
    count = test_vfs_read_at(fd, PAGE_SIZE, buf, TEST_VFS_BUF_SIZE);
    test_report(TEST_VFS_TAG, "hole at page 1 reads as zeros", count == TEST_VFS_BUF_SIZE && test_vfs_is_zeroed(buf, TEST_VFS_BUF_SIZE));

    // and the real data is still where it was put
    memset(buf, 0x00, TEST_VFS_BUF_SIZE);
    count = test_vfs_read_at(fd, 2 * PAGE_SIZE, buf, TEST_VFS_BUF_SIZE);
    test_report(TEST_VFS_TAG, "data after the hole is intact", count == 4 && memcmp(buf, "DATA", 4) == 0);

    sys_close(fd);
}

static void test_vfs_getdents(uint8_t *buf) {
    // a directory this test owns, so the entry count and every name are known.
    // mkdir_parents leaves the leaf alone, so this builds .../dents and .../dents/sub
    if (!vfs_mkdir_parents(TEST_VFS_DENTS_DIR "/sub/leaf")) return;
    if (!test_vfs_make_file(TEST_VFS_DENTS_DIR "/short.txt")) return;

    // 127 chars plus the NUL is the longest name a vfs_dirent can carry, and the record it packs
    // into is the largest sys_getdents64 ever builds -- the one that overflows a buffer sized
    // without the round up
    char long_name[VFS_NAME_SIZE];
    memset(long_name, 'L', VFS_NAME_SIZE - 1);
    long_name[VFS_NAME_SIZE - 1] = '\0';

    char long_path[VFS_PATH_MAX];
    snprintf(long_path, VFS_PATH_MAX, "%s/%s", TEST_VFS_DENTS_DIR, long_name);
    if (!test_vfs_make_file(long_path)) return;

    int fd = sys_open(TEST_VFS_DENTS_DIR, O_RDONLY);
    test_report(TEST_VFS_TAG, "open the getdents64 directory", fd >= 0);
    if (fd < 0) return;

    // 24 + 32 + 152 fits well inside the scratch buffer, so one call takes all three
    int64_t len = sys_getdents64(fd, buf, TEST_VFS_BUF_SIZE);
    bool found, well_formed;
    uint32_t count = test_vfs_walk_dents(buf, len, NULL, &found, &well_formed);
    test_report(TEST_VFS_TAG, "getdents64 returns every entry", count == 3);
    test_report(TEST_VFS_TAG, "getdents64 walk lands exactly on the length", well_formed);

    // d_type has to survive the repack, not just the names
    const struct linux_dirent64 *sub = test_vfs_find_dent(buf, len, "sub");
    test_report(TEST_VFS_TAG, "getdents64 types a directory DT_DIR", sub != NULL && sub->d_type == DT_DIR);
    const struct linux_dirent64 *reg = test_vfs_find_dent(buf, len, "short.txt");
    test_report(TEST_VFS_TAG, "getdents64 types a file DT_REG", reg != NULL && reg->d_type == DT_REG);

    // the longest name must round up to exactly the max record, 152 bytes today
    const struct linux_dirent64 *longest = test_vfs_find_dent(buf, len, long_name);
    test_report(TEST_VFS_TAG, "getdents64 packs the longest name", longest != NULL);
    test_report(TEST_VFS_TAG, "longest record is the rounded up max", longest != NULL && longest->d_reclen == TEST_VFS_DENT_MAX_RECLEN);

    // the directory is drained now
    test_report(TEST_VFS_TAG, "getdents64 at the end returns zero", sys_getdents64(fd, buf, TEST_VFS_BUF_SIZE) == 0);

    // d_off is where lseek has to go to land on the entry AFTER this one,
    // so resuming from the first one must return the other two and never repeat it
    sys_lseek(fd, 0, SEEK_SET);
    len = sys_getdents64(fd, buf, TEST_VFS_BUF_SIZE);
    if (len > 0) {
        const struct linux_dirent64 *first = (const struct linux_dirent64 *)buf;
        int64_t after_first = first->d_off;
        // copy the name out, the next call overwrites the buffer it lives in
        char first_name[VFS_NAME_SIZE];
        snprintf(first_name, VFS_NAME_SIZE, "%s", first->d_name);

        sys_lseek(fd, after_first, SEEK_SET);
        len = sys_getdents64(fd, buf, TEST_VFS_BUF_SIZE);
        uint32_t rest = test_vfs_walk_dents(buf, len, first_name, &found, &well_formed);
        test_report(TEST_VFS_TAG, "d_off resumes after that entry", rest == 2 && !found);
    }

    // a buffer too small for even one record is the caller's error,
    // reporting it as an empty directory would be a wrong answer instead of an error
    sys_lseek(fd, 0, SEEK_SET);
    test_report(TEST_VFS_TAG, "getdents64 into a tiny buffer fails", sys_getdents64(fd, buf, 8) == -EINVAL);
    test_report(TEST_VFS_TAG, "getdents64 with a zero count fails", sys_getdents64(fd, buf, 0) == -EINVAL);

    sys_close(fd);

    // a regular file is not a directory, and saying so beats saying "not implemented"
    int file_fd = sys_open(TEST_VFS_DENTS_DIR "/short.txt", O_RDONLY);
    if (file_fd >= 0) {
        test_report(TEST_VFS_TAG, "getdents64 on a file fails", sys_getdents64(file_fd, buf, TEST_VFS_BUF_SIZE) == -ENOTDIR);
        sys_close(file_fd);
    }
}

void test_vfs(void) {
    // sys_read/sys_write bounds-check against VMM_ADDR_SPLIT,
    // so the shared scratch buffer has to be a real mapped page below it,
    // not a kernel stack array
    uint64_t buf_phys = pmm_alloc(1);
    if (!buf_phys) return;
    if (vmm_map_page(vmm_kernel_context, TEST_VFS_BUF_VIRT, buf_phys, VMM_WRITABLE) != 0) {
        pmm_free(buf_phys, 1);
        return;
    }
    uint8_t *buf = (uint8_t *)TEST_VFS_BUF_VIRT;

    test_vfs_initrd(buf);
    test_vfs_directories(buf);
    test_vfs_getdents(buf);
    test_vfs_write(buf);
    test_vfs_page_boundary(buf);
    test_vfs_sparse(buf);

    vmm_unmap_page(vmm_kernel_context, TEST_VFS_BUF_VIRT);
    pmm_free(buf_phys, 1);
}
