"""Build state.html from the US disassembly (or the committed inventory) and verified ranges.

The instruction inventory comes from build/splat/<module>/asm when that split exists locally.
Without it (for example on CI, which has no disc) the committed config/inventory/<module>.txt
files are used instead. Both sources produce identical numbers; --check-inventory verifies that.
"""

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import UTC, datetime
from html import escape
from pathlib import Path
from string import Template
from tempfile import NamedTemporaryFile

ROOT = Path(__file__).resolve().parents[1]
SPLAT = ROOT / "build/splat"
INVENTORY = ROOT / "config/inventory"

# One disassembled word: "/* <rom> <vram> <bytes> */  <text>".
WORD_LINE = re.compile(r"^\s*/\*\s+[0-9A-F]+\s+([0-9A-F]{8})\s+([0-9A-F]{8})\s*\*/\s+(\S.*)$")
RAW_WORD = re.compile(r"^\.word\s+0x([0-9a-fA-F]{8})\b")
LABEL_DIRECTIVE = re.compile(r"^(glabel|dlabel|alabel|jlabel)\s+(\S+)")
LOCAL_LABEL = re.compile(r"^(\.L[0-9A-Za-z_]+):")
SYMBOL = re.compile(r"[A-Za-z_.$][\w.$]*")

# Some US modules share this Psy-Q routine but Splat emits it as raw words
# because the bytes after its return include SDK metadata. Match the complete
# instruction sequence before counting anything from such a block.
# fmt: off
LONGJMP_WORDS = (
    0x8C9F0000, 0x8C9C002C, 0x8C9D0004, 0x8C9E0008,  # lw ra, gp, sp, fp
    0x8C90000C, 0x8C910010, 0x8C920014, 0x8C930018,  # lw s0..s3
    0x8C94001C, 0x8C950020, 0x8C960024, 0x8C970028,  # lw s4..s7
    0x00A01021, 0x03E00008, 0x00000000,              # move v0,a1; jr ra; nop
)
# fmt: on

BRANCHES = {
    "b", "bal", "beq", "beqz", "bne", "bnez", "blez", "bgtz", "bltz", "bgez",
    "bltzal", "bgezal", "beql", "bnel", "blezl", "bgtzl", "bltzl", "bgezl",
    "bc0f", "bc0t", "bc1f", "bc1t", "bc2f", "bc2t", "j", "jal",
}  # fmt: skip


@dataclass
class Entry:
    """One word of a text segment: a decoded instruction or a raw .word."""

    address: int
    word: int
    mnemonic: str  # ".word" for raw words
    operands: str
    labeled: bool


@dataclass
class Unit:
    """A function (glabel/dlabel up to the next one) and its counted instruction addresses."""

    start: int
    end: int
    counted: list[int]
    padding: list[int]
    name: str | None = None
    entries: list[Entry] = field(default_factory=list)

    @property
    def decoded(self):
        return [*self.counted, *self.padding]


@dataclass
class Module:
    stem: str
    name: str
    units: list[Unit]
    source: str  # "asm" or "inventory"
    labels: dict[str, int] = field(default_factory=dict)

    @property
    def counted(self):
        return {a for unit in self.units for a in unit.counted}

    @property
    def decoded(self):
        return {a for unit in self.units for a in unit.decoded}

    @property
    def padding(self):
        return sum(len(unit.padding) for unit in self.units)


def unconditional(entry):
    if entry.mnemonic in ("j", "jr", "b"):
        return True
    return entry.mnemonic == "beq" and entry.operands.replace(" ", "").startswith("$zero,$zero,")


def branch_target(entry, labels):
    """Address a branch or jump names directly (label or literal), if any."""
    if entry.mnemonic not in BRANCHES or not entry.operands:
        return None
    target = entry.operands.rsplit(",", 1)[-1].strip()
    if target in labels:
        return labels[target]
    try:
        return int(target, 16)
    except ValueError:
        return None


