// Engine checks against synthetic curves whose answers are known in advance.
#include <cmath>
#include <cstdio>

#include "engine.h"
#include "model.h"

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                      \
        }                                                                    \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

// A market with one row per calendar day. `level(t)` gives the flat curve
// level in % on day t; `steep` is added per year of maturity.
template <class Fn>
static Market makeMarket(int days, Fn level, double steep = 0.0, double onSpread = 0.0) {
    Market mk;
    int start = serialFromDate(Date{2010, 1, 1});
    for (int t = 0; t < days; t++) {
        mk.days.push_back(start + t);
        double lv = level(t);
        for (int k = 0; k < kCurveNodes; k++)
            mk.y[(size_t)k].push_back(lv + steep * seriesInfo(k).maturity);
        mk.on.push_back(lv + onSpread);
    }
    return mk;
}

static void testCalendar() {
    CHECK(serialFromDate(Date{1970, 1, 1}) == 0);
    CHECK(dateStr(serialFromDate(Date{2024, 2, 29})) == "2024-02-29");
    CHECK(parseDate("2026-11-13") == serialFromDate(Date{2026, 11, 13}));
    CHECK(parseDate("basura") == kNoDay);
    Date d = dateFromSerial(serialFromDate(Date{2004, 9, 6}));
    CHECK(d.y == 2004 && d.m == 9 && d.d == 6);
    CHECK(fmtNum(-0.001, 2) == "0,00");
    CHECK(fmtSigned(1.5, 1) == "+1,5");
    CHECK(fmtBp(-15.2) == "-15 pb");
}

static void testSeries() {
    Series a, b;
    a.days = {10, 11, 12};
    a.vals = {1, 2, 3};
    b.days = {12, 13};
    b.vals = {30, 4};
    CHECK(a.merge(b) == 1);
    CHECK(a.days.size() == 4 && a.vals[2] == 30 && a.vals[3] == 4);
    CHECK(a.asOf(11) == 2 && a.asOf(100) == 4);
    CHECK(std::isnan(a.asOf(9)));
}

static void testSpotInterpolation() {
    Market mk = makeMarket(5, [](int) { return 1.0; }, 1.0);  // y(m) = 1 + m
    NEAR(mk.spot(0, 2.0), 3.0, 1e-12);
    NEAR(mk.spot(0, 1.5), 2.5, 1e-12);
    NEAR(mk.spot(0, 4.0), 5.0, 1e-12);
    NEAR(mk.spot(0, 0.1), 1.25, 1e-12);  // clamped at the 3-month node
}

// Flat curve that never moves: both funds earn the same rate, every trend is
// zero, and the score is the all-neutral-or-better one.
static void testFlatCurve() {
    Params p;
    p.fundTer = p.mmTer = 0;
    Market mk = makeMarket(800, [](int) { return 3.0; });
    Indices ix = buildIndices(mk, p);
    // continuously compounded 3 % for 799 days
    NEAR(ix.bond.back(), 100.0 * std::exp(0.03 * 799 / 365.25), 1e-6);
    // overnight 3 % act/360, compounded daily
    NEAR(ix.mm.back(), 100.0 * std::pow(1.0 + 0.03 / 360.0, 799), 1e-6);

    std::vector<Indicators> ind = computeAll(mk, p);
    CHECK(!ind[50].valid);
    CHECK(ind[200].valid);
    const Indicators& in = ind[200];
    NEAR(in.d2y3m, 0, 1e-9);
    NEAR(in.slope, 0, 1e-9);
    NEAR(in.exp12, 0, 1e-9);
    NEAR(in.premium, 0, 1e-9);
    CHECK(in.st[0] == Neutral && in.st[1] == Neutral && in.st[2] == Neutral);
    CHECK(in.st[3] == Neutral);     // premium 0 is "between 0 and 0,50"
    CHECK(in.st[4] == Favourable);  // the 10-year is stable
    CHECK(in.score == 6);
}

