#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <limits>

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();

// ---- Params as a table, so load / save / signature cannot drift apart ------
struct Field {
    const char* name;
    char type;  // 'd' double, 'i' int, 'b' bool
    size_t off;
};
#define F(type, member) {#member, type, offsetof(Params, member)}
const Field kFields[] = {
    F('d', t2yFav),     F('d', t2yUnf),      F('b', confirm1m),  F('d', t2yConfirm),
    F('d', slopeThr),   F('d', expFav),      F('d', expUnf),     F('d', premFav),
    F('d', premUnf),    F('d', t10Fav),      F('d', t10Unf),     F('d', duration),
    F('d', fundSpread), F('d', fundTer),     F('d', mmTer),      F('i', fullScore),
    F('i', partialScore), F('i', exitScore), F('d', partialPct), F('d', premEntry), F('i', freq),
    F('d', switchCostBp), F('i', startYear), F('i', endYear),
};
#undef F

std::string fieldGet(const Params& p, const Field& f) {
    const char* base = (const char*)&p;
    if (f.type == 'd') return plainNum(*(const double*)(base + f.off), 4);
    if (f.type == 'i') return std::to_string(*(const int*)(base + f.off));
    return *(const bool*)(base + f.off) ? "1" : "0";
}

void fieldSet(Params& p, const Field& f, const std::string& v) {
    char* base = (char*)&p;
    if (f.type == 'd') {
        char* end = nullptr;
        double d = strtod(v.c_str(), &end);
        if (end != v.c_str() && std::isfinite(d)) *(double*)(base + f.off) = d;
    } else if (f.type == 'i') {
        *(int*)(base + f.off) = atoi(v.c_str());
    } else {
        *(bool*)(base + f.off) = v == "1" || v == "si" || v == "true";
    }
}

// keeps a hand-edited file from putting the engine somewhere meaningless
void sanitize(Params& p) {
    p.duration = std::clamp(p.duration, 0.5, 5.0);
    p.fullScore = std::clamp(p.fullScore, 1, 10);
    p.partialScore = std::clamp(p.partialScore, 1, p.fullScore);
    p.exitScore = std::clamp(p.exitScore, 0, 9);
    p.partialPct = std::clamp(p.partialPct, 0.0, 100.0);
    p.freq = std::clamp(p.freq, 0, 1);
    p.switchCostBp = std::clamp(p.switchCostBp, 0.0, 200.0);
}

double bp(double now, double then) { return (now - then) * 100.0; }

}  // namespace

std::string Params::signature() const {
    std::string s;
    for (const Field& f : kFields) s += fieldGet(*this, f) + "/";
    return s;
}

bool Params::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        for (const Field& fd : kFields)
            if (key == fd.name) fieldSet(*this, fd, val);
    }
    sanitize(*this);
    return true;
}

bool Params::save(const std::string& path) const {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << "# renta-fija: umbrales y parametros del backtest\n";
    for (const Field& fd : kFields) f << fd.name << '=' << fieldGet(*this, fd) << '\n';
    return (bool)f;
}

// ---- indicators -------------------------------------------------------------
double Indicators::raw(int k) const {
    switch (k) {
        case 0: return d2y3m;
        case 1: return dSlope3m;
        case 2: return exp12;
        case 3: return premium;
        default: return d10y3m;
    }
}

