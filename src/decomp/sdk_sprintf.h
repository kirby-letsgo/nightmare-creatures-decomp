#ifndef NC_DECOMP_SDK_SPRINTF_H
#define NC_DECOMP_SDK_SPRINTF_H

#include "decomp/interrupt_control.h"

static inline u32 sprintf_hi(u32 address) {
    return (address + 0x8000u) & 0xffff0000u;
}

/* memmove copies forwards when the destination is below the source and
 * backwards otherwise. Only the forward copy advances a0, which is returned. */
static inline bool sdk_memmove(CPUState *c) {
    if (c->budget <= 1)
        return false;
    R(2) = R(4) < R(5);
    c->budget--;
    bool forward = R(2) != 0;
    R(2) = R(6);
    NC_POLL(c, 2);
    bool more;
    if (forward) {
        bool empty = (s32)R(2) <= 0;
        R(6)--;
        NC_POLL(c, 2);
        if (!empty) {
            do {
                R(2) = MEM_R8(R(5));
                NC_POLL(c, NC_LOAD_CYCLES);
                set_reg(c, 5, R(5) + 1);
                set_reg(c, 3, R(6));
                set_reg(c, 6, R(6) - 1);
                MEM_W8(R(4), (u8)R(2));
                NC_POLL(c, 1);
                more = (s32)R(3) > 0;
                R(4)++;
                NC_POLL(c, 2);
            } while (more);
        }
    } else {
        bool empty = (s32)R(2) <= 0;
        R(6)--;
        NC_POLL(c, 2);
        if (!empty) {
            set_reg(c, 7, R(6) + R(4));
            set_reg(c, 5, R(6) + R(5));
            do {
                R(2) = MEM_R8(R(5));
                NC_POLL(c, NC_LOAD_CYCLES);
                set_reg(c, 5, R(5) - 1);
                set_reg(c, 3, R(6));
                set_reg(c, 6, R(6) - 1);
                MEM_W8(R(7), (u8)R(2));
                NC_POLL(c, 1);
                more = (s32)R(3) > 0;
                R(7)--;
                NC_POLL(c, 2);
            } while (more);
            NC_POLL(c, 2); /* j to the return */
        }
    }
    R(2) = R(4);
    NC_POLL(c, 2);
    return true;
}

typedef struct {
    u32 entry;
    u32 defaults;      /* initial flags, width and precision words */
    u32 conversions;   /* jump table indexed by conversion character - 'L' */
    u32 upper_digits;  /* %X and %p */
    u32 lower_digits;  /* %x */
    u32 string_length; /* strlen */
    u32 find_byte;     /* memchr */
    u32 move_bytes;    /* memmove */
} SprintfLayout;

/* One macro per original instruction keeps the cycle accounting exact: every
 * instruction costs one cycle (three for a load) and a branch retires with
 * its delay slot, whose effect lands after the branch condition is read. */
#define SPF_OP(stmt)                                                                               \
    do {                                                                                           \
        stmt;                                                                                      \
        NC_POLL(c, NC_INSN_CYCLES);                                                                \
    } while (0)
#define SPF_LD(stmt)                                                                               \
    do {                                                                                           \
        stmt;                                                                                      \
        NC_POLL(c, NC_LOAD_CYCLES);                                                                \
    } while (0)
#define SPF_NOP() NC_POLL(c, NC_INSN_CYCLES)
#define SPF_BR(cond, delay, label)                                                                 \
    do {                                                                                           \
        bool taken_ = (cond);                                                                      \
        delay;                                                                                     \
        NC_POLL(c, 2 * NC_INSN_CYCLES);                                                            \
        if (taken_)                                                                                \
            goto label;                                                                            \
    } while (0)
#define SPF_J(delay, label)                                                                        \
    do {                                                                                           \
        delay;                                                                                     \
        NC_POLL(c, 2 * NC_INSN_CYCLES);                                                            \
        goto label;                                                                                \
    } while (0)
#define SPF_SP(offset) (R(29) + (offset))

/* The SDK sprintf parses each conversion into a word of flags (sign character
 * in byte 1), a width and a precision at sp+0x210..0x218, builds the digits
 * backwards below sp+0x210 and copies them out with memmove. The format and
 * argument cursors live in the frame. An unknown conversion other than '%'
 * ends the output. */
