/* Disc-free cold-start test. A tiny test program calls BIOS Exec, changes a saved
 * register in the child, then returns to its parent. CI runs it at both safe points. */
#include "port/exe.h"
#include "port/runtime.h"
#include "port/savestate.h"
#include "port/settings.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 4) {
        return 1;
    }
    settings_load(argv[1]);
    savestate_init(argv[1]);
    bios_init();
    u32 pc = strcmp(argv[3], "playing") == 0 ? 0x8006E7EC : 0x8006E50C;
    if (strcmp(argv[2], "save") == 0) {
        static const u32 parent[] = {
            0x2410000C, /* li s0,12 */
            0x03E08821, /* move s1,ra */
            0x3C048001, 0x34842000, /* la a0, child EXE header */
            0x24090043, 0x240800A0, /* BIOS A0:43 Exec */
            0x0100F809, 0,         /* jalr t0; nop */
            0xAC100054, 0xAC020058, /* store parent's s0 and Exec result */
            0x02200008, 0           /* jr s1; nop */
        };
        static const u32 child[] = {
            0x26100007, 0xAC100050, /* addiu s0,s0,7; store child's result */
            0x03E00008, 0           /* jr ra; nop */
        };
        for (unsigned i = 0; i < sizeof parent / sizeof parent[0]; i++) {
            MEM_W32(0x80011000 + 4 * i, parent[i]);
        }
        for (unsigned i = 0; i < sizeof child / sizeof child[0]; i++) {
            MEM_W32(pc + 4 * i, child[i]);
        }
        ExecInfo outer = {.pc0 = 0x80011000}, inner = {.pc0 = pc};
        exe_write_info(&inner, 0x80012000);
        nc_set_module(NC_MOD_PSX2);
        nc_cpu.budget = 4096;
        savestate_request_save_quit();
        exe_exec(&nc_cpu, &outer, 0, 0);
        return 1; /* the safe point should have saved and exited */
    }
    ExecStack stack;
    u32 restored_pc;
    if (!savestate_load_resume(&restored_pc, &stack) || restored_pc != pc || stack.count != 2) {
        return 1;
    }
    exe_resume(&nc_cpu, restored_pc, &stack);
    if (MEM_R32(0x50) != 19 || MEM_R32(0x54) != 12 || MEM_R32(0x58) != 1) {
        fprintf(stderr, "Child result or BIOS caller registers did not survive Continue\n");
        return 1;
    }
    return 0;
}
