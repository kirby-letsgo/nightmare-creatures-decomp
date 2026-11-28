"""Run every replay with shadow mode on and report how the native registry fared.

Shadow mode (src/port/shadow.c, NC_SHADOW=1) checks each call to a handwritten routine against
the original instructions on the replay's own game state. This runs the first-level, combat,
Continue and credits replays against a `make build-native` build, then prints, per module, how
many registry entries were exercised and checked, every mismatch, and the entries no replay
reached. Fails when any replay fails or any routine mismatches.
"""

import argparse
import os
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path
from tempfile import TemporaryDirectory

from gen_registry import MODULES, load

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"


def replays(build: Path, disc: Path):
    """(name, command) for each replay."""
    py = sys.executable
    nightmare = str(build / "nightmare")
    level = [py, str(TOOLS / "test_first_level.py"), nightmare, "--disc", str(disc)]
    yield "first_level", level
    yield "first_combat", [*level, "--scenario", "first_combat"]
    yield "first_level_no_adrenaline", [*level, "--no-adrenaline"]
    yield "continue_game", [py, str(TOOLS / "test_continue.py"), nightmare, str(disc)]
    credits = [
        py,
        str(TOOLS / "test_credits_roll.py"),
        str(build / "test_credits_roll_registry"),
        str(build / "test_credits_roll_original"),
        str(disc),
    ]
    for scenario in ("skip", "full", "exception_return"):
        yield f"credits_roll_{scenario}", [*credits, scenario]


def read_report(path: Path):
    """Sum the per-process rows of one replay's report: {(module, addr): [counts], why, name}."""
    rows, mismatches = {}, []
    if not path.exists():
        return rows, mismatches
    for line in path.read_text().splitlines():
        kind, _, rest = line.partition(" ")
        if kind == "mismatch":
            mismatches.append(rest)
            continue
        module, addr, *counts, why, name = rest.split(" ", 8)
        key = (module, int(addr, 16))
        row = rows.setdefault(key, {"counts": [0] * 5, "why": "-", "name": "-"})
        row["counts"] = [a + int(b) for a, b in zip(row["counts"], counts, strict=True)]
        row["why"] = why if why != "-" else row["why"]
        row["name"] = name if name != "-" else row["name"]
    return rows, mismatches


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build", type=Path, default=ROOT / "build" / "cmake-native")
    ap.add_argument("--disc", type=Path, default=ROOT / "roms" / "Nightmare Creatures.chd")
    ap.add_argument("--limit", type=int, default=50, help="calls checked per routine and process")
    ap.add_argument("--fatal", action="store_true", help="stop each replay at its first mismatch")
    ap.add_argument("--only", action="append", help="run only these replays")
    args = ap.parse_args()
    if not args.disc.exists():
        print(f"Skipping shadow replays: missing {args.disc}")
        return 77
    entries, _ = load(ROOT / "config" / "splices")
    total = defaultdict(lambda: {"counts": [0] * 5, "why": "-", "name": "-"})
    failed, all_mismatches = [], []
    with TemporaryDirectory(prefix="nc-shadow-") as tmp:
        for name, cmd in replays(args.build.resolve(), args.disc.resolve()):
            if args.only and name not in args.only:
                continue
            report = Path(tmp) / f"{name}.txt"
            env = os.environ | {
                "NC_SHADOW": "1",
                "NC_SHADOW_LIMIT": str(args.limit),
                "NC_SHADOW_FATAL": "1" if args.fatal else "0",
                "NC_SHADOW_REPORT": str(report),
            }
            start = time.monotonic()
            result = subprocess.run(cmd, env=env, capture_output=True, text=True)
            seconds = time.monotonic() - start
            rows, mismatches = read_report(report)
            checked = sum(r["counts"][1] for r in rows.values())
            routines = sum(1 for r in rows.values() if r["counts"][1])
            status = "ok" if result.returncode == 0 else f"FAILED ({result.returncode})"
            print(
                f"{name:27} {status:12} {checked:6} calls checked, "
                f"{routines:3} routines checked, {seconds:5.1f} s",
                flush=True,
            )
            if result.returncode:
                failed.append(name)
                if not mismatches:  # mismatches are listed below; anything else is shown here
                    print((result.stdout + result.stderr)[-3000:])
            for line in mismatches:
                all_mismatches.append(f"{name}: {line}")
            for key, row in rows.items():
                t = total[key]
                t["counts"] = [a + b for a, b in zip(t["counts"], row["counts"], strict=True)]
                t["why"] = row["why"] if row["why"] != "-" else t["why"]
    print()
    print(f"{'module':12} {'entries':>7} {'reached':>7} {'checked':>7} {'checks':>7}")
    never, unchecked = [], []
    for module in MODULES:
        addrs = sorted(entries[module])
        if not addrs:
            continue
        stats = [total[(module, a)]["counts"] for a in addrs]
        reached = sum(1 for s in stats if s[0])
        checked = sum(1 for s in stats if s[1])
        calls = sum(s[1] for s in stats)
        print(f"{module:12} {len(addrs):7} {reached:7} {checked:7} {calls:7}")
        for addr, s in zip(addrs, stats, strict=True):
            call = entries[module][addr]
            if not s[0]:
                never.append(f"  {module} {addr:08X} {call}")
            elif not s[1]:
                why = total[(module, addr)]["why"]
                unchecked.append(f"  {module} {addr:08X} {call}: abandoned at {why}")
    if unchecked:
        print(f"\nReached but never checkable ({len(unchecked)}; the native run needed a host")
        print("effect such as a VBlank, so those calls ran unchecked):")
        print("\n".join(unchecked))
    if never:
        print(f"\nNever exercised by any replay ({len(never)}):")
        print("\n".join(never))
    if all_mismatches:
        print(f"\nMismatches ({len(all_mismatches)}):")
        print("\n".join(all_mismatches))
    if failed:
        print(f"\nFailed replays: {', '.join(failed)}")
    return 1 if failed or all_mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
