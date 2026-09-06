// tools/difficulty_analyzer.h  –  C++ port of difficulty_analyzer.py's analyze()
//
// Simulates human-style reasoning (easiest -> hardest) on a Meowdoku board
// and reports how many times each difficulty tier was needed to make
// progress. See difficulty_analyzer.py's module docstring for the tier
// definitions (D1 single-step, D2 intersection, D3..D7 k-group, D9
// backtracking required). This header only exposes the check the C++
// generator needs; use difficulty_analyzer.py for human-readable reports.
#pragma once

namespace difficulty {

constexpr int MAXN = 12;

struct Result {
    bool solved = false;
    int histogram[10] = {0};  // index 1..9 used, 0 unused
    int max_diff = 0;
};

// owner[r][c] = region id (0..n-1), n <= MAXN. Read-only; owner is not modified.
Result analyze(const int owner[][MAXN], int n);

// True if solvable by pure logical deduction (tiers 1-7), no backtracking (D9) needed.
inline bool is_pure_logic_solvable(const int owner[][MAXN], int n) {
    Result r = analyze(owner, n);
    return r.solved && r.histogram[9] == 0;
}

}  // namespace difficulty