def parse_asm(lines):
    """Split one Splat text file into raw units of entries plus its label map."""
    units, labels, pending, text = [], {}, [], True
    current = None
    for line in lines:
        stripped = line.strip()
        if stripped.startswith(".section"):
            text = stripped.startswith(".section .text")
            continue
        if not text:
            continue
        if match := LABEL_DIRECTIVE.match(stripped):
            kind, name = match.groups()
            if kind in ("glabel", "dlabel"):
                current = (name, [])
                units.append(current)
            pending.append(name)
            continue
        if match := LOCAL_LABEL.match(stripped):
            pending.append(match[1])
            continue
        match = WORD_LINE.match(line)
        if not match:
            continue
        address = int(match[1], 16)
        body = match[3].split("/*", 1)[0].strip()
        if raw := RAW_WORD.match(body):
            word, mnemonic, operands = int(raw[1], 16), ".word", ""
        elif body[0].isalpha():
            word = int.from_bytes(bytes.fromhex(match[2]), "little")
            mnemonic, _, operands = body.partition(" ")
            operands = operands.strip()
        else:
            continue
        for name in pending:
            labels[name] = address
        if current is None:
            current = (None, [])
            units.append(current)
        current[1].append(Entry(address, word, mnemonic, operands, bool(pending)))
        pending = []
    return units, labels


def counted_entries(name, entries):
    """Entries that count as original instructions, before padding is removed."""
    if name == "longjmp" and len(entries) >= len(LONGJMP_WORDS):
        head = entries[: len(LONGJMP_WORDS)]
        if all(
            e.word == w and e.address == head[0].address + 4 * i
            for i, (e, w) in enumerate(zip(head, LONGJMP_WORDS, strict=True))
        ):
            rest = [e for e in entries[len(LONGJMP_WORDS) :] if e.mnemonic != ".word"]
            return head + rest
    return [e for e in entries if e.mnemonic != ".word"]


def trailing_padding(entries, referenced):
    """Alignment nops after the delay slot of the unit's last unconditional transfer.

    They must be the final entries of the unit, contiguous, carry no label, and no
    branch, jump or symbol reference anywhere in the module may point at them.
    """
    last = None
    for index, entry in enumerate(entries):
        if entry.mnemonic != ".word" and unconditional(entry):
            last = index
    if last is None or last + 1 >= len(entries):
        return []
    tail = entries[last + 2 :]
    previous = entries[last + 1].address
    for entry in tail:
        if (
            entry.mnemonic != "nop"
            or entry.labeled
            or entry.address in referenced
            or entry.address != previous + 4
        ):
            return []
        previous = entry.address
    return tail


def referenced_addresses(units, labels, extra_text=""):
    referenced = set()
    for _, entries in units:
        for entry in entries:
            if (target := branch_target(entry, labels)) is not None:
                referenced.add(target)
            for symbol in SYMBOL.findall(entry.operands):
                if symbol in labels:
                    referenced.add(labels[symbol])
    for symbol in SYMBOL.findall(extra_text):
        if symbol in labels:
            referenced.add(labels[symbol])
    return referenced


def module_from_asm(stem, name):
    asm = SPLAT / stem / "asm"
    raw_units, labels = [], {}
    for path in sorted(asm.glob("*.s")):
        with path.open() as lines:
            file_units, file_labels = parse_asm(lines)
        raw_units += file_units
        labels.update(file_labels)
    data_text = "".join(p.read_text() for p in sorted((asm / "data").glob("*.s")))
    referenced = referenced_addresses(raw_units, labels, data_text)
    units = []
    for unit_name, entries in raw_units:
        counted = counted_entries(unit_name, entries)
        if not counted:
            continue
        padding = {e.address for e in trailing_padding(entries, referenced)}
        units.append(
            Unit(
                start=entries[0].address,
                end=entries[-1].address + 4,
                counted=sorted(e.address for e in counted if e.address not in padding),
                padding=sorted(padding),
                name=unit_name,
                entries=entries,
            )
        )
    units.sort(key=lambda u: u.start)
    return Module(stem, name, units, "asm", labels)


