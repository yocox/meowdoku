#!/usr/bin/env python3
"""
Difficulty analyzer for Meowdoku levels.

Simulates human-style reasoning (easiest → hardest) and tracks how many
times each difficulty tier was needed to make progress.

Difficulty tiers
  1  Single-step deductions:
       · color / row / col with exactly one remaining cell → place cat
       · all remaining cells in a row are one color → that color confined to this row → elim from other rows
       · all remaining cells in a col are one color → same, for cols
       · all remaining cells of a color are in one row → that row dedicated to this color → elim others from row
       · all remaining cells of a color are in one col → same, for cols
  2  Intersection of exclusion sets:
       · for each possible placement of a color, compute what would be eliminated;
         intersect over all placements → those cells can always be eliminated
  3  2-group (rows/cols/colors): two rows cover exactly two colors (or vice versa)
  4  3-group, 5 4-group, 6 5-group, 7 6-group  (symmetric; stop at n//2)
  9  Backtracking required (logic tiers 1-7 are all exhausted)
"""

from __future__ import annotations
import sys
from itertools import combinations
from pathlib import Path
from typing import Optional

# ── Level parsing ─────────────────────────────────────────────────────────────

def parse_level(path: Path) -> tuple[int, list[list[int]]]:
    text = path.read_text().splitlines()
    n = int(text[0])
    regions = [[ord(ch) - ord('A') for ch in text[r + 1]] for r in range(n)]
    return n, regions

# ── Board state ───────────────────────────────────────────────────────────────

class Board:
    def __init__(self, n: int, regions: list[list[int]]):
        self.n = n
        self.regions = regions
        self.num_colors = max(regions[r][c] for r in range(n) for c in range(n)) + 1
        self.eliminated = [[False] * n for _ in range(n)]
        self.cat_at     = [[False] * n for _ in range(n)]
        self.solved_colors: set[int] = set()
        self.solved_rows:   set[int] = set()
        self.solved_cols:   set[int] = set()

    def is_solved(self) -> bool:
        return len(self.solved_colors) == self.num_colors

    def place_cat(self, r: int, c: int):
        color = self.regions[r][c]
        self.cat_at[r][c] = True
        self.solved_colors.add(color)
        self.solved_rows.add(r)
        self.solved_cols.add(c)
        n = self.n
        for c2 in range(n):
            if c2 != c: self.eliminated[r][c2] = True
        for r2 in range(n):
            if r2 != r: self.eliminated[r2][c] = True
        for dr in (-1, 0, 1):
            for dc in (-1, 0, 1):
                if dr == 0 and dc == 0: continue
                r2, c2 = r + dr, c + dc
                if 0 <= r2 < n and 0 <= c2 < n:
                    self.eliminated[r2][c2] = True
        for r2 in range(n):
            for c2 in range(n):
                if (r2, c2) != (r, c) and self.regions[r2][c2] == color:
                    self.eliminated[r2][c2] = True

    def elim(self, r: int, c: int) -> bool:
        if self.cat_at[r][c] or self.eliminated[r][c]:
            return False
        self.eliminated[r][c] = True
        return True

    def avail(self, *,
              color: Optional[int] = None,
              row:   Optional[int] = None,
              col:   Optional[int] = None) -> list[tuple[int, int]]:
        out = []
        for r in range(self.n):
            if row is not None and r != row: continue
            for c in range(self.n):
                if col   is not None and c != col:               continue
                if color is not None and self.regions[r][c] != color: continue
                if not self.eliminated[r][c] and not self.cat_at[r][c]:
                    out.append((r, c))
        return out

# ── Exclusion-set helper ──────────────────────────────────────────────────────

