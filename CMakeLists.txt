cmake_minimum_required(VERSION 3.20)

# 1. Set Compiler Wrapper BEFORE project() call
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_SOURCE_DIR}/scripts/zig-cc")
set(CMAKE_ASM_COMPILER "${CMAKE_CURRENT_SOURCE_DIR}/scripts/zig-cc")

# 2. Set the Architecture (defaults to 'riscv' if not specified)
set(ARCH "riscv" CACHE STRING "Architecture target: riscv or arm")

project(pros C ASM)

# 3. Architecture-Specific Flags & QEMU Commands
if (ARCH STREQUAL "riscv")
    set(ARCH_FLAGS -target riscv32-freestanding-none)
    set(QEMU qemu-system-riscv32)
    set(QEMU_FLAGS -bios none)
elseif (ARCH STREQUAL "arm")
    set(ARCH_FLAGS -target arm-freestanding-none -mcpu=cortex_a15)
    set(QEMU qemu-system-arm)
    set(QEMU_FLAGS "")
else ()
    message(FATAL_ERROR "Unknown ARCH '${ARCH}'. Must be 'riscv' or 'arm'.")
endif ()

# 4. Source Files & Executable Target
set(SRCS
        src/fdt.c
        src/kernel.c
        src/mem.c
        src/string.c
        arch/${ARCH}/arch.c
        arch/${ARCH}/boot.S
)

add_executable(kernel.elf ${SRCS})

# 5. Include Directories
target_include_directories(kernel.elf PUBLIC include)

# 6. Compiler Options
target_compile_options(kernel.elf PRIVATE
        ${ARCH_FLAGS}
        -fno-sanitize=undefined
        -ffreestanding
)

# 7. Linker Options & Linker Script
target_link_options(kernel.elf PRIVATE
        ${ARCH_FLAGS}
        -fno-sanitize=undefined
        -ffreestanding
        -T "${CMAKE_CURRENT_SOURCE_DIR}/arch/${ARCH}/linker.ld"
)

# 8. Add QEMU 'run' Target
add_custom_target(run
        COMMAND ${QEMU} ${QEMU_FLAGS} -machine virt -nographic --no-reboot -kernel $<TARGET_FILE:kernel.elf>
        DEPENDS kernel.elf
        USES_TERMINAL
)
