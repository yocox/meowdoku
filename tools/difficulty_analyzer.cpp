// tools/difficulty_analyzer.cpp  –  C++ port of difficulty_analyzer.py's analyze()
//
// Faithful port of the tier1/tier2/tierk deduction rules in
// difficulty_analyzer.py, minus the human-readable step descriptions (the
// generator only needs the histogram/solved verdict, not the prose). Keep
// the two files in sync if the reasoning rules change.

#include "difficulty_analyzer.h"

#include <algorithm>
#include <cstring>

namespace difficulty {

namespace {

struct Board {
    int n;
    int num_colors;
    const int (*regions)[MAXN];

    bool eliminated[MAXN][MAXN] = {};
    bool cat_at[MAXN][MAXN] = {};
    bool solved_colors[MAXN] = {};
    bool solved_rows[MAXN] = {};
    bool solved_cols[MAXN] = {};
    int solved_color_count = 0;

    Board(const int owner[][MAXN], int n_) : n(n_), regions(owner) {
        int mx = 0;
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c) mx = std::max(mx, owner[r][c]);
        num_colors = mx + 1;
    }

    bool is_solved() const { return solved_color_count == num_colors; }

    void place_cat(int r, int c) {
        int color = regions[r][c];
        cat_at[r][c] = true;
        if (!solved_colors[color]) { solved_colors[color] = true; ++solved_color_count; }
        solved_rows[r] = true;
        solved_cols[c] = true;
        for (int c2 = 0; c2 < n; ++c2) if (c2 != c) eliminated[r][c2] = true;
        for (int r2 = 0; r2 < n; ++r2) if (r2 != r) eliminated[r2][c] = true;
        for (int dr = -1; dr <= 1; ++dr)
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr == 0 && dc == 0) continue;
                int r2 = r + dr, c2 = c + dc;
                if (r2 >= 0 && r2 < n && c2 >= 0 && c2 < n) eliminated[r2][c2] = true;
            }
        for (int r2 = 0; r2 < n; ++r2)
            for (int c2 = 0; c2 < n; ++c2)
                if (!(r2 == r && c2 == c) && regions[r2][c2] == color) eliminated[r2][c2] = true;
    }

    bool elim(int r, int c) {
        if (cat_at[r][c] || eliminated[r][c]) return false;
        eliminated[r][c] = true;
        return true;
    }
};

// Filters use -1 as "no constraint". Returns the number of cells written to out.
int avail_cells(const Board &b, int color, int row, int col, int (*out)[2]) {
    int cnt = 0;
    for (int r = 0; r < b.n; ++r) {
        if (row >= 0 && r != row) continue;
        for (int c = 0; c < b.n; ++c) {
            if (col >= 0 && c != col) continue;
            if (color >= 0 && b.regions[r][c] != color) continue;
            if (!b.eliminated[r][c] && !b.cat_at[r][c]) { out[cnt][0] = r; out[cnt][1] = c; ++cnt; }
        }
    }
    return cnt;
}

// Cells that would be eliminated if a cat were placed at (r, c), restricted
// to cells still available (mirrors excl_for in difficulty_analyzer.py).
void excl_for(const Board &b, int r, int c, bool out[MAXN][MAXN]) {
    int n = b.n;
    std::memset(out, 0, sizeof(bool) * MAXN * MAXN);
    int color = b.regions[r][c];
    for (int c2 = 0; c2 < n; ++c2) if (c2 != c) out[r][c2] = true;
    for (int r2 = 0; r2 < n; ++r2) if (r2 != r) out[r2][c] = true;
    for (int dr = -1; dr <= 1; ++dr)
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            int r2 = r + dr, c2 = c + dc;
            if (r2 >= 0 && r2 < n && c2 >= 0 && c2 < n) out[r2][c2] = true;
        }
    for (int r2 = 0; r2 < n; ++r2)
        for (int c2 = 0; c2 < n; ++c2)
            if (!(r2 == r && c2 == c) && b.regions[r2][c2] == color) out[r2][c2] = true;
    for (int r2 = 0; r2 < n; ++r2)
        for (int c2 = 0; c2 < n; ++c2)
            if (out[r2][c2] && (b.eliminated[r2][c2] || b.cat_at[r2][c2])) out[r2][c2] = false;
}