def excl_for(board: Board, r: int, c: int) -> frozenset[tuple[int, int]]:
    """Cells eliminated if a cat were placed at (r, c) — among currently available cells."""
    n, color = board.n, board.regions[r][c]
    s: set[tuple[int, int]] = set()
    for c2 in range(n):
        if c2 != c: s.add((r, c2))
    for r2 in range(n):
        if r2 != r: s.add((r2, c))
    for dr in (-1, 0, 1):
        for dc in (-1, 0, 1):
            if dr == 0 and dc == 0: continue
            r2, c2 = r + dr, c + dc
            if 0 <= r2 < n and 0 <= c2 < n: s.add((r2, c2))
    for r2 in range(n):
        for c2 in range(n):
            if (r2, c2) != (r, c) and board.regions[r2][c2] == color:
                s.add((r2, c2))
    return frozenset((rr, cc) for (rr, cc) in s
                     if not board.eliminated[rr][cc] and not board.cat_at[rr][cc])

# ── Analysis functions ────────────────────────────────────────────────────────

def tier1(board: Board) -> bool:
    """Each call makes exactly one atomic deduction, then returns."""
    n = board.n

    # 1a. Color with exactly one remaining cell → place cat
    for color in range(board.num_colors):
        if color in board.solved_colors: continue
        cells = board.avail(color=color)
        if len(cells) == 1:
            board.place_cat(*cells[0])
            return True

    # 1b. Row with exactly one remaining cell → place cat
    for r in range(n):
        if r in board.solved_rows: continue
        cells = board.avail(row=r)
        if len(cells) == 1:
            board.place_cat(*cells[0])
            return True

    # 1c. Col with exactly one remaining cell → place cat
    for c in range(n):
        if c in board.solved_cols: continue
        cells = board.avail(col=c)
        if len(cells) == 1:
            board.place_cat(*cells[0])
            return True

    # 1d. All remaining cells in a row are one color → color confined to this row → elim elsewhere
    for r in range(n):
        if r in board.solved_rows: continue
        cells = board.avail(row=r)
        if not cells: continue
        colors_here = {board.regions[rr][cc] for (rr, cc) in cells}
        if len(colors_here) == 1:
            col = next(iter(colors_here))
            if col in board.solved_colors: continue
            changed = False
            for r2 in range(n):
                if r2 == r: continue
                for c2 in range(n):
                    if board.regions[r2][c2] == col:
                        if board.elim(r2, c2): changed = True
            if changed: return True

    # 1e. All remaining cells in a col are one color → elim elsewhere
    for c in range(n):
        if c in board.solved_cols: continue
        cells = board.avail(col=c)
        if not cells: continue
        colors_here = {board.regions[rr][cc] for (rr, cc) in cells}
        if len(colors_here) == 1:
            col = next(iter(colors_here))
            if col in board.solved_colors: continue
            changed = False
            for r2 in range(n):
                for c2 in range(n):
                    if c2 == c: continue
                    if board.regions[r2][c2] == col:
                        if board.elim(r2, c2): changed = True
            if changed: return True

    # 1f. All remaining cells of a color are in one row → row dedicated → elim other colors
    for color in range(board.num_colors):
        if color in board.solved_colors: continue
        cells = board.avail(color=color)
        if not cells: continue
        rows_here = {r for (r, c) in cells}
        if len(rows_here) == 1:
            the_row = next(iter(rows_here))
            if the_row in board.solved_rows: continue
            changed = False
            for c2 in range(n):
                if board.regions[the_row][c2] != color:
                    if board.elim(the_row, c2): changed = True
            if changed: return True

    # 1g. All remaining cells of a color are in one col → col dedicated → elim other colors
    for color in range(board.num_colors):
        if color in board.solved_colors: continue
        cells = board.avail(color=color)
        if not cells: continue
        cols_here = {c for (r, c) in cells}
        if len(cols_here) == 1:
            the_col = next(iter(cols_here))
            if the_col in board.solved_cols: continue
            changed = False
            for r2 in range(n):
                if board.regions[r2][the_col] != color:
                    if board.elim(r2, the_col): changed = True
            if changed: return True

    return False


