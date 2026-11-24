#ifndef NC_DECOMP_SDK_FORMATTER_H
#define NC_DECOMP_SDK_FORMATTER_H

#include "decomp/interrupt_control.h"

typedef struct {
    u32 entry, lower_digits, null_string, upper_digits;
    u32 conversions, char_class, put_char, find_byte, string_length;
} PrintLayout;

static inline u32 print_hi(u32 address) {
    return (address + 0x8000u) & 0xffff0000u;
}

/* The formatter keeps its cursors and field state in callee-saved guest
 * registers across calls to putchar, memchr and strlen. s4 holds precision
 * during parsing and the unpadded length during output. */
enum {
    PRINT_INDEX = 16, PRINT_ARGS, PRINT_DATA, PRINT_FLAGS, PRINT_PRECISION,
    PRINT_FIELD_SIZE, PRINT_LENGTH, PRINT_FORMAT, PRINT_SIGN = 30
};

static void parse_print_width(CPUState *c, const PrintLayout *layout) {
    set_reg(c, PRINT_INDEX, 0);
    for (;;) {
        R(2) = MEM_R8(R(PRINT_FORMAT));
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, PRINT_FORMAT, R(PRINT_FORMAT) + 1);
        set_reg(c, 3, R(PRINT_INDEX) << 2);
        set_reg(c, 3, R(3) + R(PRINT_INDEX));
        set_reg(c, 3, R(3) << 1);
        set_reg(c, 3, R(3) - 0x30);
        R(4) = MEM_R8(R(PRINT_FORMAT));
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, PRINT_INDEX, R(3) + R(2));
        set_reg(c, 2, R(4) < 0x80);
        bool non_ascii = !R(2);
        NC_POLL(c, 2);
        if (non_ascii)
            break;
        set_reg(c, 1, print_hi(layout->char_class));
        set_reg(c, 1, R(1) + R(4));
        R(2) = MEM_R8(R(1) + (s16)layout->char_class);
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        set_reg(c, 2, R(2) & 4);
        bool digit = R(2) != 0;
        NC_POLL(c, 2);
        if (!digit)
            break;
    }
    MEM_W32(R(29) + 0x50, R(PRINT_INDEX));
    NC_POLL(c, 1);
    R(PRINT_FORMAT)--;
    NC_POLL(c, 2);
}

static void parse_print_precision(CPUState *c, const PrintLayout *layout) {
    set_reg(c, PRINT_FORMAT, R(PRINT_FORMAT) + 1);
    R(3) = MEM_R8(R(PRINT_FORMAT));
    NC_POLL(c, NC_LOAD_CYCLES);
    set_reg(c, 2, 0x2a);
    bool decimal = R(3) != R(2);
    R(2) = R(3) < 0x80;
    NC_POLL(c, 2);
    if (!decimal) {
        set_reg(c, PRINT_ARGS, R(PRINT_ARGS) + 4);
        load_word(c, PRINT_INDEX, R(PRINT_ARGS) - 4);
        R(PRINT_PRECISION) = R(PRINT_INDEX);
        NC_POLL(c, 2);
    } else {
        bool non_ascii = !R(2);
        R(PRINT_INDEX) = 0;
        NC_POLL(c, 2);
        while (!non_ascii) {
            R(4) = MEM_R8(R(PRINT_FORMAT));
            NC_POLL(c, NC_LOAD_CYCLES);
            set_reg(c, 1, print_hi(layout->char_class));
            set_reg(c, 1, R(1) + R(4));
            R(2) = MEM_R8(R(1) + (s16)layout->char_class);
            NC_POLL(c, NC_LOAD_CYCLES);
            NC_POLL(c, 1);
            set_reg(c, 2, R(2) & 4);
            bool finished = !R(2);
            R(2) = R(PRINT_INDEX) << 2;
            NC_POLL(c, 2);
            if (finished)
                break;
            set_reg(c, PRINT_FORMAT, R(PRINT_FORMAT) + 1);
            set_reg(c, 2, R(2) + R(PRINT_INDEX));
            set_reg(c, 2, R(2) << 1);
            set_reg(c, 2, R(2) - 0x30);
            R(3) = MEM_R8(R(PRINT_FORMAT));
            NC_POLL(c, NC_LOAD_CYCLES);
            NC_POLL(c, 1);
            set_reg(c, 3, R(3) < 0x80);
            bool ascii = R(3) != 0;
            R(PRINT_INDEX) = R(2) + R(4);
            NC_POLL(c, 2);
            non_ascii = !ascii;
        }
        set_reg(c, PRINT_FORMAT, R(PRINT_FORMAT) - 1);
        set_reg(c, PRINT_PRECISION, R(PRINT_INDEX));
    }
    set_reg(c, 2, (s32)R(PRINT_PRECISION) < -1);
    bool valid = !R(2);
    NC_POLL(c, 2);
    if (!valid) {
        R(PRINT_PRECISION) = -1;
        NC_POLL(c, 2);
    }
}

