#include "port/disc.h"
#include "port/exe.h"
#include "port/hw/gpu.h"
#include "port/hw/hw.h"
#include "port/runtime.h"
#include "port/savestate.h"
#include <stdlib.h>

/* Run once per process so both executions start with fresh BIOS and devices.
 * The two binaries share this driver and differ only in their dispatch tables. */
static FILE *trace;
static bool skip;

static void record(const void *data, size_t size) {
    if (fwrite(data, 1, size, trace) != size)
        exit(1);
}

static void frame(void) {
    if (nc_frame_count > 5000)
        exit(1);
    bios_set_pad(skip && nc_frame_count >= 240 ? 8 : 0);
    record(&nc_cpu, sizeof nc_cpu);
    record(&nc_cycles, sizeof nc_cycles);
    /* Sample all three strips while the image is still scrolling. */
    if (nc_frame_count % 128 == 0) {
        static u32 pixels[1024 * 512];
        const GpuDisplay vram = {.width = 1024, .height = 512, .scale = 1};
        gpu_display_rgba(pixels, &vram);
        record(pixels, sizeof pixels);
    }
}

int main(int argc, char **argv) {
    if (argc != 4 || !disc_open(argv[1]))
        return 1;
    skip = !strcmp(argv[2], "skip");
    trace = fopen(argv[3], "wb");
    if (!trace)
        return 1;
    bios_init();
    ExecInfo exe;
    if (!exe_load("cdrom:\\SLUS_005.82;1", &exe) || !exe_load("cdrom:\\CREDITS.EXE;1", &exe))
        return 1;
    nc_frame_hook = frame;
    nc_cpu.cop0[12] = 0x40000401;
    exe_exec(&nc_cpu, &exe, 0, 0);
    /* The credits exit writes mode 2 to the launcher's shared record. */
    if (MEM_R32(MEM_R32(0x80024c70) + 0x24) != 2)
        return 1;
    StateIO final = {0};
    cpu_serialize(&final);
    spu_serialize(&final);
    if (final.error)
        return 1;
    record(final.buf, final.len);
    free(final.buf);
    /* Read back every VRAM word, including pixels' mask bits. */
    gpu_gp0(0xc0000000);
    gpu_gp0(0);
    gpu_gp0((512u << 16) | 1024u);
    for (unsigned i = 0; i < 1024 * 512 / 2; i++) {
        u32 pair = gpu_read();
        record(&pair, sizeof pair);
    }
    fprintf(stderr, "Credits returned after %u VBlanks\n", nc_frame_count);
    disc_close();
    return fclose(trace) ? 1 : 0;
}
