#include "decomp/psx_intr.h"
#include "decomp/cd_irq.h"
#include "decomp/interrupt_dispatch.h"

/* PSX.EXE links the same interrupt driver as CREDITS.EXE and the launcher. Its library
 * keeps several copies of the small helpers (memclr, setjmp, the critical-section
 * stubs), so the driver's calls are listed here rather than translated from CREDITS.EXE. */
static const InterruptControlLayout interrupt_control = {
    .uninitialized_message = 0x8001845c,
    .unhandled_message = 0x80018478,
    .print = 0x8001a640,
    .return_from_exception = 0x8001c3b0,
    .set_handler = 0x8001c05c,
    .clear_pad = 0x8001b824,
    .clear_rcnt = 0x8001bbf0,
    .start = 0x8001bd98,
    .clear = 0x8001c2e4,
    .save_context = 0x8001c310,
    .trap = 0x8001be74,
    .start_vsync = 0x8001c3e0,
    .start_dma = 0x8001c508,
    .remove = 0x8001c398,
    .state = 0x80025da0,
    .ports = 0x80026e2c,
    .stop = 0x8001c1b0,
    .restart = 0x8001c25c,
    .enter_critical = 0x8001b7a4,
    .exit_critical = 0x8001b7b4,
    .reset_hook = 0x8001c3c0,
    .install_hook = 0x8001c3d0,
};

bool nc_psx_start_interrupts(CPUState *c) {
    return start_driver_interrupts(c, &interrupt_control);
}

bool nc_psx_resume_interrupt_start(CPUState *c) {
    return resume_interrupt_start(c, &interrupt_control);
}

bool nc_psx_dispatch_interrupts(CPUState *c) {
    return dispatch_interrupts(c, &interrupt_control);
}

bool nc_psx_set_interrupt_handler(CPUState *c) {
    return set_interrupt_handler(c, &interrupt_control);
}

bool nc_psx_stop_interrupts(CPUState *c) {
    return stop_driver_interrupts(c, &interrupt_control);
}

bool nc_psx_restart_interrupts(CPUState *c) {
    return restart_driver_interrupts(c, &interrupt_control);
}

/* The fields the CD interrupt reader (getintr) uses. */
static const CdWaitLayout cd_irq = {
    .command_names = 0x80027890,
    .status = 0x80027b48,
    .puts = 0x8001bba0,
    .print = 0x8001a640,
    .ports = 0x80027b30,
    .callbacks = 0x8002786c,
    .results = 0x80027e24,
    .getintr = 0x80023174,
    .error_message = 0x80018acc,
    .error_format = 0x80018ad8,
    .unknown_message = 0x80018af4,
    .unknown_format = 0x80018b08,
    .irq_table = 0x80018b14,
};

bool nc_psx_get_cd_interrupt(CPUState *c) {
    return get_cd_interrupt(c, &cd_irq);
}
