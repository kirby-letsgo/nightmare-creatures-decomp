#!/usr/bin/env python3
"""Download Psy-Q 3.6 library signatures into build/psyq_sigs/ (not committed).

Source: https://github.com/lab313ru/psx_psyq_signatures. Nightmare Creatures was built with
SDK 3.6 (tools/match_psyq.py confirms this), so only that version is fetched.
"""

from __future__ import annotations

import json
import sys
import urllib.request

from gen_splat import ROOT

REPO = "lab313ru/psx_psyq_signatures"
REF = "main"
VERSION = "3610"
DEST = ROOT / "build" / "psyq_sigs"


def main() -> int:
    tree_url = f"https://api.github.com/repos/{REPO}/git/trees/{REF}?recursive=1"
    with urllib.request.urlopen(tree_url) as resp:
        tree = json.load(resp)["tree"]
    paths = [e["path"] for e in tree if e["type"] == "blob" and e["path"].startswith(VERSION + "/")]
    for path in paths:
        dest = DEST / path
        if dest.exists():
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        url = f"https://raw.githubusercontent.com/{REPO}/{REF}/{path}"
        with urllib.request.urlopen(url) as resp:
            dest.write_bytes(resp.read())
    print(f"{len(paths)} signature files in {DEST.relative_to(ROOT)}/{VERSION}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