static inline bool sdk_sprintf(CPUState *c, const SprintfLayout *layout) {
    const u32 entry = layout->entry;
    if (c->budget <= 1)
        return false;
    MEM_W32(R(29) + 4, R(5));
    c->budget--;
    SPF_OP(MEM_W32(SPF_SP(8), R(6)));
    SPF_OP(MEM_W32(SPF_SP(0xc), R(7)));
    SPF_OP(R(29) -= 0x248);
    SPF_OP(MEM_W32(SPF_SP(0x234), R(19)));
    SPF_OP(R(19) = R(4));
    SPF_OP(R(2) = SPF_SP(0x250));
    SPF_OP(MEM_W32(SPF_SP(0x244), R(31)));
    SPF_OP(MEM_W32(SPF_SP(0x240), R(22)));
    SPF_OP(MEM_W32(SPF_SP(0x23c), R(21)));
    SPF_OP(MEM_W32(SPF_SP(0x238), R(20)));
    SPF_OP(MEM_W32(SPF_SP(0x230), R(18)));
    SPF_OP(MEM_W32(SPF_SP(0x22c), R(17)));
    SPF_OP(MEM_W32(SPF_SP(0x228), R(16)));
    SPF_OP(MEM_W32(SPF_SP(0x24c), R(5)));
    SPF_OP(MEM_W32(SPF_SP(0x220), R(2)));
    SPF_LD(R(5) = MEM_R8(R(5)));
    SPF_NOP();
    SPF_BR(R(5) == 0, R(18) = 0, finish);
    SPF_OP(R(22) = '-');
    SPF_OP(R(21) = '+');
    SPF_OP(R(20) = ' ');
    SPF_OP(R(2) = '%');
next_character:
    SPF_BR(R(5) != R(2), R(2) = R(19) + R(18), literal);
    SPF_OP(R(5) = sprintf_hi(layout->defaults));
    SPF_OP(R(5) = layout->defaults);
    SPF_LD(R(2) = MEM_R32(R(5)));
    SPF_LD(R(3) = MEM_R32(R(5) + 4));
    SPF_LD(R(4) = MEM_R32(R(5) + 8));
    SPF_OP(MEM_W32(SPF_SP(0x210), R(2)));
    SPF_OP(MEM_W32(SPF_SP(0x214), R(3)));
    SPF_OP(MEM_W32(SPF_SP(0x218), R(4)));
    SPF_OP(R(6) = '#');
    SPF_OP(R(3) = '0');
parse_flag:
    SPF_LD(R(4) = MEM_R32(SPF_SP(0x24c)));
    SPF_NOP();
    SPF_OP(R(2) = R(4) + 1);
    SPF_OP(MEM_W32(SPF_SP(0x24c), R(2)));
    SPF_LD(R(5) = MEM_R8(R(4) + 1));
    SPF_NOP();
    SPF_BR(R(5) != R(22), (void)0, not_minus);
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) |= 1);
    SPF_J(MEM_W32(SPF_SP(0x210), R(2)), parse_flag);
not_minus:
    SPF_BR(R(5) != R(21), (void)0, not_plus);
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) |= 2);
    SPF_J(MEM_W32(SPF_SP(0x210), R(2)), parse_flag);
not_plus:
    SPF_BR(R(5) != R(20), (void)0, not_space);
    SPF_J(MEM_W8(SPF_SP(0x211), (u8)R(5)), parse_flag);
not_space:
    SPF_BR(R(5) != R(6), (void)0, not_hash);
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) |= 4);
    SPF_J(MEM_W32(SPF_SP(0x210), R(2)), parse_flag);
not_hash:
    SPF_BR(R(5) != R(3), R(2) = '*', not_zero);
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) |= 8);
    SPF_J(MEM_W32(SPF_SP(0x210), R(2)), parse_flag);
not_zero:
    SPF_BR(R(5) != R(2), R(2) = R(5) - '0', width_digit);
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x220)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) + 4);
    SPF_OP(MEM_W32(SPF_SP(0x220), R(2)));
    SPF_LD(R(2) = MEM_R32(R(3)));
    SPF_NOP();
    SPF_BR((s32)R(2) >= 0, MEM_W32(SPF_SP(0x214), R(2)), width_star_done);
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x210)));
    SPF_OP(R(2) = 0u - R(2));
    SPF_OP(MEM_W32(SPF_SP(0x214), R(2)));
    SPF_OP(R(3) |= 1);
    SPF_OP(MEM_W32(SPF_SP(0x210), R(3)));
