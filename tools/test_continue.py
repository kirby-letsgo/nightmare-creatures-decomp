"""Save in one process and compare the resumed session with uninterrupted gameplay."""

import os
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory


def main(binary, disc):
    if not disc.exists():
        return 77
    boot = Path(__file__).resolve().parents[1] / "tests" / "first_level_boot.input"
    with TemporaryDirectory(prefix="nc-continue-") as tmp:
        folder = Path(tmp)
        (folder / "settings.ini").write_text("controls=tank\nadrenaline=on\nmouse_camera=off\n")
        level = folder / "level.input"
        env = {k: v for k, v in os.environ.items() if not k.startswith("NC_")}
        env.update(
            NC_HEADLESS="1",
            NC_DATA_DIR=tmp + os.sep,
            NC_INPUT=str(boot),
            NC_LEVEL_INPUT=str(level),
            SDL_VIDEO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
        )

        def run(extra_args=(), **extra_env):
            result = subprocess.run(
                [binary, "--disc", disc, *extra_args],
                cwd=tmp,
                env=env | extra_env,
                capture_output=True,
                timeout=240,
            )
            if result.returncode:
                raise RuntimeError(result.stderr.decode(errors="replace")[-1500:])
            return [
                line.split()[2:]
                for line in result.stderr.splitlines()
                if line.startswith(b"replay ")
            ]

        # Walk into combat, then release the pad before saving.
        level.write_text("20 70 up\n71 80 right\n81 95 up\n96 180\n")
        reference = run()
        if len(reference) != 181:
            raise ValueError("incomplete reference replay")
        (folder / "card1.mcd").unlink()
        cut = 100
        saved = run(NC_TEST_SAVE_QUIT_AT=str(cut))
        if saved != reference[:cut] or not (folder / "states/resume.state").is_file():
            raise ValueError("Save & Quit did not capture the expected update")
        level.write_text(f"1 {len(reference) - cut}\n")
        continued = run(["--continue"])
        expected = reference[cut - 1 :]
        if len(continued) != len(expected):
            raise ValueError("Continue did not reach the last expected update")
        for frame, (want, got) in enumerate(zip(expected, continued, strict=True), cut):
            # Hit counts are diagnostic totals for the current process.
            if want[:5] != got[:5] or any(
                int(want[i]) - int(expected[0][i]) != int(got[i]) for i in (5, 6)
            ):
                raise ValueError(
                    f"Continue diverged on level update {frame}: {got}, expected {want}"
                )
        print(f"Continue matched {len(expected)} gameplay updates after a cold start.")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()))
