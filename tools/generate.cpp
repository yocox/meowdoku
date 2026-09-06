// tools/generate.cpp  –  Meowdoku level generator (C++ rewrite of generate.py + solver.py)
// Compile: clang++ -O3 -std=c++17 tools/generate.cpp tools/difficulty_analyzer.cpp -o tools/generate
// Usage:   generate --to N [--sizes N...] [--out DIR] [--seed S] [--legacy]

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "difficulty_analyzer.h"

namespace fs = std::filesystem;

static constexpr int MAXN = 12;
static const int DR[4] = {-1, 1,  0, 0};
static const int DC[4] = { 0, 0, -1, 1};
static const char LETTERS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

// ── Solver ───────────────────────────────────────────────────────────────────
//
// owner[r][c] = integer region id (0..n-1)
// Bitmasks for used columns and used regions fit in uint32_t (n <= 12).
// pc = previous column (-1 means "no previous row yet").

static int count_solutions(const int g[][MAXN], int n, int limit) {
    struct S {
        const int (*g)[MAXN];
        int n, limit, cnt;
        void bt(int row, int pc, uint32_t uc, uint32_t ur) {
            if (cnt >= limit) return;
            if (row == n) { ++cnt; return; }
            for (int c = 0; c < n; ++c) {
                if (uc >> c & 1) continue;
                if (pc >= 0 && c >= pc-1 && c <= pc+1) continue;
                uint32_t rb = 1u << g[row][c];
                if (ur & rb) continue;
                bt(row+1, c, uc|(1u<<c), ur|rb);
                if (cnt >= limit) return;
            }
        }
    } s{g, n, limit, 0};
    s.bt(0, -1, 0, 0);
    return s.cnt;
}

// Randomised search for a placement ≠ excl that is legal under the (possibly
// partial) colouring g. A cell with g[r][c] < 0 is uncoloured and imposes no
// region constraint.
//
// The column order is reshuffled at every node. That matters: with a fixed
// 0..n-1 order the search always returns the lexicographically smallest
// alternate, so callers that repeatedly ask for "an alternate" keep getting
// neighbours of the same one and only ever probe one corner of the solution
// space.
//
//   budget < 0  → exhaustive; returning false *proves* no alternate exists.
//   budget ≥ 0  → a bounded random dive; false may just mean "gave up".
struct AltSearch {
    const int (*g)[MAXN];
    int n;
    const int *excl;
    std::mt19937 *rng;
    long   budget;
    int  (*sink)[MAXN];  // where to store hits; null keeps just the last one in pl
    int    limit;        // stop once this many have been found
    int    found;
    int    pl[MAXN];

    // Returns true when the search should stop: limit reached, or out of budget.
    bool bt(int row, int pc, uint32_t uc, uint32_t ur) {
        if (budget >= 0 && --budget < 0) return true;  // dive gave up; keep what we have
        if (row == n) {
            for (int r = 0; r < n; ++r) {
                if (pl[r] == excl[r]) continue;
                if (sink) std::copy(pl, pl+n, sink[found]);
                return ++found >= limit;
            }
            return false;  // this is the intended solution, keep searching
        }
        int order[MAXN], m = 0;
        for (int c = 0; c < n; ++c) {
            if (uc >> c & 1) continue;
            if (pc >= 0 && c >= pc-1 && c <= pc+1) continue;
            int reg = g[row][c];
            if (reg >= 0 && (ur >> reg & 1)) continue;
            order[m++] = c;
        }
        std::shuffle(order, order+m, *rng);
        for (int i = 0; i < m; ++i) {
            int c = order[i], reg = g[row][c];
            pl[row] = c;
            if (bt(row+1, c, uc|(1u<<c), reg >= 0 ? ur|(1u<<reg) : ur)) return true;
        }
        return false;
    }
};