def tier2(board: Board) -> bool:
    """Intersection of exclusion sets — one color at a time, return on first progress."""
    for color in range(board.num_colors):
        if color in board.solved_colors: continue
        cells = board.avail(color=color)
        if len(cells) <= 1: continue
        inter: Optional[frozenset] = None
        for (r, c) in cells:
            ex = excl_for(board, r, c)
            inter = ex if inter is None else inter & ex
            if not inter: break
        if inter:
            changed = False
            for (r2, c2) in inter:
                if board.elim(r2, c2): changed = True
            if changed: return True
    return False


def tierk(board: Board, k: int) -> bool:
    """
    k-group deduction — return on first k-group found that yields new eliminations.
      · k rows  covering exactly k colors → those k colors confined → elim from other rows
      · k cols  covering exactly k colors → same
      · k colors spanning exactly k rows  → those rows dedicated → elim other colors from them
      · k colors spanning exactly k cols  → same
    """
    n = board.n
    unsolved_rows   = [r  for r  in range(n)               if r  not in board.solved_rows]
    unsolved_cols   = [c  for c  in range(n)               if c  not in board.solved_cols]
    unsolved_colors = [cl for cl in range(board.num_colors) if cl not in board.solved_colors]

    # k rows → k colors
    if len(unsolved_rows) >= k:
        for rows in combinations(unsolved_rows, k):
            cells = [cell for r in rows for cell in board.avail(row=r)]
            if not cells: continue
            colors_in = {board.regions[r][c] for (r, c) in cells}
            if len(colors_in) == k:
                changed = False
                for r2 in unsolved_rows:
                    if r2 in rows: continue
                    for cl in colors_in:
                        for c2 in range(n):
                            if board.regions[r2][c2] == cl:
                                if board.elim(r2, c2): changed = True
                if changed: return True

    # k cols → k colors
    if len(unsolved_cols) >= k:
        for cols in combinations(unsolved_cols, k):
            cells = [cell for c in cols for cell in board.avail(col=c)]
            if not cells: continue
            colors_in = {board.regions[r][c] for (r, c) in cells}
            if len(colors_in) == k:
                changed = False
                for c2 in unsolved_cols:
                    if c2 in cols: continue
                    for cl in colors_in:
                        for r2 in range(n):
                            if board.regions[r2][c2] == cl:
                                if board.elim(r2, c2): changed = True
                if changed: return True

    # k colors → k rows
    if len(unsolved_colors) >= k:
        for cgroup in combinations(unsolved_colors, k):
            cells = [cell for cl in cgroup for cell in board.avail(color=cl)]
            if not cells: continue
            rows_span = {r for (r, c) in cells}
            if len(rows_span) == k:
                changed = False
                for r2 in rows_span:
                    for c2 in range(n):
                        if board.regions[r2][c2] not in cgroup:
                            if board.elim(r2, c2): changed = True
                if changed: return True

    # k colors → k cols
    if len(unsolved_colors) >= k:
        for cgroup in combinations(unsolved_colors, k):
            cells = [cell for cl in cgroup for cell in board.avail(color=cl)]
            if not cells: continue
            cols_span = {c for (r, c) in cells}
            if len(cols_span) == k:
                changed = False
                for c2 in cols_span:
                    for r2 in range(n):
                        if board.regions[r2][c2] not in cgroup:
                            if board.elim(r2, c2): changed = True
                if changed: return True

    return False

# ── Solver / analyzer ─────────────────────────────────────────────────────────

def analyze(path: Path) -> dict:
    n, regions = parse_level(path)
    board = Board(n, regions)
    histogram: dict[int, int] = {}

    # Build tier list: (difficulty, callable)
    tiers: list[tuple[int, any]] = [(1, tier1), (2, tier2)]
    max_k = n // 2
    for k in range(2, max_k + 1):
        diff = k + 1  # k=2→D3, k=3→D4, …
        tiers.append((diff, lambda b, _k=k: tierk(b, _k)))

    for _ in range(n * n * 20):  # safety cap
        if board.is_solved():
            break
        progress = False
        for (diff, fn) in tiers:
            if fn(board):
                histogram[diff] = histogram.get(diff, 0) + 1
                progress = True
                break
        if not progress:
            histogram[9] = histogram.get(9, 0) + 1
            break

    return {
        "solved":   board.is_solved(),
        "histogram": histogram,
        "max_diff": max(histogram.keys()) if histogram else 0,
    }

