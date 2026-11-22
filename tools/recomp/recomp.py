#!/usr/bin/env python3
"""Statically recompile a PS1 executable's MIPS R3000A code into C.

Each MIPS function (boundaries from splat's output) becomes one C function operating on a
CPUState. Branches become gotos, delay slots are emitted before the branch takes effect, and
indirect jumps/calls go through the runtime dispatcher (nc_call / nc_jump).

Output (gitignored, derived from the user's disc):
  gen/<module>/code_N.c   recompiled functions
  gen/<module>/table.c    sorted address -> function table for the dispatcher
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from gen_splat import DISC, HEADER_SIZE  # noqa: E402

FUNCS_PER_FILE = 120
LAUNCHER = "slus_005_82"
LAUNCHER_RANGE = (0x80010000, 0x80018000)

PSX2_PAD_BIOS_SERVICES = {
    0x80073D7C: "NC_BIOS_STOP_PAD",
    0x80073E3C: "NC_BIOS_CHANGE_CLEAR_PAD",
    0x80073F44: "NC_BIOS_INIT_PAD",
    0x80073F54: "NC_BIOS_START_PAD",
}

CREDITS_ROUTINES = {
    0x80018C8C: "nc_credits_skip_input(c)",
    0x80018CFC: "nc_credits_init_pad(c)",
    0x80018D4C: "nc_credits_stop_pad(c)",
    0x80018D6C: "nc_credits_read_sectors(c)",
    0x80018E28: "nc_credits_load_file(c)",
    0x80018EE4: "nc_credits_load_tim(c)",
    0x80018F94: "nc_credits_run(c)",
    0x80019220: "nc_credits_init_shared_record(c)",
    0x800192E4: "nc_credits_set_cd_volume(c)",
    0x8001932C: "nc_credits_reset_music(c)",
    0x8001935C: "nc_credits_empty_hook(c)",
    0x80019364: "nc_credits_play_track(c)",
    0x800193B4: "nc_credits_heap_warning(c, NC_WARN_CALLOC)",
    0x800193DC: "nc_credits_heap_warning(c, NC_WARN_MALLOC)",
    0x80019404: "nc_credits_heap_warning(c, NC_WARN_REALLOC)",
    0x8001942C: "nc_credits_heap_warning(c, NC_WARN_FREE)",
    0x80019454: "nc_credits_init_heap(c)",
    0x8001949C: "nc_credits_resize(c)",
    0x80019680: "nc_credits_allocate(c)",
    0x800197E0: "nc_credits_free(c)",
    0x800198B0: "nc_credits_find_allocation(c)",
    0x800198EC: "nc_credits_allocation_size(c)",
    0x80019928: "nc_credits_copy_bytes(c)",
    0x8001995C: "nc_credits_fill_bytes(c)",
    0x80019988: "nc_credits_append_string(c)",
    0x80019A30: "nc_credits_string_length(c)",
    0x80019A60: "nc_credits_printf(c)",
    0x8001A128: "nc_credits_change_case(c, true)",
    0x8001A158: "nc_credits_change_case(c, false)",
    0x8001A188: "nc_credits_find_byte(c)",
    0x8001A1D8: "nc_credits_put_char(c)",
    0x8001A28C: "nc_credits_bios_stop_pad(c)",
    0x8001A29C: "nc_credits_bios_write(c)",
    0x8001A2AC: "nc_credits_change_clear_pad(c)",
    0x8001A2BC: "nc_credits_legacy_pad_init(c)",
    0x8001A324: "nc_credits_sdk_init_pad(c)",
    0x8001A38C: "nc_credits_start_pad(c)",
    0x8001A3B4: "nc_credits_bios_init_pad(c)",
    0x8001A3C4: "nc_credits_bios_start_pad(c)",
    0x8001A3D4: "nc_credits_bios_pad_init_and_start(c)",
    0x8001A3E4: "nc_credits_patch_pad(c)",
    0x8001A434: "nc_credits_flush_cache(c)",
    0x8001A444: "nc_credits_enter_critical_section(c)",
    0x8001A638: "nc_credits_put_string(c)",
    0x8001A688: "nc_credits_change_clear_counter(c)",
    0x8001A698: "nc_credits_reset_callback(c)",
    0x8001A6C8: "nc_credits_interrupt_callback(c)",
    0x8001A6F8: "nc_credits_dma_callback(c)",
    0x8001A728: "nc_credits_vsync_callback(c)",
    0x8001A75C: "nc_credits_vsync_callbacks(c)",
    0x8001A78C: "nc_credits_stop_callback(c)",
    0x8001A7BC: "nc_credits_restart_callback(c)",
    0x8001A7EC: "nc_credits_check_callback(c)",
    0x8001A7FC: "nc_credits_interrupt_mask(c, false)",
    0x8001A814: "nc_credits_interrupt_mask(c, true)",
    0x8001AC48: "nc_credits_stop_interrupts(c)",
    0x8001ACF4: "nc_credits_restart_interrupts(c)",
    0x8001AD7C: "nc_credits_clear_irq_words(c)",
    0x8001ADA8: "nc_credits_save_irq_context(c)",
    0x8001AE58: "nc_credits_reset_irq_entry(c)",
    0x8001AE68: "nc_credits_hook_irq_entry(c)",
    0x8001AE78: "nc_credits_exit_critical_section(c)",
    0x8001AE88: "nc_credits_start_vsync(c)",
    0x8001AEE0: "nc_credits_dispatch_vblank(c)",
    0x8001AF58: "nc_credits_set_vsync_slot(c)",
    0x8001AF84: "nc_credits_clear_irq_words(c)",
    0x8001AFB0: "nc_credits_start_dma(c)",
    0x8001B184: "nc_credits_set_dma_slot(c)",
    0x8001B22C: "nc_credits_clear_irq_words(c)",
    0x8001B414: "nc_credits_default_draw(c)",
    0x8001B4E4: "nc_credits_default_display(c)",
    0x8001B520: "nc_credits_video_mode(c, true)",
    0x8001B538: "nc_credits_video_mode(c, false)",
    0x8001B548: "nc_credits_texture_page(c)",
    0x8001B610: "nc_credits_palette_id(c)",
    0x8001B628: "nc_credits_dump_texture_page(c)",
    0x8001B6F8: "nc_credits_dump_palette(c)",
    0x8001B738: "nc_credits_packet_link(c, false)",
    0x8001B754: "nc_credits_packet_link(c, true)",
    0x8001B770: "nc_credits_prepend_packets(c, false)",
    0x8001B7AC: "nc_credits_prepend_packets(c, true)",
    0x8001B7E8: "nc_credits_link_packets(c)",
    0x8001B80C: "nc_credits_end_packet(c)",
    0x8001B824: "nc_credits_packet_flag(c, 2)",
    0x8001B84C: "nc_credits_packet_flag(c, 1)",
    0x8001B874: "nc_credits_init_primitive(c, NC_POLY_F3)",
    0x8001B888: "nc_credits_init_primitive(c, NC_POLY_FT3)",
    0x8001B89C: "nc_credits_init_primitive(c, NC_POLY_G3)",
    0x8001B8B0: "nc_credits_init_primitive(c, NC_POLY_GT3)",
    0x8001B8C4: "nc_credits_init_primitive(c, NC_POLY_F4)",
    0x8001B8D8: "nc_credits_init_primitive(c, NC_POLY_FT4)",
    0x8001B8EC: "nc_credits_init_primitive(c, NC_POLY_G4)",
    0x8001B900: "nc_credits_init_primitive(c, NC_POLY_GT4)",
    0x8001B914: "nc_credits_init_primitive(c, NC_SPRT8)",
    0x8001B928: "nc_credits_init_primitive(c, NC_SPRT16)",
    0x8001B93C: "nc_credits_init_primitive(c, NC_SPRT)",
    0x8001B950: "nc_credits_init_primitive(c, NC_TILE1)",
    0x8001B964: "nc_credits_init_primitive(c, NC_TILE8)",
    0x8001B978: "nc_credits_init_primitive(c, NC_TILE16)",
    0x8001B98C: "nc_credits_init_primitive(c, NC_TILE)",
    0x8001B9A0: "nc_credits_init_primitive(c, NC_LINE_F2)",
    0x8001B9B4: "nc_credits_init_primitive(c, NC_LINE_G2)",
    0x8001B9C8: "nc_credits_init_primitive(c, NC_LINE_F3)",
    0x8001B9E8: "nc_credits_init_primitive(c, NC_LINE_G3)",
    0x8001BA08: "nc_credits_init_primitive(c, NC_LINE_F4)",
    0x8001BA28: "nc_credits_init_primitive(c, NC_LINE_G4)",
    0x8001BA48: "nc_credits_init_primitive(c, NC_BLOCK_FILL)",
    0x8001BA5C: "nc_credits_draw_tpage(c)",
    0x8001BB10: "nc_credits_draw_move(c)",
    0x8001BB70: "nc_credits_draw_load(c)",
    0x8001BBDC: "nc_credits_merge_primitives(c)",
    0x8001BC14: "nc_credits_dump_draw(c)",
    0x8001BD94: "nc_credits_dump_display(c)",
    0x8001C0B0: "nc_credits_set_graph_debug(c)",
    0x8001C1C8: "nc_credits_graph_status(c, false)",
    0x8001C1D8: "nc_credits_graph_status(c, true)",
    0x8001C1E8: "nc_credits_draw_sync_callback(c)",
    0x8001C244: "nc_credits_set_display_mask(c)",
    0x8001C2E0: "nc_credits_draw_sync(c)",
    0x8001CAF4: "nc_credits_read_environment(c, false)",
    0x8001CFDC: "nc_credits_read_environment(c, true)",
    0x8001D014: "nc_credits_display_field(c)",
    0x8001D048: "nc_credits_texture_window_packet(c)",
    0x8001D084: "nc_credits_draw_area_packet(c)",
    0x8001D108: "nc_credits_draw_offset_packet(c)",
    0x8001D14C: "nc_credits_priority_packet(c)",
    0x8001D174: "nc_credits_draw_mode_packet(c)",
    0x8001D470: "nc_credits_draw_mode_word(c)",
    0x8001D4CC: "nc_credits_draw_corner(c, false)",
    0x8001D598: "nc_credits_draw_corner(c, true)",
    0x8001D664: "nc_credits_draw_offset_word(c)",
    0x8001D6AC: "nc_credits_texture_window_word(c)",
    0x8001D730: "nc_credits_display_x(c)",
    0x8001D7EC: "nc_credits_graphics_status(c)",
    0x8001E04C: "nc_credits_gpu_control(c)",
    0x8001E074: "nc_credits_gpu_control_byte(c)",
    0x8001E088: "nc_credits_gpu_command_block(c)",
    0x8001E0D8: "nc_credits_gpu_command_chain(c)",
    0x8001E124: "nc_credits_gpu_parameter(c)",
    0x8001E154: "nc_credits_queue_command(c)",
    0x8001E9FC: "nc_credits_set_queue_alarm(c)",
    0x8001EB9C: "nc_credits_gpu_version(c)",
    0x8001EC78: "nc_credits_gpu_fill_bytes(c)",
    0x8001ECA4: "nc_credits_bios_gpu_command(c)",
    0x8001ECB4: "nc_credits_tim_info(c)",
    0x8001FA98: "nc_credits_spu_write_register(c)",
    0x8001FAE0: "nc_credits_spu_set_address(c)",
    0x8001FB84: "nc_credits_spu_read_address(c)",
    0x8001FBC0: "nc_credits_spu_set_dma_priority(c)",
    0x8001FC20: "nc_credits_spu_set_delay(c, false)",
    0x8001FC4C: "nc_credits_spu_set_delay(c, true)",
    0x8001FC78: "nc_credits_spu_wait(c)",
    0x8001FCD8: "nc_credits_deliver_event(c)",
    0x8001FD78: "nc_credits_cd_default_sync(c)",
    0x8001FDA0: "nc_credits_cd_default_ready(c)",
    0x8001FDC8: "nc_credits_cd_default_read(c)",
    0x8001FDF0: "nc_credits_cd_status(c)",
    0x8001FE00: "nc_credits_cd_mode(c)",
    0x8001FE10: "nc_credits_cd_last_command(c)",
    0x8001FE20: "nc_credits_cd_last_position(c)",
    0x8001FE9C: "nc_credits_cd_flush(c)",
    0x8001FEBC: "nc_credits_cd_set_debug(c)",
    0x8001FED4: "nc_credits_cd_command_name(c)",
    0x8001FF08: "nc_credits_cd_interrupt_name(c)",
    0x8001FF7C: "nc_credits_cd_sync_callback(c)",
    0x8001FF94: "nc_credits_cd_ready_callback(c)",
    0x80020354: "nc_credits_cd_mix(c)",
    0x800203E0: "nc_credits_cd_sector_to_position(c)",
    0x800204E4: "nc_credits_cd_position_to_sector(c)",
    0x80021440: "nc_credits_cd_volume(c)",
    0x800214C8: "nc_credits_cd_flush_controller(c)",
    0x800215A8: "nc_credits_cd_init_volume(c)",
    0x8002169C: "nc_credits_cd_init_interrupt(c)",
    0x80021B44: "nc_credits_cd_set_test_parameter_count(c)",
    0x80021F14: "nc_credits_cd_names_equal(c)",
    0x80022214: "nc_credits_cd_search_directory(c)",
    0x800225CC: "nc_credits_cd_copy_identifier(c)",
    0x80022600: "nc_credits_strcmp(c)",
    0x80022664: "nc_credits_strncmp(c)",
    0x8002335C: "nc_credits_empty_hook(c)",
}

GPR = [
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
]  # fmt: skip


def r(n: int) -> str:
    return "0" if n == 0 else f"R({n})"


def s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


@dataclass
class Insn:
    addr: int
    word: int

    @property
    def op(self) -> int:
        return self.word >> 26

    @property
    def rs(self) -> int:
        return (self.word >> 21) & 31

    @property
    def rt(self) -> int:
        return (self.word >> 16) & 31

    @property
    def rd(self) -> int:
        return (self.word >> 11) & 31

    @property
    def sa(self) -> int:
        return (self.word >> 6) & 31

    @property
    def funct(self) -> int:
        return self.word & 63

    @property
    def imm(self) -> int:
        return self.word & 0xFFFF

    @property
    def simm(self) -> int:
        return s16(self.imm)

    def branch_target(self) -> int:
        return (self.addr + 4 + (self.simm << 2)) & 0xFFFFFFFF

    def jump_target(self) -> int:
        return ((self.addr + 4) & 0xF0000000) | ((self.word & 0x03FFFFFF) << 2)

    def is_branch(self) -> bool:
        return self.op in (1, 4, 5, 6, 7)

    def is_jump(self) -> bool:
        return self.op in (2, 3) or (self.op == 0 and self.funct in (8, 9))

    def has_delay(self) -> bool:
        return self.is_branch() or self.is_jump()


@dataclass
class Func:
    addr: int
    end: int
    insns: list[Insn] = field(default_factory=list)


@dataclass
class Module:
    name: str  # e.g. psx2_exe
    data: bytes
    vram: int
    funcs: list[Func]
    func_starts: set[int]
    words: set[int]  # every aligned word value in the image (jump table candidates)
    # Mid-function entry points (address -> containing function): return sites of setjmp
    # calls (Psy-Q's interrupt handler is entered by "longjmp"-ing to one), and code addresses
    # taken as pointers that splat did not split into their own function (callbacks).
    resumes: dict[int, Func] = field(default_factory=dict)

    def cname(self, addr: int) -> str:
        return f"{self.name}_{addr:08X}"

    def resume_name(self, entry: int) -> str:
        return f"{self.cname(self.resumes[entry].addr)}_r{entry:08X}"


def load_module(name: str, exe: str) -> Module:
    data = (DISC / exe).read_bytes()
    vram = struct.unpack_from("<I", data, 0x18)[0]
    yaml = (ROOT / "config" / "splat" / f"{name}.yaml").read_text()
    text_off, data_off = (int(x, 16) for x in re.findall(r"\[0x([0-9A-F]+), (?:asm|data)\]", yaml))
    text_lo = vram + text_off - HEADER_SIZE
    text_hi = vram + data_off - HEADER_SIZE

    # splat emits "nonmatching name[, 0xSIZE]" then "glabel name" (or "dlabel" for data) and
    # lines commented "/* file_offset vram ... */". Sizes matter: code can contain data tables.
    # spimdisasm occasionally emits a real function as .word data without a size; those end
    # at the next label.
    label = re.compile(
        r"^nonmatching (\w+)(?:, 0x([0-9A-F]+))?\s*\n"
        r"([gd])label \1\n\s*/\* [0-9A-F]+ ([0-9A-F]{8})",
        re.M,
    )
    found: list[tuple[int, str, int | None]] = []
    for asm in (ROOT / "build" / "splat" / name / "asm").glob("*.s"):
        for m in label.finditer(asm.read_text()):
            size = int(m.group(2), 16) if m.group(2) else None
            found.append((int(m.group(4), 16), m.group(3), size))
    if not found:
        raise RuntimeError(f"no functions found in build/splat/{name}/asm; run `make split`")
    found.sort()
    sizes: dict[int, int] = {}
    for idx, (addr, kind, size) in enumerate(found):
        if kind != "g" or not text_lo <= addr < text_hi:
            continue
        nxt = found[idx + 1][0] if idx + 1 < len(found) else text_hi
        sizes[addr] = size if size is not None else nxt - addr

    funcs = []
    for a in sorted(sizes):
        f = Func(a, a + sizes[a])
        for pc in range(a, f.end, 4):
            off = pc - vram + HEADER_SIZE
            f.insns.append(Insn(pc, struct.unpack_from("<I", data, off)[0]))
        funcs.append(f)
    starts = set(sizes)

    body = data[HEADER_SIZE:]
    words = set(struct.unpack(f"<{len(body) // 4}I", body[: len(body) // 4 * 4]))
    mod = Module(name, data, vram, funcs, starts, words)

    setjmp = symbol_addr(name, "setjmp")
    if setjmp is not None:
        for f in funcs:
            for i in f.insns:
                if i.op == 3 and i.jump_target() == setjmp and i.addr + 8 < f.end:
                    mod.resumes[i.addr + 8] = f
    entries = pointer_targets(mod)
    if name == "credits_exe":
        # Splat may group standalone routines after the preceding return.
        # Provide dispatcher entries even when no static pointer refers to them.
        entries.update(CREDITS_ROUTINES)
    if name == "psx2_exe":
        # Splat groups these independent routines after preceding returns.
        entries.update(
            (
                0x80034610,
                0x80046E18,
                0x80046EDC,
                0x80046FA0,
                0x800471E0,
                0x80047268,
                0x80047300,
                0x8004737C,
                0x80047420,
            )
        )
    for addr in entries:
        owner = containing(funcs, addr)
        if owner is not None and addr != owner.addr:
            mod.resumes[addr] = owner
    return mod


def containing(funcs: list[Func], addr: int) -> Func | None:
    for f in funcs:
        if f.addr <= addr < f.end:
            return f
    return None


def word_at(mod: Module, addr: int) -> int | None:
    off = addr - mod.vram + HEADER_SIZE
    if HEADER_SIZE <= off <= len(mod.data) - 4:
        return struct.unpack_from("<I", mod.data, off)[0]
    return None


def plausible_entry(mod: Module, addr: int) -> bool:
    """A code address that looks like a function start: a stack-frame prologue, or the word
    after a `jr $ra` + delay slot."""
    w = word_at(mod, addr)
    if w is None:
        return False
    if (w & 0xFFFF8000) == 0x27BD8000:  # addiu $sp, $sp, -N
        return True
    return word_at(mod, addr - 8) == 0x03E00008


def pointer_targets(mod: Module) -> set[int]:
    """Code addresses used as values: lui/addiu (or ori) pairs in code, and words in data."""
    lo = min(f.addr for f in mod.funcs)
    hi = max(f.end for f in mod.funcs)
    found: set[int] = set()
    for f in mod.funcs:
        upper: dict[int, int] = {}
        for i in f.insns:
            if i.op == 0x0F:  # lui
                upper[i.rt] = i.imm << 16
            elif i.op in (0x09, 0x0D) and i.rs in upper:  # addiu / ori
                base = upper[i.rs]
                addr = (base + i.simm if i.op == 0x09 else base | i.imm) & 0xFFFFFFFF
                if lo <= addr < hi and addr % 4 == 0:
                    found.add(addr)
    for w in mod.words:
        if lo <= w < hi and w % 4 == 0 and plausible_entry(mod, w):
            found.add(w)
    return found


def symbol_addr(module: str, symbol: str) -> int | None:
    pattern = re.compile(rf"^{re.escape(symbol)} = 0x([0-9A-F]+);", re.M)
    for path in (ROOT / "config" / "symbols").glob(f"{module}*.txt"):
        m = pattern.search(path.read_text())
        if m:
            return int(m.group(1), 16)
    return None


def load_patches(module: str) -> dict[int, str]:
    """config/patches.txt: instructions to skip while a runtime flag (nc_flag_<name>) is set."""
    patches: dict[int, str] = {}
    path = ROOT / "config" / "patches.txt"
    if not path.exists():
        return patches
    for line in path.read_text().splitlines():
        fields = line.split("#", 1)[0].split()
        if len(fields) == 3 and fields[0] == module:
            patches[int(fields[1], 16)] = fields[2]
    return patches


def load_hooks(module: str) -> dict[int, str]:
    """config/hooks.txt: native functions called before the instruction at an address."""
    hooks: dict[int, str] = {}
    path = ROOT / "config" / "hooks.txt"
    if not path.exists():
        return hooks
    for line in path.read_text().splitlines():
        fields = line.split("#", 1)[0].split()
        if len(fields) == 3 and fields[0] == module:
            hooks[int(fields[1], 16)] = fields[2]
    return hooks


class Emitter:
    def __init__(self, mod: Module, launcher: Module | None):
        self.mod = mod
        self.launcher = launcher
        self.patches = load_patches(mod.name)
        self.hooks = load_hooks(mod.name)

    def guarded(self, addr: int, stmt: str) -> str:
        """Wraps a translated instruction so it is skipped while its patch flag is set."""
        flag = self.patches.get(addr)
        if flag is None or not stmt:
            return stmt
        return f"if (!nc_flag_{flag}) {{ {stmt} }}"

    # --- call targets -------------------------------------------------------------------
    def direct(self, addr: int) -> str | None:
        """C function name for a statically known call target, or None if unknown."""
        if addr in self.mod.func_starts:
            return self.mod.cname(addr)
        if self.launcher and LAUNCHER_RANGE[0] <= addr < LAUNCHER_RANGE[1]:
            if addr in self.launcher.func_starts:
                return self.launcher.cname(addr)
        return None

    def call(self, addr: int) -> str:
        name = self.direct(addr)
        return f"{name}(c);" if name else f"nc_call(c, 0x{addr:08X});"

    # --- single instructions --------------------------------------------------------------
    def simple(self, i: Insn) -> str:
        """Translate a non-control-flow instruction."""
        op, rs, rt, rd, sa, fn = i.op, i.rs, i.rt, i.rd, i.sa, i.funct
        imm, simm = i.imm, i.simm

        def set_(reg: int, expr: str) -> str:
            return "" if reg == 0 else f"R({reg}) = {expr};"

        if i.word == 0:
            return ""
        if op == 0:
            match fn:
                case 0x00:
                    return set_(rd, f"{r(rt)} << {sa}")
                case 0x02:
                    return set_(rd, f"{r(rt)} >> {sa}")
                case 0x03:
                    return set_(rd, f"(u32)((s32){r(rt)} >> {sa})")
                case 0x04:
                    return set_(rd, f"{r(rt)} << ({r(rs)} & 31)")
                case 0x06:
                    return set_(rd, f"{r(rt)} >> ({r(rs)} & 31)")
                case 0x07:
                    return set_(rd, f"(u32)((s32){r(rt)} >> ({r(rs)} & 31))")
                case 0x0C:
                    return "nc_syscall(c);"
                case 0x0D:
                    return f"nc_break(c, 0x{i.addr:08X}, {(i.word >> 6) & 0xFFFFF});"
                case 0x10:
                    return set_(rd, "c->hi")
                case 0x11:
                    return f"c->hi = {r(rs)};"
                case 0x12:
                    return set_(rd, "c->lo")
                case 0x13:
                    return f"c->lo = {r(rs)};"
                case 0x18:
                    return f"nc_mult(c, {r(rs)}, {r(rt)});"
                case 0x19:
                    return f"nc_multu(c, {r(rs)}, {r(rt)});"
                case 0x1A:
                    return f"nc_div(c, {r(rs)}, {r(rt)});"
                case 0x1B:
                    return f"nc_divu(c, {r(rs)}, {r(rt)});"
                case 0x20 | 0x21:  # add (overflow trap ignored), addu
                    return set_(rd, f"{r(rs)} + {r(rt)}")
                case 0x22 | 0x23:
                    return set_(rd, f"{r(rs)} - {r(rt)}")
                case 0x24:
                    return set_(rd, f"{r(rs)} & {r(rt)}")
                case 0x25:
                    return set_(rd, f"{r(rs)} | {r(rt)}")
                case 0x26:
                    return set_(rd, f"{r(rs)} ^ {r(rt)}")
                case 0x27:
                    return set_(rd, f"~({r(rs)} | {r(rt)})")
                case 0x2A:
                    return set_(rd, f"(s32){r(rs)} < (s32){r(rt)}")
                case 0x2B:
                    return set_(rd, f"{r(rs)} < {r(rt)}")
        match op:
            case 0x08 | 0x09:  # addi (trap ignored), addiu
                return set_(rt, f"{r(rs)} + 0x{simm & 0xFFFFFFFF:X}u")
            case 0x0A:
                return set_(rt, f"(s32){r(rs)} < {simm}")
            case 0x0B:
                return set_(rt, f"{r(rs)} < 0x{simm & 0xFFFFFFFF:X}u")
            case 0x0C:
                return set_(rt, f"{r(rs)} & 0x{imm:X}u")
            case 0x0D:
                return set_(rt, f"{r(rs)} | 0x{imm:X}u")
            case 0x0E:
                return set_(rt, f"{r(rs)} ^ 0x{imm:X}u")
            case 0x0F:
                return set_(rt, f"0x{imm << 16:08X}u")
            case 0x10:
                return self.cop0(i)
            case 0x12:
                return self.cop2(i)
        ea = f"{r(rs)} + 0x{simm & 0xFFFFFFFF:X}u" if simm else r(rs)
        match op:
            case 0x20:
                return set_(rt, f"(u32)(s32)(s8)MEM_R8({ea})")
            case 0x21:
                return set_(rt, f"(u32)(s32)(s16)MEM_R16({ea})")
            case 0x22:
                return set_(rt, f"nc_lwl(c, {ea}, {r(rt)})")
            case 0x23:
                return set_(rt, f"MEM_R32({ea})")
            case 0x24:
                return set_(rt, f"MEM_R8({ea})")
            case 0x25:
                return set_(rt, f"MEM_R16({ea})")
            case 0x26:
                return set_(rt, f"nc_lwr(c, {ea}, {r(rt)})")
            case 0x28:
                return f"MEM_W8({ea}, (u8){r(rt)});"
            case 0x29:
                return f"MEM_W16({ea}, (u16){r(rt)});"
            case 0x2A:
                return f"nc_swl(c, {ea}, {r(rt)});"
            case 0x2B:
                return f"MEM_W32({ea}, {r(rt)});"
            case 0x2E:
                return f"nc_swr(c, {ea}, {r(rt)});"
            case 0x32:
                return f"gte_write_data(c, {rt}, MEM_R32({ea}));"
            case 0x3A:
                return f"MEM_W32({ea}, gte_read_data(c, {rt}));"
        return f"nc_unimplemented(c, 0x{i.addr:08X}, 0x{i.word:08X});"

    def cop0(self, i: Insn) -> str:
        sub = i.rs
        if sub == 0x00:  # mfc0
            return "" if i.rt == 0 else f"R({i.rt}) = nc_mfc0(c, {i.rd});"
        if sub == 0x04:  # mtc0
            return f"nc_mtc0(c, {i.rd}, {r(i.rt)});"
        if sub == 0x10 and i.funct == 0x10:  # rfe
            return "nc_rfe(c);"
        return f"nc_unimplemented(c, 0x{i.addr:08X}, 0x{i.word:08X});"

    def cop2(self, i: Insn) -> str:
        sub = i.rs
        if sub & 0x10:
            return f"gte_command(c, 0x{i.word & 0x1FFFFFF:07X});"
        match sub:
            case 0x00:  # mfc2
                return "" if i.rt == 0 else f"R({i.rt}) = gte_read_data(c, {i.rd});"
            case 0x02:  # cfc2
                return "" if i.rt == 0 else f"R({i.rt}) = gte_read_ctrl(c, {i.rd});"
            case 0x04:  # mtc2
                return f"gte_write_data(c, {i.rd}, {r(i.rt)});"
            case 0x06:  # ctc2
                return f"gte_write_ctrl(c, {i.rd}, {r(i.rt)});"
        return f"nc_unimplemented(c, 0x{i.addr:08X}, 0x{i.word:08X});"

    def timed_simple(self, i: Insn, *, delay_slot: bool = False) -> str:
        """Execute an instruction, then poll. A branch and its slot retire together."""
        stmt = self.simple(i)
        cost = "NC_LOAD_CYCLES" if 0x20 <= i.op <= 0x26 or i.op == 0x32 else "NC_INSN_CYCLES"
        flag = self.patches.get(i.addr)
        if flag and cost != "NC_INSN_CYCLES":
            cost = f"(nc_flag_{flag} ? NC_INSN_CYCLES : {cost})"
        if delay_slot:
            cost = f"NC_INSN_CYCLES + {cost}"
        hook = self.hooks.get(i.addr)
        parts = [
            f"nc_hook_{hook}(c);" if hook else "",
            self.guarded(i.addr, stmt),
            f"NC_POLL(c, {cost});",
        ]
        return " ".join(part for part in parts if part)

    def cond(self, i: Insn) -> str:
        rs, rt = r(i.rs), r(i.rt)
        match i.op:
            case 4:
                return f"{rs} == {rt}"
            case 5:
                return f"{rs} != {rt}"
            case 6:
                return f"(s32){rs} <= 0"
            case 7:
                return f"(s32){rs} > 0"
            case 1:
                return f"(s32){rs} >= 0" if i.rt & 1 else f"(s32){rs} < 0"
        raise AssertionError

    # --- functions --------------------------------------------------------------------------
    def function(self, f: Func, entry: int | None = None) -> list[str]:
        """Emit f as a C function; with `entry`, emit a copy that starts mid-function there."""
        targets: set[int] = set() if entry is None else {entry}
        jump_tables = False
        for i in f.insns:
            if i.is_branch():
                targets.add(i.branch_target())
            elif i.op == 2 and f.addr <= i.jump_target() < f.end:
                targets.add(i.jump_target())
            elif i.op == 0 and i.funct == 8 and i.rs != 31:
                jump_tables = True
        # Continuations for the inline combat checks.
        contact_blocks = (
            {
                0x800426E8: ("nc_player_can_attack", 0x80042734, 0x80042D68),
                0x800422DC: ("nc_attack_bounds", 0x80042360, 0x800424AC),
                0x80042AA0: ("nc_attack_bounds", 0x80042B24, 0x80042D10),
                0x80042BF0: ("nc_target_state_allows_contact", 0x80042C14, 0x80042D68),
                0x8004E510: ("nc_target_accepts_contact", 0x8004E570, 0x8004E6AC),
            }
            if self.mod.name == "psx2_exe"
            else {}
        )
        box_builders = (
            {
                0x800420B4: (False, 0x8004212C),
                0x800421A4: (False, 0x8004221C),
                0x8004281C: (True, 0x80042894),
                0x80042904: (True, 0x8004297C),
            }
            if self.mod.name == "psx2_exe"
            else {}
        )
        for start, (_, resume) in box_builders.items():
            if f.addr <= start < f.end:
                targets.add(resume)
        for start, (_, passed, rejected) in contact_blocks.items():
            if f.addr <= start < f.end:
                targets.update((passed, rejected))
        targets = {t for t in targets if f.addr <= t < f.end}
        if self.mod.name == "psx2_exe" and f.addr == 0x8002AE78:
            targets.update(
                (
                    0x8002AEE0,
                    0x8002AEFC,
                    0x8002AF08,
                    0x8002AF28,
                    0x8002AF38,
                    0x8002AF48,
                    0x8002AF58,
                    0x8002AF68,
                )
            )
        if self.mod.name == "psx2_exe" and f.addr == 0x8002B22C:
            targets.update((0x8002B260, 0x8002B304))
        if self.mod.name == "psx2_exe" and f.addr == 0x8002B3D0:
            targets.update((0x8002B40C, 0x8002B5E8))
        if self.mod.name == "psx2_exe" and f.addr == 0x8002B0B0:
            targets.update(
                (
                    0x8002B160,
                    0x8002B16C,
                    0x8002B180,
                    0x8002B18C,
                    0x8002B19C,
                    0x8002B1A8,
                    0x8002B1B0,
                    0x8002B1B8,
                    0x8002B1C0,
                    0x8002B1D0,
                    0x8002B200,
                    0x8002B208,
                )
            )
        if self.mod.name == "psx2_exe" and f.addr == 0x8004CE18:
            targets.add(0x8004CE58)
        if self.mod.name == "psx2_exe" and f.addr == 0x80047128:
            targets.update((0x800471AC, 0x800471B4, 0x8004720C))
        if self.mod.name == "psx2_exe" and f.addr == 0x8006C6F0:
            targets.update((0x8006C714, 0x8006C750, 0x8006C7C8, 0x8006C7DC, 0x8006C814, 0x8006C894))
        if self.mod.name == "psx2_exe" and f.addr == 0x8006C8E4:
            targets.update((0x8006C904, 0x8006C938, 0x8006C9B0, 0x8006C9C4, 0x8006C9F8, 0x8006CA34))
        if self.mod.name == "psx2_exe" and f.addr == 0x8006DE98:
            targets.update((0x8006E014, 0x8006E04C, 0x8006E06C))
        if self.mod.name == "psx2_exe" and f.addr == 0x800586D8:
            targets.update((0x800587A0, 0x800587CC, 0x80058830))
        if self.mod.name == "psx2_exe" and f.addr == 0x80058868:
            targets.add(0x800588B0)
        if self.mod.name == "psx2_exe" and f.addr == 0x80047220:
            targets.update((0x80047254, 0x800472B8))
        if self.mod.name == "psx2_exe" and f.addr == 0x80042D9C:
            targets.update((0x80042E18, 0x80042E30, 0x80042E8C, 0x80042EF4, 0x80042F40))
        if self.mod.name == "psx2_exe" and f.addr == 0x80067130:
            targets.update((0x800672B4, 0x800672BC))
        if self.mod.name == "psx2_exe" and f.addr == 0x80044724:
            targets.update((0x80044754, 0x80044780, 0x800447B8))
        if self.mod.name == "psx2_exe" and f.addr == 0x80067358:
            targets.add(0x80067464)
            targets.update((0x80068424, 0x80068480, 0x80068698, 0x8006874C, 0x8006893C, 0x80068980))
        if self.mod.name == "psx2_exe" and f.addr == 0x80070050:
            targets.add(0x800700C8)
        if self.mod.name == "psx2_exe" and f.addr == 0x800472D8:
            targets.update((0x80047328, 0x800473C0, 0x800473C8))
        if self.mod.name == "psx2_exe" and f.addr == 0x800473E8:
            targets.update((0x80047480, 0x80047518))
        if self.mod.name == "psx2_exe" and f.addr == 0x80046B1C:
            targets.update((0x80046E68, 0x80046F2C, 0x8004701C, 0x800470A4))
        if self.mod.name == "psx2_exe" and f.addr == 0x80025FE4:
            targets.add(0x8002607C)
        if self.mod.name == "psx2_exe" and f.addr == 0x800261C8:
            targets.add(0x80026248)
        if self.mod.name == "psx2_exe" and f.addr == 0x80026560:
            targets.update((0x80026634, 0x800266A4))
        if self.mod.name == "psx2_exe" and f.addr == 0x80026828:
            targets.update((0x80026908, 0x80026AA0, 0x80026BB8))
        if self.mod.name == "psx2_exe" and f.addr == 0x800266F4:
            targets.update((0x80026784, 0x800267C8, 0x800267DC))
        if self.mod.name == "psx2_exe" and f.addr == 0x80026394:
            targets.add(0x80026414)
        if self.mod.name == "psx2_exe" and f.addr == 0x8003433C:
            targets.update((0x800343D8, 0x80034480, 0x8003475C, 0x80034788))
        table_cases: list[int] = []
        if jump_tables:
            table_cases = sorted(w for w in self.mod.words if f.addr < w < f.end and w % 4 == 0)
            targets.update(table_cases)

        insns = f.insns
        n = len(insns)
        # Delay slots that are also branch targets get emitted twice; the normal path then
        # skips over the second copy to the instruction after the slot.
        slot_targets = {
            insns[k + 1].addr
            for k in range(n - 1)
            if insns[k].has_delay() and insns[k + 1].addr in targets
        }
        targets.update(a + 4 for a in slot_targets)

        name = self.mod.cname(f.addr) if entry is None else self.mod.resume_name(entry)
        out = [
            f"void {name}(CPUState *c) {{",
            f"    NC_FN_ENTER(c, 0x{f.addr:08X}u);",
        ]
        if entry is not None:
            out.append(f"    goto L_{entry:08X};")
        k = 0
        while k < n:
            i = insns[k]
            if i.addr in targets:
                out.append(f"L_{i.addr:08X}:;")
            if self.mod.name == "credits_exe" and i.addr in CREDITS_ROUTINES:
                out.append(f"    if ({CREDITS_ROUTINES[i.addr]}) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80044724:
                out.extend(
                    [
                        "    if (nc_enter_player_input(c)) {",
                        "        if (R(2)) goto L_800451EC;",
                        "        goto L_80044754;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr in (0x800451E8, 0x800451EC):
                store_buttons = "true" if i.addr == 0x800451E8 else "false"
                out.append(f"    if (nc_leave_player_input(c, {store_buttons})) return;")
            if self.mod.name == "psx2_exe" and i.addr in (0x800447B8, 0x800448D4):
                held = "true" if i.addr == 0x800447B8 else "false"
                out.append(f"    if (nc_player_l2_direction(c, {held})) goto L_800448E0;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80042D9C:
                out.extend(
                    [
                        "    switch (nc_enter_pad_dispatch(c)) {",
                        "    case 0x80042E7C: goto L_80042E7C;",
                        "    case 0x80042DE4: goto L_80042DE4;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80042DE4:
                out.extend(
                    [
                        "    switch (nc_pad_playback_poll_gate(c)) {",
                        "    case 0x80042E18: goto L_80042E18;",
                        "    case 0x80042F54: goto L_80042F54;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80042E18:
                out.extend(
                    [
                        "    switch (nc_poll_playback_input(c)) {",
                        "    case 0x80042E30: goto L_80042E30;",
                        "    case 0x80042E38: goto L_80042E38;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr in (0x80042E30, 0x80042E38):
                check_end = "true" if i.addr == 0x80042E30 else "false"
                out.append(f"    if (nc_pad_playback_interrupt(c, {check_end})) goto L_80042F54;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80042EF4:
                out.append("    if (nc_call_pad_action(c, false)) goto L_80042EFC;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80042F40:
                out.append("    if (nc_call_pad_action(c, true)) goto L_80042F54;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80042F54:
                out.append("    if (nc_leave_pad_dispatch(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80042EFC:
                out.extend(
                    [
                        "    switch (nc_pad_select_edge(c)) {",
                        "    case 0x80042F40: goto L_80042F40;",
                        "    case 0x80042F54: goto L_80042F54;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80042E7C:
                out.append("    if (nc_call_live_pad(c, 0x80042E8C)) goto L_80042E8C;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80042E8C:
                out.extend(
                    [
                        "    switch (nc_pad_start_edge(c)) {",
                        "    case 0x80042EF4: goto L_80042EF4;",
                        "    case 0x80042F18: goto L_80042F18;",
                        "    case 0x80042EFC: goto L_80042EFC;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80067130:
                out.extend(
                    [
                        "    switch (nc_menu_button_scan_entry(c)) {",
                        "    case 0x8006718C: goto L_8006718C;",
                        "    case 0x80067308: goto L_80067308;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x8006718C:
                out.extend(
                    [
                        "    switch (nc_menu_sequence_match(c)) {",
                        "    case 0x8006725C: goto L_8006725C;",
                        "    case 0x800672EC: goto L_800672EC;",
                        "    case 0x800672E0: goto L_800672E0;",
                        "    case 0x80067200: goto L_80067200;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80067200:
                out.append("    if (nc_menu_apply_enabled_record(c)) goto L_80067240;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80067240:
                out.extend(
                    [
                        "    switch (nc_menu_advance_enabled_record(c)) {",
                        "    case 0x80067200: goto L_80067200;",
                        "    case 0x800672FC: goto L_800672FC;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x8006725C:
                out.extend(
                    [
                        "    switch (nc_menu_record_sequence(c)) {",
                        "    case 0x800672F8: goto L_800672F8;",
                        "    case 0x800672EC: goto L_800672EC;",
                        "    case 0x800672E0: goto L_800672E0;",
                        "    case 0x800672B4: goto L_800672B4;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x800672B4:
                out.append("    if (nc_menu_apply_sequence(c)) goto L_800672BC;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800672BC:
                out.append("    if (nc_menu_reset_sequences(c)) goto L_800672F8;")
            if self.mod.name == "psx2_exe" and i.addr in (0x800672E0, 0x800672EC):
                matched = "true" if i.addr == 0x800672E0 else "false"
                out.append(f"    if (nc_menu_store_progress(c, {matched})) goto L_800672F8;")
            if self.mod.name == "psx2_exe" and i.addr in (0x800672F8, 0x800672FC):
                increment = "true" if i.addr == 0x800672F8 else "false"
                out.extend(
                    [
                        f"    switch (nc_menu_next_record(c, {increment})) {{",
                        "    case 0x8006718C: goto L_8006718C;",
                        "    case 0x80067308: goto L_80067308;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80067308:
                out.append("    if (nc_leave_menu_button_scan(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800687D8:
                out.extend(
                    [
                        "    switch (nc_menu_record_shoulders(c)) {",
                        "    case 0x80068D60: goto L_80068D60;",
                        "    case 0x800689A8: goto L_800689A8;",
                        "    case 0x8006885C: goto L_8006885C;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr in (0x800689A8, 0x80068ADC):
                if i.addr == 0x800689A8:
                    routine, next_check = "nc_menu_record_direction_repeat", "80068ADC"
                else:
                    routine, next_check = "nc_menu_record_shoulder_repeat", "80068B74"
                out.extend(
                    [
                        f"    switch ({routine}(c)) {{",
                        f"    case 0x{next_check}: goto L_{next_check};",
                        "    case 0x80068CE0: goto L_80068CE0;",
                        "    case 0x80068CF8: goto L_80068CF8;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr in (0x80068CF8, 0x80068D00):
                load_record = "true" if i.addr == 0x80068CF8 else "false"
                out.extend(
                    [
                        f"    switch (nc_menu_save_record_buttons(c, {load_record})) {{",
                        "    case 0x80068D30: goto L_80068D30;",
                        "    case 0x80068D3C: goto L_80068D3C;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x8006745C:
                out.append("    if (nc_menu_call_selected_record(c)) goto L_80067464;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80067464:
                out.append("    if (nc_menu_record_timeout(c)) goto L_800674A0;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80067400:
                out.extend(
                    [
                        "    switch (nc_menu_select_mode_record(c)) {",
                        "    case 0x8006745C: goto L_8006745C;",
                        "    case 0x800674A0: goto L_800674A0;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x800673BC:
                out.append("    if (nc_menu_mode_record_call(c)) goto L_800674A0;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80067358:
                out.extend(
                    [
                        "    switch (nc_enter_menu_input(c)) {",
                        "    case 0x800673BC: goto L_800673BC;",
                        "    case 0x80067400: goto L_80067400;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068FE4:
                out.append("    if (nc_leave_menu_input(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80068F88:
                out.append("    if (nc_menu_dispatch_face_button(c)) goto L_80068FA8;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80068FA8:
                out.append("    if (nc_menu_mode_record_result(c)) goto L_80068FE4;")
            if self.mod.name == "psx2_exe" and i.addr in (0x80068F34, 0x80068F38):
                mask_cross = "true" if i.addr == 0x80068F34 else "false"
                out.extend(
                    [
                        f"    switch (nc_menu_mode_face_buttons(c, {mask_cross})) {{",
                        "    case 0x80068F88: goto L_80068F88;",
                        "    case 0x80068FA8: goto L_80068FA8;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068EE8:
                out.extend(
                    [
                        "    switch (nc_menu_mode_down(c)) {",
                        "    case 0x80068F38: goto L_80068F38;",
                        "    case 0x80068F34: goto L_80068F34;",
                        "    case 0x80068F0C: goto L_80068F0C;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068F0C:
                out.append("    if (nc_menu_dispatch_direction(c)) goto L_80068FE4;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80068E84:
                out.extend(
                    [
                        "    switch (nc_menu_mode_up(c)) {",
                        "    case 0x80068FE4: goto L_80068FE4;",
                        "    case 0x80068EE8: goto L_80068EE8;",
                        "    case 0x80068F0C: goto L_80068F0C;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068E18:
                out.append("    if (nc_menu_check_record_102(c)) goto L_80068E50;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80068E50:
                out.extend(
                    [
                        "    switch (nc_menu_check_record_103(c)) {",
                        "    case 0x80068E84: goto L_80068E84;",
                        "    case 0x80068FE4: goto L_80068FE4;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068DBC:
                out.append("    if (nc_menu_dispatch_timed_record(c)) goto L_80068DD4;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80068DD4:
                out.append("    if (nc_menu_record_cross(c)) goto L_80068E18;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80068D60:
                out.extend(
                    [
                        "    switch (nc_menu_record_four_timing(c)) {",
                        "    case 0x80068E84: goto L_80068E84;",
                        "    case 0x80068DD4: goto L_80068DD4;",
                        "    case 0x80068DBC: goto L_80068DBC;",
                        "    case 0x80068FE4: goto L_80068FE4;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068D30:
                out.append("    if (nc_menu_reset_record_100(c)) goto L_80068D3C;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80068D3C:
                out.extend(
                    [
                        "    switch (nc_menu_check_record_101(c)) {",
                        "    case 0x80068D60: goto L_80068D60;",
                        "    case 0x80068FE4: goto L_80068FE4;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068CA4:
                out.extend(
                    [
                        "    switch (nc_menu_record_start(c)) {",
                        "    case 0x80068CF4: goto L_80068CF4;",
                        "    case 0x80068D00: goto L_80068D00;",
                        "    case 0x80068CE0: goto L_80068CE0;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068CE0:
                out.append("    if (nc_menu_dispatch_and_save_buttons(c)) goto L_80068CF8;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80068CF4:
                out.append("    if (nc_menu_clear_repeat(c)) goto L_80068CF8;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80068BEC:
                out.extend(
                    [
                        "    switch (nc_menu_record_face_repeat(c)) {",
                        "    case 0x80068CA4: goto L_80068CA4;",
                        "    case 0x80068CE0: goto L_80068CE0;",
                        "    case 0x80068CF8: goto L_80068CF8;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068B74:
                out.extend(
                    [
                        "    switch (nc_menu_record_face_edges(c)) {",
                        "    case 0x80068BEC: goto L_80068BEC;",
                        "    case 0x80068CE0: goto L_80068CE0;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068980:
                out.append("    if (nc_menu_apply_record_select(c)) goto L_80068CF8;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006893C:
                out.append("    if (nc_menu_dispatch_record_action(c)) goto L_80068944;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80068944:
                out.extend(
                    [
                        "    switch (nc_menu_record_select(c)) {",
                        "    case 0x80068980: goto L_80068980;",
                        "    case 0x80068D00: goto L_80068D00;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x800688EC:
                out.extend(
                    [
                        "    switch (nc_menu_record_triangle(c)) {",
                        "    case 0x8006893C: goto L_8006893C;",
                        "    case 0x80068944: goto L_80068944;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x8006885C:
                out.append("    if (nc_menu_record_button_pairs(c)) goto L_800688EC;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800687CC:
                out.append("    if (nc_menu_scan_and_save_buttons(c)) goto L_800687D8;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800687B4:
                out.append("    if (nc_menu_apply_direction(c)) goto L_800687CC;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800687C8:
                out.append("    if (nc_menu_clear_repeat(c)) goto L_800687CC;")
            if self.mod.name == "psx2_exe" and i.addr in (0x8006865C, 0x8006870C):
                right = i.addr == 0x8006870C
                change = 0x8006874C if right else 0x80068698
                out.extend(
                    [
                        f"    switch (nc_menu_direction_selection(c, {str(right).lower()})) {{",
                        f"    case 0x{change:X}: goto L_{change:X};",
                        "    case 0x800687B4: goto L_800687B4;",
                        "    case 0x800687CC: goto L_800687CC;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068624:
                out.extend(
                    [
                        "    switch (nc_menu_horizontal_repeat(c)) {",
                        "    case 0x8006865C: goto L_8006865C;",
                        "    case 0x8006870C: goto L_8006870C;",
                        "    case 0x800687C8: goto L_800687C8;",
                        "    case 0x800687CC: goto L_800687CC;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068578:
                out.extend(
                    [
                        "    switch (nc_menu_vertical_repeat(c)) {",
                        "    case 0x800685D8: goto L_800685D8;",
                        "    case 0x800685FC: goto L_800685FC;",
                        "    case 0x80068624: goto L_80068624;",
                        "    case 0x800687CC: goto L_800687CC;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x8006851C:
                out.extend(
                    [
                        "    switch (nc_menu_face_press(c)) {",
                        "    case 0x80068578: goto L_80068578;",
                        "    case 0x800685FC: goto L_800685FC;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x800684D0:
                out.extend(
                    [
                        "    switch (nc_menu_start_down(c)) {",
                        "    case 0x8006851C: goto L_8006851C;",
                        "    case 0x800685D8: goto L_800685D8;",
                        "    case 0x800685FC: goto L_800685FC;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x800685D8:
                out.extend(
                    [
                        "    switch (nc_menu_down_repeat(c)) {",
                        "    case 0x800685FC: goto L_800685FC;",
                        "    case 0x800687CC: goto L_800687CC;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x8006848C:
                out.extend(
                    [
                        "    switch (nc_menu_up_repeat(c)) {",
                        "    case 0x800684D0: goto L_800684D0;",
                        "    case 0x800685FC: goto L_800685FC;",
                        "    case 0x800687CC: goto L_800687CC;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068438:
                out.extend(
                    [
                        "    switch (nc_menu_right_input(c)) {",
                        "    case 0x80068480: goto L_80068480;",
                        "    case 0x80068488: goto L_80068488;",
                        "    case 0x8006848C: goto L_8006848C;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x800683D0:
                out.extend(
                    [
                        "    switch (nc_menu_left_input(c)) {",
                        "    case 0x80068424: goto L_80068424;",
                        "    case 0x80068438: goto L_80068438;",
                        "    case 0x8006848C: goto L_8006848C;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80068344:
                out.append("    if (nc_menu_pad_type_flags(c)) goto L_800683D0;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80068154:
                out.extend(
                    [
                        "    switch (nc_menu_confirm_edge(c)) {",
                        "    case 0x8006818C: goto L_8006818C;",
                        "    case 0x80068304: goto L_80068304;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x8006D548:
                out.append("    if (nc_pad_inactivity(c)) goto L_8006D5A8;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80044784:
                out.extend(
                    [
                        "    switch (nc_dispatch_player_buttons(c)) {",
                        "    case 0x800451EC: goto L_800451EC;",
                        "    case 0x800451E8: goto L_800451E8;",
                        "    case 0x800447B8: goto L_800447B8;",
                        "    case 0x800448D4: goto L_800448D4;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80044754:
                out.extend(
                    [
                        "    switch (nc_player_input_source(c)) {",
                        "    case 0x80044780: goto L_80044780;",
                        "    case 0x80044784: goto L_80044784;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80070050:
                out.extend(
                    [
                        "    switch (nc_read_pad(c)) {",
                        "    case 0: return;",
                        "    case 0x800700C8: goto L_800700C8;",
                        "    case 0x800705D8: goto L_800705D8;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80062C28:
                out.append("    if (nc_init_pad_sequence(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80062CB0:
                out.append("    if (nc_stop_pad_sequence(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80062CC8:
                out.append("    if (nc_pad_sequence_ended(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80062D2C:
                out.append("    if (nc_read_pad_sequence(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr in PSX2_PAD_BIOS_SERVICES:
                service = PSX2_PAD_BIOS_SERVICES[i.addr]
                out.append(f"    if (nc_pad_bios_call(c, {service})) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80073F74:
                out.append("    if (nc_sdk_patch_pad(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80073EB4:
                out.append("    if (nc_sdk_init_pad(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80073F1C:
                out.append("    if (nc_sdk_start_pad(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006FEBC:
                out.append("    if (nc_start_pad_driver(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006FF0C:
                out.append("    if (nc_stop_pad_driver(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006FF34:
                out.append("    if (nc_select_pad(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8007001C:
                out.append("    if (nc_pad_packet_type(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8007002C:
                out.append("    if (nc_read_pad_stick(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800700FC:
                out.append("    if (nc_decode_pad(c)) goto L_800705D8;")
            if i.addr in contact_blocks:
                check, passed, rejected = contact_blocks[i.addr]
                out.extend(
                    [
                        f"    switch ({check}(c)) {{",
                        f"    case NC_CONTACT_PASS: goto L_{passed:08X};",
                        f"    case NC_CONTACT_REJECT: goto L_{rejected:08X};",
                        "    case NC_CONTACT_TIMED: break;",
                        "    }",
                    ]
                )
            if i.addr in box_builders:
                outgoing, resume = box_builders[i.addr]
                arg = "true" if outgoing else "false"
                out.append(f"    if (nc_prepare_attack_box(c, {arg})) goto L_{resume:08X};")
            if self.mod.name == "psx2_exe" and i.addr == 0x8004CE24:
                out.append("    if (nc_actor_distance_terms(c)) goto L_8004CE58;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800475E4:
                out.append("    if (nc_distance_root(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80047268:
                out.append("    if (nc_prepare_reference_distance(c)) {")
                out.append(f"        {self.call(0x8007A050)}")
                out.append("        goto L_800472B8; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800472B8:
                out.append("    if (nc_store_reference_distance(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8004737C:
                out.append("    if (nc_prepare_record_projection(c)) {")
                out.append(f"        {self.call(0x80047128)}")
                out.append("        goto L_800473C0; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800473C0:
                out.append("    if (nc_call_projected_point_heading(c)) {")
                out.append(f"        {self.call(0x80047220)}")
                out.append("        goto L_800473C8; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800473C8:
                out.append("    if (nc_store_projected_heading(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800475A8:
                out.append("    if (nc_prepare_saved_bearing_projection(c)) {")
                out.append(f"        {self.call(0x80047128)}")
                out.append("        goto L_800475CC; }")
            if self.mod.name == "psx2_exe" and i.addr in (0x80047480, 0x800475CC):
                store_heading = "true" if i.addr == 0x80047480 else "false"
                out.append(f"    if (nc_finish_projected_point_update(c, {store_heading})) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80047564:
                out.append(
                    "    { bool reproject; if (nc_select_projected_heading(c, &reproject)) {"
                )
                out.append("        if (reproject) goto L_800475A8;")
                out.append("        goto L_800475CC; } }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80047420:
                out.append(
                    "    { bool use_point; if (nc_begin_projected_point_update(c, &use_point)) {"
                )
                out.append("        if (!use_point) goto L_80047488;")
                out.append(f"        {self.call(0x80047220)}")
                out.append("        goto L_80047480; } }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80047488:
                out.append("    if (nc_prepare_facing_offset(c)) {")
                out.append(f"        {self.call(0x80047128)}")
                out.append("        goto L_80047518; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80047518:
                out.append("    if (nc_projected_heading_gap(c)) goto L_80047564;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80047300:
                out.append("    if (nc_prepare_relative_heading(c)) {")
                out.append(f"        {self.call(0x80047220)}")
                out.append("        goto L_80047328; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80047328:
                out.append("    if (nc_finish_relative_heading(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800472D8:
                out.append("    if (nc_actor_heading(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80047220:
                out.append("    if (nc_prepare_point_heading(c)) {")
                out.append(f"        {self.call(0x80047684)}")
                out.append("        goto L_80047254; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800471E0:
                out.append("    if (nc_prepare_local_point_heading(c)) {")
                out.append(f"        {self.call(0x80047684)}")
                out.append("        goto L_8004720C; }")
            if self.mod.name == "psx2_exe" and i.addr in (0x8004720C, 0x80047254):
                out.append("    if (nc_return_point_heading(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80025FE4:
                out.append("    if (nc_sample_part_rotation(c, NC_ROTATION_PART)) goto L_8002607C;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026560:
                out.append("    if (nc_start_node_traversal(c)) goto L_800265CC;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800265AC:
                out.append("    if (nc_restore_node_branch(c)) goto L_800265CC;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800266A4:
                out.append("    if (nc_apply_node_matrix(c)) goto L_800266B4;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026828:
                out.append("    if (nc_start_scaled_part_position(c)) goto L_80026908;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026908:
                out.append("    if (nc_begin_scaled_part_walk(c)) goto L_800269B4;")
            if self.mod.name == "psx2_exe" and i.addr in (0x80026AA0, 0x80026B28):
                shared = "true" if i.addr == 0x80026AA0 else "false"
                out.append(f"    if (nc_sample_traversal_angles(c, {shared})) goto L_80026BB8;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026CDC:
                out.append("    if (nc_compose_traversal_rotation(c)) goto L_80026F60;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026BB8:
                out.append("    if (nc_build_traversal_matrix(c)) goto L_80026CDC;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026A6C:
                out.append("    switch (nc_select_part_heading_source(c)) {")
                out.append("    case NC_HEADING_SKIP: goto L_80026CDC;")
                out.append("    case NC_HEADING_FROM_PART: goto L_80026B28;")
                out.append("    case NC_HEADING_FROM_SHARED: goto L_80026AA0;")
                out.append("    case NC_HEADING_FALLBACK: break; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026A28:
                out.append("    if (nc_update_child_position(c)) goto L_80026A6C;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800269B4:
                out.append("    if (nc_filter_scaled_part(c)) {")
                out.append("        if (R(22) ? R(2) != 0 : R(2) == 0) goto L_80026A28;")
                out.append("        goto L_80026F60; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026F60:
                out.append("    switch (nc_advance_scaled_part_walk(c)) {")
                out.append("    case NC_NODE_NEXT: goto L_800269B4;")
                out.append("    case NC_NODE_POP: goto L_8002695C;")
                out.append("    case NC_NODE_RETURN: return;")
                out.append("    case NC_NODE_FALLBACK: break; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002695C:
                out.append("    if (nc_resume_scaled_part_walk(c)) goto L_800269B4;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800266F4:
                out.append("    if (nc_start_part_traversal(c)) goto L_80026754;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026738:
                out.append("    if (nc_restore_part_branch(c)) goto L_80026754;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026754:
                out.append("    if (nc_prepare_part_translation(c)) {")
                out.append(f"        {self.call(0x8007A530)}")
                out.append("        goto L_80026784; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026784:
                out.append("    if (nc_select_part_rotation(c)) {")
                out.append("        if (!R(2)) goto L_800267EC;")
                out.append(f"        {self.call(0x80025FE4)}")
                out.append("        goto L_800267C8; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800265CC:
                out.append("    if (nc_select_node_transform(c)) {")
                out.append("        if (!R(2)) goto L_800266B4;")
                out.append(f"        {self.call(0x8007A530)}")
                out.append("        goto L_80026634; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800267C8:
                out.append("    if (nc_prepare_part_composition(c)) {")
                out.append(f"        {self.call(0x8007A0E0)}")
                out.append("        goto L_800267DC; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800267DC:
                out.append("    if (nc_apply_part_matrix(c)) goto L_800267EC;")
            if self.mod.name == "psx2_exe" and i.addr in (0x800266B4, 0x800267EC):
                if i.addr == 0x800267EC:
                    part, next_node, pop = "true", "L_80026754", "L_80026738"
                else:
                    part, next_node, pop = "false", "L_800265CC", "L_800265AC"
                out.append(f"    switch (nc_advance_node_transform(c, {part})) {{")
                out.append(f"    case NC_NODE_NEXT: goto {next_node};")
                out.append(f"    case NC_NODE_POP: goto {pop};")
                out.append("    case NC_NODE_RETURN: return;")
                out.append("    case NC_NODE_FALLBACK: break; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026634:
                out.append("    if (nc_prepare_node_rotation(c)) {")
                out.append(f"        {self.call(0x8007A0E0)}")
                out.append("        goto L_800266A4; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80026394:
                out.append(
                    "    if (nc_sample_part_rotation(c, NC_ROTATION_PACKED)) goto L_80026414;"
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80026414:
                out.append("    if (nc_finish_part_rotation(c, NC_ROTATION_PACKED)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800261C8:
                out.append(
                    "    if (nc_sample_part_rotation(c, NC_ROTATION_TRANSPOSED)) goto L_80026248;"
                )
            if self.mod.name == "psx2_exe" and i.addr == 0x80026248:
                out.append("    if (nc_finish_part_rotation(c, NC_ROTATION_TRANSPOSED)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002607C:
                out.append("    if (nc_finish_part_rotation(c, NC_ROTATION_PART)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8003433C:
                out.append("    if (nc_init_part_transform(c)) goto L_800343D8;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800343D8:
                out.append("    if (nc_begin_part_chain(c)) {")
                out.append("        if (R(2)) goto L_800343F4;")
                out.append("        goto L_80034410; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800343F4:
                out.append("    if (nc_append_part_parent(c)) {")
                out.append("        if (R(2)) goto L_800343F4;")
                out.append("        goto L_80034410; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80034410:
                out.append("    { bool have_parts = R(16) != 0;")
                out.append("      if (nc_prepare_part_chain_rotation(c)) {")
                out.append("          if (have_parts) goto L_80034430;")
                out.append("          goto L_800345BC; } }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80034430:
                out.append("    if (nc_transform_part_offset(c)) {")
                out.append(f"        {self.call(0x80025FE4)}")
                out.append("        goto L_80034480; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80034480:
                out.append("    { bool more_parts = R(16) != 0;")
                out.append("      if (nc_compose_part_matrix(c)) {")
                out.append("          if (more_parts) goto L_80034430;")
                out.append("          goto L_800345BC; } }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800345BC:
                out.append("    if (nc_finish_part_translation(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80034610:
                out.append("    if (nc_prepare_heading_matrix(c)) {")
                out.append(f"        {self.call(0x8007A1F0)}")
                out.append("        goto L_8003475C; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x8003475C:
                out.append("    if (nc_prepare_matrix_planar_length(c)) {")
                out.append(f"        {self.call(0x800475E4)}")
                out.append("        goto L_80034788; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x80034788:
                out.append("    if (nc_finish_matrix_headings(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80047684:
                out.append("    if (nc_vector_heading(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr in (0x8006C7C8, 0x8006C9B0):
                actor = i.addr == 0x8006C7C8
                resume = 0x8006C7DC if actor else 0x8006C9C4
                out.append(
                    f"    if (nc_call_spatial_attenuation(c, {'true' if actor else 'false'})) {{"
                )
                out.append(f"        {self.call(0x8006C63C)}")
                out.append(f"        goto L_{resume:08X}; }}")
            if self.mod.name == "psx2_exe" and i.addr in (0x8006C7DC, 0x8006C9C4):
                actor = i.addr == 0x8006C7DC
                flagged, ordinary = (0x8006C814, 0x8006C89C) if actor else (0x8006C9F8, 0x8006CA3C)
                out.append(f"    if (nc_scale_spatial_level(c, {'true' if actor else 'false'})) {{")
                out.append(f"        if (R(2)) goto L_{flagged:08X};")
                out.append(f"        goto L_{ordinary:08X}; }}")
            if self.mod.name == "psx2_exe" and i.addr in (0x8006C9F8, 0x8006CA3C):
                flagged = "true" if i.addr == 0x8006C9F8 else "false"
                out.append(f"    switch (nc_select_point_levels(c, {flagged})) {{")
                out.append("    case NC_POINT_LEVEL_SET:")
                out.append(f"        {self.call(0x8006C524)}")
                out.append("        goto L_8006CA60;")
                out.append("    case NC_POINT_LEVEL_DISTANT:")
                out.append(f"        {self.call(0x8006BFE8)}")
                out.append("        goto L_8006CA34;")
                out.append("    case NC_POINT_LEVEL_FALLBACK: break; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006C8E4:
                out.append("    if (nc_begin_spatial_point(c)) goto L_8006C904;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006C814:
                out.append("    switch (nc_check_actor_level_distance(c)) {")
                out.append("    case NC_ACTOR_LEVEL_CHECK_FLAGS: goto L_8006C848;")
                out.append("    case NC_ACTOR_LEVEL_DISTANT: goto L_8006C880;")
                out.append("    case NC_ACTOR_LEVEL_DISTANT_RECORD_READY: goto L_8006C884;")
                out.append("    default: break; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006C848:
                out.append("    switch (nc_check_actor_level_flags(c)) {")
                out.append("    case NC_ACTOR_LEVEL_SET: goto L_8006C89C;")
                out.append("    case NC_ACTOR_LEVEL_CHANNEL_READY: goto L_8006C8A0;")
                out.append("    case NC_ACTOR_LEVEL_DISTANT: goto L_8006C880;")
                out.append("    case NC_ACTOR_LEVEL_DISTANT_RECORD_READY: goto L_8006C884;")
                out.append("    default: break; }")
            if self.mod.name == "psx2_exe" and i.addr in (0x8006C880, 0x8006C884):
                load_record = "true" if i.addr == 0x8006C880 else "false"
                out.append(f"    if (nc_call_distant_actor(c, {load_record})) {{")
                out.append(f"        {self.call(0x8006BFE8)}")
                out.append("        goto L_8006C894; }")
            if self.mod.name == "psx2_exe" and i.addr in (0x8006C89C, 0x8006C8A0):
                load_channel = "true" if i.addr == 0x8006C89C else "false"
                out.append(f"    if (nc_set_actor_levels(c, {load_channel})) {{")
                out.append(f"        {self.call(0x8006C524)}")
                out.append("        goto L_8006C8BC; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006C6F0:
                out.append("    if (nc_begin_spatial_actor(c)) goto L_8006C714;")
            if self.mod.name == "psx2_exe" and i.addr in (
                0x8006C894,
                0x8006C8BC,
                0x8006CA34,
                0x8006CA60,
            ):
                actor = "true" if i.addr in (0x8006C894, 0x8006C8BC) else "false"
                distant = "true" if i.addr in (0x8006C894, 0x8006CA34) else "false"
                out.append(f"    if (nc_finish_spatial_frame(c, {actor}, {distant})) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006C63C:
                out.append("    if (nc_spatial_attenuation(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr in (0x8006C714, 0x8006C904):
                actor = i.addr == 0x8006C714
                source = "actor" if actor else "point"
                resume = 0x8006C750 if actor else 0x8006C938
                out.append(f"    if (nc_prepare_spatial_{source}_bearing(c)) {{")
                out.append(f"        {self.call(0x80047684)}")
                out.append(f"        goto L_{resume:08X}; }}")
            if self.mod.name == "psx2_exe" and i.addr in (0x8006C750, 0x8006C938):
                actor = i.addr == 0x8006C750
                resume = 0x8006C7C8 if actor else 0x8006C9B0
                out.append(
                    f"    if (nc_prepare_spatial_distance(c, {'true' if actor else 'false'})) {{"
                )
                out.append(f"        {self.call(0x800475E4)}")
                out.append(f"        goto L_{resume:08X}; }}")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002AFB4:
                out.append("    if (nc_step_placement_record(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002AE78:
                out.append("    if (nc_begin_placement_vectors(c)) goto L_8002AEE0;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002AF68:
                out.append("    if (nc_finish_placement_vectors(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr in (0x8002AEE0, 0x8002AEFC, 0x8002AF08):
                helper, callee, resume = {
                    0x8002AEE0: ("yaw", 0x8007A7A0, 0x8002AEFC),
                    0x8002AEFC: ("yaw_scale", 0x8007A1F0, 0x8002AF08),
                    0x8002AF08: ("heading_vector", 0x8007A320, 0x8002AF28),
                }[i.addr]
                out.append(f"    if (nc_prepare_placement_{helper}(c)) {{")
                out.append(f"        {self.call(callee)}")
                out.append(f"        goto L_{resume:08X}; }}")
            if self.mod.name == "psx2_exe" and i.addr in (
                0x8002AF28,
                0x8002AF38,
                0x8002AF48,
                0x8002AF58,
            ):
                source, result = {
                    0x8002AF28: (0xC8, 0xA8),
                    0x8002AF38: (0xD0, 0xB8),
                    0x8002AF48: (0xF8, 0xD8),
                    0x8002AF58: (0x100, 0xE8),
                }[i.addr]
                resume = i.addr + 0x10
                args = f"c, 0x{source:X}, 0x{result:X}, 0x{resume:X}"
                out.append(f"    if (nc_prepare_placement_vector({args})) {{")
                out.append(f"        {self.call(0x8007A320)}")
                out.append(f"        goto L_{resume:08X}; }}")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002B0B0:
                out.append("    if (nc_begin_placement_matrices(c)) goto L_8002B14C;")
            if self.mod.name == "psx2_exe" and i.addr in (
                0x8002B14C,
                0x8002B160,
                0x8002B16C,
                0x8002B180,
                0x8002B18C,
                0x8002B19C,
                0x8002B1C0,
            ):
                helper, callee, resume = {
                    0x8002B14C: ("rotation", 0x8007A7A0, 0x8002B160),
                    0x8002B160: ("scale", 0x8007A1F0, 0x8002B16C),
                    0x8002B16C: ("position", 0x8007A320, 0x8002B180),
                    0x8002B180: ("translation", 0x8007A370, 0x8002B18C),
                    0x8002B18C: ("light_rotation", 0x8007A7A0, 0x8002B19C),
                    0x8002B19C: ("light_scale", 0x8007A1F0, 0x8002B1A8),
                    0x8002B1C0: ("transpose", 0x8007A750, 0x8002B1D0),
                }[i.addr]
                out.append(f"    if (nc_prepare_placement_{helper}(c)) {{")
                out.append(f"        {self.call(callee)}")
                out.append(f"        goto L_{resume:08X}; }}")
            if self.mod.name == "psx2_exe" and i.addr in (
                0x8002B1A8,
                0x8002B1B0,
                0x8002B1B8,
                0x8002B200,
            ):
                reg, callee = {
                    0x8002B1A8: (16, 0x8007A3A0),
                    0x8002B1B0: (16, 0x8007A430),
                    0x8002B1B8: (17, 0x8007A3D0),
                    0x8002B200: (19, 0x8002AE78),
                }[i.addr]
                resume = i.addr + 8
                args = f"c, R({reg}), 0x{resume:X}"
                out.append(f"    if (nc_prepare_placement_call({args})) {{")
                out.append(f"        {self.call(callee)}")
                out.append(f"        goto L_{resume:08X}; }}")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002B208:
                out.append("    if (nc_finish_placement_matrices(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002B1D0:
                out.append("    if (nc_prepare_transposed_placement_scale(c)) {")
                out.append(f"        {self.call(0x8007A1F0)}")
                out.append("        goto L_8002B200; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002B22C:
                out.append("    { bool has_pose;")
                out.append("      if (nc_begin_actor_placement_target(c, &has_pose)) {")
                out.append("        if (!has_pose) goto L_8002B330;")
                out.append(f"        {self.call(0x800472D8)}")
                out.append("        goto L_8002B260; } }")
            if self.mod.name == "psx2_exe" and i.addr in (0x8002B304, 0x8002B330):
                has_pose = "true" if i.addr == 0x8002B304 else "false"
                out.append(f"    if (nc_finish_actor_placement_target(c, {has_pose})) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002B260:
                out.append("    if (nc_prepare_actor_placement_target(c)) {")
                out.append(f"        {self.call(0x80047128)}")
                out.append("        goto L_8002B304; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002B348:
                out.append("    { bool update_actor;")
                out.append("      if (nc_begin_placement_mode(c, &update_actor)) {")
                out.append(f"        if (update_actor) {{ {self.call(0x80047948)} }}")
                out.append("        goto L_8002B384; } }")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002B384:
                out.append("    { bool step;")
                out.append("      if (nc_select_placement_step(c, &step)) {")
                out.append(f"        if (step) {{ {self.call(0x8002AFB4)} }}")
                out.append("        goto L_8002B3BC; } }")
            if self.mod.name == "psx2_exe" and i.addr in (0x8002B3BC, 0x8002B5E8):
                out.append("    if (nc_restore_placement_frame(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002B3D0:
                out.append("    if (nc_begin_placement_update(c)) {")
                out.append("        if (R(3) & 4) goto L_8002B40C;")
                out.append("        goto L_8002B43C; }")
            if self.mod.name == "psx2_exe" and i.addr in (0x8002B5D8, 0x8002B5E0):
                callee = 0x8002B348 if i.addr == 0x8002B5D8 else 0x8002B0B0
                resume = i.addr + 8
                out.append(f"    if (nc_prepare_placement_call(c, R(16), 0x{resume:X})) {{")
                out.append(f"        {self.call(callee)}")
                out.append(f"        goto L_{resume:08X}; }}")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002B40C:
                out.append("    if (nc_step_record_angle_pair(c)) goto L_8002B5E0;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8002AF7C:
                out.append("    if (nc_step_record_angle(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006DFE8:
                out.append("    if (nc_prepare_paired_point_bearing(c)) {")
                out.append(f"        {self.call(0x80047684)}")
                out.append("        goto L_8006E014; }")
            if self.mod.name == "psx2_exe" and i.addr in (0x8006E014, 0x8006E024):
                store_heading = "true" if i.addr == 0x8006E014 else "false"
                out.append(f"    if (nc_place_paired_point_actor(c, {store_heading})) {{")
                out.append(f"        {self.call(0x80039F00)}")
                out.append("        goto L_8006E04C; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006E05C:
                out.append("    if (nc_call_placement_record_update(c)) {")
                out.append(f"        {self.call(0x8002B3D0)}")
                out.append("        goto L_8006E06C; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006E06C:
                out.append("    if (nc_finish_paired_point_placement(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8006E04C:
                out.append("    if (nc_call_actor_coordinate_init(c)) {")
                out.append(f"        {self.call(0x800470DC)}")
                out.append("        goto L_8006E05C; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x8005876C:
                out.append("    if (nc_prepare_target_bearing(c)) {")
                out.append(f"        {self.call(0x80047684)}")
                out.append("        goto L_800587A0; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800587A0:
                out.append("    if (nc_prepare_target_distance(c)) {")
                out.append(f"        {self.call(0x800475E4)}")
                out.append("        goto L_800587CC; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800587CC:
                out.append("    if (nc_store_target_vertical_velocity(c)) goto L_80058830;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80058868:
                out.append("    if (nc_prepare_planar_velocity(c)) {")
                out.append(f"        {self.call(0x80047684)}")
                out.append("        goto L_800588B0; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800588B0:
                out.append("    if (nc_finish_planar_velocity(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8003531C:
                out.append("    if (nc_heading_cosine(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80035338:
                out.append("    if (nc_heading_sine(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr in (0x80034828, 0x800348FC):
                quarter = "true" if i.addr == 0x80034828 else "false"
                out.append(f"    if (nc_transform_part_position(c, {quarter})) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80047128:
                out.append("    if (nc_prepare_heading_projection(c)) {")
                out.append(f"        {self.call(0x80047684)}")
                out.append("        goto L_800471B4; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800471B4:
                out.append("    if (nc_finish_heading_projection(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8004716C:
                out.append("    if (nc_project_heading_offset(c)) goto L_800471AC;")
            if self.mod.name == "psx2_exe" and i.addr in (0x80046E18, 0x80046EDC):
                negative_z = i.addr == 0x80046E18
                resume = 0x80046E68 if negative_z else 0x80046F2C
                arg = "true" if negative_z else "false"
                out.append(f"    if (nc_prepare_rotated_point_offset(c, {arg})) {{")
                out.append(f"        {self.call(0x8007A320)}")
                out.append(f"        goto L_{resume:08X}; }}")
            if self.mod.name == "psx2_exe" and i.addr in (0x80046E68, 0x80046F2C):
                subtract_x = "true" if i.addr == 0x80046E68 else "false"
                out.append(f"    if (nc_apply_rotated_point_offset(c, {subtract_x})) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80046FA0:
                out.append("    if (nc_begin_actor_point_placement(c)) {")
                out.append(f"        {self.call(0x8003531C)}")
                out.append("        goto L_8004701C; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800470A4:
                out.append("    if (nc_finish_actor_point_placement(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8004701C:
                out.append("    if (nc_place_actor_points(c)) {")
                out.append(f"        {self.call(0x80047128)}")
                out.append("        goto L_800470A4; }")
            if self.mod.name == "psx2_exe" and i.addr == 0x800470DC:
                out.append("    if (nc_init_actor_coordinates(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80052B80:
                out.append("    if (nc_refill_hit_adrenaline(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800562A0:
                out.append("    if (nc_clear_adrenaline_hud(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80055DD4:
                out.append("    if (nc_update_adrenaline(c)) goto L_80055F48;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8004260C:
                out.append("    if (nc_player_attack_kind(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80041540:
                out.append("    if (nc_attack_part(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800362F8:
                out.append("    if (nc_restore_part_links(c)) goto L_80036358;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80036368:
                out.append("    if (nc_restore_attached_part(c)) goto L_800363F0;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80034D2C:
                out.append("    if (nc_mark_actor_noninteractive(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80042760:
                out.append("    if (nc_finish_attack_part(c, true)) goto L_800427A0;")
            if self.mod.name == "psx2_exe" and i.addr == 0x800427B4:
                out.append("    if (nc_finish_attack_part(c, false)) goto L_800427D0;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8004EE40:
                out.extend(
                    [
                        "    switch (nc_route_hit_reaction(c)) {",
                        "    case NC_REACTION_SELECT_ACTION: goto L_8004EEC8;",
                        "    case NC_REACTION_STATE_13: goto L_8004EFCC;",
                        "    case NC_REACTION_APPLY_FLAGS: goto L_8004F024;",
                        "    case NC_REACTION_TIMED: break;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr in (0x8004EFCC, 0x8004EFD0):
                clear_hits = "true" if i.addr == 0x8004EFCC else "false"
                out.append(f"    if (nc_enter_hit_state_13(c, {clear_hits})) goto L_8004F02C;")
            if self.mod.name == "psx2_exe" and i.addr == 0x80039964:
                out.append("    if (nc_reset_hit_animation(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8004EDCC:
                out.append("    { bool skip; if (nc_prepare_hit_reaction(c, &skip)) {")
                out.append("        if (skip) goto L_8004F02C;")
                out.append("        goto L_8004EE40; } }")
            if self.mod.name == "psx2_exe" and i.addr in (0x8004ED2C, 0x8004ED8C):
                out.append(f"    if (nc_clear_hit_reaction_flags(c)) goto L_{i.addr + 0x40:08X};")
            if self.mod.name == "psx2_exe" and i.addr == 0x8004EC68:
                out.extend(
                    [
                        "    switch (nc_check_lethal_hit(c)) {",
                        "    case NC_HIT_SURVIVED: goto L_8004ECD0;",
                        "    case NC_HIT_PLAYER_DEATH: goto L_8004EC8C;",
                        "    case NC_HIT_OTHER_DEATH: goto L_8004ECAC;",
                        "    case NC_HIT_TIMED: break;",
                        "    }",
                    ]
                )
            if self.mod.name == "psx2_exe" and i.addr in (0x8004EC8C, 0x8004ECAC):
                player = "true" if i.addr == 0x8004EC8C else "false"
                out.append(f"    if (nc_apply_lethal_reaction(c, {player})) goto L_8004F02C;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8004D0CC:
                out.append("    if (nc_enter_death_state(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8004D1A0:
                out.append("    if (nc_set_actor_state(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr == 0x8004E6E0:
                out.append("    if (nc_select_base_damage(c)) return;")
            if self.mod.name == "psx2_exe" and i.addr in (0x8004EB30, 0x8004EB6C):
                check_mode = "true" if i.addr == 0x8004EB30 else "false"
                out.append(f"    if (nc_calculate_hit_health(c, {check_mode})) goto L_8004EB78;")
            if not i.has_delay():
                out.append(f"    {self.timed_simple(i)}")
                k += 1
                continue

            if i.addr in self.hooks:
                out.append(f"    nc_hook_{self.hooks[i.addr]}(c);")
            slot = (
                insns[k + 1] if k + 1 < n else Insn(i.addr + 4, word_at(self.mod, i.addr + 4) or 0)
            )
            delay = self.timed_simple(slot, delay_slot=True)
            control = self.control(f, i, delay, table_cases)
            flag = self.patches.get(i.addr)
            if flag is not None:
                # A patched branch/jump becomes a nop: only its delay slot runs.
                out.append(f"    if (nc_flag_{flag}) {{ {delay} }} else {{")
                out.extend(control)
                out.append("    }")
            else:
                out.extend(control)
            if k + 1 < n and insns[k + 1].addr in slot_targets:
                out.append(f"    goto L_{slot.addr + 4:08X};")
                out.append(f"L_{slot.addr:08X}:;")
                out.append(f"    {self.timed_simple(slot)}")
            k += 2
        # A final branch may have consumed its delay slot across a function boundary.
        fallthrough = f.end + 4 if k > n else f.end
        if f.end in targets:
            out.append(f"L_{f.end:08X}:;")
        out.append(f"    {self.call(fallthrough)}")
        out.append("}")
        return out

    def control(self, f: Func, i: Insn, delay: str, cases: list[int]) -> list[str]:
        d = f"    {delay}" if delay else None
        lines: list[str] = []

        def emit(*xs: str | None) -> list[str]:
            return [x for x in xs if x]

        if i.is_branch():
            tgt = i.branch_target()
            link = i.op == 1 and (i.rt & 0x1E) == 0x10  # bltzal / bgezal
            lines.append(f"    {{ int bc = {self.cond(i)};")
            if link:
                lines.append(f"    R(31) = 0x{i.addr + 8:08X}u;")
            lines.extend(emit(d))
            if f.addr <= tgt < f.end and not link:
                lines.append(f"    if (bc) {{ goto L_{tgt:08X}; }} }}")
            elif link:
                lines.append(f"    if (bc) {{ {self.call(tgt)} }} }}")
            else:
                lines.append(f"    if (bc) {{ {self.call(tgt)} return; }} }}")
            return lines

        if i.op == 2:  # j
            tgt = i.jump_target()
            lines.extend(emit(d))
            if f.addr <= tgt < f.end:
                lines.append(f"    goto L_{tgt:08X};")
            else:
                lines.append(f"    {self.call(tgt)} return;")
            return lines
        if i.op == 3:  # jal
            lines.append(f"    R(31) = 0x{i.addr + 8:08X}u;")
            lines.extend(emit(d))
            lines.append(f"    {self.call(i.jump_target())}")
            return lines
        if i.funct == 8:  # jr
            if i.rs == 31:
                lines.extend(emit(d))
                lines.append("    return;")
                return lines
            lines.append(f"    {{ u32 jt = {r(i.rs)};")
            lines.extend(emit(d))
            if cases:
                lines.append("    switch (jt) {")
                lines.extend(f"    case 0x{a:08X}u: goto L_{a:08X};" for a in cases)
                lines.append("    default: break; }")
            lines.append("    nc_jump(c, jt); return; }")
            return lines
        if i.funct == 9:  # jalr
            lines.append(f"    {{ u32 jt = {r(i.rs)};")
            if i.rd:
                lines.append(f"    R({i.rd}) = 0x{i.addr + 8:08X}u;")
            lines.extend(emit(d))
            lines.append("    nc_call(c, jt); }")
            return lines
        raise AssertionError(hex(i.word))


def write_module(mod: Module, launcher: Module | None, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    em = Emitter(mod, launcher)
    header = ['#include "port/recomp.h"', f'#include "{mod.name}.h"', ""]
    if mod.name == "credits_exe":
        header.append('#include "decomp/credits.h"')
    if mod.name == "psx2_exe":
        header.append('#include "decomp/adrenaline.h"')
        header.append('#include "decomp/damage.h"')
        header.append('#include "decomp/pad.h"')
        header.append('#include "decomp/contact.h"')
        header.append('#include "decomp/distance.h"')
    for idx in range(0, len(mod.funcs), FUNCS_PER_FILE):
        chunk = mod.funcs[idx : idx + FUNCS_PER_FILE]
        lines = list(header)
        lines.extend(f"extern bool nc_flag_{flag};" for flag in sorted(set(em.patches.values())))
        lines.extend(f"void nc_hook_{h}(CPUState *c);" for h in sorted(set(em.hooks.values())))
        if launcher:
            lines.insert(2, f'#include "../{LAUNCHER}/{LAUNCHER}.h"')
        for f in chunk:
            lines.extend(em.function(f))
            lines.append("")
            for entry in sorted(e for e, rf in mod.resumes.items() if rf is f):
                lines.extend(em.function(f, entry))
                lines.append("")
        (out / f"code_{idx // FUNCS_PER_FILE}.c").write_text("\n".join(lines))

    entries = sorted(
        [(f.addr, mod.cname(f.addr)) for f in mod.funcs]
        + [(e, mod.resume_name(e)) for e in mod.resumes]
    )
    decls = [f"void {name}(CPUState *c);" for _, name in entries]
    guard = f"NC_GEN_{mod.name.upper()}_H"
    (out / f"{mod.name}.h").write_text(
        "\n".join(
            [
                f"#ifndef {guard}",
                f"#define {guard}",
                '#include "port/recomp.h"',
                *decls,
                "#endif",
                "",
            ]
        )
    )
    table = [
        '#include "port/recomp.h"',
        f'#include "{mod.name}.h"',
        "",
        f"const NcFuncEntry {mod.name}_table[] = {{",
        *(f"    {{0x{addr:08X}u, {name}}}," for addr, name in entries),
        "};",
        f"const unsigned {mod.name}_table_len = {len(entries)};",
        "",
    ]
    (out / "table.c").write_text("\n".join(table))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=ROOT / "gen")
    ap.add_argument("modules", nargs="*", default=None)
    args = ap.parse_args()

    from gen_splat import EXES

    exes = {e.lower().replace(".", "_"): e for e in EXES}
    names = args.modules or list(exes)
    launcher = load_module(LAUNCHER, exes[LAUNCHER])
    for name in names:
        mod = launcher if name == LAUNCHER else load_module(name, exes[name])
        write_module(mod, None if name == LAUNCHER else launcher, args.out / name)
        print(f"{name}: {len(mod.funcs)} functions, {len(mod.resumes)} extra entry points")
    return 0


if __name__ == "__main__":
    sys.exit(main())
