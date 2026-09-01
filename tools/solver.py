"""Constraint solver for meowdoku boards.

Board rules:
  - n x n grid, exactly one cat per row and per column.
  - No two cats may be adjacent (including diagonally).
  - Each color region holds exactly one cat.

Since exactly one cat is placed per row, two cats can only be adjacent
if they sit in consecutive rows, so we only need to check the previous
row's column when backtracking.
"""

from __future__ import annotations

from typing import List, Optional, Sequence


def count_solutions(regions: Sequence[Sequence[str]], n: int, limit: int = 2) -> int:
    """Count solutions up to `limit` (stops early once the cap is hit)."""
    used_cols = [False] * n
    used_regions: set[str] = set()
    count = 0

    def backtrack(row: int, prev_col: Optional[int]) -> None:
        nonlocal count
        if count >= limit:
            return
        if row == n:
            count += 1
            return
        for col in range(n):
            if used_cols[col]:
                continue
            if prev_col is not None and abs(col - prev_col) <= 1:
                continue
            region = regions[row][col]
            if region in used_regions:
                continue
            used_cols[col] = True
            used_regions.add(region)
            backtrack(row + 1, col)
            used_cols[col] = False
            used_regions.discard(region)
            if count >= limit:
                return

    backtrack(0, None)
    return count


def count_solutions_int(grid: Sequence[Sequence[int]], n: int, limit: int = 2) -> int:
    """Same as count_solutions but grid holds integer region ids (faster,
    used internally by the generator's hill-climbing repair loop)."""
    count = 0

    def backtrack(row: int, prev_col: Optional[int], used_cols: int, used_regions: int) -> None:
        nonlocal count
        if count >= limit:
            return
        if row == n:
            count += 1
            return
        for col in range(n):
            bit = 1 << col
            if used_cols & bit:
                continue
            if prev_col is not None and abs(col - prev_col) <= 1:
                continue
            region_bit = 1 << grid[row][col]
            if used_regions & region_bit:
                continue
            backtrack(row + 1, col, used_cols | bit, used_regions | region_bit)
            if count >= limit:
                return

    backtrack(0, None, 0, 0)
    return count


def find_alternate_solution_int(grid: Sequence[Sequence[int]], n: int,
                                 exclude: Sequence[int]) -> Optional[List[int]]:
    """Find a solution (as a list of columns per row) different from
    `exclude`, or None if `exclude` is the only solution. Integer-region
    variant used by the generator's targeted repair loop."""
    result: List[Optional[List[int]]] = [None]
    placement: List[int] = []

    def backtrack(row: int, prev_col: Optional[int], used_cols: int, used_regions: int) -> None:
        if result[0] is not None:
            return
        if row == n:
            if placement != list(exclude):
                result[0] = placement.copy()
            return
        for col in range(n):
            bit = 1 << col
            if used_cols & bit:
                continue
            if prev_col is not None and abs(col - prev_col) <= 1:
                continue
            region_bit = 1 << grid[row][col]
            if used_regions & region_bit:
                continue
            placement.append(col)
            backtrack(row + 1, col, used_cols | bit, used_regions | region_bit)
            placement.pop()
            if result[0] is not None:
                return

    backtrack(0, None, 0, 0)
    return result[0]


def find_solution(regions: Sequence[Sequence[str]], n: int) -> Optional[List[int]]:
    """Return one solution as a list of columns per row, or None."""
    used_cols = [False] * n
    used_regions: set[str] = set()
    placement: List[int] = []

    def backtrack(row: int, prev_col: Optional[int]) -> bool:
        if row == n:
            return True
        for col in range(n):
            if used_cols[col]:
                continue
            if prev_col is not None and abs(col - prev_col) <= 1:
                continue
            region = regions[row][col]
            if region in used_regions:
                continue
            used_cols[col] = True
            used_regions.add(region)
            placement.append(col)
            if backtrack(row + 1, col):
                return True
            placement.pop()
            used_cols[col] = False
            used_regions.discard(region)
        return False

    if backtrack(0, None):
        return placement
    return None


def load_level(path: str):
    """Parse a level text file: first line n, next n lines region rows."""
    with open(path, encoding="utf-8") as f:
        lines = [line.rstrip("\n") for line in f]
    n = int(lines[0].strip())
    regions = [list(lines[1 + i]) for i in range(n)]
    return n, regions


if __name__ == "__main__":
    import sys

    for path in sys.argv[1:]:
        n, regions = load_level(path)
        solutions = count_solutions(regions, n, limit=2)
        print(f"{path}: n={n} solutions={'>=2' if solutions >= 2 else solutions}")
