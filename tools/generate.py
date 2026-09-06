"""Generate meowdoku levels with a guaranteed-unique solution.

Approach:
  1. Pick a random permutation for the n cats (one per row/col) such that
     no two consecutive rows place cats in adjacent columns (adjacency
     between non-consecutive rows is impossible since each row has one cat).
  2. Grow n color regions from the cat cells via randomized multi-source
     flood fill until every cell belongs to exactly one region.
  3. Verify the resulting board has exactly one solution with the solver.
     Retry region growth (and, if that keeps failing, the permutation)
     until a unique-solution board is found.

Levels are written as plain text files: first line `n`, followed by n
rows of single-letter region ids, followed by a `# solution: ...` comment
line for downstream python/c++ analysis (ignored by the web client).
"""

from __future__ import annotations

import argparse
import random
import string
from collections import deque
from pathlib import Path
from typing import List, Optional

from solver import count_solutions_int, find_alternate_solution_int

REGION_LETTERS = string.ascii_uppercase  # supports up to 26 regions (n <= 12 needed)


def random_permutation_no_adjacent(n: int, rng: random.Random) -> List[int]:
    perm = list(range(n))
    for _ in range(10000):
        rng.shuffle(perm)
        if all(abs(perm[r] - perm[r - 1]) > 1 for r in range(1, n)):
            return perm
    raise RuntimeError("failed to find a valid permutation")


def grow_regions(n: int, cat_cols: List[int], rng: random.Random) -> Optional[List[List[int]]]:
    owner: List[List[Optional[int]]] = [[None] * n for _ in range(n)]
    frontiers: List[deque] = []
    for row, col in enumerate(cat_cols):
        owner[row][col] = row
        frontiers.append(deque([(row, col)]))

    unclaimed = n * n - n
    active = list(range(n))
    while unclaimed > 0:
        rng.shuffle(active)
        progressed = False
        for idx in active:
            frontier = frontiers[idx]
            if not frontier:
                continue
            r, c = frontier[0]
            neighbors = [(r - 1, c), (r + 1, c), (r, c - 1), (r, c + 1)]
            rng.shuffle(neighbors)
            claimed = False
            for nr, nc in neighbors:
                if 0 <= nr < n and 0 <= nc < n and owner[nr][nc] is None:
                    owner[nr][nc] = idx
                    frontier.append((nr, nc))
                    unclaimed -= 1
                    claimed = True
                    progressed = True
                    break
            if not claimed:
                frontier.popleft()
        if not progressed and unclaimed > 0:
            # Every frontier ran dry before the board filled: shouldn't
            # happen on a connected grid, but bail out to trigger a retry.
            return None
    return owner


def _is_connected(cells: set, seed: tuple) -> bool:
    visited = {seed}
    dq = deque([seed])
    while dq:
        r, c = dq.popleft()
        for nr, nc in ((r - 1, c), (r + 1, c), (r, c - 1), (r, c + 1)):
            if (nr, nc) in cells and (nr, nc) not in visited:
                visited.add((nr, nc))
                dq.append((nr, nc))
    return visited == cells


def repair_uniqueness(n: int, owner: List[List[int]], cat_cols: List[int], rng: random.Random,
                       max_iters: int = 400) -> Optional[List[List[str]]]:
    """Repeatedly find an alternate solution and recolor the one cell that
    makes it possible, without ever moving a cat's own (seed) cell, so the
    true (cat_cols) solution stays valid throughout."""

    def as_letters() -> List[List[str]]:
        return [[REGION_LETTERS[owner[r][c]] for c in range(n)] for r in range(n)]

    seeds = {idx: (idx, cat_cols[idx]) for idx in range(n)}
    seed_cells = set(seeds.values())
    region_cells = {idx: set() for idx in range(n)}
    for r in range(n):
        for c in range(n):
            region_cells[owner[r][c]].add((r, c))

    for _ in range(max_iters):
        alt = find_alternate_solution_int(owner, n, cat_cols)
        if alt is None:
            return as_letters()

        diff_rows = [r for r in range(n) if alt[r] != cat_cols[r]]
        rng.shuffle(diff_rows)
        moved = False
        for row in diff_rows:
            c = alt[row]
            if (row, c) in seed_cells:
                continue
            old_region = owner[row][c]
            used_before = {owner[r2][alt[r2]] for r2 in range(row)}
            neighbor_cells = [(row - 1, c), (row + 1, c), (row, c - 1), (row, c + 1)]
            candidates = []
            for nr, nc in neighbor_cells:
                if 0 <= nr < n and 0 <= nc < n and owner[nr][nc] != old_region:
                    candidates.append(owner[nr][nc])
            if not candidates:
                continue
            preferred = [r for r in candidates if r in used_before]
            new_region = rng.choice(preferred or candidates)

            region_cells[old_region].discard((row, c))
            if not region_cells[old_region] or not _is_connected(region_cells[old_region], seeds[old_region]):
                region_cells[old_region].add((row, c))
                continue
            region_cells[new_region].add((row, c))
            owner[row][c] = new_region
            moved = True
            break
        if not moved:
            # Stuck: nudge with a random neighbor swap to escape the local optimum.
            r, c = rng.randrange(n), rng.randrange(n)
            if (r, c) in seed_cells:
                continue
            a = owner[r][c]
            neighbor_cells = [(r - 1, c), (r + 1, c), (r, c - 1), (r, c + 1)]
            rng.shuffle(neighbor_cells)
            for nr, nc in neighbor_cells:
                if not (0 <= nr < n and 0 <= nc < n):
                    continue
                b = owner[nr][nc]
                if b == a:
                    continue
                region_cells[a].discard((r, c))
                if not region_cells[a] or not _is_connected(region_cells[a], seeds[a]):
                    region_cells[a].add((r, c))
                    continue
                region_cells[b].add((r, c))
                owner[r][c] = b
                break
    return as_letters() if count_solutions_int(owner, n, limit=2) == 1 else None


