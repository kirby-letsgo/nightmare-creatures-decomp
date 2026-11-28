#include "decomp/psx_libc.h"
#include "decomp/bios_service.h"
#include "decomp/jump_context.h"
#include "decomp/sdk_card.h"
#include "decomp/sdk_ctype.h"
#include "decomp/sdk_formatter.h"
#include "decomp/sdk_putchar.h"
#include "decomp/sdk_sprintf.h"

enum {
    PSX_CHAR_CLASS = 0x80025cf1,
    PSX_PUT_CHAR = 0x8001adb8,
    PSX_WRITE = 0x8001b7e4,
    PSX_COLUMN = 0x80027cc8,
};

bool nc_psx_sprintf(CPUState *c) {
    static const SprintfLayout layout = {
        .entry = 0x8001ae6c,
        .defaults = 0x80025d74,
        .conversions = 0x8001835c,
        .upper_digits = 0x80018334,
        .lower_digits = 0x80018348,
        .string_length = 0x8001a610,
        .find_byte = 0x8001ad68,
        .move_bytes = 0x8001b6c8,
    };
    return sdk_sprintf(c, &layout);
}

bool nc_psx_format_output(CPUState *c) {
    static const PrintLayout layout = {
        .entry = 0x8001a67c,
        .lower_digits = 0x80018120,
        .null_string = 0x80018134,
        .upper_digits = 0x8001813c,
        .conversions = 0x80018150,
        .char_class = PSX_CHAR_CLASS,
        .put_char = PSX_PUT_CHAR,
        .find_byte = 0x8001ad68,
        .string_length = 0x8001a610,
    };
    return sdk_format_output(c, &layout);
}

bool nc_psx_put_char(CPUState *c) {
    return sdk_putchar(c, PSX_PUT_CHAR, PSX_WRITE, PSX_COLUMN, PSX_CHAR_CLASS);
}

bool nc_psx_change_case(CPUState *c, bool uppercase) {
    return sdk_change_case(c, uppercase, PSX_CHAR_CLASS);
}

bool nc_psx_move_bytes(CPUState *c) {
    return sdk_memmove(c);
}

void nc_psx_longjmp(CPUState *c) {
    restore_jump_context(c);
}

bool nc_psx_bios_service(CPUState *c, u32 table, u32 service) {
    return bios_service(c, table, service);
}

static const CardLayout psx_card = {
    .enter_critical = 0x8001b7a4,
    .exit_critical = 0x8001b7b4,
    .clear_pad = 0x8001b824,
    .flush_cache = 0x8001b9ac,
    .bios_init = 0x80025a20,
    .bios_start = 0x80025a30,
    .bios_stop = 0x80025a40,
    .new_card = 0x8002590c,
    .card_write = 0x80025954,
    .patch_card = 0x80025aa8,
    .patch_card2 = 0x80025b60,
    .exit_card = 0x80025bf0,
    .saved_ra = 0x80027c34,
    .exit_saved_ra = 0x80027c44,
    .card_code = 0x80025a50,
    .card_code_end = 0x80025a64,
    .patch_slot = 0x80027c38,
    .swap_code = 0x80025b30,
    .swap_code_end = 0x80025b44,
    .exit_code = 0x80025c58,
    .exit_code_end = 0x80025c64,
};

bool nc_psx_init_card(CPUState *c) {
    return sdk_init_card(c, 0x80025964, &psx_card);
}

bool nc_psx_start_card(CPUState *c) {
    return sdk_start_card(c, 0x800259b8, &psx_card);
}

bool nc_psx_stop_card(CPUState *c) {
    return sdk_stop_card(c, 0x800259f0, &psx_card);
}

bool nc_psx_card_clear(CPUState *c) {
    return sdk_card_clear(c, 0x8002591c, &psx_card);
}

bool nc_psx_patch_card(CPUState *c) {
    return sdk_patch_card(c, &psx_card);
}

bool nc_psx_patch_card2(CPUState *c) {
    return sdk_patch_card2(c, &psx_card);
}

bool nc_psx_exit_card(CPUState *c) {
    return sdk_exit_card(c, &psx_card);
}
