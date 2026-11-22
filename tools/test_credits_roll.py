"""Compare the recompiled credits roll to the US executable from entry through return."""

import filecmp
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory


def main():
    native, original, disc, scenario = sys.argv[1:]
    if not Path(disc).is_file():
        return 77
    with TemporaryDirectory() as tmp:
        traces = [Path(tmp) / name for name in ("native", "original")]
        for binary, trace in zip((native, original), traces, strict=True):
            subprocess.run([binary, disc, scenario, trace], check=True, timeout=180)
        if not filecmp.cmp(*traces, shallow=False):
            print(f"Credits traces differ ({scenario})", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
