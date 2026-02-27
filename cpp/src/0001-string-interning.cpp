/* micrometrics : Symbol Interning Profiling
 *
 * Two benchmark scenarios:
 *
 *  [1-to-1]   Each incoming symbol is matched against one target once.
 *             Registry: get_id(sym) + (id == target_id)   — 1 lookup, 1 cmp
 *             Direct  : sym == target_string              — 1 string cmp
 *             Expected winner: Direct (lookup overhead > SSO char cmp)
 *
 *  [1-to-many] Each incoming symbol is looked up once, then its ID is reused
 *             across FANOUT downstream operations (e.g. routing to N order
 *             books, writing to N ring-buffer slots).
 *             Registry: get_id(sym) + FANOUT × (id == target_id)
 *             Direct  : FANOUT × (sym == target_string)
 *             Expected winner: Registry (lookup amortised over FANOUT ops)
 *             Fanout swept from 8 to 1024 (doubling each step).
 *             A summary table is printed at the end.
 *
 * Design notes
 *   - Incoming stream is a vector of std::string copies, not references
 *     into SYMBOL_POOL, eliminating the pointer-identity shortcut that
 *     std::string::operator== may use when &lhs == &rhs.
 *   - The registry lookup cost is included in both registry benchmarks.
 *   - All symbols are under 15 chars (SSO), realistic for market tickers.
 *
 * Build:
 *   g++ -std=c++17 -O2 -o string-interning 0001-string-interning.cpp
 *
 * Run:
 *   ./string-interning [iterations]
 *   default: iterations=10 000 000
 *   fanout is swept automatically from 8 to 1024 (×2 each step)
 *
 * Copyright (c) 2026, Augusto Damasceno. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * See (https://github.com/augustodamasceno/micrometrics)
 */

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>


class SymbolRegistry {
private:
    std::unordered_map<std::string, uint32_t> string_to_id_;
    std::vector<std::string> id_to_string_;
    std::mutex mtx;

public:
    uint32_t get_id(std::string_view symbol) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = string_to_id_.find(std::string(symbol));
        if (it != string_to_id_.end()) return it->second;

        uint32_t new_id = static_cast<uint32_t>(id_to_string_.size());
        string_to_id_[std::string(symbol)] = new_id;
        id_to_string_.emplace_back(symbol);
        return new_id;
    }

    inline std::string_view get_symbol(uint32_t id) const {
        return id_to_string_.at(id);
    }
};


static const std::vector<std::string> SYMBOL_POOL = {
    // Equities
    "AAPL", "MSFT", "GOOGL", "AMZN", "NVDA",
    "TSLA", "META", "BRK.B", "JPM",  "V",
    // ETFs
    "SPY",  "QQQ",  "IWM",   "DIA",  "GLD",
    "TLT",  "VTI",  "EEM",   "XLF",  "HYG",
    // Forex pairs
    "EURUSD", "GBPUSD", "USDJPY", "USDCHF", "AUDUSD",
    "NZDUSD", "USDCAD", "EURGBP", "EURJPY", "GBPJPY",
    // Futures / commodities
    "ES",  "NQ",  "CL",  "GC",  "SI",
    "NG",  "ZB",  "ZN",  "ZC",  "ZS",
    // Crypto
    "BTCUSD", "ETHUSD", "SOLUSD", "BNBUSD", "XRPUSD",
};


template <typename Clock = std::chrono::high_resolution_clock>
struct Timer {
    typename Clock::time_point start = Clock::now();
    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
};

/* Simulate an incoming network stream: each element is a fresh std::string
 * copy so that &stream[i] != &SYMBOL_POOL[j], eliminating the pointer-
 * identity shortcut in std::string::operator==. */
static std::vector<std::string>
generate_incoming_stream(std::size_t n, unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pick(
        0, static_cast<int>(SYMBOL_POOL.size()) - 1);
    std::vector<std::string> stream;
    stream.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        stream.push_back(SYMBOL_POOL[pick(rng)]);   // value copy, new allocation
    return stream;
}


static void print_table_row(int w, const std::string& label, double ms, std::size_t matches) {
    std::cout << std::left  << std::setw(w) << label
              << std::right << std::setw(12) << ms
              << std::setw(12) << matches << "\n";
}

static void print_speedup(double ms_registry, double ms_direct) {
    const double speedup = ms_direct / ms_registry;
    if (speedup >= 1.0)
        std::cout << "  Registry is " << std::fixed << std::setprecision(2)
                  << speedup << "x faster than direct.\n";
    else
        std::cout << "  Direct is " << std::fixed << std::setprecision(2)
                  << (1.0 / speedup) << "x faster than registry.\n";
}