def runs(addresses):
    result = []
    for address in addresses:
        if result and result[-1][1] == address:
            result[-1][1] = address + 4
        else:
            result.append([address, address + 4])
    return result


def inventory_text(module):
    lines = [
        f"# Counted instruction inventory for {module.name}. Generated by",
        "# tools/decomp_state.py --write-inventory; addresses and counts only.",
        "# <start> <end-exclusive> <counted> <padding> [<run-start>-<run-end> ...]",
        "# The last <padding> words before <end> are alignment nops (decoded, not counted).",
        "# Counted addresses are the listed runs, or [start, end - 4*padding) if none.",
    ]
    for unit in module.units:
        fields = [f"{unit.start:08X}", f"{unit.end:08X}", str(len(unit.counted))]
        fields.append(str(len(unit.padding)))
        counted_runs = runs(unit.counted)
        if counted_runs != [[unit.start, unit.end - 4 * len(unit.padding)]]:
            fields += [f"{a:08X}-{b:08X}" for a, b in counted_runs]
        lines.append(" ".join(fields))
    return "\n".join(lines) + "\n"


def module_from_inventory(stem, name, path):
    units = []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        fields = line.partition("#")[0].split()
        if not fields:
            continue
        try:
            start, end = int(fields[0], 16), int(fields[1], 16)
            count, pad = int(fields[2]), int(fields[3])
            pad_start = end - 4 * pad
            padding = list(range(pad_start, end, 4))
            spans = [tuple(int(x, 16) for x in run.split("-")) for run in fields[4:]]
            counted = [a for a, b in spans or [(start, pad_start)] for a in range(a, b, 4)]
            if len(counted) != count:
                raise ValueError("counted runs disagree with the instruction count")
        except (ValueError, IndexError) as exc:
            raise ValueError(f"{path.relative_to(ROOT)}, line {number}: {exc}") from exc
        units.append(Unit(start, end, counted, padding))
    return Module(stem, name, units, "inventory")


def splat_configs():
    configs = sorted((ROOT / "config/splat").glob("*.yaml"))
    if not configs:
        raise ValueError("No executable configs found in config/splat.")
    for config in configs:
        name = re.search(r"^name:\s*(\S+)\s*$", config.read_text(), re.M).group(1)
        yield config.stem, name


def has_asm(stem):
    return any((SPLAT / stem / "asm").glob("*.s"))


def inventory(source="auto"):
    """Load every module from build/splat (preferred) or config/inventory."""
    modules = {}
    for stem, name in splat_configs():
        path = INVENTORY / f"{stem}.txt"
        if source == "asm" or (source == "auto" and has_asm(stem)):
            module = module_from_asm(stem, name)
        elif path.exists():
            module = module_from_inventory(stem, name, path)
        else:
            module = None
        if module is None or not module.units:
            raise ValueError(
                f"No instructions found for {name}. Run make extract and make split, "
                f"or restore {path.relative_to(ROOT)}."
            )
        modules[stem] = module
    return modules


def coverage(modules):
    covered = {module: set() for module in modules}
    sources = set()
    manifest = ROOT / "config/decomp_ranges.txt"
    for number, line in enumerate(manifest.read_text().splitlines(), 1):
        fields = line.partition("#")[0].split()
        if not fields:
            continue
        try:
            module, start, end, source = fields
            first, stop = int(start, 16), int(end, 16)
            if first >= stop or first % 4 or stop % 4 or module not in modules:
                raise ValueError("invalid module or instruction range")
            addresses = set(range(first, stop, 4))
            if not addresses <= modules[module].decoded:
                raise ValueError("range includes an address outside the disassembly")
            covered[module].update(addresses & modules[module].counted)
            sources.add(source)
        except ValueError as exc:
            raise ValueError(f"{manifest.name}, line {number}: {exc}") from exc
    return covered, sources


def project_files():
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
        capture_output=True,
    )
    if result.returncode:
        return "—"
    paths = {ROOT / os.fsdecode(p) for p in result.stdout.split(b"\0") if p}
    return f"{sum(p.is_file() or p.is_symlink() for p in paths):,}"