width_star_done:
    SPF_OP(R(2) = R(4) + 2);
    SPF_OP(MEM_W32(SPF_SP(0x24c), R(2)));
    SPF_LD(R(5) = MEM_R8(R(4) + 2));
    SPF_J(R(2) = '.', precision);
width_loop:
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x214)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) << 2);
    SPF_OP(R(2) += R(3));
    SPF_OP(R(2) <<= 1);
    SPF_OP(R(2) -= '0');
    SPF_OP(R(2) += R(5));
    SPF_OP(MEM_W32(SPF_SP(0x214), R(2)));
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x24c)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) + 1);
    SPF_OP(MEM_W32(SPF_SP(0x24c), R(2)));
    SPF_LD(R(5) = MEM_R8(R(3) + 1));
    SPF_NOP();
    SPF_OP(R(2) = R(5) - '0');
width_digit:
    SPF_OP(R(2) = R(2) < 10);
    SPF_BR(R(2) != 0, R(2) = '.', width_loop);
precision:
    SPF_BR(R(5) != R(2), (void)0, length_flags);
    SPF_LD(R(4) = MEM_R32(SPF_SP(0x24c)));
    SPF_NOP();
    SPF_OP(R(2) = R(4) + 1);
    SPF_OP(MEM_W32(SPF_SP(0x24c), R(2)));
    SPF_LD(R(5) = MEM_R8(R(4) + 1));
    SPF_OP(R(2) = '*');
    SPF_BR(R(5) != R(2), R(2) = R(5) - '0', precision_digit);
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x220)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) + 4);
    SPF_OP(MEM_W32(SPF_SP(0x220), R(2)));
    SPF_LD(R(2) = MEM_R32(R(3)));
    SPF_NOP();
    SPF_OP(MEM_W32(SPF_SP(0x218), R(2)));
    SPF_OP(R(2) = R(4) + 2);
    SPF_OP(MEM_W32(SPF_SP(0x24c), R(2)));
    SPF_LD(R(5) = MEM_R8(R(4) + 2));
    SPF_J((void)0, precision_done);
precision_loop:
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x218)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) << 2);
    SPF_OP(R(2) += R(3));
    SPF_OP(R(2) <<= 1);
    SPF_OP(R(2) -= '0');
    SPF_OP(R(2) += R(5));
    SPF_OP(MEM_W32(SPF_SP(0x218), R(2)));
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x24c)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) + 1);
    SPF_OP(MEM_W32(SPF_SP(0x24c), R(2)));
    SPF_LD(R(5) = MEM_R8(R(3) + 1));
    SPF_NOP();
    SPF_OP(R(2) = R(5) - '0');
precision_digit:
    SPF_OP(R(2) = R(2) < 10);
    SPF_BR(R(2) != 0, (void)0, precision_loop);
precision_done:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x218)));
    SPF_NOP();
    SPF_BR((s32)R(2) < 0, (void)0, length_flags);
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) |= 0x10);
    SPF_OP(MEM_W32(SPF_SP(0x210), R(2)));
length_flags:
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) & 1);
    SPF_BR(R(2) == 0, R(17) = SPF_SP(0x210), conversion);
    SPF_OP(R(2) = (u32)-9);
    SPF_OP(R(2) = R(3) & R(2));
    SPF_OP(MEM_W32(SPF_SP(0x210), R(2)));
conversion:
    SPF_OP(R(3) = R(5) - 'L');