static void format_print_string(CPUState *c, const PrintLayout *layout) {
    set_reg(c, PRINT_ARGS, R(PRINT_ARGS) + 4);
    load_word(c, PRINT_DATA, R(PRINT_ARGS) - 4);
    NC_POLL(c, 1);
    bool present = R(PRINT_DATA) != 0;
    NC_POLL(c, 2);
    if (!present) {
        set_reg(c, PRINT_DATA, print_hi(layout->null_string));
        set_reg(c, PRINT_DATA, layout->null_string);
    }
    bool unbounded = (s32)R(PRINT_PRECISION) < 0;
    R(4) = R(PRINT_DATA);
    NC_POLL(c, 2);
    if (unbounded) {
        R(31) = layout->entry + 0x340;
        R(4) = R(PRINT_DATA);
        NC_POLL(c, 2);
        nc_call(c, layout->string_length);
        set_reg(c, PRINT_LENGTH, R(2));
        R(PRINT_SIGN) = 0;
        NC_POLL(c, 2);
    } else {
        set_reg(c, 5, 0);
        R(31) = layout->entry + 0x318;
        R(6) = R(PRINT_PRECISION);
        NC_POLL(c, 2);
        nc_call(c, layout->find_byte);
        bool use_precision = !R(2);
        R(PRINT_SIGN) = 0;
        NC_POLL(c, 2);
        if (!use_precision) {
            set_reg(c, PRINT_LENGTH, R(2) - R(PRINT_DATA));
            set_reg(c, 2, (s32)R(PRINT_PRECISION) < (s32)R(PRINT_LENGTH));
            bool within_limit = !R(2);
            NC_POLL(c, 2);
            use_precision = !within_limit;
        }
        if (use_precision) {
            R(PRINT_LENGTH) = R(PRINT_PRECISION);
            NC_POLL(c, 2);
        }
    }
}

/* Convert the magnitude backwards into the local digit buffer. The SDK
 * keeps the selected alphabet in the frame and resets it after conversion. */
static void format_print_digits(CPUState *c, const PrintLayout *layout) {
    bool unspecified = (s32)R(PRINT_PRECISION) < 0;
    MEM_W32(R(29) + 0x40, R(PRINT_PRECISION));
    NC_POLL(c, 2);
    if (!unspecified) {
        set_reg(c, 2, -0x21);
        set_reg(c, PRINT_FLAGS, R(PRINT_FLAGS) & R(2));
    }
    bool digits = R(3) != 0;
    R(PRINT_DATA) = R(29) + 0x38;
    NC_POLL(c, 2);
    if (!digits) {
        load_word(c, 7, R(29) + 0x40);
        NC_POLL(c, 1);
        bool empty = !R(7);
        R(2) = R(29) - R(PRINT_DATA);
        NC_POLL(c, 2);
        digits = !empty;
    }
    if (digits) {
        bool more;
        do {
            nc_divu(c, R(3), R(4));
            NC_POLL(c, 1);
            NC_POLL(c, 2); /* The radix is 8, 10 or 16. */
            set_reg(c, 3, c->lo);
            set_reg(c, 2, c->hi);
            load_word(c, 7, R(29) + 0x58);
            NC_POLL(c, 1);
            set_reg(c, 2, R(7) + R(2));
            R(2) = MEM_R8(R(2));
            NC_POLL(c, NC_LOAD_CYCLES);
            set_reg(c, PRINT_DATA, R(PRINT_DATA) - 1);
            more = R(3) != 0;
            MEM_W8(R(PRINT_DATA), (u8)R(2));
            NC_POLL(c, 2);
        } while (more);
        set_reg(c, 7, print_hi(layout->lower_digits));
        set_reg(c, 7, layout->lower_digits);
        set_reg(c, 2, R(PRINT_FLAGS) & 8);
        bool plain = !R(2);
        MEM_W32(R(29) + 0x58, R(7));
        NC_POLL(c, 2);
        if (!plain) {
            set_reg(c, 2, 8);
            bool non_octal = R(4) != R(2);
            R(2) = R(29) - R(PRINT_DATA);
            NC_POLL(c, 2);
            if (non_octal)
                goto length;
            R(3) = MEM_R8(R(PRINT_DATA));
            NC_POLL(c, NC_LOAD_CYCLES);
            set_reg(c, 2, 0x30);
            bool leading_zero = R(3) == R(2);
            R(2) = 0x30;
            NC_POLL(c, 2);
            if (!leading_zero) {
                set_reg(c, PRINT_DATA, R(PRINT_DATA) - 1);
                MEM_W8(R(PRINT_DATA), (u8)R(2));
                NC_POLL(c, 1);
            }
        }
        set_reg(c, 2, R(29) - R(PRINT_DATA));
    }
length:
    set_reg(c, PRINT_LENGTH, R(2) + 0x38);
}