// Yields rising 1 pb a day: everything is unfavourable and the bond fund loses
// to the money market.
static void testRisingRates() {
    Params p;
    Market mk = makeMarket(600, [](int t) { return 1.0 + 0.01 * t; });
    std::vector<Indicators> ind = computeAll(mk, p);
    const Indicators& in = ind[300];
    NEAR(in.d2y3m, 91, 1e-6);
    NEAR(in.d2y1m, 30, 1e-6);
    NEAR(in.d2y1w, 7, 1e-6);
    CHECK(in.st[0] == Unfavourable);
    CHECK(in.st[4] == Unfavourable);
    Indices ix = buildIndices(mk, p);
    CHECK(ix.bond.back() < ix.mm.back());

    // falling by the same amount flips both trends
    Market down = makeMarket(600, [](int t) { return 8.0 - 0.01 * t; });
    Indicators f = computeIndicators(down, 300, p);
    CHECK(f.st[0] == Favourable && f.st[4] == Favourable);
    Indices dx = buildIndices(down, p);
    CHECK(dx.bond.back() > dx.mm.back());
}

// A one-off 100 pb jump costs a 2-year zero about 2 %.
static void testDurationShock() {
    Params p;
    p.fundTer = 0;
    Market mk = makeMarket(10, [](int t) { return t < 5 ? 2.0 : 3.0; });
    Indices ix = buildIndices(mk, p);
    double ret = ix.bond[5] / ix.bond[4] - 1.0;
    NEAR(ret, -0.02, 0.0005);
}

// An upward-sloping curve prices rate rises and pays a premium.
static void testSlopeAndForwards() {
    Params p;
    p.fundTer = p.mmTer = 0;
    Market mk = makeMarket(300, [](int) { return 2.0; }, 1.0);  // y(m) = 2 + m
    Indicators in = computeIndicators(mk, 200, p);
    NEAR(in.slope, 175, 1e-9);  // (2+2) - (2+0,25)
    // forward of a linear continuously-compounded curve: f(a,b) = 2 + a + b
    NEAR(in.f6, 2 + 0.5 + 0.75, 1e-9);
    NEAR(in.f12, 2 + 1.0 + 1.25, 1e-9);
    NEAR(in.exp12, (4.25 - 2.25) * 100, 1e-9);
    CHECK(in.st[2] == Unfavourable);
    // fund 4 %, money market 2 % + (3 - 2,25) expected
    NEAR(in.premium, 4.0 - 2.75, 1e-9);
    CHECK(in.st[3] == Favourable);
}

static void testConfirmation() {
    // fell 60 pb two months ago, then bounced 20 pb in the last month
    auto level = [](int t) { return t < 130 ? 4.0 : t < 170 ? 3.4 : 3.6; };
    Market mk = makeMarket(260, level);
    Params p;
    Indicators in = computeIndicators(mk, 190, p);
    NEAR(in.d2y3m, -40, 1e-6);
    NEAR(in.d2y1m, 20, 1e-6);
    CHECK(in.st[0] == Neutral);
    p.confirm1m = false;
    CHECK(computeIndicators(mk, 190, p).st[0] == Favourable);
}

static void testDecision() {
    Params p;
    CHECK(decide(8, p) == Decision::Full);
    CHECK(decide(7, p) == Decision::Partial);
    CHECK(decide(5, p) == Decision::Partial);
    CHECK(decide(4, p) == Decision::Hold);

    // the premium filter only stops entries
    Indicators in;
    in.score = 8;
    in.premium = -0.10;
    CHECK(decide(in, p) == Decision::Full && !entryBlocked(in, p));
    in.premium = -0.50;
    CHECK(decide(in, p) == Decision::Hold && entryBlocked(in, p));
    in.score = 2;
    CHECK(decide(in, p) == Decision::Hold && !entryBlocked(in, p));
}

