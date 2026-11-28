#include "decomp/psx_gpu.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/savestate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PSX.EXE's address for a CREDITS.EXE address used by the shared libgpu checks. Inside each
 * libgpu region the two executables differ by a constant; the rest are listed. */
static u32 psx_address(u32 address) {
    static const struct {
        u32 start, end;
        s32 delta;
    } regions[] = {
        {0x80018300, 0x80018700, 0x118},   /* libgpu diagnostics */
        {0x8001b414, 0x8001ed00, 0x1558},  /* libgpu code */
        {0x80024500, 0x80024680, 0x2988},  /* libgpu data */
        {0x80024cec, 0x80024d40, 0x2fe0},  /* clear-rectangle packet */
        {0x80027488, 0x800274a0, 0x2b034}, /* last command and the command queue */
    };
    static const u32 others[][2] = {
        {0x8001935c, 0x80025cd8},                           /* jr ra; nop */
        {0x80019928, 0x8001a508},                           /* memcpy */
        {0x80019a60, 0x8001a640},                           /* printf */
        {0x8001a454, 0x8001b9bc},                           /* VSync */
        {0x8001a698, 0x8001bc00},                           /* ResetCallback */
        {0x8001a6f8, 0x8001bc60},                           /* DMACallback */
        {0x8001a814, 0x8001bd7c},                           /* SetIntrMask */
        {0x80023400, 0x80025d88},                           /* VSync counters */
        {0x80023404, 0x80025d8c}, {0x800244a0, 0x80026e28}, /* interrupt driver tables */
        {0x800244a8, 0x80026e30}, {0x800244d4, 0x80026e5c},
    };
    for (unsigned i = 0; i < sizeof regions / sizeof regions[0]; i++)
        if (address >= regions[i].start && address < regions[i].end)
            return address + (u32)regions[i].delta;
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++)
        if (address == others[i][0])
            return others[i][1];
    return address; /* scratch RAM and arbitrary values */
}

#define GA(address) psx_address(address)
#define GPU_NATIVE(name) nc_psx_##name
#define GPU_LEAF(name) ((bool (*)(CPUState *))NULL)
#include "sdk_gpu.h"

/* Every handwritten routine leaves the machine untouched when the budget cannot reach its
 * first call or the end of a leaf; none runs on one cycle. */
static bool (*const routines[])(CPUState *) = {
    nc_psx_default_draw,      nc_psx_texture_page,      nc_psx_dump_texture_page,
    nc_psx_draw_tpage,        nc_psx_dump_draw,         nc_psx_dump_display,
    nc_psx_reset_graph,       nc_psx_set_graph_reverse, nc_psx_draw_sync_callback,
    nc_psx_draw_sync,         nc_psx_check_rect,        nc_psx_clear_image,
    nc_psx_clear_image2,      nc_psx_load_image,        nc_psx_store_image,
    nc_psx_draw_otag,         nc_psx_put_draw_env,      nc_psx_draw_otag_env,
    nc_psx_put_display_env,   nc_psx_draw_area_packet,  nc_psx_draw_offset_packet,
    nc_psx_draw_mode_packet,  nc_psx_set_draw_env,      nc_psx_clear_gpu_rectangle,
    nc_psx_upload_gpu_image,  nc_psx_read_gpu_image,    nc_psx_submit_gpu_command,
    nc_psx_execute_gpu_queue, nc_psx_reset_gpu_driver,  nc_psx_sync_gpu_queue,
    nc_psx_check_queue_alarm, nc_psx_set_geom_offset,   nc_psx_patch_gte,
};

static bool check_declines(void) {
    bool ok = true;
    for (unsigned i = 0; ok && i < sizeof routines / sizeof routines[0]; i++)
        for (int budget = -1; ok && budget <= 1; budget++) {
            StateIO before = {0}, after = {0};
            nc_cpu = arguments(0x80060000, 0x80060100, 0x80060200, budget);
            cpu_serialize(&before);
            ok = !routines[i](&nc_cpu);
            cpu_serialize(&after);
            ok = ok && !before.error && !after.error && before.len == after.len &&
                 !memcmp(before.buf, after.buf, before.len);
            free(before.buf);
            free(after.buf);
            if (!ok)
                fprintf(stderr, "routine %u ran with budget %d\n", i, budget);
        }
    return ok;
}