bool tier1(Board &b) {
    int n = b.n;
    int cells[MAXN * MAXN][2];
    int cnt;

    // 1a. color with exactly one remaining cell -> place cat
    for (int color = 0; color < b.num_colors; ++color) {
        if (b.solved_colors[color]) continue;
        cnt = avail_cells(b, color, -1, -1, cells);
        if (cnt == 1) { b.place_cat(cells[0][0], cells[0][1]); return true; }
    }
    // 1b. row with exactly one remaining cell -> place cat
    for (int r = 0; r < n; ++r) {
        if (b.solved_rows[r]) continue;
        cnt = avail_cells(b, -1, r, -1, cells);
        if (cnt == 1) { b.place_cat(cells[0][0], cells[0][1]); return true; }
    }
    // 1c. col with exactly one remaining cell -> place cat
    for (int c = 0; c < n; ++c) {
        if (b.solved_cols[c]) continue;
        cnt = avail_cells(b, -1, -1, c, cells);
        if (cnt == 1) { b.place_cat(cells[0][0], cells[0][1]); return true; }
    }
    // 1d. row confined to one color -> eliminate that color elsewhere
    for (int r = 0; r < n; ++r) {
        if (b.solved_rows[r]) continue;
        cnt = avail_cells(b, -1, r, -1, cells);
        if (cnt == 0) continue;
        int col0 = b.regions[cells[0][0]][cells[0][1]];
        bool single = true;
        for (int i = 1; i < cnt; ++i)
            if (b.regions[cells[i][0]][cells[i][1]] != col0) { single = false; break; }
        if (!single || b.solved_colors[col0]) continue;
        bool changed = false;
        for (int r2 = 0; r2 < n; ++r2) {
            if (r2 == r) continue;
            for (int c2 = 0; c2 < n; ++c2)
                if (b.regions[r2][c2] == col0 && b.elim(r2, c2)) changed = true;
        }
        if (changed) return true;
    }
    // 1e. col confined to one color -> eliminate that color elsewhere
    for (int c = 0; c < n; ++c) {
        if (b.solved_cols[c]) continue;
        cnt = avail_cells(b, -1, -1, c, cells);
        if (cnt == 0) continue;
        int col0 = b.regions[cells[0][0]][cells[0][1]];
        bool single = true;
        for (int i = 1; i < cnt; ++i)
            if (b.regions[cells[i][0]][cells[i][1]] != col0) { single = false; break; }
        if (!single || b.solved_colors[col0]) continue;
        bool changed = false;
        for (int r2 = 0; r2 < n; ++r2)
            for (int c2 = 0; c2 < n; ++c2) {
                if (c2 == c) continue;
                if (b.regions[r2][c2] == col0 && b.elim(r2, c2)) changed = true;
            }
        if (changed) return true;
    }
    // 1f. color confined to one row -> eliminate other colors in that row
    for (int color = 0; color < b.num_colors; ++color) {
        if (b.solved_colors[color]) continue;
        cnt = avail_cells(b, color, -1, -1, cells);
        if (cnt == 0) continue;
        int row0 = cells[0][0];
        bool single = true;
        for (int i = 1; i < cnt; ++i) if (cells[i][0] != row0) { single = false; break; }
        if (!single || b.solved_rows[row0]) continue;
        bool changed = false;
        for (int c2 = 0; c2 < n; ++c2)
            if (b.regions[row0][c2] != color && b.elim(row0, c2)) changed = true;
        if (changed) return true;
    }
    // 1g. color confined to one col -> eliminate other colors in that col
    for (int color = 0; color < b.num_colors; ++color) {
        if (b.solved_colors[color]) continue;
        cnt = avail_cells(b, color, -1, -1, cells);
        if (cnt == 0) continue;
        int col0 = cells[0][1];
        bool single = true;
        for (int i = 1; i < cnt; ++i) if (cells[i][1] != col0) { single = false; break; }
        if (!single || b.solved_cols[col0]) continue;
        bool changed = false;
        for (int r2 = 0; r2 < n; ++r2)
            if (b.regions[r2][col0] != color && b.elim(r2, col0)) changed = true;
        if (changed) return true;
    }
    return false;
}

