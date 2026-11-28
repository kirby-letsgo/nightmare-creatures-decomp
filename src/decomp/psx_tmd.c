/* PSX.EXE's TIM and TMD readers (LIBGS). The code is shared with PSX2.EXE and the FMV
 * players through sdk_tmd.h. */
#include "decomp/psx_front.h"
#include "decomp/sdk_tmd.h"

static const TmdLayout psx_tmd_layout = {
    .tim_cursor = 0x80027e10,
    .vertices = 0x80027e14,
    .normals = 0x80027e18,
    .primitives = 0x80027e1c,
    .count = 0x80027e20,
    .read_tim = 0x8002021c,
    .tim_info = 0x80020570,
    .open_tmd = 0x80020284,
    .read_tmd = 0x800202c4,
    .tmd_info = 0x80020690,
    .graph_debug = 0x8001d730,
    .print = 0x8001a640,
    .unpack_packet = 0x80020828,
    .tim_messages = {0x800187c4, 0x800187d0, 0x800187dc},
    .tmd_messages = {0x800187ec, 0x80018800, 0x80018828, 0x80018840, 0x80018858},
};

bool nc_psx_open_tim(CPUState *c) {
    return sdk_open_tim(c, &psx_tmd_layout);
}

bool nc_psx_read_tim(CPUState *c) {
    return sdk_read_tim(c, &psx_tmd_layout);
}

bool nc_psx_tim_info(CPUState *c) {
    return sdk_tim_info(c, &psx_tmd_layout);
}

bool nc_psx_open_tmd(CPUState *c) {
    return sdk_open_tmd(c, &psx_tmd_layout);
}

bool nc_psx_read_tmd(CPUState *c) {
    return sdk_read_tmd(c, &psx_tmd_layout);
}

bool nc_psx_tmd_info(CPUState *c) {
    return sdk_tmd_info(c, &psx_tmd_layout);
}
