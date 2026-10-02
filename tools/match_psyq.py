#!/usr/bin/env python3
"""Identify Psy-Q SDK library code in each executable using byte signatures.

Signatures (lab313ru/psx_psyq_signatures) are fetched by `make sigs` into build/psyq_sigs/.
Each entry is one library object's .text with `??` wildcards for relocations, plus label offsets.
For every executable we pick the SDK version that covers the most bytes, then write
config/symbols/<exe>.psyq.txt with the named functions.
"""

from __future__ import annotations

import json
import re
import struct
import sys
from dataclasses import dataclass

from gen_splat import DISC, EXES, HEADER_SIZE, ROOT

SIGS = ROOT / "build" / "psyq_sigs"
MIN_SIG_BYTES = 32  # shorter signatures match by accident too easily


@dataclass
class Sig:
    lib: str
    obj: str
    pattern: re.Pattern[bytes]
    size: int
    labels: list[tuple[str, int]]


@dataclass
class Match:
    sig: Sig
    offset: int  # file offset in the executable


def load_version(version: str) -> list[Sig]:
    sigs = []
    for path in sorted((SIGS / version).glob("*.json")):
        lib = path.name.removesuffix(".json")
        for entry in json.loads(path.read_text()):
            tokens = entry.get("sig", "").split()
            if len(tokens) < MIN_SIG_BYTES:
                continue
            regex = b"".join(b"." if t == "??" else re.escape(bytes([int(t, 16)])) for t in tokens)
            labels = [
                (label["name"], label["offset"])
                for label in entry.get("labels", [])
                if not label["name"].startswith(("loc_", "text_", "data_", "bss_"))
            ]
            sigs.append(Sig(lib, entry["name"], re.compile(regex, re.DOTALL), len(tokens), labels))
    return sigs


def match(data: bytes, sigs: list[Sig]) -> list[Match]:
    """Find each signature (word-aligned) and keep non-overlapping matches, longest first."""
    found = []
    for sig in sigs:
        for m in sig.pattern.finditer(data, HEADER_SIZE):
            if m.start() % 4 == 0:
                found.append(Match(sig, m.start()))
    found.sort(key=lambda m: -m.sig.size)
    taken: list[tuple[int, int]] = []
    kept = []
    for m in found:
        lo, hi = m.offset, m.offset + m.sig.size
        if all(hi <= a or lo >= b for a, b in taken):
            taken.append((lo, hi))
            kept.append(m)
    return sorted(kept, key=lambda m: m.offset)


def main() -> int:
    versions = sorted(p.name for p in SIGS.iterdir() if p.is_dir()) if SIGS.exists() else []
    if not versions:
        print("no signatures; run `make sigs` first", file=sys.stderr)
        return 1
    sig_sets = {v: load_version(v) for v in versions}

    for name in EXES:
        data = (DISC / name).read_bytes()
        vram = struct.unpack_from("<I", data, 0x18)[0]
        results = {v: match(data, s) for v, s in sig_sets.items()}
        coverage = {v: sum(m.sig.size for m in ms) for v, ms in results.items()}
        best = max(coverage, key=lambda v: coverage[v])
        summary = ", ".join(f"{v}:{coverage[v] // 1024}K" for v in versions)

        seen: set[str] = set()
        lines = []
        for m in results[best]:
            for label, off in m.sig.labels:
                if label in seen:
                    continue
                seen.add(label)
                addr = vram + m.offset - HEADER_SIZE + off
                lines.append(f"{label} = 0x{addr:08X}; // type:func  {m.sig.lib}/{m.sig.obj}\n")
        base = name.lower().replace(".", "_")
        out = ROOT / "config" / "symbols" / f"{base}.psyq.txt"
        out.write_text(f"// Psy-Q {best} signatures\n" + "".join(lines))
        objs = len(results[best])
        print(f"{name:12} best={best} objs={objs:3} funcs={len(lines):4}  ({summary})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
