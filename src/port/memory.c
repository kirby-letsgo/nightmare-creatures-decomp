#include "port/runtime.h"

/* Hardware register space (0x1F801000..) and anything else outside RAM/scratchpad. */
u32 io_read(u32 phys, int size) {
    NC_LOG("io_read%d 0x%08X", size * 8, phys);
    return 0;
}

void io_write(u32 phys, u32 value, int size) {
    NC_LOG("io_write%d 0x%08X = 0x%08X", size * 8, phys, value);
}