bool tier2(Board &b) {
    int n = b.n;
    int cells[MAXN * MAXN][2];
    for (int color = 0; color < b.num_colors; ++color) {
        if (b.solved_colors[color]) continue;
        int cnt = avail_cells(b, color, -1, -1, cells);
        if (cnt <= 1) continue;

        bool inter[MAXN][MAXN];
        excl_for(b, cells[0][0], cells[0][1], inter);
        bool empty = true;
        for (int r = 0; r < n && empty; ++r)
            for (int c = 0; c < n; ++c) if (inter[r][c]) { empty = false; break; }

        for (int i = 1; i < cnt && !empty; ++i) {
            bool ex[MAXN][MAXN];
            excl_for(b, cells[i][0], cells[i][1], ex);
            empty = true;
            for (int r = 0; r < n; ++r)
                for (int c = 0; c < n; ++c) {
                    inter[r][c] = inter[r][c] && ex[r][c];
                    if (inter[r][c]) empty = false;
                }
        }
        if (empty) continue;

        bool changed = false;
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c)
                if (inter[r][c] && b.elim(r, c)) changed = true;
        if (changed) return true;
    }
    return false;
}

// Enumerate k-combinations of indices [0, total) as index arrays; fn
// returns true to stop early (combination found), false to keep searching.
template <typename Fn>
bool for_each_combination(int total, int k, Fn &&fn) {
    if (k > total || k <= 0) return false;
    int idx[MAXN];
    for (int i = 0; i < k; ++i) idx[i] = i;
    while (true) {
        if (fn(idx)) return true;
        int i = k - 1;
        while (i >= 0 && idx[i] == total - k + i) --i;
        if (i < 0) return false;
        ++idx[i];
        for (int j = i + 1; j < k; ++j) idx[j] = idx[j - 1] + 1;
    }
}

