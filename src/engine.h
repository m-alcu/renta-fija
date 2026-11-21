#pragma once
// The five-indicator scoreboard, the synthetic fund indices and the backtests.
// Pure: no UI, no network, no files (except Params load/save). Tested in
// tests/engine_tests.cpp against synthetic curves with known answers.
#include <string>
#include <vector>

#include "model.h"

// ---- parameters -------------------------------------------------------------
// Every threshold of explicacion.md is here so the backtest can replace the
// heuristic value by an empirical one.
struct Params {
    // 1. 2-year trend (pb over 3 months)
    double t2yFav = -15;      // <= : favourable
    double t2yUnf = 15;       // >= : unfavourable
    bool confirm1m = true;    // favourable needs the last month not to rise
    double t2yConfirm = 10;   // ... by more than this (pb)
    // 2. slope 2y - 3m (pb moved over 3 months to count as a direction)
    double slopeThr = 10;
    // 3. ECB expectations: implied 3-month rate in 12 months minus today's (pb)
    double expFav = -25;      // <= : cuts priced in
    double expUnf = 25;       // >= : hikes priced in
    // 4. premium over the money market (pp)
    double premFav = 0.50;    // >  : favourable
    double premUnf = 0.00;    // <  : unfavourable
    // 5. 10-year trend (pb over 3 months)
    double t10Fav = 10;       // <= : favourable
    double t10Unf = 25;       // >  : unfavourable

    // the bond fund: 0 a synthetic AAA zero, 1 the real fund's NAV (Finect)
    int fundSource = 1;
    // the synthetic one
    double duration = 2.0;    // years, constant-maturity zero coupon
    double fundSpread = 0.0;  // pp over the AAA curve (credit, peripherals)
    double fundTer = 0.20;    // % a year
    // How the real fund is described to indicator 4 (premium) and the safety
    // margin; its returns come from the NAV itself. The defaults are
    // fitFund() on IE00B67T5G21 from 2012-02 to 2026-11: it moves like a
    // 4,25-year AAA zero and beat it by 1,83 pp a year after costs.
    double realDuration = 4.25;
    double realSpread = 1.98; // pp over the AAA curve, before costs
    double realTer = 0.15;    // % a year (already inside the NAV)
    double mmTer = 0.10;      // % a year, money-market fund

    // decision rule
    int fullScore = 8;        // >= : whole position in the bond fund
    int partialScore = 5;     // >= : partial position
    int exitScore = 3;        // <= : back to the money market
    double partialPct = 50;   // % held in the bond fund on a partial entry
    // Entering (partial or full) also needs premium >= this (pp). Exits are
    // untouched. -0.30 keeps out of 2024-25, when the score said buy but the
    // bond yielded far less than the money market, and still lets in late
    // 2008 (about -0.15), the best entry of the sample; above -0.15 it does not.
    double premEntry = -0.30;

    // backtest
    int freq = 0;             // review: 0 monthly, 1 weekly
    double switchCostBp = 0;  // cost of moving 100 % of the portfolio, in pb
    int startYear = 0;        // 0: from the first data
    int endYear = 0;          // 0: to the last data

    // what the premium indicator uses for the bond fund, whichever it is
    double fundDur() const { return fundSource ? realDuration : duration; }
    double fundSpr() const { return fundSource ? realSpread : fundSpread; }
    double fundCost() const { return fundSource ? realTer : fundTer; }

    std::string signature() const;          // changes when anything above does
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

// ---- indicators -------------------------------------------------------------
enum State { Unfavourable = 0, Neutral = 1, Favourable = 2 };
enum class Decision { Hold, Partial, Full };

struct Indicators {
    bool valid = false;       // false until there are 3 months of history
    // levels, %
    double y3m = 0, y1y = 0, y2 = 0, y10 = 0, on = 0;
    // 1
    double d2y1w = 0, d2y1m = 0, d2y3m = 0;  // pb
    // 2
    double slope = 0, dSlope3m = 0, d3m3m = 0;  // pb
    // 3
    double f6 = 0, f12 = 0;        // implied 3-month rate in 6 / 12 months, %
    double exp6 = 0, exp12 = 0;    // ... minus today's 3-month rate, pb
    // 4
    double fundYield = 0;          // net of costs, %
    double mmExpected = 0;         // net of costs, next 12 months, %
    double premium = 0;            // pp
    double breakevenBp = 0;        // yield rise that wipes the premium in a year
    // 5
    double d10y1m = 0, d10y3m = 0; // pb

