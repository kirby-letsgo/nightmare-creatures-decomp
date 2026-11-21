"""Check the first-level replay, optionally comparing a second executable on every level frame."""

import argparse
import os
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

FIXTURES = Path(__file__).resolve().parents[1] / "tests"
FIELDS = ("heading", "X", "Z", "adrenaline")


def checkpoints():
    rows = {}
    for line in (FIXTURES / "first_level.checkpoints").read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            frame, *values = map(int, line.split())
            rows[frame] = values
    return rows


def replay(binary, disc, end):
    with TemporaryDirectory(prefix="nc-first-level-") as tmp:
        work = Path(tmp)
        (work / "settings.ini").write_text("controls=tank\nadrenaline=on\nmouse_camera=off\n")
        env = {k: v for k, v in os.environ.items() if not k.startswith("NC_")}
        env.update(
            NC_HEADLESS="1",
            NC_DATA_DIR=tmp + os.sep,
            NC_INPUT=str(FIXTURES / "first_level_boot.input"),
            NC_LEVEL_INPUT=str(FIXTURES / "first_level.input"),
            SDL_VIDEO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
        )
        try:
            result = subprocess.run(
                [binary, "--disc", disc], cwd=work, env=env, capture_output=True, timeout=240
            )
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
        return rows


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
    args = parser.parse_args()
    if not args.disc.exists():
        print(f"Skipping first-level replay: missing {args.disc}")
        return 77
    disc = args.disc.resolve()
    expected = checkpoints()
    first = replay(args.binary.resolve(), disc, max(expected))
    compare(expected, first, args.binary)
    print(f"{args.binary}: {len(expected)} first-level checkpoints passed", flush=True)
    if args.compare:
        second = replay(args.compare.resolve(), disc, max(expected))
        compare(first, second, args.compare)
        print(f"Both builds agree on all {len(first)} level frames")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        sys.exit(str(error))