/* SetGeomOffset shifts both offsets into 16.16 fixed point. */
static bool check_geom_offset_rules(void) {
    const u32 values[][2] = {{0, 0}, {160, 120}, {0xffff, 0x8000}, {0x12345678, 0xfffffff0}};
    bool ok = true;
    for (unsigned i = 0; ok && i < sizeof values / sizeof values[0]; i++) {
        nc_cpu = arguments(values[i][0], values[i][1], 0, 7);
        ok = nc_psx_set_geom_offset(&nc_cpu) && nc_cpu.r[4] == values[i][0] << 16 &&
             nc_cpu.r[5] == values[i][1] << 16 &&
             gte_read_ctrl(&nc_cpu, 24) == values[i][0] << 16 &&
             gte_read_ctrl(&nc_cpu, 25) == values[i][1] << 16 && nc_cpu.budget == 1;
    }
    nc_cpu = arguments(1, 2, 0, 6);
    return ok && !nc_psx_set_geom_offset(&nc_cpu) && nc_cpu.r[4] == 1;
}

static bool check_geom_offset(void) {
    const u32 values[][2] = {{0, 0}, {160, 120}, {0xffff, 0x8000}, {0x12345678, 0xfffffff0}};
    for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++)
        for (int budget = 7; budget <= 9; budget++)
            if (!compare(arguments(values[i][0], values[i][1], 0, budget), 0x80021a38,
                         nc_psx_set_geom_offset, 6))
                return false;
    return true;
}

/* _patch_gte copies fourteen words over the handler the BIOS C0 table points at. */
static bool check_patch_gte(void) {
    const int budgets[] = {5, 9, 30, 64, 4096};
    bool ok = true;
    for (unsigned i = 0; ok && i < sizeof budgets / sizeof budgets[0]; i++) {
        CPUState state = arguments(1, 2, 3, budgets[i]);
        ok = compare(state, 0x80021a68, nc_psx_patch_gte, 4);
        if (!ok)
            fprintf(stderr, "_patch_gte budget %d failed\n", budgets[i]);
    }
    return ok;
}

#define CHECK(call) (call || (fprintf(stderr, "failed: %s\n", #call), false))

int main(int argc, char **argv) {
    if (argc == 1)
        return check_declines() && check_geom_offset_rules() ? 0 : 1;
    if (argc != 2)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 77;
    fclose(file);
    ExecInfo exe;
    if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX.EXE;1", &exe))
        return 1;
    bool pass = CHECK(check_declines());
    pass = pass && CHECK(check_geom_offset());
    pass = pass && CHECK(check_patch_gte());
    pass = pass && CHECK(check_draw_mode_packet());
    pass = pass && CHECK(check_set_draw_env());
    pass = pass && CHECK(check_draw_offset());
    pass = pass && CHECK(check_rectangle_diagnostics());
    pass = pass && CHECK(check_set_graph_reverse());
    pass = pass && CHECK(check_image_queue(GA(0x8001c474), nc_psx_clear_image, 0x0c));
    pass = pass && CHECK(check_image_queue(GA(0x8001c508), nc_psx_clear_image2, 0x0c));
    pass = pass && CHECK(check_image_queue(GA(0x8001c5a4), nc_psx_load_image, 0x20));
    pass = pass && CHECK(check_image_queue(GA(0x8001c608), nc_psx_store_image, 0x1c));
    pass = pass && CHECK(check_put_display_env());
    pass = pass && CHECK(check_draw_env_submission(false));
    pass = pass && CHECK(check_draw_env_submission(true));
    pass = pass && CHECK(check_draw_otag());
    pass = pass && CHECK(check_draw_sync());
    pass = pass && CHECK(check_draw_sync_callback());
    pass = pass && CHECK(check_dump_environment(false, 0));
    for (unsigned graph = 0; graph <= 2; graph++)
        pass = pass && CHECK(check_dump_environment(true, graph));
    pass = pass && CHECK(check_dump_texture_page());
    pass = pass && CHECK(check_draw_area());
    pass = pass && CHECK(check_draw_mode_words());
    pass = pass && CHECK(check_default_draw());
    pass = pass && CHECK(check_texture_page());
    pass = pass && CHECK(check_gpu_queue_sync());
    pass = pass && CHECK(check_execute_gpu_queue());
    pass = pass && CHECK(check_submit_gpu_command());
    pass = pass && CHECK(check_queue_alarm_expiry());
    pass = pass && CHECK(check_gpu_driver_reset());
    pass = pass && CHECK(check_reset_graph());
    pass = pass && CHECK(check_gpu_clear_rectangle());
    pass = pass && CHECK(check_gpu_image_transfer(false));
    pass = pass && CHECK(check_gpu_image_transfer(true));
    disc_close();
    return pass ? 0 : 1;
}