// Exhaustive: false means the board's solution is unique.
static bool find_alternate(const int g[][MAXN], int n, const int *excl, int *out,
                           std::mt19937 &rng) {
    AltSearch s{g, n, excl, &rng, -1, nullptr, 1, 0, {}};
    s.bt(0, -1, 0, 0);
    if (!s.found) return false;
    std::copy(s.pl, s.pl+n, out);
    return true;
}

// ── Generator ────────────────────────────────────────────────────────────────

static bool random_perm_no_adj(int perm[], int n, std::mt19937 &rng) {
    std::iota(perm, perm+n, 0);
    for (int i = 0; i < 10000; ++i) {
        std::shuffle(perm, perm+n, rng);
        bool ok = true;
        for (int r = 1; r < n && ok; ++r)
            if (perm[r] >= perm[r-1]-1 && perm[r] <= perm[r-1]+1) ok = false;
        if (ok) return true;
    }
    return false;
}

// Randomised multi-source flood fill (mirrors Python's grow_regions).
static bool grow_regions(int owner[][MAXN], int n, const int *cat_cols, std::mt19937 &rng) {
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c)
            owner[r][c] = -1;

    std::deque<std::pair<int,int>> frontier[MAXN];
    for (int i = 0; i < n; ++i) {
        owner[i][cat_cols[i]] = i;
        frontier[i].push_back({i, cat_cols[i]});
    }

    int unclaimed = n*n - n;
    std::vector<int> active(n);
    std::iota(active.begin(), active.end(), 0);
    int nbr[4] = {0,1,2,3};

    while (unclaimed > 0) {
        std::shuffle(active.begin(), active.end(), rng);
        bool progressed = false;
        for (int idx : active) {
            auto &q = frontier[idx];
            if (q.empty()) continue;
            auto [r, c] = q.front();
            std::shuffle(nbr, nbr+4, rng);
            bool claimed = false;
            for (int d : nbr) {
                int nr = r+DR[d], nc = c+DC[d];
                if (nr<0||nr>=n||nc<0||nc>=n) continue;
                if (owner[nr][nc] != -1) continue;
                owner[nr][nc] = idx;
                q.push_back({nr, nc});
                --unclaimed;
                claimed = progressed = true;
                break;
            }
            if (!claimed) q.pop_front();
        }
        if (!progressed && unclaimed > 0) return false;
    }
    return true;
}

// Per-region, per-row column bitmasks (mask[region][row] = col bits).
// n <= 12 → uint16_t is sufficient (16 bits).
using RegionMask = uint16_t[MAXN][MAXN];

static bool is_connected(const uint16_t mask[], int n, int sr, int sc) {
    if (!(mask[sr] >> sc & 1)) return false;
    uint16_t vis[MAXN] = {};
    std::deque<std::pair<int,int>> q;
    q.push_back({sr, sc});
    vis[sr] |= 1u << sc;
    int total = 0, found = 0;
    for (int r = 0; r < n; ++r) total += __builtin_popcount(mask[r]);
    while (!q.empty()) {
        auto [r, c] = q.front(); q.pop_front();
        ++found;
        for (int d = 0; d < 4; ++d) {
            int nr = r+DR[d], nc = c+DC[d];
            if (nr<0||nr>=n||nc<0||nc>=n) continue;
            if (!(mask[nr]>>nc&1)) continue;
            if (vis[nr]>>nc&1) continue;
            vis[nr] |= 1u<<nc;
            q.push_back({nr, nc});
        }
    }
    return found == total;
}

