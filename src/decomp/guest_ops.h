/* Single guest instructions with their scheduler boundaries, for routines written out
 * instruction by instruction. Each helper retires one instruction (or a branch or jal with its
 * delay slot) and polls exactly where the interpreter does. */
#ifndef NC_DECOMP_GUEST_OPS_H
#define NC_DECOMP_GUEST_OPS_H

#include "decomp/interrupt_control.h" /* set_reg, load_word */

/* %hi of an address paired with a signed %lo, as lui loads it. */
static inline u32 guest_hi(u32 address) {
    return (address + 0x8000u) & 0xffff0000u;
}

/* jal: the caller has already performed the delay slot; the pair retires together. */
static inline void guest_call(CPUState *c, u32 target, u32 resume) {
    R(31) = resume;
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    nc_call(c, target);
}

/* A branch with its delay slot: the condition was captured before the delay slot ran. */
static inline bool guest_branch(CPUState *c, bool taken) {
    NC_POLL(c, 2 * NC_INSN_CYCLES);
    return taken;
}

/* jr ra and its delay slot. */
static inline void guest_return(CPUState *c) {
    NC_POLL(c, 2 * NC_INSN_CYCLES);
}

static inline void guest_nop(CPUState *c) {
    NC_POLL(c, NC_INSN_CYCLES);
}

static inline void store_word(CPUState *c, u32 address, u32 value) {
    MEM_W32(address, value);
    NC_POLL(c, NC_INSN_CYCLES);
}
static inline void store_half(CPUState *c, u32 address, u32 value) {
    MEM_W16(address, (u16)value);
    NC_POLL(c, NC_INSN_CYCLES);
}
static inline void store_byte(CPUState *c, u32 address, u32 value) {
    MEM_W8(address, (u8)value);
    NC_POLL(c, NC_INSN_CYCLES);
}

/* lui at, %hi(address) followed by a store through at. */
static inline void store_word_at(CPUState *c, u32 address, u32 value) {
    set_reg(c, 1, guest_hi(address));
    store_word(c, address, value);
}
static inline void store_half_at(CPUState *c, u32 address, u32 value) {
    set_reg(c, 1, guest_hi(address));
    store_half(c, address, value);
}
static inline void store_byte_at(CPUState *c, u32 address, u32 value) {
    set_reg(c, 1, guest_hi(address));
    store_byte(c, address, value);
}

static inline void load_half(CPUState *c, unsigned reg, u32 address) {
    R(reg) = MEM_R16(address);
    NC_POLL(c, NC_LOAD_CYCLES);
}
static inline void load_signed_half(CPUState *c, unsigned reg, u32 address) {
    R(reg) = (u32)(s32)(s16)MEM_R16(address);
    NC_POLL(c, NC_LOAD_CYCLES);
}
static inline void load_byte(CPUState *c, unsigned reg, u32 address) {
    R(reg) = MEM_R8(address);
    NC_POLL(c, NC_LOAD_CYCLES);
}
static inline void load_signed_byte(CPUState *c, unsigned reg, u32 address) {
    R(reg) = (u32)(s32)(s8)MEM_R8(address);
    NC_POLL(c, NC_LOAD_CYCLES);
}

/* lui reg, %hi(address) followed by lw reg, %lo(address)(reg). */
static inline void load_word_hi(CPUState *c, unsigned reg, u32 address) {
    set_reg(c, reg, guest_hi(address));
    load_word(c, reg, address);
}

/* lui reg, %hi(address) followed by addiu reg, reg, %lo(address). */
static inline void set_address(CPUState *c, unsigned reg, u32 address) {
    set_reg(c, reg, guest_hi(address));
    set_reg(c, reg, address);
}

/* Restore ra and the saved registers from the frame, pop it and return. The guest reloads ra
 * first, then the saved registers from the highest down. */
static inline void guest_leave(CPUState *c, u32 frame, u32 ra_offset, unsigned first_saved,
                               unsigned last_saved, u32 saved_offset) {
    load_word(c, 31, R(29) + ra_offset);
    for (unsigned reg = last_saved + 1; reg-- > first_saved;)
        load_word(c, reg, R(29) + saved_offset + 4 * (reg - first_saved));
    set_reg(c, 29, R(29) + frame);
    guest_return(c);
}

/* A frame that saves only ra. */
static inline void guest_leave_ra(CPUState *c, u32 frame, u32 ra_offset) {
    load_word(c, 31, R(29) + ra_offset);
    set_reg(c, 29, R(29) + frame);
    guest_return(c);
}

#endif