static void format_signed_decimal(CPUState *c, const PrintLayout *layout) {
    set_reg(c, 2, R(PRINT_FLAGS) & 1);
    bool word = R(2) != 0;
    R(2) = R(PRINT_FLAGS) & 4;
    NC_POLL(c, 2);
    if (!word) {
        word = !R(2);
        NC_POLL(c, 2);
    }
    if (word) {
        set_reg(c, PRINT_ARGS, R(PRINT_ARGS) + 4);
        load_word(c, 3, R(PRINT_ARGS) - 4);
    } else {
        set_reg(c, PRINT_ARGS, R(PRINT_ARGS) + 4);
        R(3) = (u32)(s32)(s16)MEM_R16(R(PRINT_ARGS) - 4);
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 2);
    }
    NC_POLL(c, 1);
    bool positive = (s32)R(3) >= 0;
    R(4) = 10;
    NC_POLL(c, 2);
    if (!positive) {
        set_reg(c, 3, 0u - R(3));
        R(PRINT_SIGN) = '-';
        NC_POLL(c, 2);
    }
    format_print_digits(c, layout);
}

static void format_unsigned_number(CPUState *c, const PrintLayout *layout, u32 radix) {
    set_reg(c, 2, R(PRINT_FLAGS) & 1);
    bool word = R(2) != 0;
    R(2) = R(PRINT_FLAGS) & 4;
    NC_POLL(c, 2);
    if (!word) {
        word = !R(2);
        if (radix == 16)
            R(2) = R(PRINT_FLAGS) & 8;
        else
            R(4) = radix;
        NC_POLL(c, 2);
    }
    set_reg(c, PRINT_ARGS, R(PRINT_ARGS) + 4);
    if (word) {
        load_word(c, 3, R(PRINT_ARGS) - 4);
        if (radix == 16) {
            set_reg(c, 2, R(PRINT_FLAGS) & 8);
        } else {
            R(4) = radix;
            NC_POLL(c, 2);
        }
    } else {
        R(3) = (u32)(s32)(s16)MEM_R16(R(PRINT_ARGS) - 4);
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 2);
    }
    if (radix == 16) {
        bool plain = !R(2);
        R(4) = 0x10;
        NC_POLL(c, 2);
        if (!plain) {
            bool zero = !R(3);
            R(PRINT_SIGN) = 0;
            NC_POLL(c, 2);
            if (zero) {
                format_print_digits(c, layout);
                return;
            }
            set_reg(c, PRINT_FLAGS, R(PRINT_FLAGS) | 0x40);
        }
    }
    set_reg(c, PRINT_SIGN, 0);
    format_print_digits(c, layout);
}