// Rates fall for a long stretch, then rise for another: the signal has to be
// in the bond fund for the first part and out for the second, and beat both
// buy-and-hold alternatives.
static void testStrategy() {
    auto level = [](int t) { return t < 700 ? 6.0 - 0.005 * t : 2.5 + 0.005 * (t - 700); };
    Market mk = makeMarket(1400, level);
    Params p;
    std::vector<Indicators> ind = computeAll(mk, p);
    Indices ix = buildIndices(mk, p);
    std::vector<Trade> trades;
    StratResult sig = simulate(mk, ix, ind, p, Strategy::Signal, &trades);
    StratResult mm = simulate(mk, ix, ind, p, Strategy::AllMM);
    StratResult bond = simulate(mk, ix, ind, p, Strategy::AllBond);
    StratResult half = simulate(mk, ix, ind, p, Strategy::Half);
    CHECK(sig.days.size() == mk.size());
    CHECK(sig.total > mm.total && sig.total > bond.total);
    CHECK(!trades.empty());
    CHECK(sig.timeInBond > 30 && sig.timeInBond < 70);
    NEAR(mm.value.back(), 100.0 * ix.mm.back() / ix.mm.front(), 1e-9);
    NEAR(bond.value.back(), 100.0 * ix.bond.back() / ix.bond.front(), 1e-9);
    CHECK(half.total > std::min(mm.total, bond.total) &&
          half.total < std::max(mm.total, bond.total));
    CHECK(mm.maxDD == 0 && mm.switches == 0);
    CHECK(bond.maxDD < 0);

    // switching costs can only hurt
    Params costly = p;
    costly.switchCostBp = 50;
    CHECK(simulate(mk, ix, ind, costly, Strategy::Signal).total < sig.total);

    // the period filter shortens the run
    Params late = p;
    late.startYear = 2012;
    StratResult cut = simulate(mk, ix, ind, late, Strategy::Signal);
    CHECK(cut.days.front() == serialFromDate(Date{2012, 1, 1}));

    SignalStudy st = studySignal(mk, ix, ind, p);
    CHECK(st.obs.size() > 30);
    // high scores are followed by the bond fund winning, low ones by losing
    int hi = 0, lo = 0;
    double hiSum = 0, loSum = 0;
    for (auto& o : st.obs) {
        if (std::isnan(o.excess[0])) continue;
        if (ind[o.idx].score >= 6) hi++, hiSum += o.excess[0];
        if (ind[o.idx].score <= 3) lo++, loSum += o.excess[0];
    }
    CHECK(hi > 0 && lo > 0 && hiSum / hi > 0 && loSum / lo < 0);
    // horizons past the end of the data are left out, not guessed
    CHECK(std::isnan(st.obs.back().excess[2]));
    CHECK(st.all[2].n < st.all[0].n);

    Grid g = thresholdGrid(mk, ix, ind, p);
    CHECK(g.bestEntry > g.bestExit);
    CHECK(g.cell[g.bestEntry][g.bestExit].excess >= g.cell[8][3].excess);
    CHECK(!g.cell[3][5].valid);
}

static void testBucket() {
    Bucket b = bucketOf({1, -1, 3, NAN, 5});
    CHECK(b.n == 4);
    NEAR(b.mean, 2, 1e-12);
    NEAR(b.median, 2, 1e-12);
    NEAR(b.pctPos, 75, 1e-12);
    CHECK(b.worst == -1 && b.best == 5);
}

static void testParamsRoundTrip() {
    Params p;
    p.t2yFav = -22.5;
    p.fullScore = 7;
    p.confirm1m = false;
    p.startYear = 2010;
    CHECK(p.save("/tmp/rf_params_test.cfg"));
    Params q;
    CHECK(q.load("/tmp/rf_params_test.cfg"));
    CHECK(q.signature() == p.signature());
    CHECK(Params().signature() != p.signature());
    std::remove("/tmp/rf_params_test.cfg");
}

int main() {
    testCalendar();
    testSeries();
    testSpotInterpolation();
    testFlatCurve();
    testRisingRates();
    testDurationShock();
    testSlopeAndForwards();
    testConfirmation();
    testDecision();
    testStrategy();
    testBucket();
    testParamsRoundTrip();
    if (failures) {
        std::printf("%d comprobaciones fallidas\n", failures);
        return 1;
    }
    std::printf("engine_tests: todo correcto\n");
    return 0;
}
