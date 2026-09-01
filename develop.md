# Developer notes

## Tuning cell icon size (cat / X mark)

The cat emoji and the X mark are sized relative to each cell's own
width, not the whole board or viewport, using CSS container query
units (`cqw`). This works because `.cell` has `container-type:
inline-size` — `1cqw` inside it means "1% of that cell's width",
regardless of board size (n) or screen size.

All three knobs live at the top of `web/styles.css`, in `:root`:

```css
:root {
  --cat-scale: 80;        /* cat emoji size */
  --mark-scale: 80;       /* X mark bounding box (diagonal reach) */
  --mark-thickness: 16;   /* X mark stroke thickness */
}
```

Each number is a percentage of the cell's width:

- `--cat-scale: 80` → the cat emoji's `font-size` is 80% of the cell
  width. Note the emoji glyph itself renders a bit smaller than its
  font-size (typical emoji font metrics), so the visible cat ends up
  roughly 70–75% of the cell even at `80`. Bump the number if you want
  it visually bigger.
- `--mark-scale: 80` → the X's bounding box (the square the two
  diagonal bars span) is 80% of the cell width.
- `--mark-thickness: 16` → each bar of the X is 16% of the cell width
  thick. This is independent of `--mark-scale`, so you can make the X
  bigger without making it thicker, or vice versa.

Just edit the numbers and reload — no build step. If you want the cat
and the X to visually match in size, keep `--cat-scale` and
`--mark-scale` equal (they're both `80` by default).

### Why not just bump `font-size` on `.cell`?

The X was originally the Unicode `✕` character. Its thickness is
whatever the browser's font renders it at — not something you can
tune, and it looked too thin. The X is now drawn as two CSS `div`s
(`.mark .bar`) rotated ±45°, so both its size and stroke thickness are
explicit numbers instead of "whatever the font happens to draw."

## Generating levels

See the "Generating levels" section in `README.md`.
