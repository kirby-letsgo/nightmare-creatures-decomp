#!/usr/bin/env python3
"""Extract files from the user's Nightmare Creatures CHD into build/disc/.

Reads the raw MODE2/2352 data track so XA (form 2) sectors are preserved:
  - form-1 files are written as plain 2048-byte-sector data, truncated to the ISO size
  - files containing form-2 sectors are written raw as 2336 bytes/sector (subheader + data),
    the conventional .STR/.XA layout
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KNOWN_CHD_SHA1 = "8592a07f87df446601902a137d69075dde4cf206"
RAW_SECTOR = 2352
USER_OFFSET = 24  # 12 sync + 4 header + 8 subheader (MODE2)
FORM1_SIZE = 2048
XA_RAW_SIZE = 2336  # subheader + form-2 payload
SUBMODE_FORM2 = 0x20


def chd_sha1(chd: Path) -> str:
    out = subprocess.run(
        ["chdman", "info", "-i", str(chd)], check=True, capture_output=True, text=True
    ).stdout
    for line in out.splitlines():
        if line.startswith("SHA1:"):
            return line.split()[1]
    raise RuntimeError("chdman info did not report a SHA1")


class Disc:
    def __init__(self, path: Path):
        self.f = path.open("rb")

    def raw(self, lba: int) -> bytes:
        self.f.seek(lba * RAW_SECTOR)
        return self.f.read(RAW_SECTOR)

    def user(self, lba: int) -> bytes:
        return self.raw(lba)[USER_OFFSET : USER_OFFSET + FORM1_SIZE]

    def is_form2(self, lba: int) -> bool:
        return bool(self.raw(lba)[18] & SUBMODE_FORM2)

    def read_extent(self, lba: int, size: int) -> bytes:
        count = (size + FORM1_SIZE - 1) // FORM1_SIZE
        return b"".join(self.user(lba + i) for i in range(count))[:size]


def walk(disc: Disc, lba: int, size: int, prefix: str = ""):
    """Yield (path, lba, size) for every file under the directory extent."""
    data = disc.read_extent(lba, size)
    pos = 0
    while pos < len(data):
        rec_len = data[pos]
        if rec_len == 0:  # records never cross sector boundaries; skip padding
            pos = (pos // FORM1_SIZE + 1) * FORM1_SIZE
            continue
        rec = data[pos : pos + rec_len]
        ext_lba = struct.unpack_from("<I", rec, 2)[0]
        ext_size = struct.unpack_from("<I", rec, 10)[0]
        flags = rec[25]
        name_len = rec[32]
        name = rec[33 : 33 + name_len]
        pos += rec_len
        if name in (b"\x00", b"\x01"):
            continue
        name = name.decode("ascii").split(";")[0]
        if flags & 0x02:
            yield from walk(disc, ext_lba, ext_size, f"{prefix}{name}/")
        else:
            yield f"{prefix}{name}", ext_lba, ext_size


def extract_file(disc: Disc, lba: int, size: int, dest: Path) -> str:
    count = (size + FORM1_SIZE - 1) // FORM1_SIZE
    xa = any(disc.is_form2(lba + i) for i in range(min(count, 16)))
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("wb") as out:
        if xa:
            for i in range(count):
                out.write(disc.raw(lba + i)[16 : 16 + XA_RAW_SIZE])
        else:
            remaining = size
            for i in range(count):
                chunk = disc.user(lba + i)[: min(FORM1_SIZE, remaining)]
                out.write(chunk)
                remaining -= len(chunk)
    return "xa" if xa else "data"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--chd", type=Path, default=ROOT / "roms" / "Nightmare Creatures.chd")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "disc")
    ap.add_argument("--force", action="store_true", help="ignore CHD hash mismatch")
    args = ap.parse_args()

    if shutil.which("chdman") is None:
        print("chdman not found (brew install rom-tools / apt install mame-tools)", file=sys.stderr)
        return 1
    if not args.chd.exists():
        print(f"missing {args.chd}; place your own dump in roms/", file=sys.stderr)
        return 1

    sha = chd_sha1(args.chd)
    if sha != KNOWN_CHD_SHA1 and not args.force:
        print(f"unexpected CHD SHA1 {sha} (want {KNOWN_CHD_SHA1}); use --force", file=sys.stderr)
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    bin_path = args.out.parent / "nc.bin"
    if not bin_path.exists():
        print("extracting CHD tracks...")
        subprocess.run(
            [
                "chdman",
                "extractcd",
                "-i",
                str(args.chd),
                "-o",
                str(bin_path.with_suffix(".cue")),
                "-ob",
                str(bin_path),
                "-f",
            ],
            check=True,
            capture_output=True,
        )

    disc = Disc(bin_path)
    pvd = disc.user(16)
    if pvd[1:6] != b"CD001":
        print("no ISO9660 PVD at sector 16", file=sys.stderr)
        return 1
    root_rec = pvd[156:190]
    root_lba = struct.unpack_from("<I", root_rec, 2)[0]
    root_size = struct.unpack_from("<I", root_rec, 10)[0]

    n = 0
    for path, lba, size in walk(disc, root_lba, root_size):
        extract_file(disc, lba, size, args.out / path)
        n += 1
    print(f"extracted {n} files to {args.out}")

    pins = ROOT / "config" / "disc.sha1"
    if pins.exists():
        bad = 0
        for line in pins.read_text().splitlines():
            want, name = line.split()
            got = hashlib.sha1((args.out / name).read_bytes()).hexdigest()
            if got != want:
                print(f"MISMATCH {name}: {got}", file=sys.stderr)
                bad += 1
        if bad:
            return 1
        print(f"verified {len(pins.read_text().splitlines())} executables")
    return 0


if __name__ == "__main__":
    sys.exit(main())
