/* Native runtime internals shared between src/port/ modules. */
#ifndef NC_PORT_RUNTIME_H
#define NC_PORT_RUNTIME_H

#include "port/bios.h"
#include "port/recomp.h"

#include <stdbool.h>
#include <stdio.h>

/* Modules (executables) that can be resident in the overlay slot at 0x80018000. */
typedef enum NcModule {
    NC_MOD_NONE = -1,
    NC_MOD_PSX,
    NC_MOD_PSX2,
    NC_MOD_CREDITS,
    NC_MOD_STREAM1,
    NC_MOD_STREAM2,
    NC_MOD_STREAM3,
    NC_MOD_COUNT
} NcModule;

#define NC_LAUNCHER_LO 0x80010000u
#define NC_OVERLAY_LO 0x80018000u

extern CPUState nc_cpu;

/* Logging (stderr + nightmare.log); NC_FATAL never returns. */
void nc_log_init(const char *path);
void nc_log(const char *fmt, ...);
_Noreturn void nc_fatal(const char *fmt, ...);
#define NC_LOG(...) nc_log(__VA_ARGS__)
#define NC_FATAL(...) nc_fatal(__VA_ARGS__)

/* dispatch.c */
void nc_set_module(NcModule mod);
NcModule nc_active_module(void);
NcModule nc_module_from_name(const char *exe_name);
NcFunc nc_lookup(u32 addr);

/* cpu.c: total emulated cycles, and a hook run once per emulated frame (VBlank). */
extern u64 nc_cycles;
extern unsigned nc_frame_count; /* VBlanks since boot */
extern void (*nc_frame_hook)(void);

#endif
