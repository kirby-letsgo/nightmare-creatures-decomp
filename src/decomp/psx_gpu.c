#include "decomp/psx_gpu.h"
#include "decomp/sdk_gpu.h"

/* PSX.EXE links the same libgpu as CREDITS.EXE, its code 0x1558 bytes later. */
static const NcGpuLayout psx_gpu = {
    .strings = 0x800184c0,
    .code = 0x8001c96c,
    .data = 0x80026ed8,
    .bss = 0x80027ccc,
    .queue = 0x800524bc,
    .memcpy = 0x8001a508,
    .printf = 0x8001a640,
    .vsync = 0x8001b9bc,
    .reset_callback = 0x8001bc00,
    .dma_callback = 0x8001bc60,
    .set_intr_mask = 0x8001bd7c,
};

bool nc_psx_draw_tpage(CPUState *c) {
    return sdk_gpu_draw_tpage(c, &psx_gpu);
}

bool nc_psx_draw_area_packet(CPUState *c) {
    return sdk_gpu_draw_area_packet(c, &psx_gpu);
}

bool nc_psx_sync_gpu_queue(CPUState *c) {
    return sdk_gpu_sync_gpu_queue(c, &psx_gpu);
}

bool nc_psx_clear_gpu_rectangle(CPUState *c) {
    return sdk_gpu_clear_gpu_rectangle(c, &psx_gpu);
}

bool nc_psx_upload_gpu_image(CPUState *c) {
    return sdk_gpu_upload_gpu_image(c, &psx_gpu);
}

bool nc_psx_read_gpu_image(CPUState *c) {
    return sdk_gpu_read_gpu_image(c, &psx_gpu);
}

bool nc_psx_reset_gpu_driver(CPUState *c) {
    return sdk_gpu_reset_gpu_driver(c, &psx_gpu);
}

bool nc_psx_check_queue_alarm(CPUState *c) {
    return sdk_gpu_check_queue_alarm(c, &psx_gpu);
}

bool nc_psx_execute_gpu_queue(CPUState *c) {
    return sdk_gpu_execute_gpu_queue(c, &psx_gpu);
}

bool nc_psx_submit_gpu_command(CPUState *c) {
    return sdk_gpu_submit_gpu_command(c, &psx_gpu);
}

bool nc_psx_draw_offset_packet(CPUState *c) {
    return sdk_gpu_draw_offset_packet(c, &psx_gpu);
}

bool nc_psx_set_draw_env(CPUState *c) {
    return sdk_gpu_set_draw_env(c, &psx_gpu);
}

bool nc_psx_draw_mode_packet(CPUState *c) {
    return sdk_gpu_draw_mode_packet(c, &psx_gpu);
}

bool nc_psx_dump_texture_page(CPUState *c) {
    return sdk_gpu_dump_texture_page(c, &psx_gpu);
}

bool nc_psx_dump_draw(CPUState *c) {
    return sdk_gpu_dump_draw(c, &psx_gpu);
}

bool nc_psx_dump_display(CPUState *c) {
    return sdk_gpu_dump_display(c, &psx_gpu);
}

bool nc_psx_check_rect(CPUState *c) {
    return sdk_gpu_check_rect(c, &psx_gpu);
}

bool nc_psx_clear_image(CPUState *c) {
    return sdk_gpu_clear_image(c, &psx_gpu);
}

bool nc_psx_clear_image2(CPUState *c) {
    return sdk_gpu_clear_image2(c, &psx_gpu);
}

bool nc_psx_load_image(CPUState *c) {
    return sdk_gpu_load_image(c, &psx_gpu);
}

bool nc_psx_store_image(CPUState *c) {
    return sdk_gpu_store_image(c, &psx_gpu);
}

bool nc_psx_put_display_env(CPUState *c) {
    return sdk_gpu_put_display_env(c, &psx_gpu);
}

bool nc_psx_put_draw_env(CPUState *c) {
    return sdk_gpu_put_draw_env(c, &psx_gpu);
}

bool nc_psx_draw_otag_env(CPUState *c) {
    return sdk_gpu_draw_otag_env(c, &psx_gpu);
}

bool nc_psx_draw_otag(CPUState *c) {
    return sdk_gpu_draw_otag(c, &psx_gpu);
}

bool nc_psx_draw_sync(CPUState *c) {
    return sdk_gpu_draw_sync(c, &psx_gpu);
}

bool nc_psx_reset_graph(CPUState *c) {
    return sdk_gpu_reset_graph(c, &psx_gpu);
}

bool nc_psx_set_graph_reverse(CPUState *c) {
    return sdk_gpu_set_graph_reverse(c, &psx_gpu);
}

bool nc_psx_texture_page(CPUState *c) {
    return sdk_gpu_texture_page(c, &psx_gpu);
}

bool nc_psx_default_draw(CPUState *c) {
    return sdk_gpu_default_draw(c, &psx_gpu);
}

bool nc_psx_draw_sync_callback(CPUState *c) {
    return sdk_gpu_draw_sync_callback(c, &psx_gpu);
}

/* SetGeomOffset(ofx, ofy): the GTE screen offset, in 16.16 fixed point. */
bool nc_psx_set_geom_offset(CPUState *c) {
    if (c->budget <= 6)
        return false;
    R(4) <<= 16;
    R(5) <<= 16;
    gte_write_ctrl(c, 24, R(4));
    gte_write_ctrl(c, 25, R(5));
    c->budget -= 6;
    return true;
}

/* _patch_gte: copy the GTE-aware exception prologue over the start of the BIOS
 * exception handler (the C0 table's handler), with interrupts disabled. */
bool nc_psx_patch_gte(CPUState *c) {
    if (c->budget <= 4)
        return false;
    set_reg(c, 1, 0x80020000);
    MEM_W32(0x80027c24, R(31));
    NC_POLL(c, NC_INSN_CYCLES);
    R(31) = 0x80021a78;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001b7a4); /* EnterCriticalSection */
    set_reg(c, 10, 0xb0);
    R(31) = 0x80021a84;
    R(9) = 0x56;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, R(10)); /* GetC0Table */

    set_reg(c, 10, 0x80020000);
    set_reg(c, 9, 0x80020000);
    load_word(c, 2, R(2) + 0x18);
    set_reg(c, 10, 0x80021ad0);
    set_reg(c, 9, 0x80021b08);
    bool more;
    do {
        load_word(c, 3, R(10));
        set_reg(c, 10, R(10) + 4);
        set_reg(c, 2, R(2) + 4);
        more = R(10) != R(9);
        MEM_W32(R(2) - 4, R(3));
        NC_POLL(c, 2 * NC_INSN_CYCLES);
    } while (more);
    R(31) = 0x80021ab4;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001b9ac); /* FlushCache */
    R(31) = 0x80021abc;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, 0x8001b7b4); /* ExitCriticalSection */
    set_reg(c, 31, 0x80020000);
    load_word(c, 31, 0x80027c24);
    NC_POLL(c, 3 * NC_INSN_CYCLES);
    return true;
}