int main(int argc, char* argv[]) {
    const std::size_t ITERATIONS =
        argc > 1 ? static_cast<std::size_t>(std::atoll(argv[1])) : 10'000'000;

    std::cout << "micrometrics - string-interning vs direct-string comparison\n"
              << "Iterations : " << ITERATIONS << "\n"
              << "Symbol pool: " << SYMBOL_POOL.size() << " unique symbols\n\n";

    SymbolRegistry registry;
    for (const auto& sym : SYMBOL_POOL)
        registry.get_id(sym);

    const std::string  target_string = "BTCUSD";
    const uint32_t     target_id     = registry.get_id(target_string);

    const auto incoming = generate_incoming_stream(ITERATIONS);

    volatile std::size_t sink = 0;
    for (const auto& s : incoming) sink += (registry.get_id(s) == target_id) ? 1 : 0;
    for (const auto& s : incoming) sink += (s == target_string) ? 1 : 0;

    const int W = 38;
    std::cout << std::fixed << std::setprecision(3);

    /* 
     * TEST 1 — 1-to-1
     *   Each incoming symbol is matched against the target exactly once.
     *   Registry path: 1 get_id lookup  + 1 integer comparison
     *   Direct path  : 1 string comparison
     */
    std::cout << "---> 1-to-1  (one lookup / comparison per incoming symbol)\n";
    std::cout << std::left  << std::setw(W) << "Method"
              << std::right << std::setw(12) << "Time (ms)"
              << std::setw(12) << "Matches" << "\n";
    std::cout << std::string(W + 24, '-') << "\n";

    Timer<> ta;
    std::size_t matches_a = 0;
    for (const std::string& sym : incoming) {
        uint32_t incoming_id = registry.get_id(sym);
        if (incoming_id == target_id) ++matches_a;
    }
    double ms_a = ta.elapsed_ms();

    Timer<> tb;
    std::size_t matches_b = 0;
    for (const std::string& sym : incoming) {
        if (sym == target_string) ++matches_b;
    }
    double ms_b = tb.elapsed_ms();

    if (matches_a != matches_b) {
        std::cerr << "ERROR [1-to-1]: match counts differ ("
                  << matches_a << " vs " << matches_b << ")\n";
        return 1;
    }
    print_table_row(W, "Registry (lookup + ID cmp)", ms_a, matches_a);
    print_table_row(W, "Direct std::string cmp",     ms_b, matches_b);
    std::cout << std::string(W + 24, '-') << "\n";
    print_speedup(ms_a, ms_b);

    /*
     * TEST 2 — 1-to-many  (fanout sweep: 8 → 1024, doubling each step)
     *   Each incoming symbol is looked up once; the resulting ID (or the
     *   string itself) is then reused across FANOUT downstream operations.
     *   Registry path: 1 get_id lookup  + FANOUT integer comparisons
     *   Direct path  : FANOUT string comparisons
     */
    struct FanoutResult {
        std::size_t fanout;
        double ms_registry;
        double ms_direct;
        std::size_t matches;
    };
    std::vector<FanoutResult> fanout_results;

    for (std::size_t fanout = 8; fanout <= 1024; fanout *= 2) {
        for (const auto& s : incoming) {
            uint32_t id = registry.get_id(s);
            for (std::size_t f = 0; f < fanout; ++f) sink += (id == target_id) ? 1 : 0;
        }
        for (const auto& s : incoming)
            for (std::size_t f = 0; f < fanout; ++f) sink += (s == target_string) ? 1 : 0;

        std::cout << "\n---> 1-to-many  fanout=" << fanout
                  << "  (one lookup reused across N operations)\n";
        std::cout << std::left  << std::setw(W) << "Method"
                  << std::right << std::setw(12) << "Time (ms)"
                  << std::setw(12) << "Matches" << "\n";
        std::cout << std::string(W + 24, '-') << "\n";

        Timer<> tc;
        std::size_t matches_c = 0;
        for (const std::string& sym : incoming) {
            uint32_t incoming_id = registry.get_id(sym);
            for (std::size_t f = 0; f < fanout; ++f)
                if (incoming_id == target_id) ++matches_c;
        }
        double ms_c = tc.elapsed_ms();

        Timer<> td;
        std::size_t matches_d = 0;
        for (const std::string& sym : incoming) {
            for (std::size_t f = 0; f < fanout; ++f)
                if (sym == target_string) ++matches_d;
        }
        double ms_d = td.elapsed_ms();

        if (matches_c != matches_d) {
            std::cerr << "ERROR [1-to-many fanout=" << fanout << "]: match counts differ ("
                      << matches_c << " vs " << matches_d << ")\n";
            return 1;
        }
        print_table_row(W, "Registry (lookup + NxID cmp)", ms_c, matches_c);
        print_table_row(W, "Direct Nxstd::string cmp",     ms_d, matches_d);
        std::cout << std::string(W + 24, '-') << "\n";
        print_speedup(ms_c, ms_d);

        fanout_results.push_back({fanout, ms_c, ms_d, matches_c});
    }

    /*  SUMMARY 1-to-many fanout sweep */
    std::cout << "\n\n--> 1-to-many summary (fanout sweep 8 to 1024)\n";
    const int SW = 10;
    std::cout << std::right
              << std::setw(SW)     << "Fanout"
              << std::setw(SW + 2) << "Reg (ms)"
              << std::setw(SW + 2) << "Dir (ms)"
              << std::setw(SW + 2) << "Speedup"
              << std::setw(12)     << "Winner" << "\n";
    std::cout << std::string(SW * 4 + 2 + 12, '-') << "\n";
    for (const auto& r : fanout_results) {
        const double speedup = r.ms_direct / r.ms_registry;
        const std::string winner = (speedup >= 1.0) ? "Registry" : "Direct";
        const double ratio = (speedup >= 1.0) ? speedup : (1.0 / speedup);
        std::cout << std::fixed << std::setprecision(3)
                  << std::right
                  << std::setw(SW)     << r.fanout
                  << std::setw(SW + 2) << r.ms_registry
                  << std::setw(SW + 2) << r.ms_direct
                  << std::setprecision(2)
                  << std::setw(SW + 2) << ratio
                  << std::setw(12)     << winner << "\n";
    }
    std::cout << std::string(SW * 4 + 2 + 12, '-') << "\n";

    std::cout << "\n";
    (void)sink;
    return 0;
}
