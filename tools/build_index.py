"""Scan levels/<n>/level_<n>_<idx>.txt and write web/levels_index.json.

The web client only needs to know how many levels exist per board size
(files are numbered sequentially from 001).
"""

from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LEVELS_DIR = ROOT / "levels"
OUT_PATH = ROOT / "web" / "levels_index.json"

PATTERN = re.compile(r"level_(\d+)_(\d+)\.txt$")


def main():
    counts: dict[str, int] = {}
    for size_dir in sorted(LEVELS_DIR.iterdir()):
        if not size_dir.is_dir():
            continue
        indices = []
        for path in size_dir.glob("level_*.txt"):
            m = PATTERN.match(path.name)
            if m:
                indices.append(int(m.group(2)))
        if indices:
            counts[size_dir.name] = max(indices)

    OUT_PATH.write_text(json.dumps(counts, indent=2, sort_keys=True) + "\n")
    print(f"wrote {OUT_PATH}: {counts}")


if __name__ == "__main__":
    main()