Indicators computeIndicators(const Market& mk, size_t i, const Params& p) {
    Indicators in;
    int day = mk.days[i];
    in.y3m = mk.y[S_3M][i];
    in.y1y = mk.y[S_1Y][i];
    in.y2 = mk.y[S_2Y][i];
    in.y10 = mk.y[S_10Y][i];
    in.on = mk.on[i];
    in.slope = bp(in.y2, in.y3m);

    // 3. The spot rates are continuously compounded, so the 3-month rate the
    // curve implies for 6 (12) months ahead is a plain weighted difference.
    in.f6 = (mk.y[S_9M][i] * 0.75 - mk.y[S_6M][i] * 0.50) / 0.25;
    in.f12 = (mk.y[S_1Y3M][i] * 1.25 - mk.y[S_1Y][i] * 1.00) / 0.25;
    in.exp6 = bp(in.f6, in.y3m);
    in.exp12 = bp(in.f12, in.y3m);

    // 4. What the fund yields against what the money market is expected to
    // pay over the next year: today's overnight rate plus the move the curve
    // prices in between 3 and 12 months.
    in.fundYield = mk.spot(i, p.duration) + p.fundSpread - p.fundTer;
    in.mmExpected = in.on + (in.y1y - in.y3m) - p.mmTer;
    in.premium = in.fundYield - in.mmExpected;
    // A rise spread evenly over the year costs duration x rise in price and
    // gives back about half of it in extra carry.
    in.breakevenBp = in.premium * 100.0 / std::max(0.25, p.duration - 0.5);

    int i1 = mk.indexAsOf(day - 30), i3 = mk.indexAsOf(day - 91), iw = mk.indexAsOf(day - 7);
    if (i3 < 0) return in;  // not enough history for the trends yet
    in.valid = true;
    size_t a1 = (size_t)i1, a3 = (size_t)i3, aw = (size_t)iw;

    in.d2y1w = bp(in.y2, mk.y[S_2Y][aw]);
    in.d2y1m = bp(in.y2, mk.y[S_2Y][a1]);
    in.d2y3m = bp(in.y2, mk.y[S_2Y][a3]);
    in.d3m3m = bp(in.y3m, mk.y[S_3M][a3]);
    in.dSlope3m = in.d2y3m - in.d3m3m;
    in.d10y1m = bp(in.y10, mk.y[S_10Y][a1]);
    in.d10y3m = bp(in.y10, mk.y[S_10Y][a3]);

    // 1. trend of the 2-year yield
    if (in.d2y3m <= p.t2yFav)
        in.st[0] = (p.confirm1m && in.d2y1m > p.t2yConfirm) ? Neutral : Favourable;
    else if (in.d2y3m >= p.t2yUnf) in.st[0] = Unfavourable;
    else in.st[0] = Neutral;

    // 2. slope 2y - 3m. Steeper because short rates fall is the good kind;
    // steeper because the 2-year rises faster, or flatter because short rates
    // are still being pushed up, are the bad kinds.
    bool steeper = in.dSlope3m >= p.slopeThr, flatter = in.dSlope3m <= -p.slopeThr;
    if (steeper && in.d3m3m < 0 && in.d2y3m < p.slopeThr) in.st[1] = Favourable;
    else if ((steeper && in.d2y3m >= p.slopeThr) ||
             (flatter && in.d3m3m > 0 && in.d2y3m > 0))
        in.st[1] = Unfavourable;
    else in.st[1] = Neutral;

    // 3. rate expectations
    if (std::max(in.exp6, in.exp12) >= p.expUnf) in.st[2] = Unfavourable;
    else if (in.exp12 <= p.expFav) in.st[2] = Favourable;
    else in.st[2] = Neutral;

    // 4. premium over the money market
    if (in.premium > p.premFav) in.st[3] = Favourable;
    else if (in.premium < p.premUnf) in.st[3] = Unfavourable;
    else in.st[3] = Neutral;

    // 5. trend of the 10-year yield
    if (in.d10y3m > p.t10Unf) in.st[4] = Unfavourable;
    else if (in.d10y3m <= p.t10Fav) in.st[4] = Favourable;
    else in.st[4] = Neutral;

    in.score = 0;
    for (int k = 0; k < 5; k++) in.score += in.st[k];
    return in;
}

std::vector<Indicators> computeAll(const Market& mk, const Params& p) {
    std::vector<Indicators> all(mk.size());
    for (size_t i = 0; i < mk.size(); i++) all[i] = computeIndicators(mk, i, p);
    return all;
}

Decision decide(int score, const Params& p) {
    if (score >= p.fullScore) return Decision::Full;
    if (score >= p.partialScore) return Decision::Partial;
    return Decision::Hold;
}

bool entryBlocked(const Indicators& in, const Params& p) {
    return in.score >= p.partialScore && in.premium < p.premEntry;
}

Decision decide(const Indicators& in, const Params& p) {
    return entryBlocked(in, p) ? Decision::Hold : decide(in.score, p);
}

const char* decisionName(Decision d) {
    switch (d) {
        case Decision::Full: return "Entrada completa";
        case Decision::Partial: return "Entrada parcial";
        default: return "Seguir en monetario";
    }
}

const char* stateName(int st) {
    return st == Favourable ? "Favorable" : st == Neutral ? "Neutral" : "Desfavorable";
}