bool tierk(Board &b, int k) {
    int n = b.n;
    int unsolved_rows[MAXN], nr = 0;
    for (int r = 0; r < n; ++r) if (!b.solved_rows[r]) unsolved_rows[nr++] = r;
    int unsolved_cols[MAXN], nc = 0;
    for (int c = 0; c < n; ++c) if (!b.solved_cols[c]) unsolved_cols[nc++] = c;
    int unsolved_colors[MAXN], ncol = 0;
    for (int cl = 0; cl < b.num_colors; ++cl) if (!b.solved_colors[cl]) unsolved_colors[ncol++] = cl;

    int cells[MAXN * MAXN][2];

    // k rows -> k colors
    if (nr >= k) {
        bool found = for_each_combination(nr, k, [&](const int *idx) {
            bool in_group[MAXN] = {};
            int cellcnt = 0;
            for (int i = 0; i < k; ++i) {
                int r = unsolved_rows[idx[i]];
                in_group[r] = true;
                cellcnt += avail_cells(b, -1, r, -1, cells + cellcnt);
            }
            if (cellcnt == 0) return false;
            bool color_seen[MAXN] = {};
            int colors_count = 0;
            for (int i = 0; i < cellcnt; ++i) {
                int cl = b.regions[cells[i][0]][cells[i][1]];
                if (!color_seen[cl]) { color_seen[cl] = true; ++colors_count; }
            }
            if (colors_count != k) return false;
            bool changed = false;
            for (int ii = 0; ii < nr; ++ii) {
                int r2 = unsolved_rows[ii];
                if (in_group[r2]) continue;
                for (int c2 = 0; c2 < n; ++c2) {
                    int cl = b.regions[r2][c2];
                    if (color_seen[cl] && b.elim(r2, c2)) changed = true;
                }
            }
            return changed;
        });
        if (found) return true;
    }

    // k cols -> k colors
    if (nc >= k) {
        bool found = for_each_combination(nc, k, [&](const int *idx) {
            bool in_group[MAXN] = {};
            int cellcnt = 0;
            for (int i = 0; i < k; ++i) {
                int c = unsolved_cols[idx[i]];
                in_group[c] = true;
                cellcnt += avail_cells(b, -1, -1, c, cells + cellcnt);
            }
            if (cellcnt == 0) return false;
            bool color_seen[MAXN] = {};
            int colors_count = 0;
            for (int i = 0; i < cellcnt; ++i) {
                int cl = b.regions[cells[i][0]][cells[i][1]];
                if (!color_seen[cl]) { color_seen[cl] = true; ++colors_count; }
            }
            if (colors_count != k) return false;
            bool changed = false;
            for (int ii = 0; ii < nc; ++ii) {
                int c2 = unsolved_cols[ii];
                if (in_group[c2]) continue;
                for (int r2 = 0; r2 < n; ++r2) {
                    int cl = b.regions[r2][c2];
                    if (color_seen[cl] && b.elim(r2, c2)) changed = true;
                }
            }
            return changed;
        });
        if (found) return true;
    }

    // k colors -> k rows
    if (ncol >= k) {
        bool found = for_each_combination(ncol, k, [&](const int *idx) {
            bool in_group[MAXN] = {};
            int cellcnt = 0;
            for (int i = 0; i < k; ++i) {
                int cl = unsolved_colors[idx[i]];
                in_group[cl] = true;
                cellcnt += avail_cells(b, cl, -1, -1, cells + cellcnt);
            }
            if (cellcnt == 0) return false;
            bool row_seen[MAXN] = {};
            int rows_count = 0;
            for (int i = 0; i < cellcnt; ++i) {
                int r = cells[i][0];
                if (!row_seen[r]) { row_seen[r] = true; ++rows_count; }
            }
            if (rows_count != k) return false;
            bool changed = false;
            for (int r2 = 0; r2 < n; ++r2) {
                if (!row_seen[r2]) continue;
                for (int c2 = 0; c2 < n; ++c2)
                    if (!in_group[b.regions[r2][c2]] && b.elim(r2, c2)) changed = true;
            }
            return changed;
        });
        if (found) return true;
    }

    // k colors -> k cols
    if (ncol >= k) {
        bool found = for_each_combination(ncol, k, [&](const int *idx) {
            bool in_group[MAXN] = {};
            int cellcnt = 0;
            for (int i = 0; i < k; ++i) {
                int cl = unsolved_colors[idx[i]];
                in_group[cl] = true;
                cellcnt += avail_cells(b, cl, -1, -1, cells + cellcnt);
            }
            if (cellcnt == 0) return false;
            bool col_seen[MAXN] = {};
            int cols_count = 0;
            for (int i = 0; i < cellcnt; ++i) {
                int c = cells[i][1];
                if (!col_seen[c]) { col_seen[c] = true; ++cols_count; }
            }
            if (cols_count != k) return false;
            bool changed = false;
            for (int c2 = 0; c2 < n; ++c2) {
                if (!col_seen[c2]) continue;
                for (int r2 = 0; r2 < n; ++r2)
                    if (!in_group[b.regions[r2][c2]] && b.elim(r2, c2)) changed = true;
            }
            return changed;
        });
        if (found) return true;
    }

    return false;
}

}  // namespace

Result analyze(const int owner[][MAXN], int n) {
    Board b(owner, n);
    Result res;
    int max_k = n / 2;
    int cap = n * n * 20;

    for (int step = 0; step < cap; ++step) {
        if (b.is_solved()) break;
        bool progress = false;
        if (tier1(b)) {
            ++res.histogram[1];
            progress = true;
        } else if (tier2(b)) {
            ++res.histogram[2];
            progress = true;
        } else {
            for (int k = 2; k <= max_k; ++k) {
                if (tierk(b, k)) { ++res.histogram[k + 1]; progress = true; break; }
            }
        }
        if (!progress) { ++res.histogram[9]; break; }
    }

    res.solved = b.is_solved();
    for (int d = 9; d >= 1; --d)
        if (res.histogram[d] > 0) { res.max_diff = d; break; }
    return res;
}

}  // namespace difficulty
