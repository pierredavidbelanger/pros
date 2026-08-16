#include "core/test/test.h"

#include "core/memory.h"
#include "core/syscalls.h"
#include "fs/vfs/vfs.h"
#include "mm/pmm.h"

#define TEST_VFS_TAG "VFS"

// what the root Makefile puts in initrd.tar
#define TEST_VFS_INITRD_DIR  "/root"
#define TEST_VFS_INITRD_FILE "/root/hello.txt"
#define TEST_VFS_INITRD_TEXT "HelloWorld\n"
#define TEST_VFS_INITRD_SIZE 11

// scratch area the write tests build for themselves, so they never depend on the archive
#define TEST_VFS_SCRATCH_DIR "/test/scratch"

#define TEST_VFS_BUF_SIZE 512

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

// seeks then reads, returning whatever sys_read reported
static int64_t test_vfs_read_at(int fd, uint64_t offset, void *buf, uint64_t size) {
    if (sys_lseek(fd, offset, SEEK_SET) < 0) return -1;
    return sys_read(fd, buf, size);
}

static void test_vfs_initrd(void) {
    // the directory the loader built out of the archive must open and enumerate
    int fd = sys_open(TEST_VFS_INITRD_DIR, O_RDONLY);
    test_report(TEST_VFS_TAG, "open initrd directory", fd >= 0);
    if (fd < 0) return;

    // look for a specific name rather than a fixed count, so adding entries to initrd.tar
    // does not break this test
    struct vfs_dirent entry;
    bool found_hello = false;
    uint32_t entry_count = 0;
    while (sys_readdir(fd, &entry) == 1) {
        entry_count++;
        if (strncmp(entry.name, "hello.txt", VFS_NAME_SIZE) == 0) found_hello = true;
    }
    sys_close(fd);
    test_report(TEST_VFS_TAG, "readdir enumerates entries", entry_count > 0);
    test_report(TEST_VFS_TAG, "readdir finds hello.txt", found_hello);

    // resolving a path that was never created must fail rather than inventing a node
    test_report(TEST_VFS_TAG, "open of a missing path fails", sys_open("/root/nope.txt", O_RDONLY) < 0);

    fd = sys_open(TEST_VFS_INITRD_FILE, O_RDONLY);
    test_report(TEST_VFS_TAG, "open initrd file", fd >= 0);
    if (fd < 0) return;

    // the whole point of the tar loader: archive bytes reach the filesystem intact
    uint8_t buf[TEST_VFS_BUF_SIZE];
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

static void test_vfs_directories(void) {
    // a directory is not a byte stream, and its private data holds live child pointers
    int fd = sys_open(TEST_VFS_INITRD_DIR, O_RDONLY);
    if (fd >= 0) {
        uint8_t buf[16];
        test_report(TEST_VFS_TAG, "read of a directory fails", sys_read(fd, buf, sizeof(buf)) < 0);
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

static void test_vfs_write(void) {
    // a file this test owns, so nothing here depends on the archive's contents
    struct vfs_node *node = test_vfs_make_file(TEST_VFS_SCRATCH_DIR "/rw.bin");
    test_report(TEST_VFS_TAG, "create a file through the VFS", node != NULL);
    if (!node) return;

    int fd = sys_open(TEST_VFS_SCRATCH_DIR "/rw.bin", O_RDWR);
    test_report(TEST_VFS_TAG, "open the created file", fd >= 0);
    if (fd < 0) return;

    uint8_t buf[TEST_VFS_BUF_SIZE];

    // the simplest round trip there is: what goes in comes back out
    memset(buf, 0x3C, 200);
    test_report(TEST_VFS_TAG, "write returns the byte count", sys_write(fd, buf, 200) == 200);
    test_report(TEST_VFS_TAG, "write updates the file size", node->size == 200);

    memset(buf, 0x00, sizeof(buf));
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

static void test_vfs_page_boundary(void) {
    // 300 bytes starting at 4000 straddles the end of page 0 and the start of page 1, the only
    // thing that exercises the chunk loop's page_offset arithmetic
    struct vfs_node *node = test_vfs_make_file(TEST_VFS_SCRATCH_DIR "/cross.bin");
    if (!node) return;

    int fd = sys_open(TEST_VFS_SCRATCH_DIR "/cross.bin", O_RDWR);
    if (fd < 0) return;

    uint8_t buf[TEST_VFS_BUF_SIZE];
    memset(buf, 0xC3, 300);

    sys_lseek(fd, 4000, SEEK_SET);
    test_report(TEST_VFS_TAG, "write across a page boundary", sys_write(fd, buf, 300) == 300);
    test_report(TEST_VFS_TAG, "cross page write sets the size", node->size == 4300);

    memset(buf, 0x00, sizeof(buf));
    int64_t count = test_vfs_read_at(fd, 4000, buf, 300);
    test_report(TEST_VFS_TAG, "read across a page boundary", count == 300 && test_vfs_is_filled(buf, 300, 0xC3));

    sys_close(fd);
}

static void test_vfs_sparse(void) {
    // seek far past EOF and write: everything skipped over must read back as zeros without ever
    // being allocated. The tar loader only writes sequentially from offset 0, so this hole path
    // has no other coverage anywhere in the tree.
    struct vfs_node *node = test_vfs_make_file(TEST_VFS_SCRATCH_DIR "/sparse.bin");
    if (!node) return;

    int fd = sys_open(TEST_VFS_SCRATCH_DIR "/sparse.bin", O_RDWR);
    if (fd < 0) return;

    uint8_t buf[TEST_VFS_BUF_SIZE];

    sys_lseek(fd, 2 * PAGE_SIZE, SEEK_SET);
    test_report(TEST_VFS_TAG, "write past EOF", sys_write(fd, "DATA", 4) == 4);
    test_report(TEST_VFS_TAG, "sparse write sets the size", node->size == 2 * PAGE_SIZE + 4);

    // page 0 was never touched
    memset(buf, 0xFF, sizeof(buf));
    int64_t count = test_vfs_read_at(fd, 0, buf, TEST_VFS_BUF_SIZE);
    test_report(TEST_VFS_TAG, "hole at page 0 reads as zeros", count == TEST_VFS_BUF_SIZE && test_vfs_is_zeroed(buf, TEST_VFS_BUF_SIZE));

    // neither was page 1
    memset(buf, 0xFF, sizeof(buf));
    count = test_vfs_read_at(fd, PAGE_SIZE, buf, TEST_VFS_BUF_SIZE);
    test_report(TEST_VFS_TAG, "hole at page 1 reads as zeros", count == TEST_VFS_BUF_SIZE && test_vfs_is_zeroed(buf, TEST_VFS_BUF_SIZE));

    // and the real data is still where it was put
    memset(buf, 0x00, sizeof(buf));
    count = test_vfs_read_at(fd, 2 * PAGE_SIZE, buf, TEST_VFS_BUF_SIZE);
    test_report(TEST_VFS_TAG, "data after the hole is intact", count == 4 && memcmp(buf, "DATA", 4) == 0);

    sys_close(fd);
}

void test_vfs(void) {
    test_vfs_initrd();
    test_vfs_directories();
    test_vfs_write();
    test_vfs_page_boundary();
    test_vfs_sparse();
}