const char* indicatorName(int k) {
    static const char* n[5] = {"Tendencia del 2 años", "Pendiente 2 años − 3 meses",
                               "Expectativas de tipos del BCE", "Prima frente al monetario",
                               "Tendencia del 10 años"};
    return n[k];
}

const char* indicatorShort(int k) {
    static const char* n[5] = {"2 años", "Pendiente", "Expectativas", "Prima", "10 años"};
    return n[k];
}

std::string indicatorReading(const Indicators& in, int k) {
    auto moved = [](double d) {
        if (std::fabs(d) < 0.5) return std::string("sin cambios");
        return std::string(d > 0 ? "sube " : "baja ") + fmtNum(std::fabs(d), 0) + " pb";
    };
    switch (k) {
        case 0:
            return "El 2 años " + moved(in.d2y3m) + " en 3 meses y " + moved(in.d2y1m) +
                   " en el último mes.";
        case 1: {
            std::string why;
            if (std::fabs(in.dSlope3m) < 0.5) why = "no se mueve";
            else if (in.dSlope3m > 0)
                why = in.d3m3m < 0 && in.d2y3m <= 0 ? "aumenta porque bajan los tipos cortos"
                      : in.d3m3m < 0 ? "aumenta: bajan los cortos y sube el 2 años"
                      : "aumenta porque el 2 años sube más que el 3 meses";
            else
                why = in.d3m3m > 0 && in.d2y3m <= 0 ? "se reduce: suben los cortos y baja el 2 años"
                      : in.d3m3m > 0 ? "se reduce porque los tipos cortos suben más"
                      : "se reduce porque el 2 años baja más que el 3 meses";
            return "La pendiente (" + fmtBp(in.slope) + ") " + why + " (" + fmtBp(in.dSlope3m) +
                   " en 3 meses).";
        }
        case 2: {
            auto priced = [](double e) {
                if (std::fabs(e) < 12.5) return std::string("tipos estables");
                return std::string(e > 0 ? "subidas de " : "bajadas de ") +
                       fmtNum(std::fabs(e), 0) + " pb";
            };
            return "La curva descuenta " + priced(in.exp6) + " a 6 meses y " + priced(in.exp12) +
                   " a 12 meses.";
        }
        case 3:
            return "El fondo rinde " + fmtPct(in.fundYield) + " neto frente a " +
                   fmtPct(in.mmExpected) + " esperado del monetario: " +
                   fmtSigned(in.premium, 2) + " pp.";
        default:
            return "El 10 años " + moved(in.d10y3m) + " en 3 meses y " + moved(in.d10y1m) +
                   " en el último mes.";
    }
}

// ---- indices ----------------------------------------------------------------
Indices buildIndices(const Market& mk, const Params& p) {
    Indices ix;
    size_t n = mk.size();
    ix.mm.assign(n, 100.0);
    ix.bond.assign(n, 100.0);
    double M = p.duration;
    for (size_t i = 0; i + 1 < n; i++) {
        double dd = mk.days[i + 1] - mk.days[i];
        ix.mm[i + 1] = ix.mm[i] * (1.0 + (mk.on[i] - p.mmTer) / 100.0 * dd / 360.0);

        // buy a zero of maturity M today, sell it tomorrow as a zero of
        // maturity M - dt at tomorrow's curve
        double dt = dd / 365.25;
        double y0 = mk.spot(i, M) / 100.0;
        double y1 = mk.spot(i + 1, std::max(M - dt, 0.0)) / 100.0;
        double ratio = std::exp(-y1 * (M - dt) + y0 * M);
        ratio *= std::exp((p.fundSpread - p.fundTer) / 100.0 * dt);
        ix.bond[i + 1] = ix.bond[i] * ratio;
    }
    return ix;
}

// ---- review calendar --------------------------------------------------------
bool periodRange(const Market& mk, const Params& p, size_t& first, size_t& last) {
    if (mk.empty()) return false;
    first = 0;
    last = mk.size() - 1;
    if (p.startYear > 0) {
        int from = serialFromDate(Date{p.startYear, 1, 1});
        auto it = std::lower_bound(mk.days.begin(), mk.days.end(), from);
        if (it == mk.days.end()) return false;
        first = (size_t)(it - mk.days.begin());
    }
    if (p.endYear > 0) {
        int idx = mk.indexAsOf(serialFromDate(Date{p.endYear, 12, 31}));
        if (idx < 0) return false;
        last = (size_t)idx;
    }
    return last > first;
}