def generate_level(n: int, rng: random.Random, max_perm_attempts: int = 100,
                    max_growth_attempts: int = 20) -> tuple[List[List[str]], List[int]]:
    for _ in range(max_perm_attempts):
        cat_cols = random_permutation_no_adjacent(n, rng)
        for _ in range(max_growth_attempts):
            owner = grow_regions(n, cat_cols, rng)
            if owner is None:
                continue
            if count_solutions_int(owner, n, limit=2) == 1:
                return [[REGION_LETTERS[owner[r][c]] for c in range(n)] for r in range(n)], cat_cols
            repaired = repair_uniqueness(n, owner, cat_cols, rng)
            if repaired is not None:
                return repaired, cat_cols
    raise RuntimeError(f"failed to generate a unique-solution level for n={n}")


def format_level(n: int, regions: List[List[str]], solution: List[int]) -> str:
    lines = [str(n)]
    lines.extend("".join(row) for row in regions)
    lines.append("# solution: " + " ".join(str(c) for c in solution))
    return "\n".join(lines) + "\n"


import re as _re
import sys as _sys
_sys.path.insert(0, str(Path(__file__).resolve().parent))
from difficulty_analyzer import analyze as _analyze


def _scan_existing(level_dir: Path, n: int) -> tuple[set[str], set[int]]:
    """Return (fingerprint set, set of existing indices)."""
    fingerprints: set[str] = set()
    existing: set[int] = set()
    pat = _re.compile(rf"level_{n}_(\d+)\.txt$")
    if not level_dir.exists():
        return fingerprints, existing
    for path in level_dir.glob(f"level_{n}_*.txt"):
        m = pat.match(path.name)
        if not m:
            continue
        idx = int(m.group(1))
        existing.add(idx)
        lines = path.read_text().splitlines()
        if len(lines) >= n + 1:
            fingerprints.add("".join(lines[1:n + 1]))
    return fingerprints, existing


def main():
    parser = argparse.ArgumentParser(description="Generate meowdoku levels")
    parser.add_argument("--sizes", type=int, nargs="+", default=list(range(8, 13)))
    parser.add_argument("--to", type=int, required=True,
                        help="fill gaps so that indices 1..N all exist")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "levels")
    parser.add_argument("--seed", type=int, default=None)
    args = parser.parse_args()

    rng = random.Random(args.seed)

    backtrack_dir = args.out / "backtrack"
    backtrack_dir.mkdir(parents=True, exist_ok=True)

    for n in args.sizes:
        out_dir = args.out / str(n)
        out_dir.mkdir(parents=True, exist_ok=True)

        bt_pat = _re.compile(rf"level_{n}_(\d+)\.txt$")
        bt_next = 1 + max(
            (int(m.group(1)) for p in backtrack_dir.glob(f"level_{n}_*.txt")
             if (m := bt_pat.match(p.name))),
            default=0,
        )

        fingerprints, existing = _scan_existing(out_dir, n)
        missing = sorted(i for i in range(1, args.to + 1) if i not in existing)
        if not missing:
            print(f"n={n}: all {args.to} levels already exist, nothing to do")
            continue
        print(f"n={n}: filling {len(missing)} gap(s) up to {args.to}")

        dupes = 0
        d9_regen = 0
        for idx in missing:
            while True:
                regions, solution = generate_level(n, rng)
                fp = "".join("".join(row) for row in regions)
                if fp in fingerprints:
                    dupes += 1
                    continue

                path = out_dir / f"level_{n}_{idx:08d}.txt"
                path.write_text(format_level(n, regions, solution))

                result = _analyze(path)
                if 9 in result["histogram"] or not result["solved"]:
                    # Can't be solved by pure logical deduction. Don't throw it
                    # away -- stash it in levels/backtrack for a future
                    # "backtracking required" challenge pack.
                    bt_path = backtrack_dir / f"level_{n}_{bt_next:08d}.txt"
                    bt_next += 1
                    path.replace(bt_path)
                    d9_regen += 1
                    continue

                fingerprints.add(fp)
                print(f"  wrote {path.name}")
                break

        if dupes:    print(f"  ({dupes} duplicate(s) skipped for n={n})")
        if d9_regen: print(f"  ({d9_regen} D9 level(s) moved to levels/backtrack for n={n})")


if __name__ == "__main__":
    main()