def totals(modules, covered):
    rows = []
    for stem, module in modules.items():
        size, count = len(module.counted), len(covered[stem])
        rows.append((module.name, size, count))
    total = sum(r[1] for r in rows)
    done = sum(r[2] for r in rows)
    return rows, total, done


def report(modules, covered, sources):
    rows, total, done = totals(modules, covered)
    percent = 100 * done / total
    html_rows = [
        f"<tr><th scope='row'>{escape(name)}</th><td>{size:,}</td>"
        f"<td>{count:,}</td><td>{size - count:,}</td><td>{100 * count / size:.2f}%</td></tr>"
        for name, size, count in rows
    ]
    updated = datetime.now(UTC).strftime("%Y-%m-%d %H:%M UTC")
    template = Template((ROOT / "tools/decomp_state.html.in").read_text())
    return template.substitute(
        percent=f"{percent:.2f}",
        done=done,
        total=total,
        done_label=f"{done:,}",
        total_label=f"{total:,}",
        remaining=f"{total - done:,}",
        project_files=project_files(),
        rows="\n".join(html_rows),
        modules=len(modules),
        sources=len(sources),
        padding=f"{sum(m.padding for m in modules.values()):,}",
        updated=updated,
    )


def summary(modules, covered, sources):
    rows, total, done = totals(modules, covered)
    origin = sorted({m.source for m in modules.values()})
    lines = [
        "| Module | Original instructions | Decompiled | Remaining | Complete |",
        "| --- | ---: | ---: | ---: | ---: |",
        f"| **Overall** | **{total:,}** | **{done:,}** | **{total - done:,}** "
        f"| **{100 * done / total:.2f}%** |",
    ]
    for name, size, count in rows:
        lines.append(
            f"| {name} | {size:,} | {count:,} | {size - count:,} | {100 * count / size:.2f}% |"
        )
    lines.append("")
    lines.append(
        f"{len(sources)} handwritten C files credited. Alignment padding is excluded. "
        f"Inventory source: {', '.join(origin)}."
    )
    return "\n".join(lines)


def write_atomic(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent, delete=False) as out:
        out.write(text)
    os.chmod(out.name, 0o644)
    Path(out.name).replace(path)


def require_asm():
    missing = [name for stem, name in splat_configs() if not has_asm(stem)]
    if missing:
        raise ValueError(f"build/splat is missing for {', '.join(missing)}. Run make split.")
    return inventory("asm")


def check_inventory():
    stale = []
    for stem, module in require_asm().items():
        path = INVENTORY / f"{stem}.txt"
        if not path.exists() or path.read_text() != inventory_text(module):
            stale.append(str(path.relative_to(ROOT)))
    if stale:
        print("Inventory out of date (run --write-inventory): " + ", ".join(stale))
        return 1
    print("Inventory matches build/splat")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--write-inventory", action="store_true", help="write config/inventory from build/splat"
    )
    parser.add_argument(
        "--check-inventory",
        action="store_true",
        help="exit nonzero if config/inventory disagrees with build/splat",
    )
    parser.add_argument(
        "--summary", action="store_true", help="print a Markdown progress table instead of HTML"
    )
    parser.add_argument(
        "--source",
        choices=("auto", "asm", "inventory"),
        default="auto",
        help="instruction inventory to use (default: build/splat if present)",
    )
    args = parser.parse_args(argv)

    if args.write_inventory:
        for stem, module in require_asm().items():
            write_atomic(INVENTORY / f"{stem}.txt", inventory_text(module))
        print(f"Updated {INVENTORY.relative_to(ROOT)}")
        return 0
    if args.check_inventory:
        return check_inventory()

    modules = inventory(args.source)
    covered, sources = coverage(modules)
    if args.summary:
        print(summary(modules, covered, sources))
        return 0
    write_atomic(ROOT / "state.html", report(modules, covered, sources))
    print("Updated state.html")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        sys.exit(str(exc))