dispatch:
    {
        SPF_OP(R(2) = R(3) < 0x2d);
        SPF_BR(R(2) == 0, R(2) = R(3) << 2, unknown);
        SPF_OP(R(1) = sprintf_hi(layout->conversions));
        SPF_OP(R(1) += R(2));
        SPF_LD(R(2) = MEM_R32(R(1) + (layout->conversions - sprintf_hi(layout->conversions))));
        SPF_NOP();
        const u32 target = R(2);
        NC_POLL(c, 2 * NC_INSN_CYCLES); /* jr v0 */
        switch (target - entry) {
        case 0x2c8: /* h */
            SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
            SPF_J(R(2) |= 0x20, store_size);
        case 0x2d4: /* l */
            SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
            SPF_J(R(2) |= 0x40, store_size);
        case 0x2e0: /* L */
            SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
            SPF_NOP();
            SPF_OP(R(2) |= 0x80);
            goto store_size;
        case 0x30c: /* d, i */
            goto signed_decimal;
        case 0x35c: /* u */
            goto unsigned_decimal;
        case 0x474: /* o */
            goto octal;
        case 0x564: /* p */
            goto pointer;
        case 0x578: /* X */
            goto upper_hex;
        case 0x588: /* x */
            goto lower_hex;
        case 0x680: /* c */
            goto character;
        case 0x6a0: /* s */
            goto string;
        case 0x724: /* n */
            goto count;
        case 0x758:
            goto unknown;
        default:
            nc_jump(c, target);
            return true;
        }
    }
store_size:
    SPF_OP(MEM_W32(SPF_SP(0x210), R(2)));
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x24c)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) + 1);
    SPF_OP(MEM_W32(SPF_SP(0x24c), R(2)));
    SPF_LD(R(5) = MEM_R8(R(3) + 1));
    SPF_J(R(3) = R(5) - 'L', dispatch);

signed_decimal:
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x220)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) + 4);
    SPF_OP(MEM_W32(SPF_SP(0x220), R(2)));
    SPF_LD(R(4) = MEM_R32(R(3)));
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) & 0x20);
    SPF_BR(R(2) == 0, R(2) = R(4) << 16, decimal_sign);
    SPF_OP(R(4) = (u32)((s32)R(2) >> 16));
decimal_sign:
    SPF_BR((s32)R(4) >= 0, R(2) = R(3) & 2, decimal_positive);
    SPF_OP(R(4) = 0u - R(4));
    SPF_J(MEM_W8(SPF_SP(0x211), (u8)R(22)), decimal_precision);
decimal_positive:
    SPF_BR(R(2) == 0, (void)0, decimal_precision);
    SPF_J(MEM_W8(SPF_SP(0x211), (u8)R(21)), decimal_precision);

unsigned_decimal:
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x220)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) + 4);
    SPF_OP(MEM_W32(SPF_SP(0x220), R(2)));
    SPF_LD(R(4) = MEM_R32(R(3)));
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) &= 0x20);
    SPF_BR(R(2) == 0, MEM_W8(SPF_SP(0x211), 0), decimal_precision);
    SPF_OP(R(4) &= 0xffff);
decimal_precision:
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) & 0x10);
    SPF_BR(R(2) != 0, R(2) = R(3) & 8, decimal_digits);
    SPF_BR(R(2) == 0, (void)0, decimal_minimum);
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x214)));
    SPF_LD(R(2) = MEM_R8(SPF_SP(0x211)));
    SPF_NOP();
    SPF_BR(R(2) == 0, MEM_W32(SPF_SP(0x218), R(3)), decimal_minimum);
    SPF_OP(R(2) = R(3) - 1);
    SPF_OP(MEM_W32(SPF_SP(0x218), R(2)));
decimal_minimum:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x218)));
    SPF_NOP();
    SPF_BR((s32)R(2) > 0, R(2) = 1, decimal_digits);
    SPF_OP(MEM_W32(SPF_SP(0x218), R(2)));
decimal_digits:
    SPF_BR(R(4) == 0, R(16) = 0, decimal_zeros);
    SPF_OP(R(5) = 0xcccc0000);
    SPF_OP(R(5) |= 0xcccd);
decimal_loop:
    {
        const u64 product = (u64)R(4) * R(5);
        SPF_OP((c->lo = (u32)product, c->hi = (u32)(product >> 32)));
    }
    SPF_OP(R(17) -= 1);
    SPF_OP(R(16) += 1);
    SPF_OP(R(3) = c->hi);
    SPF_OP(R(3) >>= 3);
    SPF_OP(R(2) = R(3) << 2);
    SPF_OP(R(2) += R(3));
    SPF_OP(R(2) <<= 1);
    SPF_OP(R(2) = R(4) - R(2));
    SPF_OP(R(2) += '0');
    SPF_OP(R(4) = R(3));
    SPF_BR(R(4) != 0, MEM_W8(R(17), (u8)R(2)), decimal_loop);
