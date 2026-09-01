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

## Tuning region colors

Region colors live in `web/game.js`, in the `REGION_COLORS` array near
the top of the file:

```js
const REGION_COLORS = [
  "#f5a9a9", "#f7c99e", "#f2e2a0", "#c5e8a8", "#a8ddd0",
  "#a8d4e8", "#adc0ea", "#c6b3e8", "#dcaee0", "#f2b8d8",
  "#dcc7a8", "#c7ccd4",
];
```

- Index 0 is region `A` in the level files, index 1 is `B`, and so on.
- The largest board is n=12, so keep at least 12 entries. If there are
  fewer than the level's region count, `game.js` wraps around with
  `REGION_COLORS[regionId % REGION_COLORS.length]` (see the
  `cellEl.style.background` line in `renderBoard`) — that means colors
  start repeating on the board, so don't let the array get shorter
  than 12 unless you're okay with that.
- Edit the hex values and reload, no build step.

## Generating levels

See the "Generating levels" section in `README.md`.