// Iterative repair: find an alternate solution → recolour a cell that enables it.
// Returns true if the board reaches unique-solution status within max_iters.
static bool repair_uniqueness(int owner[][MAXN], int n, const int *cat_cols,
                               std::mt19937 &rng, int max_iters = 400) {
    RegionMask mask = {};
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c)
            mask[owner[r][c]][r] |= 1u << c;

    bool seed_cell[MAXN][MAXN] = {};
    for (int i = 0; i < n; ++i) seed_cell[i][cat_cols[i]] = true;

    int alt[MAXN];
    int nbr_ord[4] = {0,1,2,3};

    for (int iter = 0; iter < max_iters; ++iter) {
        if (!find_alternate(owner, n, cat_cols, alt, rng)) return true;  // unique!

        // Rows where alt differs from the target solution
        int diff[MAXN], ndiff = 0;
        for (int r = 0; r < n; ++r)
            if (alt[r] != cat_cols[r]) diff[ndiff++] = r;
        std::shuffle(diff, diff+ndiff, rng);

        bool moved = false;
        for (int di = 0; di < ndiff && !moved; ++di) {
            int row = diff[di], c = alt[row];
            if (seed_cell[row][c]) continue;
            int old_reg = owner[row][c];

            // Regions already used by the alt path above this row
            uint32_t used_before = 0;
            for (int r2 = 0; r2 < row; ++r2)
                used_before |= 1u << owner[r2][alt[r2]];

            // Collect neighbour regions different from old_reg
            int cands[4], nc_cnt = 0;
            for (int d = 0; d < 4; ++d) {
                int nr = row+DR[d], nc = c+DC[d];
                if (nr<0||nr>=n||nc<0||nc>=n) continue;
                if (owner[nr][nc] != old_reg) cands[nc_cnt++] = owner[nr][nc];
            }
            if (nc_cnt == 0) continue;

            // Prefer regions the alt path already used (makes alt solution illegal)
            int pref[4], np = 0;
            for (int i = 0; i < nc_cnt; ++i)
                if (used_before >> cands[i] & 1) pref[np++] = cands[i];
            const int *pool = np ? pref : cands;
            int pool_sz    = np ? np   : nc_cnt;
            int new_reg = pool[std::uniform_int_distribution<int>(0, pool_sz-1)(rng)];

            // Verify old_reg stays connected after losing this cell
            mask[old_reg][row] &= ~(1u<<c);
            if (!is_connected(mask[old_reg], n, old_reg, cat_cols[old_reg])) {
                mask[old_reg][row] |= 1u<<c;
                continue;
            }
            mask[new_reg][row] |= 1u<<c;
            owner[row][c] = new_reg;
            moved = true;
        }

        if (!moved) {
            // Nudge: random neighbour swap to escape local optimum
            int r = std::uniform_int_distribution<int>(0, n-1)(rng);
            int c = std::uniform_int_distribution<int>(0, n-1)(rng);
            if (seed_cell[r][c]) continue;
            int a = owner[r][c];
            std::shuffle(nbr_ord, nbr_ord+4, rng);
            for (int d : nbr_ord) {
                int nr = r+DR[d], nc = c+DC[d];
                if (nr<0||nr>=n||nc<0||nc>=n) continue;
                int b = owner[nr][nc];
                if (b == a) continue;
                mask[a][r] &= ~(1u<<c);
                if (!is_connected(mask[a], n, a, cat_cols[a])) {
                    mask[a][r] |= 1u<<c;
                    continue;
                }
                mask[b][r] |= 1u<<c;
                owner[r][c] = b;
                break;
            }
        }
    }
    return count_solutions(owner, n, 2) == 1;
}

// ── Witness-pool guided incremental colouring ────────────────────────────────
//
// Instead of "flood fill the whole board, then repair it", grow the colouring
// one cell at a time and let the surviving alternate solutions pick the next
// move.
//
// This rests on one property of *partial* colourings. Call a placement p legal
// for a partial colouring when the cells of p that are already coloured have
// pairwise distinct regions. If c2 extends c1 (only adds colour, never
// recolours) then every p legal for c2 is also legal for c1 — p's c1-coloured
// cells are a subset of its c2-coloured ones, with the same colours. So the
// legal set only ever shrinks as we colour, and the intended solution stays in
// it forever (each cat cell is its own region's seed and never changes). Hence:
//
//     once the partial board has exactly one legal placement, *every*
//     completion of it is a unique-solution level.
//
// That is what makes this cheap. Uniqueness is normally reached well before the
// board is full; from that point the remaining cells can be coloured freely,
// and there is never anything to undo. Compare the repair loop above, where
// recolouring is "region A loses a cell, region B gains one" — B growing can
// create new solutions, so progress is not monotone and ~85% of attempts at
// n=12 simply fail.
//
// We can't track the legal set exactly (it starts in the tens of millions), so
// we keep a random sample of it — the witness pool — colour the (cell, region)
// pair that kills the most witnesses, and refill the pool when it runs dry. A
// refill that comes back empty *and* survives an exhaustive search is the
// uniqueness proof.