static void store_print_count(CPUState *c) {
    set_reg(c, 2, R(PRINT_FLAGS) & 1);
    bool word = R(2) != 0;
    R(2) = R(PRINT_FLAGS) & 4;
    NC_POLL(c, 2);
    if (!word) {
        word = !R(2);
        NC_POLL(c, 2);
    }
    if (word) {
        set_reg(c, PRINT_ARGS, R(PRINT_ARGS) + 4);
        load_word(c, 2, R(PRINT_ARGS) - 4);
        load_word(c, 7, R(29) + 0x38);
        MEM_W32(R(2), R(7));
        NC_POLL(c, 2);
    } else {
        set_reg(c, PRINT_ARGS, R(PRINT_ARGS) + 4);
        load_word(c, 2, R(PRINT_ARGS) - 4);
        R(7) = MEM_R16(R(29) + 0x38);
        NC_POLL(c, NC_LOAD_CYCLES);
        MEM_W16(R(2), (u16)R(7));
        NC_POLL(c, 2);
    }
}

/* Emit left/right padding or precision zeros. The limit is reloaded after
 * each putchar, just as it is in the formatter's four padding loops. */
static void emit_print_padding(CPUState *c, const PrintLayout *layout, u32 ch, u32 limit_offset, u32 resume) {
    bool more;
    do {
        R(31) = resume;
        R(4) = ch;
        NC_POLL(c, 2);
        nc_call(c, layout->put_char);
        load_word(c, 7, R(29) + limit_offset);
        set_reg(c, PRINT_INDEX, R(PRINT_INDEX) + 1);
        set_reg(c, 2, (s32)R(PRINT_INDEX) < (s32)R(7));
        more = R(2) != 0;
        if (resume == layout->entry + 0x4d4)
            R(4) = R(PRINT_SIGN) & 0xff;
        NC_POLL(c, 2);
    } while (more);
}

