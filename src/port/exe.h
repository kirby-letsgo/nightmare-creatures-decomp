/* PS-X EXE loading and the BIOS Exec() calling convention. */
#ifndef NC_PORT_EXE_H
#define NC_PORT_EXE_H

#include "port/recomp.h"

#include <stdbool.h>

/* Mirrors the BIOS EXEC structure (the first 10 words are what we use). */
typedef struct ExecInfo {
    u32 pc0, gp0;
    u32 t_addr, t_size;
    u32 d_addr, d_size;
    u32 b_addr, b_size;
    u32 s_addr, s_size;
} ExecInfo;

/* Loads an executable from the disc into RAM and makes its overlay active. */
bool exe_load(const char *path, ExecInfo *info);
void exe_write_info(const ExecInfo *info, u32 addr);
void exe_read_info(u32 addr, ExecInfo *info);
/* Runs a loaded executable until its entry point returns. */
u32 exe_exec(CPUState *c, const ExecInfo *info, u32 arg0, u32 arg1);

#endif