decimal_zeros:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x218)));
    SPF_NOP();
    SPF_OP(R(2) = (s32)R(16) < (s32)R(2));
    SPF_BR(R(2) == 0, (void)0, decimal_sign_char);
    SPF_OP(R(3) = '0');
    SPF_OP(R(17) -= 1);
decimal_zero_loop:
    SPF_OP(MEM_W8(R(17), (u8)R(3)));
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x218)));
    SPF_OP(R(16) += 1);
    SPF_OP(R(2) = (s32)R(16) < (s32)R(2));
    SPF_BR(R(2) != 0, R(17) -= 1, decimal_zero_loop);
    SPF_OP(R(17) += 1);
decimal_sign_char:
    SPF_LD(R(2) = MEM_R8(SPF_SP(0x211)));
    SPF_NOP();
    SPF_BR(R(2) == 0, (void)0, emit);
    SPF_OP(R(17) -= 1);
    SPF_LD(R(2) = MEM_R8(SPF_SP(0x211)));
    SPF_OP(R(16) += 1);
    SPF_J(MEM_W8(R(17), (u8)R(2)), emit);

octal:
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x220)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) + 4);
    SPF_OP(MEM_W32(SPF_SP(0x220), R(2)));
    SPF_LD(R(4) = MEM_R32(R(3)));
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) & 0x20);
    SPF_BR(R(2) == 0, R(2) = R(3) & 0x10, octal_precision);
    SPF_OP(R(4) &= 0xffff);
octal_precision:
    SPF_BR(R(2) != 0, R(2) = R(3) & 8, octal_digits);
    SPF_BR(R(2) == 0, (void)0, octal_minimum);
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x214)));
    SPF_NOP();
    SPF_OP(MEM_W32(SPF_SP(0x218), R(2)));
octal_minimum:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x218)));
    SPF_NOP();
    SPF_BR((s32)R(2) > 0, R(2) = 1, octal_digits);
    SPF_OP(MEM_W32(SPF_SP(0x218), R(2)));
octal_digits:
    SPF_BR(R(4) == 0, R(16) = 0, octal_prefix);
octal_loop:
    SPF_OP(R(17) -= 1);
    SPF_OP(R(2) = R(4) & 7);
    SPF_OP(R(2) += '0');
    SPF_OP(MEM_W8(R(17), (u8)R(2)));
    SPF_OP(R(4) >>= 3);
    SPF_BR(R(4) != 0, R(16) += 1, octal_loop);
octal_prefix:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) &= 4);
    SPF_BR(R(2) == 0, (void)0, octal_zeros);
    SPF_BR(R(16) == 0, R(2) = '0', octal_zeros);
    SPF_LD(R(3) = MEM_R8(R(17)));
    SPF_NOP();
    SPF_BR(R(3) == R(2), R(2) = '0', octal_zeros);
    SPF_OP(R(17) -= 1);
    SPF_OP(MEM_W8(R(17), (u8)R(2)));
    SPF_OP(R(16) += 1);
octal_zeros:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x218)));
    SPF_NOP();
    SPF_OP(R(2) = (s32)R(16) < (s32)R(2));
    SPF_BR(R(2) == 0, R(3) = '0', emit);
    SPF_OP(R(17) -= 1);
octal_zero_loop:
    SPF_OP(MEM_W8(R(17), (u8)R(3)));
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x218)));
    SPF_OP(R(16) += 1);
    SPF_OP(R(2) = (s32)R(16) < (s32)R(2));
    SPF_BR(R(2) != 0, R(17) -= 1, octal_zero_loop);
    SPF_J(R(17) += 1, emit);

pointer:
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x210)));
    SPF_OP(R(2) = 8);
    SPF_OP(MEM_W32(SPF_SP(0x218), R(2)));
    SPF_OP(R(3) |= 0x50);
    SPF_OP(MEM_W32(SPF_SP(0x210), R(3)));
upper_hex:
    SPF_OP(R(7) = sprintf_hi(layout->upper_digits));
    SPF_OP(R(7) = layout->upper_digits);
    SPF_J((void)0, hex_argument);
lower_hex:
    SPF_OP(R(7) = sprintf_hi(layout->lower_digits));
    SPF_OP(R(7) = layout->lower_digits);