struct IcgcParams {
    int    pool_target = 512;   // witnesses collected per refill
    long   dive_budget = 4000;  // search nodes per randomised dive
    int    dive_yield  = 8;     // witnesses harvested per dive
    int    dive_giveup = 24;    // consecutive dives finding nothing new
    int    size_cap    = 0;     // hard region size cap; 0 → 2n (relaxed if stuck)
    double size_w      = 0.15;  // soft size balancing, in units of "witnesses killed"
    double jitter      = 0.25;  // tie-breaking noise, ditto
};

struct Witness {
    int      cols[MAXN];
    uint32_t used;   // regions already used by this placement's coloured cells
    bool     alive;
};

// Grow a colouring from the cat seeds until the board is full. Returns true if
// the result has a unique solution.
static bool icgc_colour(int owner[][MAXN], int n, const int *cat_cols, std::mt19937 &rng,
                        const IcgcParams &P = IcgcParams{}) {
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c)
            owner[r][c] = -1;

    int size[MAXN] = {};
    for (int i = 0; i < n; ++i) { owner[i][cat_cols[i]] = i; size[i] = 1; }
    int unassigned = n*n - n;

    std::vector<Witness> pool;
    std::vector<int>     through[MAXN][MAXN];  // cell → witnesses passing over it
    std::set<uint64_t>   seen;
    int  alive = 0, refill_at = 1;
    bool proven_unique = false;
    bool pool_complete = false;  // pool holds *all* survivors, not a sample

    auto add_witness = [&](const int *cols) {
        Witness w;
        std::copy(cols, cols+n, w.cols);
        w.used = 0;
        w.alive = true;
        for (int r = 0; r < n; ++r) {
            int reg = owner[r][cols[r]];
            if (reg >= 0) w.used |= 1u << reg;
        }
        int wi = (int)pool.size();
        pool.push_back(w);
        for (int r = 0; r < n; ++r) through[r][cols[r]].push_back(wi);
        ++alive;
    };

    auto refill = [&]() {
        pool.clear();
        seen.clear();
        alive = 0;
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c)
                through[r][c].clear();

        // Each dive is an independent randomised descent, which is what keeps
        // the sample spread out — one deep DFS would instead return a clump of
        // placements all sharing a prefix. Harvesting a few per dive amortises
        // the descent without collapsing that spread.
        std::vector<int> dflat((size_t)P.dive_yield * MAXN);
        auto *dsink = reinterpret_cast<int(*)[MAXN]>(dflat.data());
        int stale = 0;
        while ((int)pool.size() < P.pool_target && stale < P.dive_giveup) {
            AltSearch dv{owner, n, cat_cols, &rng, P.dive_budget,
                         dsink, P.dive_yield, 0, {}};
            dv.bt(0, -1, 0, 0);
            int added = 0;
            for (int i = 0; i < dv.found && (int)pool.size() < P.pool_target; ++i) {
                uint64_t h = 0;
                for (int r = 0; r < n; ++r) h = h << 4 | (unsigned)dsink[i][r];
                if (!seen.insert(h).second) continue;
                add_witness(dsink[i]);
                ++added;
            }
            if (added) stale = 0; else ++stale;
        }

        // Dives came up empty — either survivors have got rare, or there are
        // none left. Only an exhaustive search separates the two, so make it
        // count: collect the whole survivor set, not just one witness. If it
        // comes back short of the limit the search ran to completion, and
        // because survivors only ever die the pool stays exact from here on —
        // no further search is ever needed for this board.
        if (pool.empty()) {
            std::vector<int> flat((size_t)P.pool_target * MAXN);
            AltSearch ex{owner, n, cat_cols, &rng, -1,
                         reinterpret_cast<int(*)[MAXN]>(flat.data()),
                         P.pool_target, 0, {}};
            ex.bt(0, -1, 0, 0);
            if (!ex.found) { proven_unique = true; return; }
            for (int i = 0; i < ex.found; ++i) add_witness(&flat[(size_t)i * MAXN]);
            pool_complete = ex.found < P.pool_target;
        }
        refill_at = std::max(1, (int)pool.size() / 8);
    };

    std::uniform_real_distribution<double> jit(0.0, P.jitter);

    // The greedy has a rich-get-richer bias that needs a cap. Colouring a cell
    // for region k kills every witness that already uses k elsewhere, so the
    // more cells k owns the more each further cell kills. Uncapped, one region
    // ends up with ~40% of the board (at n=12: largest 40.7 cells, second 35.0,
    // against 28.7/21.1 from the flood-fill generator) — same speed, but a
    // visibly lumpier board than the levels already shipped. Capping too hard
    // is worse than not capping, since it takes away the strong moves: n+3
    // roughly halves the success rate. 2n measured as the balance — shape close
    // to the old generator (31.6/27.1) at no cost in speed.
    int cap = P.size_cap > 0 ? P.size_cap : 2*n;

    while (unassigned > 0) {
        if (!proven_unique && pool_complete && alive == 0) proven_unique = true;
        if (!proven_unique && !pool_complete && alive < refill_at) refill();

        // Pick the (cell, region) pair that kills the most witnesses. Once
        // uniqueness is proven every score is 0 kills, so this degenerates into
        // "grow the smallest neighbouring region" — a plain balanced fill.
        double best = -1e18;
        int br = -1, bc = -1, bk = -1;
        while (br < 0) {
            for (int r = 0; r < n; ++r) {
                for (int c = 0; c < n; ++c) {
                    if (owner[r][c] >= 0) continue;
                    uint32_t cand = 0;
                    for (int d = 0; d < 4; ++d) {
                        int nr = r+DR[d], nc = c+DC[d];
                        if (nr<0||nr>=n||nc<0||nc>=n) continue;
                        if (owner[nr][nc] >= 0) cand |= 1u << owner[nr][nc];
                    }
                    if (!cand) continue;  // not on the frontier yet
                    for (int k = 0; k < n; ++k) {
                        if (!(cand >> k & 1)) continue;
                        if (size[k] >= cap) continue;
                        int kills = 0;
                        for (int wi : through[r][c]) {
                            const Witness &w = pool[wi];
                            if (w.alive && (w.used >> k & 1)) ++kills;
                        }
                        double s = kills - P.size_w * size[k] + jit(rng);
                        if (s > best) { best = s; br = r; bc = c; bk = k; }
                    }
                }
            }
            // Every frontier cell is hemmed in by regions that have hit the
            // cap. Loosen it rather than give up on the board.
            if (br < 0 && ++cap > n*n) return false;
        }

        owner[br][bc] = bk;
        ++size[bk];
        --unassigned;
        for (int wi : through[br][bc]) {
            Witness &w = pool[wi];
            if (!w.alive) continue;
            if (w.used >> bk & 1) { w.alive = false; --alive; }
            else                    w.used |= 1u << bk;
        }
    }

    // Reaching a full board without a proof means the sample never ran dry —
    // the board still has alternates. (Cheap to confirm; boards are full here.)
    return proven_unique || count_solutions(owner, n, 2) == 1;
}