# ── Report helpers ────────────────────────────────────────────────────────────

def _print_report(size_str: str, results: list):
    from collections import Counter
    all_diffs = [1, 2, 3, 4, 5, 6, 7, 9]
    header = f"{'Level':<18}" + "".join(f"  D{d}" for d in all_diffs) + "   Max  Status"
    sep    = "─" * len(header)
    bar    = "━" * len(header)
    print(f"\n{bar}")
    print(f"  Size {size_str}×{size_str}  ({len(results)} levels)")
    print(bar)
    print(header)
    print(sep)

    for r in results:
        h   = r["histogram"]
        row = f"{r['name']:<18}"
        row += "".join(f"{h.get(d, 0):>4}" for d in all_diffs)
        mx  = r["max_diff"]
        ok  = "✓" if r["solved"] else "✗"
        row += f"    D{mx}  {ok}"
        print(row)

    print(sep)
    solved   = sum(1 for r in results if r["solved"])
    bt_count = sum(1 for r in results if 9 in r["histogram"])
    print(f"Solved: {solved}/{len(results)}   Backtrack needed: {bt_count}/{len(results)}")

    dist = Counter(r["max_diff"] for r in results)
    print("Max tier : " + "  ".join(f"D{d}:{dist[d]}" for d in sorted(dist)))

    tier_totals: dict[int, int] = {}
    for r in results:
        for d, cnt in r["histogram"].items():
            tier_totals[d] = tier_totals.get(d, 0) + cnt
    print("Steps    : " + "  ".join(f"D{d}:{tier_totals[d]}" for d in sorted(tier_totals)))


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    """
    Usage: difficulty_analyzer.py [--prune] [size [start [end]]]
           difficulty_analyzer.py --check FILE

    --check FILE   single-file check: exits 0 if pure-logic solvable, 1 if D9
    --prune        after analysis, delete all D9 level files
    size           board size (e.g. 8); omit to analyze all sizes
    start          0-based start index (default 0)
    end            0-based exclusive end index (default: all)
    """
    # --check mode: used by the C++ generator to test a single file
    if len(sys.argv) >= 3 and sys.argv[1] == "--check":
        r = analyze(Path(sys.argv[2]))
        sys.exit(0 if r["solved"] and 9 not in r["histogram"] else 1)

    prune = "--prune" in sys.argv
    argv  = [a for a in sys.argv[1:] if a != "--prune"]

    root        = Path(__file__).resolve().parent.parent
    levels_root = root / "levels"

    if argv:
        size_strs = [argv[0]]
        argv = argv[1:]
    else:
        size_strs = sorted(
            d.name for d in levels_root.iterdir()
            if d.is_dir() and d.name.isdigit()
        )

    start = int(argv[0]) if len(argv) >= 1 else 0
    end   = int(argv[1]) if len(argv) >= 2 else None

    any_found = False
    for size_str in size_strs:
        level_dir = levels_root / size_str
        files = sorted(level_dir.glob(f"level_{size_str}_*.txt"))[start:end]
        if not files:
            print(f"[{size_str}] no levels found")
            continue
        any_found = True
        results = []
        for path in files:
            r = analyze(path)
            r["name"] = path.name
            r["path"] = path
            results.append(r)
        _print_report(size_str, results)

        if prune:
            stuck = [r for r in results if 9 in r["histogram"]]
            if stuck:
                print(f"  Pruning {len(stuck)} D9 level(s):")
                for r in stuck:
                    r["path"].unlink()
                    print(f"    deleted {r['name']}")

    if not any_found:
        print("No levels found.")
        sys.exit(1)


if __name__ == "__main__":
    main()