static void emit_print_field(CPUState *c, const PrintLayout *layout) {
    load_word(c, 7, R(29) + 0x48);
    bool no_sign = !R(PRINT_SIGN);
    R(PRINT_PRECISION) = R(PRINT_LENGTH) + R(7);
    NC_POLL(c, 2);
    if (!no_sign) {
        set_reg(c, PRINT_PRECISION, R(PRINT_PRECISION) + 1);
    }
    set_reg(c, 2, R(PRINT_FLAGS) & 0x40);
    bool no_prefix = !R(2);
    NC_POLL(c, 2);
    if (!no_prefix) {
        set_reg(c, PRINT_PRECISION, R(PRINT_PRECISION) + 2);
    }
    load_word(c, PRINT_FIELD_SIZE, R(29) + 0x40);
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(PRINT_FIELD_SIZE) < (s32)R(PRINT_PRECISION));
    bool precision_larger = !R(2);
    R(2) = R(PRINT_FLAGS) & 0x30;
    NC_POLL(c, 2);
    if (!precision_larger) {
        set_reg(c, PRINT_FIELD_SIZE, R(PRINT_PRECISION));
    }
    bool skip_spaces = R(2) != 0;
    R(4) = R(PRINT_SIGN) & 0xff;
    NC_POLL(c, 2);
    if (!skip_spaces) {
        load_word(c, 7, R(29) + 0x50);
        NC_POLL(c, 1);
        bool no_width = !R(7);
        R(2) = (s32)R(PRINT_FIELD_SIZE) < (s32)R(7);
        NC_POLL(c, 2);
        if (!no_width) {
            bool full_width = !R(2);
            R(PRINT_INDEX) = R(PRINT_FIELD_SIZE);
            NC_POLL(c, 2);
            if (!full_width)
                emit_print_padding(c, layout, ' ', 0x50, layout->entry + 0x4d4);
        }
    }
    bool skip_sign = !R(4);
    R(2) = R(PRINT_FLAGS) & 0x40;
    NC_POLL(c, 2);
    if (!skip_sign) {
        R(31) = layout->entry + 0x4f8;
        NC_POLL(c, 2);
        nc_call(c, layout->put_char);
        set_reg(c, 2, R(PRINT_FLAGS) & 0x40);
    }
    bool skip_prefix = !R(2);
    R(3) = R(PRINT_FLAGS) & 0x30;
    NC_POLL(c, 2);
    if (!skip_prefix) {
        R(31) = layout->entry + 0x50c;
        R(4) = 0x30;
        NC_POLL(c, 2);
        nc_call(c, layout->put_char);
        R(4) = MEM_R8(R(PRINT_FORMAT));
        NC_POLL(c, NC_LOAD_CYCLES);
        R(31) = layout->entry + 0x518;
        NC_POLL(c, 2);
        nc_call(c, layout->put_char);
        set_reg(c, 3, R(PRINT_FLAGS) & 0x30);
    }
    set_reg(c, 2, 0x20);
    bool skip_zero_pad = R(3) != R(2);
    NC_POLL(c, 2);
    if (!skip_zero_pad) {
        load_word(c, 7, R(29) + 0x50);
        NC_POLL(c, 1);
        set_reg(c, 2, (s32)R(PRINT_FIELD_SIZE) < (s32)R(7));
        bool full_width = !R(2);
        R(PRINT_INDEX) = R(PRINT_FIELD_SIZE);
        NC_POLL(c, 2);
        if (!full_width)
            emit_print_padding(c, layout, '0', 0x50, layout->entry + 0x544);
    }
    load_word(c, 7, R(29) + 0x40);
    set_reg(c, PRINT_INDEX, R(PRINT_PRECISION));
    set_reg(c, 2, (s32)R(PRINT_INDEX) < (s32)R(7));
    bool full_precision = !R(2);
    NC_POLL(c, 2);
    if (!full_precision)
        emit_print_padding(c, layout, '0', 0x40, layout->entry + 0x574);
    set_reg(c, PRINT_INDEX, R(PRINT_LENGTH) - 1);
    bool empty = (s32)R(PRINT_INDEX) < 0;
    NC_POLL(c, 2);
    while (!empty) {
        R(4) = MEM_R8(R(PRINT_DATA));
        NC_POLL(c, NC_LOAD_CYCLES);
        set_reg(c, PRINT_DATA, R(PRINT_DATA) + 1);
        R(31) = layout->entry + 0x5a4;
        R(PRINT_INDEX)--;
        NC_POLL(c, 2);
        nc_call(c, layout->put_char);
        empty = (s32)R(PRINT_INDEX) < 0;
        NC_POLL(c, 2);
        if (!empty) {
            NC_POLL(c, 2);
        }
    }
    for (;;) {
        load_word(c, 7, R(29) + 0x48);
        NC_POLL(c, 1);
        set_reg(c, 7, R(7) - 1);
        bool more_zeros = (s32)R(7) >= 0;
        MEM_W32(R(29) + 0x48, R(7));
        NC_POLL(c, 2);
        if (!more_zeros)
            break;
        R(31) = layout->entry + 0x5bc;
        R(4) = 0x30;
        NC_POLL(c, 2);
        nc_call(c, layout->put_char);
    }
    set_reg(c, 2, R(PRINT_FLAGS) & 0x10);
    bool skip_trailing = !R(2);
    NC_POLL(c, 2);
    if (!skip_trailing) {
        load_word(c, 7, R(29) + 0x50);
        NC_POLL(c, 1);
        set_reg(c, 2, (s32)R(PRINT_FIELD_SIZE) < (s32)R(7));
        bool full_width = !R(2);
        R(PRINT_INDEX) = R(PRINT_FIELD_SIZE);
        NC_POLL(c, 2);
        if (!full_width)
            emit_print_padding(c, layout, ' ', 0x50, layout->entry + 0x5f8);
    }
    load_word(c, 3, R(29) + 0x50);
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(3) < (s32)R(PRINT_FIELD_SIZE));
    bool width_larger = !R(2);
    R(PRINT_FORMAT)++;
    NC_POLL(c, 2);
    if (!width_larger) {
        set_reg(c, 3, R(PRINT_FIELD_SIZE));
    }
    load_word(c, 7, R(29) + 0x38);
    NC_POLL(c, 1);
    set_reg(c, 7, R(7) + R(3));
    MEM_W32(R(29) + 0x38, R(7));
    NC_POLL(c, 2);
}

/* prnt uses the guest conversion table and calls the SDK character writer.
 * Literal text is emitted without incrementing this SDK's conversion count. */