hex_argument:
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x220)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) + 4);
    SPF_OP(MEM_W32(SPF_SP(0x220), R(2)));
    SPF_LD(R(4) = MEM_R32(R(3)));
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) & 0x20);
    SPF_BR(R(2) == 0, R(2) = R(3) & 0x10, hex_precision);
    SPF_OP(R(4) &= 0xffff);
hex_precision:
    SPF_BR(R(2) != 0, R(2) = R(3) & 8, hex_digits);
    SPF_BR(R(2) == 0, R(2) = R(3) & 4, hex_minimum);
    SPF_LD(R(6) = MEM_R32(SPF_SP(0x214)));
    SPF_BR(R(2) == 0, MEM_W32(SPF_SP(0x218), R(6)), hex_minimum);
    SPF_OP(R(2) = R(6) - 2);
    SPF_OP(MEM_W32(SPF_SP(0x218), R(2)));
hex_minimum:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x218)));
    SPF_NOP();
    SPF_BR((s32)R(2) > 0, R(2) = 1, hex_digits);
    SPF_OP(MEM_W32(SPF_SP(0x218), R(2)));
hex_digits:
    SPF_BR(R(4) == 0, R(16) = 0, hex_zeros);
hex_loop:
    SPF_OP(R(17) -= 1);
    SPF_OP(R(2) = R(4) & 0xf);
    SPF_OP(R(4) >>= 4);
    SPF_OP(R(2) = R(7) + R(2));
    SPF_LD(R(2) = MEM_R8(R(2)));
    SPF_OP(R(16) += 1);
    SPF_BR(R(4) != 0, MEM_W8(R(17), (u8)R(2)), hex_loop);
hex_zeros:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x218)));
    SPF_NOP();
    SPF_OP(R(2) = (s32)R(16) < (s32)R(2));
    SPF_BR(R(2) == 0, (void)0, hex_prefix);
    SPF_OP(R(3) = '0');
    SPF_OP(R(17) -= 1);
hex_zero_loop:
    SPF_OP(MEM_W8(R(17), (u8)R(3)));
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x218)));
    SPF_OP(R(16) += 1);
    SPF_OP(R(2) = (s32)R(16) < (s32)R(2));
    SPF_BR(R(2) != 0, R(17) -= 1, hex_zero_loop);
    SPF_OP(R(17) += 1);
hex_prefix:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) &= 4);
    SPF_BR(R(2) == 0, R(2) = '0', emit);
    SPF_OP(R(17) -= 1);
    SPF_OP(MEM_W8(R(17), (u8)R(5))); /* the conversion character itself */
    SPF_OP(R(17) -= 1);
    SPF_OP(R(16) += 2);
    SPF_J(MEM_W8(R(17), (u8)R(2)), emit);

character:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x220)));
    SPF_OP(R(17) -= 1);
    SPF_OP(R(3) = R(2) + 4);
    SPF_OP(MEM_W32(SPF_SP(0x220), R(3)));
    SPF_LD(R(2) = MEM_R8(R(2)));
    SPF_OP(R(16) = 1);
    SPF_J(MEM_W8(R(17), (u8)R(2)), emit);

string:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x220)));
    SPF_NOP();
    SPF_OP(R(3) = R(2) + 4);
    SPF_OP(MEM_W32(SPF_SP(0x220), R(3)));
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x210)));
    SPF_LD(R(17) = MEM_R32(R(2)));
    SPF_OP(R(2) = R(3) & 4);
    SPF_BR(R(2) == 0, R(2) = R(3) & 0x10, string_length);
    /* "#" takes a counted string: the first byte is the length. */
    SPF_LD(R(16) = MEM_R8(R(17)));
    SPF_BR(R(2) == 0, R(17) += 1, emit);
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x218)));
    SPF_NOP();
    SPF_OP(R(2) = (s32)R(3) < (s32)R(16));
    SPF_BR(R(2) == 0, (void)0, emit);
    SPF_J(R(16) = R(3), emit);
string_length:
    SPF_BR(R(2) != 0, R(4) = R(17), string_bounded);
    R(31) = entry + 0x6fc;
    SPF_OP(R(4) = R(17));
    NC_POLL(c, NC_INSN_CYCLES);
    nc_call(c, layout->string_length);
    SPF_J(R(16) = R(2), emit);