std::vector<size_t> reviewDays(const Market& mk, const Params& p) {
    std::vector<size_t> out;
    size_t first, last;
    if (!periodRange(mk, p, first, last)) return out;
    auto bucket = [&](int serial) {
        // weeks start on Monday; 1970-01-01 was a Thursday
        return p.freq == 1 ? (serial + 3 >= 0 ? (serial + 3) / 7 : -1) : monthOrdinalOf(serial);
    };
    for (size_t i = std::max<size_t>(first, 1); i <= last; i++)
        if (bucket(mk.days[i]) != bucket(mk.days[i - 1])) out.push_back(i);
    return out;
}

// ---- signal study -----------------------------------------------------------
Bucket bucketOf(std::vector<double> v) {
    Bucket b;
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }),
            v.end());
    b.n = (int)v.size();
    if (v.empty()) return b;
    std::sort(v.begin(), v.end());
    double sum = 0;
    int pos = 0;
    for (double x : v) {
        sum += x;
        if (x > 0) pos++;
    }
    b.mean = sum / b.n;
    b.median = b.n % 2 ? v[(size_t)b.n / 2] : (v[(size_t)b.n / 2 - 1] + v[(size_t)b.n / 2]) / 2;
    b.pctPos = 100.0 * pos / b.n;
    b.worst = v.front();
    b.best = v.back();
    return b;
}

SignalStudy studySignal(const Market& mk, const Indices& ix, const std::vector<Indicators>& ind,
                        const Params& p) {
    SignalStudy st{};
    for (size_t i : reviewDays(mk, p)) {
        if (!ind[i].valid) continue;
        Obs o;
        o.idx = i;
        o.day = mk.days[i];
        for (int h = 0; h < kHorizons; h++) {
            int target = o.day + (int)std::lround(kHorizonMonths[h] * 30.4375);
            o.bond[h] = o.mm[h] = o.excess[h] = kNaN;
            if (target > mk.days.back()) continue;
            size_t j = (size_t)mk.indexAsOf(target);
            o.bond[h] = (ix.bond[j] / ix.bond[i] - 1.0) * 100.0;
            o.mm[h] = (ix.mm[j] / ix.mm[i] - 1.0) * 100.0;
            o.excess[h] = o.bond[h] - o.mm[h];
        }
        st.obs.push_back(o);
    }

    // quintile edges of each indicator's raw figure
    for (int k = 0; k < 5; k++) {
        std::vector<double> raw;
        for (auto& o : st.obs) raw.push_back(ind[o.idx].raw(k));
        std::sort(raw.begin(), raw.end());
        for (int q = 0; q < 4; q++)
            st.quintileEdge[k][q] =
                raw.empty() ? 0 : raw[std::min(raw.size() - 1, raw.size() * (size_t)(q + 1) / 5)];
    }

    for (int h = 0; h < kHorizons; h++) {
        std::vector<double> all, score[11], dec[3], state[5][3], quint[5][5];
        for (auto& o : st.obs) {
            const Indicators& in = ind[o.idx];
            double e = o.excess[h];
            all.push_back(e);
            score[in.score].push_back(e);
            dec[(int)decide(in, p)].push_back(e);
            for (int k = 0; k < 5; k++) {
                state[k][in.st[k]].push_back(e);
                int q = 0;
                while (q < 4 && in.raw(k) >= st.quintileEdge[k][q]) q++;
                quint[k][q].push_back(e);
            }
        }
        st.all[h] = bucketOf(all);
        for (int s = 0; s <= 10; s++) st.byScore[s][h] = bucketOf(score[s]);
        for (int d = 0; d < 3; d++) st.byDecision[d][h] = bucketOf(dec[d]);
        for (int k = 0; k < 5; k++) {
            for (int s = 0; s < 3; s++) st.byState[k][s][h] = bucketOf(state[k][s]);
            for (int q = 0; q < 5; q++) st.byQuintile[k][q][h] = bucketOf(quint[k][q]);
        }
    }
    return st;
}

