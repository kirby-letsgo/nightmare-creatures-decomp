"""List functions that still need handwritten C, easiest first, as TSV on stdout.

Uses build/splat when present (callees and cross-module twins need it) and falls back to
config/inventory otherwise. Coverage comes from config/decomp_ranges.txt.
"""

import argparse
import hashlib
import re
import signal
import sys

import decomp_state as state

SYMBOL_LINE = re.compile(r"^\s*([A-Za-z_.$][\w.$]*)\s*=\s*0x([0-9A-Fa-f]+)\s*;")
COLUMNS = (
    "module",
    "address",
    "size",
    "status",
    "covered",
    "name",
    "callees",
    "callees_covered",
    "twins",
)


def library_names(stem):
    """Psy-Q and BIOS names by address; BIOS names win when both exist."""
    names = {}
    for kind in ("psyq", "bios"):
        path = state.ROOT / "config/symbols" / f"{stem}.{kind}.txt"
        if not path.exists():
            continue
        for line in path.read_text().splitlines():
            if match := SYMBOL_LINE.match(line):
                names[int(match[2], 16)] = match[1]
    return names


def masked(entry):
    """Instruction word with relocation-dependent fields cleared."""
    word = entry.word
    opcode = word >> 26
    if opcode in (2, 3):  # j, jal: 26-bit target
        return word & 0xFC000000
    if entry.mnemonic == "lui" or "%hi(" in entry.operands or "%lo(" in entry.operands:
        return word & 0xFFFF0000
    if opcode >= 4 and opcode not in (0x10, 0x11, 0x12, 0x13) and (word >> 21) & 31 == 28:
        return word & 0xFFFF0000  # I-type with a $gp base or source
    return word


def body_hash(unit):
    counted = set(unit.counted)
    words = [masked(e) for e in unit.entries if e.address in counted]
    if len(words) != len(unit.counted):
        return None
    data = b"".join(w.to_bytes(4, "little") for w in words)
    return hashlib.sha1(data).hexdigest()


def callees(unit, starts):
    found = set()
    for entry in unit.entries:
        if entry.mnemonic != "jal":
            continue
        target = (entry.address & 0xF0000000) | (entry.word & 0x03FFFFFF) << 2
        if target in starts and target != unit.start:
            found.add(target)
    return found


def status_of(unit, covered):
    done = sum(a in covered for a in unit.counted)
    if done == len(unit.counted):
        return "covered", done
    return ("partial" if done else "uncovered"), done


def build_rows(modules, covered):
    with_asm = all(m.source == "asm" for m in modules.values())
    status, hashes = {}, {}
    for stem, module in modules.items():
        for unit in module.units:
            status[stem, unit.start] = status_of(unit, covered[stem])[0]
            if with_asm and (digest := body_hash(unit)):
                hashes.setdefault(digest, []).append((stem, unit.start))

    rows = []
    for stem, module in modules.items():
        names = library_names(stem)
        starts = {u.start for u in module.units}
        for unit in module.units:
            kind, done = status_of(unit, covered[stem])
            if kind == "covered":
                continue
            row = {
                "module": stem,
                "address": f"{unit.start:08X}",
                "size": len(unit.counted),
                "status": kind,
                "covered": done,
                "name": names.get(unit.start, ""),
                "callees": "",
                "callees_covered": "",
                "twins": "",
                "_pending": 0,
                "_covered_twin": False,
            }
            if module.source == "asm":
                targets = callees(unit, starts)
                done_targets = [t for t in targets if status[stem, t] == "covered"]
                row["callees"] = len(targets)
                row["callees_covered"] = len(done_targets)
                row["_pending"] = len(targets) - len(done_targets)
            if with_asm and (digest := body_hash(unit)):
                twins = [(other, start) for other, start in hashes[digest] if other != stem]
                row["twins"] = ",".join(
                    f"{other}:{start:08X}({status[other, start]})" for other, start in twins
                )
                row["_covered_twin"] = any(status[t] == "covered" for t in twins)
            rows.append(row)
    rows.sort(key=lambda r: (r["_pending"], r["size"], r["module"], r["address"]))
    return rows, with_asm


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--module", help="only list this module (e.g. psx2_exe or PSX2.EXE)")
    parser.add_argument("--limit", type=int, help="print at most this many functions")
    parser.add_argument(
        "--twins-covered-only",
        action="store_true",
        help="only functions with a fully covered same-body twin in another module",
    )
    args = parser.parse_args(argv)

    modules = state.inventory()
    covered, _ = state.coverage(modules)
    if args.module and not any(args.module in (s, m.name) for s, m in modules.items()):
        raise ValueError(f"Unknown module {args.module}. Known: {', '.join(modules)}")
    rows, with_asm = build_rows(modules, covered)
    if not with_asm:
        print(
            "work_queue: build/splat not found; callees and twins are unavailable.",
            file=sys.stderr,
        )
        if args.twins_covered_only:
            raise ValueError("--twins-covered-only needs build/splat. Run make split.")
    if args.module:
        rows = [r for r in rows if args.module in (r["module"], modules[r["module"]].name)]
    if args.twins_covered_only:
        rows = [r for r in rows if r["_covered_twin"]]
    if args.limit is not None:
        rows = rows[: args.limit]
    print("\t".join(COLUMNS))
    for row in rows:
        print("\t".join(str(row[c]) for c in COLUMNS))
    return 0


if __name__ == "__main__":
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)  # allow piping into head
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        sys.exit(str(exc))