// Below this the flood-fill-then-repair path wins: a raw n=9 board only has a
// few thousand alternates, which repair clears easily, and ICGC's witness pool
// is pure overhead. Measured ms/level (mean over 120 levels, 60 at n=11, 30 at
// n=12) — legacy here already has the randomised alternate search:
//
//   n:            8      9     10     11      12
//   legacy      1.9    9.0   67.3  259.4  4541.3
//   ICGC+repair 6.3   23.9   65.2  231.0   596.3
//
static constexpr int ICGC_MIN_N = 10;

static void generate_level(int n, std::mt19937 &rng,
                             int owner[][MAXN], int cat_cols[], bool force_legacy) {
    bool legacy = force_legacy || n < ICGC_MIN_N;
    for (int pi = 0; pi < 100; ++pi) {
        if (!random_perm_no_adj(cat_cols, n, rng))
            throw std::runtime_error("failed to find valid permutation");
        if (!legacy) {
            // Growth alone lands just short of unique, and that is structural
            // rather than a tuning problem: colouring one cell can only kill
            // witnesses that pass through it, i.e. at most a ~1/n fraction of
            // what is left, and there are only n²−n cells to colour. That caps
            // the whole growth phase at about n(1−1/n)^m ≈ n²·ln(1−1/n)⁻¹ nats,
            // which at n=12 is ~11.5 against the ~18 needed to go from |P| to
            // one. So ICGC gets the board within a few dozen solutions and the
            // repair loop — which has no step budget — closes the gap in a
            // handful of iterations.
            for (int gi = 0; gi < 8; ++gi) {
                if (icgc_colour(owner, n, cat_cols, rng)) return;
                if (repair_uniqueness(owner, n, cat_cols, rng)) return;
            }
        } else {
            for (int gi = 0; gi < 20; ++gi) {
                if (!grow_regions(owner, n, cat_cols, rng)) continue;
                if (count_solutions(owner, n, 2) == 1) return;
                if (repair_uniqueness(owner, n, cat_cols, rng)) return;
            }
        }
    }
    throw std::runtime_error("failed to generate unique-solution level");
}

