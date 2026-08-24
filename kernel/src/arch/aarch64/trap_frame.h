#ifndef PROS_AARCH64_TRAP_FRAME_H
#define PROS_AARCH64_TRAP_FRAME_H

#define TRAP_FRAME_OFF_X(n) ((n) * 8)

#define TRAP_FRAME_OFF_SP 248
#define TRAP_FRAME_OFF_ELR 256
#define TRAP_FRAME_OFF_SPSR 264
#define TRAP_FRAME_OFF_ESR 272
#define TRAP_FRAME_OFF_FAR 280
#define TRAP_FRAME_OFF_VECTOR_TYPE 288

// sizeof(struct trap_frame) is 296, but the stubs move sp by 304.
// The extra 8 bytes are because AArch64 check for 16-byte alignment
#define TRAP_FRAME_SIZE 304

#endif  // PROS_AARCH64_TRAP_FRAME_H
