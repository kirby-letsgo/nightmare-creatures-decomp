#include "decomp/psx_spu.h"
#include "decomp/spu_control.h"

static const SpuControlLayout spu_control = {
    .init = 0x80021b28,
    .start = 0x80021c20,
    .quit = 0x8002287c,
    .data_callback = 0x80022858,
    .dma_complete = 0x800220fc,
    .reset_callback = 0x8001bc00,
    .driver_init = 0x80021c9c,
    .set_register = 0x80022608,
    .enter_critical = 0x8001b7a4,
    .exit_critical = 0x8001b7b4,
    .open_event = 0x8001b744,
    .close_event = 0x8001b754,
    .enable_event = 0x8001b774,
    .disable_event = 0x8001b784,
    .dma_callback = 0x8001bc60,
    .started = 0x80027480,
    .event = 0x80027018,
    .callbacks = {0x800274bc, 0x800274c0},
    .voice_table = 0x8002707a,
    .reverb_mode = 0x800274ec,
    .reverb_copy = 0x8002702c,
    .cleared = {0x80027024, 0x80027028, 0x80027034, 0x80027038, 0x8002703a, 0x8002703c, 0x80027040},
    .cleared_after = {0x800274e0, 0x800274e4, 0x800274e8, 0x80027020, 0x800274a0, 0x8002701c,
                      0x80027048, 0x80027044, 0x8002747c},
};

/* SsUtReverbOn calls a one-argument routine with 1; SsUtReverbOff reinitializes libspu. */
bool nc_psx_reverb_on(CPUState *c) {
    return spu_call_with_argument(c, 0x80019d2c, 0x8001d79c, 1);
}

bool nc_psx_reverb_off(CPUState *c) {
    return spu_call_with_argument(c, 0x80021b08, spu_control.init, 0);
}

bool nc_psx_spu_data_callback(CPUState *c) {
    return spu_data_callback(c, &spu_control);
}

bool nc_psx_spu_init(CPUState *c) {
    return spu_control_init(c, &spu_control);
}

bool nc_psx_spu_start(CPUState *c) {
    return spu_control_start(c, &spu_control);
}

bool nc_psx_spu_quit(CPUState *c) {
    return spu_control_quit(c, &spu_control);
}
