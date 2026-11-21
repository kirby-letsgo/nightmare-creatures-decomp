"""Check the first-level replay, optionally comparing a second executable on every level frame."""

import argparse
import os
import subprocess
import sys
from itertools import zip_longest
from pathlib import Path
from tempfile import TemporaryDirectory

FIXTURES = Path(__file__).resolve().parents[1] / "tests"
FIELDS = ("heading", "X", "Z", "adrenaline", "health", "hits dealt", "hits taken")


def checkpoints(scenario):
    rows = {}
    for line in (FIXTURES / f"{scenario}.checkpoints").read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            frame, *values = map(int, line.split())
            rows[frame] = values
    return rows


def replay(binary, disc, end, scenario, adrenaline=True, trace_damage=False):
    with TemporaryDirectory(prefix="nc-first-level-") as tmp:
        work = Path(tmp)
        (work / "settings.ini").write_text(
            f"controls=tank\nadrenaline={'on' if adrenaline else 'off'}\nmouse_camera=off\n"
        )
        env = {k: v for k, v in os.environ.items() if not k.startswith("NC_")}
        env.update(
            NC_HEADLESS="1",
            NC_DATA_DIR=tmp + os.sep,
            SDL_VIDEO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
        )
        cmd = [binary, "--disc", disc]
        if trace_damage:
            env["NC_TRACE_DAMAGE"] = "1"
        if scenario == "first_combat":
            cmd.extend(["--play-input", FIXTURES / "first_combat.movie"])
        else:
            env.update(
                NC_INPUT=str(FIXTURES / "first_level_boot.input"),
                NC_LEVEL_INPUT=str(FIXTURES / "first_level.input"),
            )
        try:
            result = subprocess.run(cmd, cwd=work, env=env, capture_output=True, timeout=240)
        except subprocess.TimeoutExpired as exc:
            raise ValueError(
                f"{binary}: timed out waiting for the end of the level replay"
            ) from exc
        log = work / "nightmare.log"
        lines = log.read_text(errors="replace").splitlines() if log.exists() else []
        if result.returncode:
            detail = result.stderr.decode(errors="replace") or "\n".join(lines)
            raise ValueError(f"{binary} exited with {result.returncode}\n{detail[-2000:]}")
        rows = {}
        for line in lines:
            if line.startswith("replay "):
                frame, *values = map(int, line.split()[1:])
                if frame != len(rows) + 1 or len(values) != len(FIELDS):
                    raise ValueError(f"{binary}: malformed or out-of-sequence record: {line}")
                rows[frame] = values
        if len(rows) != end:
            raise ValueError(f"{binary}: reached {len(rows)} of {end} expected level frames")
        damage = [line for line in lines if line.startswith(("damage-select ", "damage-hit "))]
        if trace_damage and not (
            any(line.startswith("damage-select ") for line in damage)
            and any(line.startswith("damage-hit ") for line in damage)
        ):
            raise ValueError(f"{binary}: missing selector or hit records from combat replay")
        return rows, damage


def compare(expected, actual, binary):
    for frame, values in expected.items():
        for field, want, got in zip(FIELDS, values, actual[frame], strict=True):
            if want != got:
                raise ValueError(
                    f"{binary}: level frame {frame}, {field}: expected {want}, got {got}"
                )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--compare", type=Path, metavar="BINARY")
    parser.add_argument(
        "--disc", type=Path, default=FIXTURES.parent / "roms/Nightmare Creatures.chd"
    )
    parser.add_argument(
        "--scenario", choices=["first_level", "first_combat"], default="first_level"
    )
    parser.add_argument(
        "--no-adrenaline", action="store_true", help="exercise the disabled countdown"
    )
    parser.add_argument(
        "--trace-damage", action="store_true", help="trace selector calls and player hits"
    )
    args = parser.parse_args()
    if args.trace_damage and args.scenario != "first_combat":
        parser.error("--trace-damage requires --scenario first_combat")
    if not args.disc.exists():
        print(f"Skipping first-level replay: missing {args.disc}")
        return 77
    disc = args.disc.resolve()
    expected = checkpoints(args.scenario)
    if args.no_adrenaline:
        # Neither of these short routes starts with a depleted gauge.
        initial = expected[1][FIELDS.index("adrenaline")]
        for values in expected.values():
            values[FIELDS.index("adrenaline")] = initial
    settings = (disc, max(expected), args.scenario, not args.no_adrenaline, args.trace_damage)
    first, hits = replay(args.binary.resolve(), *settings)
    compare(expected, first, args.binary)
    if args.trace_damage:
        print("\n".join(hits))
    print(f"{args.binary}: {len(expected)} {args.scenario} checkpoints passed", flush=True)
    if args.compare:
        second, other_hits = replay(args.compare.resolve(), *settings)
        compare(first, second, args.compare)
        if args.trace_damage:
            for index, (want, got) in enumerate(zip_longest(hits, other_hits), 1):
                if want != got:
                    raise ValueError(f"Damage event {index} differs:\n{want}\n{got}")
        print(f"Both builds agree on all {len(first)} level frames")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        sys.exit(str(error))