    int st[5] = {1, 1, 1, 1, 1};
    int score = 0;                 // 0..10
    // the raw figure each indicator is judged on (for the quintile study)
    double raw(int k) const;
};

Indicators computeIndicators(const Market& mk, size_t i, const Params& p);
std::vector<Indicators> computeAll(const Market& mk, const Params& p);
// what the score alone says (score tables, colours)
Decision decide(int score, const Params& p);
// what the rule says on a given day: the score plus the premium filter
Decision decide(const Indicators& in, const Params& p);
// true when the score is enough to enter but the premium filter stops it
bool entryBlocked(const Indicators& in, const Params& p);
const char* decisionName(Decision d);
const char* stateName(int st);
const char* indicatorName(int k);       // long
const char* indicatorShort(int k);
// what the indicator's state means in words, e.g. "Baja 22 pb en 3 meses"
std::string indicatorReading(const Indicators& in, int k);

// ---- total-return indices ---------------------------------------------------
// mm:    the overnight rate, act/360, less the fund's costs.
// synth: a zero-coupon bond of fundDur() years rolled every day along the AAA
//        curve - carry, roll-down and price change - plus fundSpr(), less
//        fundCost().
// bond:  the bond fund the backtests trade. With fundSource 1 and a NAV
//        series, the real fund's NAV (carried over the days it is not
//        published) and [first, last] is the stretch the NAV covers; else the
//        synthetic one over the whole table.
// mm and synth start at 100 on the first day of the table, bond on `first`.
struct Indices {
    std::vector<double> mm, bond, synth;
    size_t first = 0, last = 0;
    bool real = false;
};
Indices buildIndices(const Market& mk, const Params& p, const Series* nav = nullptr);

// ---- the real fund against the AAA curve ------------------------------------
// Which AAA zero the fund moves like, and by how much it beat it: the
// duration is the one at which weekly NAV returns regress on that zero's yield
// changes with a slope equal to itself; the excess is the fund's annual
// return over the zero's (no spread, no costs), so it comes net of costs and
// realSpread = excess + realTer.
struct FundFit {
    bool valid = false;
    int from = kNoDay, to = kNoDay;
    double duration = 0;     // years
    double r2 = 0;           // of the weekly regression
    double excess = 0;       // pp a year over the AAA zero, whole history
    double excess3y = 0;     // ... over the last three years
    double fundCagr = 0, zeroCagr = 0;
};
FundFit fitFund(const Market& mk, const Series& nav);

// ---- review calendar --------------------------------------------------------
// indices of the first market day of every month (or week) inside the period
std::vector<size_t> reviewDays(const Market& mk, const Indices& ix, const Params& p);
// [first, last] market index of the backtest period - the years chosen,
// inside what the bond index covers; false if it is empty
bool periodRange(const Market& mk, const Indices& ix, const Params& p, size_t& first,
                 size_t& last);

// ---- signal study -----------------------------------------------------------
constexpr int kHorizons = 3;
constexpr int kHorizonMonths[kHorizons] = {6, 12, 24};

struct Obs {
    size_t idx = 0;
    int day = 0;
    double bond[kHorizons];    // forward return of each fund, %; NaN when the
    double mm[kHorizons];      //   horizon runs past the data
    double excess[kHorizons];  // bond - mm, pp
};

struct Bucket {
    int n = 0;
    double mean = 0, median = 0, pctPos = 0, worst = 0, best = 0;
};
Bucket bucketOf(std::vector<double> v);

struct SignalStudy {
    std::vector<Obs> obs;                    // one per review day
    Bucket all[kHorizons];
    Bucket byScore[11][kHorizons];
    Bucket byDecision[3][kHorizons];
    Bucket byState[5][3][kHorizons];
    // each indicator's raw figure cut in five equal-sized groups
    double quintileEdge[5][4];
    Bucket byQuintile[5][5][kHorizons];
};
SignalStudy studySignal(const Market& mk, const Indices& ix, const std::vector<Indicators>& ind,
                        const Params& p);

// ---- strategy ---------------------------------------------------------------
enum class Strategy { Signal, AllMM, AllBond, Half };

struct StratResult {
    std::string name;
    std::vector<int> days;
    std::vector<double> value;   // starts at 100
    std::vector<double> weight;  // share in the bond fund, 0..1
    double total = 0;            // %
    double cagr = 0;             // % a year
    double maxDD = 0;            // %, negative
    double vol = 0;              // % a year
    double timeInBond = 0;       // % of days with any bond position
    int switches = 0;
    double years = 0;
};
struct Trade {
    int day = 0;
    int score = 0;
    double from = 0, to = 0;     // weights
};
StratResult simulate(const Market& mk, const Indices& ix, const std::vector<Indicators>& ind,
                     const Params& p, Strategy s, std::vector<Trade>* trades = nullptr);

// ---- threshold grid ---------------------------------------------------------
// The signal strategy for every (entry, exit) pair: all-in at score >= entry,
// all-out at score <= exit, no partial step.
struct GridCell {
    bool valid = false;
    double cagr = 0, excess = 0, maxDD = 0, timeInBond = 0;
    int switches = 0;
};
struct Grid {
    GridCell cell[11][11];  // [entry 1..10][exit 0..9]
    double mmCagr = 0, bondCagr = 0;
    int bestEntry = 0, bestExit = 0;
};
Grid thresholdGrid(const Market& mk, const Indices& ix, const std::vector<Indicators>& ind,
                   const Params& p);
