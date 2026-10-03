/* Save states: snapshots of the whole emulated machine, taken and restored at a safe point in
 * the game's level loop (config/hooks.txt), where the native call stack always has the same
 * shape. Requests made elsewhere wait for the next safe point (at most one game frame). */
#ifndef NC_PORT_SAVESTATE_H
#define NC_PORT_SAVESTATE_H

#include "port/recomp.h"

#include <stdbool.h>
#include <stddef.h>

#define SAVESTATE_SLOTS 5

/* Serialization: each module reads or writes its state through the same function. */
typedef struct StateIO {
    bool loading;
    u8 *buf;
    size_t len, cap, pos;
    bool error;
} StateIO;

void state_io(StateIO *io, void *data, size_t size);
#define STATE_VAR(io, v) state_io((io), &(v), sizeof(v))

/* Module serializers. */
void cpu_serialize(StateIO *io);
void bios_serialize(StateIO *io);
void dispatch_serialize(StateIO *io);
void irq_serialize(StateIO *io);
void timers_serialize(StateIO *io);
void dma_serialize(StateIO *io);
void gpu_serialize(StateIO *io);
void spu_serialize(StateIO *io);
void cdrom_serialize(StateIO *io);
void mdec_serialize(StateIO *io);

/* `dir` ends with a path separator; states go to <dir>states/. */
void savestate_init(const char *dir);
void savestate_request_save(int slot);
void savestate_request_load(int slot);
bool savestate_exists(int slot);
/* Short status message for the on-screen display, or NULL when there is nothing to show. */
const char *savestate_message(void);
/* Called after a load has been applied (renderer scale, audio queue...). */
extern void (*savestate_after_load)(void);

/* Safe-point hook (config/hooks.txt). */
void nc_hook_savestate_point(CPUState *c);

#endif