static inline bool sdk_format_output(CPUState *c, const PrintLayout *layout) {
    if (c->budget <= 1)
        return false;
    set_reg(c, 29, R(29) - 0x88);
    MEM_W32(R(29) + 0x64, R(PRINT_ARGS));
    NC_POLL(c, 1);
    set_reg(c, PRINT_ARGS, R(6));
    const unsigned saved[] = {31, 30, 23, 22, 21, 20, 19, 18};
    for (unsigned i = 0; i < sizeof saved / sizeof saved[0]; i++) {
        MEM_W32(R(29) + 0x84 - 4 * i, R(saved[i]));
        NC_POLL(c, 1);
    }
    bool has_format = R(5) != 0;
    MEM_W32(R(29) + 0x60, R(PRINT_INDEX));
    NC_POLL(c, 2);
    if (!has_format) {
        R(2) = 0;
        NC_POLL(c, 2);
        goto restore;
    }
    set_reg(c, PRINT_FORMAT, R(5));
    set_reg(c, 7, print_hi(layout->lower_digits));
    set_reg(c, 7, layout->lower_digits);
    MEM_W32(R(29) + 0x58, R(7));
    NC_POLL(c, 1);
    MEM_W32(R(29) + 0x38, 0);
    NC_POLL(c, 1);
    for (;;) {
        R(4) = MEM_R8(R(PRINT_FORMAT));
        NC_POLL(c, NC_LOAD_CYCLES);
        NC_POLL(c, 1);
        bool has_char = R(4) != 0;
        R(2) = '%';
        NC_POLL(c, 2);
        if (!has_char)
            break;
        bool literal = R(4) != R(2);
        R(PRINT_FLAGS) = 0;
        NC_POLL(c, 2);
        if (literal) {
            R(31) = layout->entry + 0x650;
            NC_POLL(c, 2);
            nc_call(c, layout->put_char);
            R(PRINT_FORMAT)++;
            NC_POLL(c, 2);
            continue;
        }
        set_reg(c, PRINT_PRECISION, -1);
        set_reg(c, PRINT_SIGN, 0);
        MEM_W32(R(29) + 0x40, 0);
        NC_POLL(c, 1);
        MEM_W32(R(29) + 0x48, 0);
        NC_POLL(c, 1);
        MEM_W32(R(29) + 0x50, 0);
        NC_POLL(c, 1);
        for (;;) {
            set_reg(c, PRINT_FORMAT, R(PRINT_FORMAT) + 1);
            R(3) = MEM_R8(R(PRINT_FORMAT));
            NC_POLL(c, NC_LOAD_CYCLES);
            NC_POLL(c, 1);
            set_reg(c, 2, R(3) < 0x79);
            bool unknown = !R(2);
            R(2) = R(3) << 2;
            NC_POLL(c, 2);
            u32 target = layout->entry + 0x638;
            if (!unknown) {
                set_reg(c, 1, print_hi(layout->conversions));
                set_reg(c, 1, R(1) + R(2));
                load_word(c, 2, R(1) + (s16)layout->conversions);
                NC_POLL(c, 1);
                target = R(2);
                NC_POLL(c, 2);
            }
            switch (target - layout->entry) {
            case 0x60:
                goto done;
            case 0xb8: { /* space sign */
                bool has_sign = R(PRINT_SIGN) != 0;
                NC_POLL(c, 2);
                if (!has_sign) {
                    R(PRINT_SIGN) = ' ';
                    NC_POLL(c, 2);
                }
                continue;
            }
            case 0xc8: /* # */
                R(PRINT_FLAGS) = R(PRINT_FLAGS) | 8;
                NC_POLL(c, 2);
                continue;
            case 0xd0: { /* dynamic width */
                set_reg(c, PRINT_ARGS, R(PRINT_ARGS) + 4);
                load_word(c, 7, R(PRINT_ARGS) - 4);
                NC_POLL(c, 1);
                bool positive = (s32)R(7) >= 0;
                MEM_W32(R(29) + 0x50, R(7));
                NC_POLL(c, 2);
                if (positive)
                    continue;
                set_reg(c, 7, 0u - R(7));
                MEM_W32(R(29) + 0x50, R(7));
                NC_POLL(c, 1);
                R(PRINT_FLAGS) = R(PRINT_FLAGS) | 0x10;
                NC_POLL(c, 2);
                continue;
            }
            case 0xec: /* - */
                R(PRINT_FLAGS) = R(PRINT_FLAGS) | 0x10;
                NC_POLL(c, 2);
                continue;
            case 0xf4: /* + */
                R(PRINT_SIGN) = '+';
                NC_POLL(c, 2);
                continue;
            case 0xfc:
                parse_print_precision(c, layout);
                continue;
            case 0x188: /* zero padding */
                R(PRINT_FLAGS) = R(PRINT_FLAGS) | 0x20;
                NC_POLL(c, 2);
                continue;
            case 0x190:
                parse_print_width(c, layout);
                continue;
            case 0x1e8: /* L */
                R(PRINT_FLAGS) = R(PRINT_FLAGS) | 2;
                NC_POLL(c, 2);
                continue;
            case 0x1f0: /* h */
                R(PRINT_FLAGS) = R(PRINT_FLAGS) | 4;
                NC_POLL(c, 2);
                continue;
            case 0x1f8: /* l */
                R(PRINT_FLAGS) = R(PRINT_FLAGS) | 1;
                NC_POLL(c, 2);
                continue;
            case 0x200: /* c */
                set_reg(c, PRINT_DATA, R(29) + 0x10);
                set_reg(c, PRINT_ARGS, R(PRINT_ARGS) + 4);
                set_reg(c, PRINT_LENGTH, 1);
                R(2) = MEM_R8(R(PRINT_ARGS) - 4);
                NC_POLL(c, NC_LOAD_CYCLES);
                set_reg(c, PRINT_SIGN, 0);
                MEM_W8(R(29) + 0x10, (u8)R(2));
                NC_POLL(c, 2);
                break;
            case 0x21c: /* D */
                set_reg(c, PRINT_FLAGS, R(PRINT_FLAGS) | 1);
                /* fall through */
            case 0x220: /* d, i */
                format_signed_decimal(c, layout);
                break;
            case 0x264: /* n */
                store_print_count(c);
                R(PRINT_FORMAT)++;
                NC_POLL(c, 2);
                goto next_character;
            case 0x2a0: /* O */
                set_reg(c, PRINT_FLAGS, R(PRINT_FLAGS) | 1);
                /* fall through */
            case 0x2a4: /* o */
                format_unsigned_number(c, layout, 8);
                break;
            case 0x2d8: /* p */
                set_reg(c, PRINT_ARGS, R(PRINT_ARGS) + 4);
                load_word(c, 3, R(PRINT_ARGS) - 4);
                R(4) = 0x10;
                NC_POLL(c, 2);
                set_reg(c, PRINT_SIGN, 0);
                format_print_digits(c, layout);
                break;
            case 0x2e8: /* s */
                format_print_string(c, layout);
                break;
            case 0x34c: /* U */
                set_reg(c, PRINT_FLAGS, R(PRINT_FLAGS) | 1);
                /* fall through */
            case 0x350: /* u */
                format_unsigned_number(c, layout, 10);
                break;
            case 0x384: /* X */
                set_reg(c, 7, print_hi(layout->upper_digits));
                set_reg(c, 7, layout->upper_digits);
                MEM_W32(R(29) + 0x58, R(7));
                NC_POLL(c, 1);
                /* fall through */
            case 0x390: /* x */
                format_unsigned_number(c, layout, 16);
                break;
            case 0x638:
                load_word(c, 7, R(29) + 0x38);
                R(4) = MEM_R8(R(PRINT_FORMAT));
                NC_POLL(c, NC_LOAD_CYCLES);
                set_reg(c, 7, R(7) + 1);
                MEM_W32(R(29) + 0x38, R(7));
                NC_POLL(c, 1);
                R(31) = layout->entry + 0x650;
                NC_POLL(c, 2);
                nc_call(c, layout->put_char);
                R(PRINT_FORMAT)++;
                NC_POLL(c, 2);
                goto next_character;
            default:
                nc_jump(c, target);
                return true;
            }
            emit_print_field(c, layout);
            break;
        }
next_character:
        ;
    }
done:
    load_word(c, 2, R(29) + 0x38);
    NC_POLL(c, 2);
restore:
    load_word(c, 31, R(29) + 0x84);
    load_word(c, PRINT_SIGN, R(29) + 0x80);
    for (int reg = 23; reg >= 16; reg--)
        load_word(c, reg, R(29) + 0x60 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + 0x88);
    NC_POLL(c, 2);
    return true;
}

#endif