// ---- strategy ---------------------------------------------------------------
StratResult simulate(const Market& mk, const Indices& ix, const std::vector<Indicators>& ind,
                     const Params& p, Strategy s, std::vector<Trade>* trades) {
    StratResult r;
    switch (s) {
        case Strategy::Signal: r.name = "Señal de 5 indicadores"; break;
        case Strategy::AllMM: r.name = "Siempre monetario"; break;
        case Strategy::AllBond: r.name = "Siempre renta fija"; break;
        default: r.name = "50 % / 50 %"; break;
    }
    if (trades) trades->clear();
    size_t first, last;
    if (!periodRange(mk, p, first, last)) return r;

    std::vector<char> review(mk.size(), 0);
    if (s == Strategy::Signal)
        for (size_t i : reviewDays(mk, p)) review[i] = 1;

    double w = s == Strategy::AllBond ? 1.0 : s == Strategy::Half ? 0.5 : 0.0;
    double pending = w;
    int pendingScore = 0;
    double value = 100.0, peak = 100.0;
    double sum = 0, sumSq = 0;
    size_t inBond = 0, steps = 0;

    r.days.push_back(mk.days[first]);
    r.value.push_back(value);
    r.weight.push_back(w);
    for (size_t i = first; i < last; i++) {
        // An order decided on yesterday's review is executed today: the ECB
        // publishes each curve the following morning, so the signal could not
        // have been traded on the day it is dated.
        if (pending != w) {
            value *= 1.0 - std::fabs(pending - w) * p.switchCostBp / 10000.0;
            if (trades) trades->push_back(Trade{mk.days[i], pendingScore, w, pending});
            w = pending;
            r.switches++;
        }
        if (review[i] && ind[i].valid) {
            int sc = ind[i].score;
            bool canEnter = ind[i].premium >= p.premEntry;
            double target = w;
            if (sc >= p.fullScore && canEnter) target = 1.0;
            else if (sc >= p.partialScore && canEnter)
                target = std::max(w, p.partialPct / 100.0);
            else if (sc <= p.exitScore) target = 0.0;
            pending = target;
            pendingScore = sc;
        }
        double ret = w * (ix.bond[i + 1] / ix.bond[i] - 1.0) +
                     (1.0 - w) * (ix.mm[i + 1] / ix.mm[i] - 1.0);
        value *= 1.0 + ret;
        peak = std::max(peak, value);
        r.maxDD = std::min(r.maxDD, (value / peak - 1.0) * 100.0);
        sum += ret;
        sumSq += ret * ret;
        steps++;
        if (w > 0) inBond++;
        r.days.push_back(mk.days[i + 1]);
        r.value.push_back(value);
        r.weight.push_back(w);
    }
    r.years = (mk.days[last] - mk.days[first]) / 365.25;
    r.total = value - 100.0;
    r.cagr = r.years > 0 ? (std::pow(value / 100.0, 1.0 / r.years) - 1.0) * 100.0 : 0;
    if (steps > 1) {
        double mean = sum / steps;
        double var = std::max(0.0, sumSq / steps - mean * mean);
        r.vol = std::sqrt(var * steps / r.years) * 100.0;
        r.timeInBond = 100.0 * inBond / steps;
    }
    return r;
}

// ---- threshold grid ---------------------------------------------------------
Grid thresholdGrid(const Market& mk, const Indices& ix, const std::vector<Indicators>& ind,
                   const Params& p) {
    Grid g;
    g.mmCagr = simulate(mk, ix, ind, p, Strategy::AllMM).cagr;
    g.bondCagr = simulate(mk, ix, ind, p, Strategy::AllBond).cagr;
    double best = -1e18;
    for (int entry = 1; entry <= 10; entry++) {
        for (int exit = 0; exit < entry; exit++) {
            Params q = p;
            q.fullScore = q.partialScore = entry;
            q.exitScore = exit;
            StratResult r = simulate(mk, ix, ind, q, Strategy::Signal);
            GridCell& c = g.cell[entry][exit];
            c.valid = !r.days.empty();
            c.cagr = r.cagr;
            c.excess = r.cagr - g.mmCagr;
            c.maxDD = r.maxDD;
            c.timeInBond = r.timeInBond;
            c.switches = r.switches;
            if (c.valid && c.excess > best) {
                best = c.excess;
                g.bestEntry = entry;
                g.bestExit = exit;
            }
        }
    }
    return g;
}
