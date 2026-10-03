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
POLL_COST = 16  # cycles charged per function entry / loop iteration (coarse timing model)
LOAD_PENALTY = 4  # extra cycles per load from main RAM

# Psy-Q functions that busy-wait with an iteration-count timeout tuned for real hardware. Their
# loops are charged at realistic cost; everything else stays cheap, which effectively runs the
# game on a faster CPU (no slowdown) while keeping these timeouts from firing early.
BUSY_WAIT_FUNCS = {
    "v_wait", "VSync", "DrawSync", "_sync", "CD_sync", "CD_ready", "CD_cw", "CD_datasync",
    "CdSync", "CdReady", "CdDataSync", "DecDCTinSync", "DecDCToutSync", "MDEC_in_sync",
    "MDEC_out_sync", "SpuIsTransferCompleted",
}  # fmt: skip

LAUNCHER = "slus_005_82"
LAUNCHER_RANGE = (0x80010000, 0x80018000)

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
    busy_wait: set[int] = field(default_factory=set)  # addresses of BUSY_WAIT_FUNCS

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
    for fname in BUSY_WAIT_FUNCS:
        addr = symbol_addr(name, fname)
        if addr is not None:
            mod.busy_wait.add(addr)
    for addr in pointer_targets(mod):
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

    def loop_cost(self, f: Func, target: int, branch: int) -> int:
        """Approximate R3000A cycles for one iteration of the loop [target, branch + delay]:
        one per instruction plus a RAM access penalty per load. Busy-wait loops with iteration
        timeouts (e.g. Psy-Q's v_wait) depend on this being close to hardware."""
        if f.addr not in self.mod.busy_wait:
            return POLL_COST
        cycles = 0
        for i in f.insns:
            if target <= i.addr <= branch + 4:
                cycles += 1 + (LOAD_PENALTY if 0x20 <= i.op <= 0x26 or i.op == 0x32 else 0)
        return max(cycles, 1)

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
        targets = {t for t in targets if f.addr <= t < f.end}
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
            f"    NC_POLL(c, {POLL_COST});",
        ]
        if entry is not None:
            out.append(f"    goto L_{entry:08X};")
        k = 0
        while k < n:
            i = insns[k]
            if i.addr in targets:
                out.append(f"L_{i.addr:08X}:;")
            if i.addr in self.hooks:
                out.append(f"    nc_hook_{self.hooks[i.addr]}(c);")
            if not i.has_delay():
                stmt = self.guarded(i.addr, self.simple(i))
                if stmt:
                    out.append(f"    {stmt}")
                k += 1
                continue

            delay = self.guarded(insns[k + 1].addr, self.simple(insns[k + 1])) if k + 1 < n else ""
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
                slot = insns[k + 1].addr
                out.append(f"    goto L_{slot + 4:08X};")
                out.append(f"L_{slot:08X}:;")
                if delay:
                    out.append(f"    {delay}")
            k += 2
        # Falling off the end means execution continues into the next function.
        out.append(f"    {self.call(f.end)}")
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
                poll = f" NC_POLL(c, {self.loop_cost(f, tgt, i.addr)});" if tgt <= i.addr else ""
                lines.append(f"    if (bc) {{{poll} goto L_{tgt:08X}; }} }}")
            elif link:
                lines.append(f"    if (bc) {{ {self.call(tgt)} }} }}")
            else:
                lines.append(f"    if (bc) {{ {self.call(tgt)} return; }} }}")
            return lines

        if i.op == 2:  # j
            tgt = i.jump_target()
            lines.extend(emit(d))
            if f.addr <= tgt < f.end:
                poll = f"NC_POLL(c, {self.loop_cost(f, tgt, i.addr)}); " if tgt <= i.addr else ""
                lines.append(f"    {poll}goto L_{tgt:08X};")
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