// ── Existing-level scanner (append + duplicate detection) ────────────────────

struct LevelDB {
    std::set<std::string> fingerprints;
    std::set<int>         existing_indices;

    void load(const fs::path &dir, int n) {
        if (!fs::exists(dir)) return;
        std::string prefix = "level_" + std::to_string(n) + "_";
        std::string suffix = ".txt";
        for (auto &e : fs::directory_iterator(dir)) {
            std::string fname = e.path().filename().string();
            if (fname.size() <= prefix.size() + suffix.size()) continue;
            if (fname.compare(0, prefix.size(), prefix) != 0) continue;
            if (fname.compare(fname.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
            std::string num = fname.substr(prefix.size(),
                                           fname.size() - prefix.size() - suffix.size());
            int idx = 0;
            try { idx = std::stoi(num); } catch (...) { continue; }
            existing_indices.insert(idx);
            std::ifstream f(e.path());
            std::string line, fp;
            std::getline(f, line); // skip the "n" line
            for (int r = 0; r < n && std::getline(f, line); ++r)
                fp += line;
            fingerprints.insert(fp);
        }
    }

    std::vector<int> missing_up_to(int to) const {
        std::vector<int> result;
        for (int i = 1; i <= to; ++i)
            if (!existing_indices.count(i))
                result.push_back(i);
        return result;
    }

    std::string fingerprint(const int owner[][MAXN], int n) const {
        std::string fp;
        fp.reserve(n * n);
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c)
                fp += LETTERS[owner[r][c]];
        return fp;
    }

    bool is_duplicate(const int owner[][MAXN], int n) const {
        return fingerprints.count(fingerprint(owner, n)) > 0;
    }

    void add(const int owner[][MAXN], int n) {
        fingerprints.insert(fingerprint(owner, n));
    }
};

static void write_level_file(const fs::path &path, int n, const int owner[][MAXN], const int *cat_cols) {
    std::ofstream f(path);
    f << n << '\n';
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) f << LETTERS[owner[r][c]];
        f << '\n';
    }
    f << "# solution:";
    for (int r = 0; r < n; ++r) f << ' ' << cat_cols[r];
    f << '\n';
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    std::vector<int> sizes;
    int to_idx = -1;
    fs::path out_dir = "levels";
    std::optional<uint64_t> seed_val;
    bool legacy = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--sizes") {
            while (i+1 < argc && std::isdigit((unsigned char)argv[i+1][0]))
                sizes.push_back(std::stoi(argv[++i]));
        } else if (a == "--to")   { to_idx   = std::stoi(argv[++i]);
        } else if (a == "--out")  { out_dir  = argv[++i];
        } else if (a == "--seed") { seed_val = std::stoull(argv[++i]);
        } else if (a == "--legacy") { legacy = true;
        }
    }
    if (sizes.empty()) sizes = {8, 9, 10, 11, 12};
    if (to_idx < 1) {
        std::cerr << "Usage: generate --to N [--sizes ...] [--seed S] [--legacy]\n";
        return 1;
    }

    std::mt19937 rng(seed_val ? (uint32_t)*seed_val : std::random_device{}());

    int owner[MAXN][MAXN];
    int cat_cols[MAXN];

    fs::path backtrack_dir = out_dir / "backtrack";
    fs::create_directories(backtrack_dir);

    for (int n : sizes) {
        fs::path lvl_dir = out_dir / std::to_string(n);
        fs::create_directories(lvl_dir);

        LevelDB db;
        db.load(lvl_dir, n);

        // Find the next free index for this size in levels/backtrack (flat
        // dir shared across sizes), so rejected candidates don't overwrite
        // each other or previously-saved ones.
        std::string bt_prefix = "level_" + std::to_string(n) + "_";
        int bt_next = 1;
        for (auto &e : fs::directory_iterator(backtrack_dir)) {
            std::string fname = e.path().filename().string();
            if (fname.compare(0, bt_prefix.size(), bt_prefix) != 0) continue;
            std::string num = fname.substr(bt_prefix.size(),
                                            fname.size() - bt_prefix.size() - 4 /* ".txt" */);
            try { bt_next = std::max(bt_next, std::stoi(num) + 1); } catch (...) {}
        }

        std::vector<int> missing = db.missing_up_to(to_idx);
        if (missing.empty()) {
            std::cout << "n=" << n << ": all " << to_idx << " levels exist, nothing to do\n";
            continue;
        }
        std::cout << "n=" << n << ": filling " << missing.size() << " gap(s) up to " << to_idx << '\n';

        int dupes = 0, d9_regen = 0;
        for (int idx : missing) {
            while (true) {
                generate_level(n, rng, owner, cat_cols, legacy);
                if (db.is_duplicate(owner, n)) { ++dupes; continue; }

                if (!difficulty::is_pure_logic_solvable(owner, n)) {
                    // Can't be solved by pure logical deduction. Don't throw it
                    // away — stash it in levels/backtrack for a future
                    // "backtracking required" challenge pack.
                    char bt_fname[32];
                    std::snprintf(bt_fname, sizeof(bt_fname), "level_%d_%08d.txt", n, bt_next++);
                    write_level_file(backtrack_dir / bt_fname, n, owner, cat_cols);
                    ++d9_regen;
                    continue;
                }

                char fname[32];
                std::snprintf(fname, sizeof(fname), "level_%d_%08d.txt", n, idx);
                fs::path path = lvl_dir / fname;
                write_level_file(path, n, owner, cat_cols);

                db.add(owner, n);
                std::cout << "  wrote " << path.filename().string() << '\n';
                break;
            }
        }
        if (dupes)    std::cout << "  (" << dupes    << " duplicate(s) skipped)\n";
        if (d9_regen) std::cout << "  (" << d9_regen << " D9 level(s) moved to levels/backtrack)\n";
    }
    return 0;
}