string_bounded:
    SPF_LD(R(6) = MEM_R32(SPF_SP(0x218)));
    R(31) = entry + 0x710;
    SPF_OP(R(5) = 0);
    NC_POLL(c, NC_INSN_CYCLES);
    nc_call(c, layout->find_byte);
    SPF_BR(R(2) != 0, R(16) = R(2) - R(17), emit);
    SPF_LD(R(16) = MEM_R32(SPF_SP(0x218)));
    SPF_J((void)0, emit);

count:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x220)));
    SPF_NOP();
    SPF_OP(R(3) = R(2) + 4);
    SPF_OP(MEM_W32(SPF_SP(0x220), R(3)));
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x210)));
    SPF_LD(R(17) = MEM_R32(R(2)));
    SPF_OP(R(2) = R(3) & 0x20);
    SPF_BR(R(2) == 0, (void)0, count_word);
    SPF_J(MEM_W16(R(17), (u16)R(18)), advance);
count_word:
    SPF_J(MEM_W32(R(17), R(18)), advance);

unknown:
    SPF_OP(R(2) = '%');
    SPF_BR(R(5) != R(2), R(2) = R(19) + R(18), finish);
literal:
    SPF_OP(MEM_W8(R(2), (u8)R(5)));
    SPF_J(R(18) += 1, advance);

emit:
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x214)));
    SPF_NOP();
    SPF_OP(R(2) = (s32)R(16) < (s32)R(2));
    SPF_BR(R(2) == 0, R(4) = R(19) + R(18), copy_source);
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x210)));
    SPF_NOP();
    SPF_OP(R(2) &= 1);
    SPF_BR(R(2) != 0, R(5) = R(17), copy);
    SPF_OP(R(3) = R(18) + R(19));
left_pad:
    SPF_OP(MEM_W8(R(3), (u8)R(20)));
    SPF_OP(R(3) += 1);
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x214)));
    SPF_NOP();
    SPF_OP(R(2) -= 1);
    SPF_OP(MEM_W32(SPF_SP(0x214), R(2)));
    SPF_OP(R(2) = (s32)R(16) < (s32)R(2));
    SPF_BR(R(2) != 0, R(18) += 1, left_pad);
    SPF_OP(R(4) = R(19) + R(18));
copy_source:
    SPF_OP(R(5) = R(17));
copy:
    R(31) = entry + 0x7d0;
    SPF_OP(R(6) = R(16));
    NC_POLL(c, NC_INSN_CYCLES);
    nc_call(c, layout->move_bytes);
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x214)));
    SPF_NOP();
    SPF_OP(R(2) = (s32)R(16) < (s32)R(2));
    SPF_BR(R(2) == 0, R(18) += R(16), advance);
    SPF_OP(R(3) = R(18) + R(19));
right_pad:
    SPF_OP(MEM_W8(R(3), (u8)R(20)));
    SPF_OP(R(3) += 1);
    SPF_LD(R(2) = MEM_R32(SPF_SP(0x214)));
    SPF_OP(R(16) += 1);
    SPF_OP(R(2) = (s32)R(16) < (s32)R(2));
    SPF_BR(R(2) != 0, R(18) += 1, right_pad);
advance:
    SPF_LD(R(3) = MEM_R32(SPF_SP(0x24c)));
    SPF_NOP();
    SPF_OP(R(2) = R(3) + 1);
    SPF_OP(MEM_W32(SPF_SP(0x24c), R(2)));
    SPF_LD(R(5) = MEM_R8(R(3) + 1));
    SPF_NOP();
    SPF_BR(R(5) != 0, R(2) = '%', next_character);
finish:
    SPF_OP(R(2) = R(19) + R(18));
    SPF_OP(MEM_W8(R(2), 0));
    SPF_OP(R(2) = R(18));
    SPF_LD(R(31) = MEM_R32(SPF_SP(0x244)));
    for (int reg = 22; reg >= 16; reg--)
        SPF_LD(R(reg) = MEM_R32(SPF_SP(0x228 + 4 * (reg - 16))));
    SPF_OP(R(29) += 0x248);
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return true;
}

#undef SPF_OP
#undef SPF_LD
#undef SPF_NOP
#undef SPF_BR
#undef SPF_J
#undef SPF_SP

#endif
