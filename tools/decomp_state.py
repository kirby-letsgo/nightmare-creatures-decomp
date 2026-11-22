"""Build state.html from the local US disassembly and verified handwritten ranges."""

import os
import re
import subprocess
import sys
from datetime import UTC, datetime
from html import escape
from pathlib import Path
from string import Template
from tempfile import NamedTemporaryFile

ROOT = Path(__file__).resolve().parents[1]
INSTRUCTION = re.compile(r"^\s*/\*\s+[0-9A-F]+\s+([0-9A-F]{8})\s+[0-9A-F]{8}\s*\*/\s+[a-zA-Z]")


def inventory():
    modules = {}
    for config in sorted((ROOT / "config/splat").glob("*.yaml")):
        name = re.search(r"^name:\s*(\S+)\s*$", config.read_text(), re.M).group(1)
        addresses = set()
        for path in (ROOT / "build/splat" / config.stem / "asm").glob("*.s"):
            with path.open() as lines:
                for line in lines:
                    if match := INSTRUCTION.match(line):
                        addresses.add(int(match[1], 16))
        if not addresses:
            raise ValueError(f"No instructions found for {name}. Run make extract and make split.")
        modules[config.stem] = (name, addresses)
    if not modules:
        raise ValueError("No executable configs found in config/splat.")
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
            if not addresses <= modules[module][1]:
                raise ValueError("range includes an address outside the disassembly")
            covered[module].update(addresses)
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


def report(modules, covered, sources):
    total = sum(len(addresses) for _, addresses in modules.values())
    done = sum(map(len, covered.values()))
    percent = 100 * done / total
    rows = []
    for module, (name, addresses) in modules.items():
        count, size = len(covered[module]), len(addresses)
        rows.append(
            f"<tr><th scope='row'>{escape(name)}</th><td>{size:,}</td>"
            f"<td>{count:,}</td><td>{size - count:,}</td><td>{100 * count / size:.2f}%</td></tr>"
        )
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
        rows="\n".join(rows),
        modules=len(modules),
        sources=len(sources),
        updated=updated,
    )


def main():
    modules = inventory()
    covered, sources = coverage(modules)
    page = report(modules, covered, sources)
    with NamedTemporaryFile(mode="w", encoding="utf-8", dir=ROOT, delete=False) as out:
        out.write(page)
    Path(out.name).replace(ROOT / "state.html")
    print("Updated state.html")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        sys.exit(str(exc))
