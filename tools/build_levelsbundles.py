"""Build deterministic browser bundles from the individual level files."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
LEVELS_DIR = ROOT / "levels"
OUT_DIR = LEVELS_DIR / "bundle"
SKIP_DIRS = {"backtrack", "bundle"}
SCHEMA_VERSION = 1


def pack_sort_key(pack: str) -> tuple[int, int | str]:
    return (0, int(pack)) if pack.isdigit() else (1, pack)


def normalized_level(path: Path) -> str:
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        raise ValueError(f"{path}: UTF-8 BOM is not allowed")
    text = raw.decode("utf-8")
    return text.replace("\r\n", "\n").replace("\r", "\n")


def validate_level(path: Path, text: str) -> None:
    lines = text.splitlines()
    data_lines = [line for line in lines if line.strip() and not line.startswith("#")]
    if not data_lines or not data_lines[0].isdigit():
        raise ValueError(f"{path}: missing board size")

    n = int(data_lines[0])
    rows = data_lines[1:]
    if len(rows) != n or any(len(row) != n or re.fullmatch(r"[A-Z]+", row) is None for row in rows):
        raise ValueError(f"{path}: expected {n} uppercase region rows of length {n}")

    solution_lines = [line for line in lines if line.startswith("# solution:")]
    if len(solution_lines) != 1:
        raise ValueError(f"{path}: expected exactly one solution line")
    try:
        solution = [int(value) for value in solution_lines[0].split(":", 1)[1].split()]
    except ValueError as exc:
        raise ValueError(f"{path}: invalid solution") from exc
    if len(solution) != n or any(value < 0 or value >= n for value in solution):
        raise ValueError(f"{path}: solution must contain {n} columns in range 0..{n - 1}")


def build_pack(pack_dir: Path) -> tuple[bytes, int]:
    pattern = re.compile(rf"level_{re.escape(pack_dir.name)}_(\d{{8}})\.txt$")
    indexed: list[tuple[int, Path]] = []
    for path in pack_dir.glob("level_*.txt"):
        match = pattern.fullmatch(path.name)
        if not match:
            raise ValueError(f"{path}: expected level_{pack_dir.name}_NNNNNNNN.txt")
        indexed.append((int(match.group(1)), path))
    indexed.sort()

    expected = list(range(1, len(indexed) + 1))
    actual = [index for index, _ in indexed]
    if not indexed or actual != expected:
        raise ValueError(f"{pack_dir}: level indices must be contiguous from 00000001")

    levels: list[str] = []
    for _, path in indexed:
        text = normalized_level(path)
        validate_level(path, text)
        levels.append(text)

    payload = {"schemaVersion": SCHEMA_VERSION, "pack": pack_dir.name, "levels": levels}
    data = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
    return data, len(levels)


def main() -> None:
    pack_dirs = sorted(
        (path for path in LEVELS_DIR.iterdir() if path.is_dir() and path.name not in SKIP_DIRS),
        key=lambda path: pack_sort_key(path.name),
    )
    if not pack_dirs:
        raise SystemExit("no level packs found")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    packs: dict[str, dict[str, int | str]] = {}
    total = 0
    for pack_dir in pack_dirs:
        data, count = build_pack(pack_dir)
        filename = f"{pack_dir.name}.json"
        (OUT_DIR / filename).write_bytes(data)
        packs[pack_dir.name] = {
            "count": count,
            "file": filename,
            "sha256": hashlib.sha256(data).hexdigest(),
        }
        total += count

    manifest = {"schemaVersion": SCHEMA_VERSION, "packs": packs}
    manifest_data = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    (OUT_DIR / "manifest.json").write_bytes(manifest_data)
    print(f"wrote {len(packs)} bundles with {total} levels to {OUT_DIR}")


if __name__ == "__main__":
    main()
