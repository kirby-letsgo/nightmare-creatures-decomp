#include "port/exe.h"

#include "port/disc.h"
#include "port/runtime.h"

#include <stdlib.h>
#include <string.h>

#define PSX_EXE_HEADER 0x800

static u32 rd32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

bool exe_load(const char *path, ExecInfo *info) {
    DiscFile file;
    if (!disc_find(path, &file)) {
        NC_LOG("exe_load: %s not found on disc", path);
        return false;
    }
    u8 *buf = malloc(file.size);
    if (buf == NULL || !disc_read_file(&file, buf)) {
        free(buf);
        return false;
    }
    if (memcmp(buf, "PS-X EXE", 8) != 0) {
        NC_LOG("exe_load: %s is not a PS-X EXE", path);
        free(buf);
        return false;
    }

    /* The header's exec info is 15 words starting at 0x10. */
    memset(info, 0, sizeof *info);
    info->pc0 = rd32(buf + 0x10);
    info->gp0 = rd32(buf + 0x14);
    info->t_addr = rd32(buf + 0x18);
    info->t_size = rd32(buf + 0x1C);
    info->d_addr = rd32(buf + 0x20);
    info->d_size = rd32(buf + 0x24);
    info->b_addr = rd32(buf + 0x28);
    info->b_size = rd32(buf + 0x2C);
    info->s_addr = rd32(buf + 0x30);
    info->s_size = rd32(buf + 0x34);

    u32 avail = file.size > PSX_EXE_HEADER ? file.size - PSX_EXE_HEADER : 0;
    u32 size = info->t_size < avail ? info->t_size : avail;
    u8 *dst = nc_fast_ptr(info->t_addr);
    if (dst == NULL || (nc_phys(info->t_addr) & (RAM_SIZE - 1)) + size > RAM_SIZE) {
        NC_LOG("exe_load: %s text 0x%08X+0x%X outside RAM", path, info->t_addr, size);
        free(buf);
        return false;
    }
    memcpy(dst, buf + PSX_EXE_HEADER, size);
    free(buf);

    NcModule mod = nc_module_from_name(path);
    if (mod != NC_MOD_NONE) {
        nc_set_module(mod);
    }
    NC_LOG("loaded %s: pc=0x%08X text=0x%08X+0x%X", path, info->pc0, info->t_addr, size);
    return true;
}

void exe_write_info(const ExecInfo *info, u32 addr) {
    const u32 words[] = {info->pc0,    info->gp0,    info->t_addr, info->t_size, info->d_addr,
                         info->d_size, info->b_addr, info->b_size, info->s_addr, info->s_size};
    for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++) {
        MEM_W32(addr + i * 4, words[i]);
    }
}

void exe_read_info(u32 addr, ExecInfo *info) {
    info->pc0 = MEM_R32(addr + 0x00);
    info->gp0 = MEM_R32(addr + 0x04);
    info->t_addr = MEM_R32(addr + 0x08);
    info->t_size = MEM_R32(addr + 0x0C);
    info->d_addr = MEM_R32(addr + 0x10);
    info->d_size = MEM_R32(addr + 0x14);
    info->b_addr = MEM_R32(addr + 0x18);
    info->b_size = MEM_R32(addr + 0x1C);
    info->s_addr = MEM_R32(addr + 0x20);
    info->s_size = MEM_R32(addr + 0x24);
}

u32 exe_exec(CPUState *c, const ExecInfo *info, u32 arg0, u32 arg1) {
    /* Clear BSS, then run the program's entry point with its own stack. When it returns,
     * restore the caller's callee-saved state, as the BIOS Exec() does. */
    if (info->b_size != 0) {
        for (u32 i = 0; i < info->b_size; i += 4) {
            MEM_W32(info->b_addr + i, 0);
        }
    }
    u32 saved[32];
    memcpy(saved, c->r, sizeof saved);

    if (info->s_addr != 0) {
        c->r[29] = c->r[30] = info->s_addr + info->s_size;
    }
    c->r[28] = info->gp0;
    c->r[4] = arg0;
    c->r[5] = arg1;
    c->r[31] = 0; /* returning to 0 means "back to Exec" */
    nc_call(c, info->pc0);

    for (int i = 16; i < 32; i++) {
        c->r[i] = saved[i];
    }
    return 1;
}
