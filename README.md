# Meowdoku 🐱

A "Queens"-style puzzle: on an n x n board, place one cat per row and per
column, no two cats touching (including diagonally), and exactly one cat
per color region.

## Layout

- `web/` — static HTML/CSS/JS. Just serve the folder, no build step.
- `levels/<n>/level_<n>_<idx>.txt` — level data, one text file per level.
- `levels/bundle/` — generated browser bundles and their content-hash manifest.
- `tools/` — Python level generator + solver, used offline (not shipped
  to the browser).

## Play locally

```sh
cd web
python3 -m http.server 8000
# open http://localhost:8000
```

## Level file format

```
8
AABBBCCD
AABBCCDD
AEBBCCDD
AEEBFCDD
AEEFFCGD
AEFFFGGD
HEFGGGGD
HHHHHGGD
# solution: 3 1 6 4 0 7 5 2
```

- Line 1: board size `n`.
- Next `n` lines: one letter per cell = color region id.
- Trailing `# solution: ...` comment: the intended solution's column for
  each row, 0-indexed. Ignored by the web client, useful for `tools/`
  analysis scripts.

## Generating levels

```sh
cd tools
python3 generate.py --sizes 8 9 10 11 12 --count 5 --seed 42
python3 build_levelsbundles.py   # refresh levels/bundle/*.json
```

`generate.py`:
1. Picks a random cat placement (one per row/col, no two in adjacent
   columns on consecutive rows — the only way two cats could touch).
2. Grows one color region per cat via randomized flood fill.
3. Verifies the board has a unique solution with `solver.py`; if not, it
   locally re-recolors boundary cells to kill alternate solutions (a
   targeted repair, not a random restart) without ever disturbing the
   cats' own cells, then re-checks. Falls back to a fresh permutation if
   repair can't converge.

Notes:
- `--count N` numbers files `001..00N` per size and **overwrites**
  existing files with those indices — it doesn't append. To add more
  levels without touching existing ones, generate into a scratch dir and
  copy over with indices past what's already there, or just bump
  `--count` and accept that low-numbered levels get regenerated (they're
  random anyway, so replacing them is harmless).
- Different `--seed` values (or omitting `--seed`) give different level
  sets. Same seed + same code = fully reproducible output.
- Runtime grows with `n`: n=8/9 take well under a second per level;
  n=11/12 can take several seconds to tens of seconds each, since a
  bigger board needs more targeted-repair iterations to reach a unique
  solution. Generating all of 8–12 at `--count 5` takes a few minutes
  total — that's expected, not a hang.
- Always re-run `build_levelsbundles.py` after generating, or the web client
  won't see the new levels. Its output is deterministic and is committed with
  the source levels.

`solver.py` also works as a standalone checker:

```sh
python3 solver.py ../levels/8/level_8_001.txt
```

## Printing levels

Export boards to a print-ready A4 PDF — colour blocks only, white
background, nothing else on the page:

```sh
# pack 6, levels 1-10 and 51-60, four boards per page -> meowdoku_6.pdf
python3 tools/gen-pdf.py --set 6 --level 1..10,51..60 --per-page 4
```

- `--set` — a pack: a board size (`6`..`12`) or `hard` / `bad`.
- `--level` — `1..10,51..60`, `5-9`, a single `7`, or `all`.
- `--per-page` — 1, 2, 4 (2x2) or 6 (3x2). Default 4.
- `--label` — print each board's level number in its bottom-left corner.
- `--out` — output path. Defaults to `meowdoku_<set>.pdf`.

Needs no third-party packages; the PDF is written directly.
