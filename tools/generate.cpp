// tools/generate.cpp  –  Meowdoku level generator (C++ rewrite of generate.py + solver.py)
// Compile: clang++ -O3 -std=c++17 tools/generate.cpp -o tools/generate
// Usage:   generate [--sizes N...] [--count K] [--out DIR] [--seed S]

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

// Find the first solution ≠ excl. Returns true and writes to out[] if found.
static bool find_alternate(const int g[][MAXN], int n, const int *excl, int *out) {
    struct S {
        const int (*g)[MAXN];
        int n; const int *excl; int *out; bool found;
        int pl[MAXN];
        void bt(int row, int pc, uint32_t uc, uint32_t ur) {
            if (found) return;
            if (row == n) {
                for (int r = 0; r < n; ++r)
                    if (pl[r] != excl[r]) { std::copy(pl, pl+n, out); found = true; return; }
                return;
            }
            for (int c = 0; c < n; ++c) {
                if (uc >> c & 1) continue;
                if (pc >= 0 && c >= pc-1 && c <= pc+1) continue;
                uint32_t rb = 1u << g[row][c];
                if (ur & rb) continue;
                pl[row] = c;
                bt(row+1, c, uc|(1u<<c), ur|rb);
                if (found) return;
            }
        }
    } s{g, n, excl, out, false, {}};
    s.bt(0, -1, 0, 0);
    return s.found;
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
        if (!find_alternate(owner, n, cat_cols, alt)) return true;  // unique!

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

static void generate_level(int n, std::mt19937 &rng,
                             int owner[][MAXN], int cat_cols[]) {
    for (int pi = 0; pi < 100; ++pi) {
        if (!random_perm_no_adj(cat_cols, n, rng))
            throw std::runtime_error("failed to find valid permutation");
        for (int gi = 0; gi < 20; ++gi) {
            if (!grow_regions(owner, n, cat_cols, rng)) continue;
            if (count_solutions(owner, n, 2) == 1) return;
            if (repair_uniqueness(owner, n, cat_cols, rng)) return;
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

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    std::vector<int> sizes;
    int to_idx = -1;
    fs::path out_dir = "levels";
    std::optional<uint64_t> seed_val;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--sizes") {
            while (i+1 < argc && std::isdigit((unsigned char)argv[i+1][0]))
                sizes.push_back(std::stoi(argv[++i]));
        } else if (a == "--to")   { to_idx   = std::stoi(argv[++i]);
        } else if (a == "--out")  { out_dir  = argv[++i];
        } else if (a == "--seed") { seed_val = std::stoull(argv[++i]);
        }
    }
    if (sizes.empty()) sizes = {8, 9, 10, 11, 12};
    if (to_idx < 1) { std::cerr << "Usage: generate --to N [--sizes ...] [--seed S]\n"; return 1; }

    // Path to the Python analyzer (same directory as this exe)
    fs::path exe_dir = fs::absolute(fs::path(argv[0])).parent_path();
    fs::path analyzer = exe_dir / "difficulty_analyzer.py";

    std::mt19937 rng(seed_val ? (uint32_t)*seed_val : std::random_device{}());

    int owner[MAXN][MAXN];
    int cat_cols[MAXN];

    for (int n : sizes) {
        fs::path lvl_dir = out_dir / std::to_string(n);
        fs::create_directories(lvl_dir);

        LevelDB db;
        db.load(lvl_dir, n);

        std::vector<int> missing = db.missing_up_to(to_idx);
        if (missing.empty()) {
            std::cout << "n=" << n << ": all " << to_idx << " levels exist, nothing to do\n";
            continue;
        }
        std::cout << "n=" << n << ": filling " << missing.size() << " gap(s) up to " << to_idx << '\n';

        int dupes = 0, d9_regen = 0;
        for (int idx : missing) {
            while (true) {
                generate_level(n, rng, owner, cat_cols);
                if (db.is_duplicate(owner, n)) { ++dupes; continue; }

                char fname[32];
                std::snprintf(fname, sizeof(fname), "level_%d_%03d.txt", n, idx);
                fs::path path = lvl_dir / fname;

                { // write candidate
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

                // D9 check via Python analyzer
                std::string cmd = "python \"" + analyzer.string()
                                + "\" --check \"" + path.string() + "\" > nul 2>&1";
                if (std::system(cmd.c_str()) != 0) {
                    fs::remove(path);
                    ++d9_regen;
                    continue;
                }

                db.add(owner, n);
                std::cout << "  wrote " << path.filename().string() << '\n';
                break;
            }
        }
        if (dupes)    std::cout << "  (" << dupes    << " duplicate(s) skipped)\n";
        if (d9_regen) std::cout << "  (" << d9_regen << " D9 level(s) regenerated)\n";
    }
    return 0;
}
