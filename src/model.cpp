#include "model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>

// ---- calendar ---------------------------------------------------------------
// Howard Hinnant's civil-date algorithms.
int serialFromDate(Date dt) {
    int y = dt.y - (dt.m <= 2 ? 1 : 0);
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (dt.m + (dt.m > 2 ? -3 : 9)) + 2) / 5 + dt.d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

Date dateFromSerial(int serial) {
    int z = serial + 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int d = doy - (153 * mp + 2) / 5 + 1;
    int m = mp < 10 ? mp + 3 : mp - 9;
    return Date{y + (m <= 2 ? 1 : 0), m, d};
}

std::string dateStr(int serial) {
    if (serial == kNoDay) return "-";
    Date dt = dateFromSerial(serial);
    char buf[16];
    snprintf(buf, sizeof buf, "%04d-%02d-%02d", dt.y, dt.m, dt.d);
    return buf;
}

int parseDate(const std::string& s) {
    int y, m, d;
    if (s.size() < 10 || sscanf(s.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3) return kNoDay;
    if (y < 1900 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31) return kNoDay;
    return serialFromDate(Date{y, m, d});
}

int monthOrdinalOf(int serial) {
    Date dt = dateFromSerial(serial);
    return monthOrdinal(dt.y, dt.m);
}

int serialOfMonthDay(int ord, int day) {
    return serialFromDate(Date{ord / 12, ord % 12 + 1, day});
}

std::string monthStr(int ord) {
    char buf[16];
    snprintf(buf, sizeof buf, "%04d-%02d", ord / 12, ord % 12 + 1);
    return buf;
}

// ---- formatting -------------------------------------------------------------
std::string plainNum(double v, int dec) {
    char buf[48];
    snprintf(buf, sizeof buf, "%.*f", dec, v);
    return buf;
}

std::string fmtNum(double v, int dec) {
    if (!std::isfinite(v)) return "-";
    std::string s = plainNum(v, dec);
    // "-0,00" reads as a negative figure that is not there
    if (s[0] == '-' && s.find_first_not_of("-0.") == std::string::npos) s.erase(0, 1);
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string fmtSigned(double v, int dec) {
    if (!std::isfinite(v)) return "-";
    std::string s = fmtNum(v, dec);
    return (s[0] != '-' && s.find_first_not_of("0,") != std::string::npos) ? "+" + s : s;
}

std::string fmtPct(double v, int dec) {
    return std::isfinite(v) ? fmtNum(v, dec) + " %" : "-";
}

std::string fmtBp(double v) { return std::isfinite(v) ? fmtSigned(v, 0) + " pb" : "-"; }

// ---- Series -----------------------------------------------------------------
int Series::indexAsOf(int serial) const {
    auto it = std::upper_bound(days.begin(), days.end(), serial);
    return (int)(it - days.begin()) - 1;
}

double Series::asOf(int serial) const {
    int i = indexAsOf(serial);
    return i < 0 ? std::numeric_limits<double>::quiet_NaN() : vals[(size_t)i];
}

int Series::merge(const Series& other) {
    if (other.days.empty()) return 0;
    std::vector<int> d;
    std::vector<double> v;
    d.reserve(days.size() + other.days.size());
    v.reserve(days.size() + other.days.size());
    size_t i = 0, j = 0;
    int added = 0;
    while (i < days.size() || j < other.days.size()) {
        if (j >= other.days.size() || (i < days.size() && days[i] < other.days[j])) {
            d.push_back(days[i]);
            v.push_back(vals[i]);
            i++;
        } else if (i >= days.size() || other.days[j] < days[i]) {
            d.push_back(other.days[j]);
            v.push_back(other.vals[j]);
            j++;
            added++;
        } else {  // same day: the fresh download wins (the ECB does revise)
            d.push_back(other.days[j]);
            v.push_back(other.vals[j]);
            i++;
            j++;
        }
    }
    days.swap(d);
    vals.swap(v);
    return added;
}

bool Series::load(const std::string& path) {
    days.clear();
    vals.clear();
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        size_t comma = line.find(',');
        if (comma == std::string::npos) continue;
        int day = parseDate(line.substr(0, comma));
        if (day == kNoDay) continue;
        char* end = nullptr;
        double v = strtod(line.c_str() + comma + 1, &end);
        if (end == line.c_str() + comma + 1 || !std::isfinite(v)) continue;
        if (!days.empty() && day <= days.back()) continue;  // keep it sorted
        days.push_back(day);
        vals.push_back(v);
    }
    return !days.empty();
}

bool Series::save(const std::string& path) const {
    // written to a sibling first: a crash mid-write must not eat the cache
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        for (size_t i = 0; i < days.size(); i++)
            f << dateStr(days[i]) << ',' << plainNum(vals[i], 6) << '\n';
        if (!f) return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// ---- catalogue --------------------------------------------------------------
namespace {
#define YC(node) "B.U2.EUR.4F.G_N_A.SV_C_YM." node
const SeriesInfo kInfo[S_COUNT] = {
    {"aaa_3m", "Deuda AAA 3 meses", "YC", YC("SR_3M"), 0.25, true},
    {"aaa_6m", "Deuda AAA 6 meses", "YC", YC("SR_6M"), 0.50, true},
    {"aaa_9m", "Deuda AAA 9 meses", "YC", YC("SR_9M"), 0.75, true},
    {"aaa_1y", "Deuda AAA 1 año", "YC", YC("SR_1Y"), 1.00, true},
    {"aaa_1y3m", "Deuda AAA 15 meses", "YC", YC("SR_1Y3M"), 1.25, true},
    {"aaa_2y", "Deuda AAA 2 años", "YC", YC("SR_2Y"), 2.00, true},
    {"aaa_3y", "Deuda AAA 3 años", "YC", YC("SR_3Y"), 3.00, true},
    {"aaa_5y", "Deuda AAA 5 años", "YC", YC("SR_5Y"), 5.00, true},
    {"aaa_10y", "Deuda AAA 10 años", "YC", YC("SR_10Y"), 10.0, true},
    {"estr", "€STR", "EST", "B.EU000A2X2A25.WT", 0, true},
    {"eonia", "EONIA (hasta 2021)", "EON", "D.EONIA_TO.RATE", 0, false},
};
#undef YC
}  // namespace

const SeriesInfo& seriesInfo(int id) { return kInfo[id]; }

std::string dataDir(const std::string& dataRoot) { return dataRoot + "series/"; }

std::string seriesPath(const std::string& dataRoot, int id) {
    return dataDir(dataRoot) + kInfo[id].file + ".csv";
}

void loadSeries(const std::string& dataRoot, SeriesSet& set) {
    for (int i = 0; i < S_COUNT; i++) set[(size_t)i].load(seriesPath(dataRoot, i));
}

std::string fundNavPath(const std::string& dataRoot, const std::string& isin) {
    return dataDir(dataRoot) + "fondo_" + isin + ".csv";
}

std::string fundNamePath(const std::string& dataRoot, const std::string& isin) {
    return dataDir(dataRoot) + "fondo_" + isin + ".txt";
}

std::string loadFundName(const std::string& dataRoot, const std::string& isin) {
    std::ifstream f(fundNamePath(dataRoot, isin));
    std::string name;
    std::getline(f, name);
    if (!name.empty() && name.back() == '\r') name.pop_back();
    return name;
}

// ---- Market -----------------------------------------------------------------
int Market::indexAsOf(int serial) const {
    auto it = std::upper_bound(days.begin(), days.end(), serial);
    return (int)(it - days.begin()) - 1;
}

double Market::spot(size_t i, double m) const {
    if (m <= kInfo[0].maturity) return y[0][i];
    for (int k = 1; k < kCurveNodes; k++) {
        double m0 = kInfo[k - 1].maturity, m1 = kInfo[k].maturity;
        if (m <= m1) {
            double t = (m - m0) / (m1 - m0);
            return y[(size_t)k - 1][i] + t * (y[(size_t)k][i] - y[(size_t)k - 1][i]);
        }
    }
    return y[kCurveNodes - 1][i];
}

Market buildMarket(const SeriesSet& set) {
    Market mk;
    const Series& ref = set[S_2Y];
    for (size_t r = 0; r < ref.days.size(); r++) {
        int day = ref.days[r];
        double node[kCurveNodes];
        bool ok = true;
        for (int k = 0; k < kCurveNodes && ok; k++) {
            const Series& s = set[(size_t)k];
            int idx = s.indexAsOf(day);
            // every node comes from the same daily fit: a node missing for
            // this exact day means a half-downloaded cache, so skip the row
            if (idx < 0 || s.days[(size_t)idx] != day) ok = false;
            else node[k] = s.vals[(size_t)idx];
        }
        if (!ok) continue;

        double on = std::numeric_limits<double>::quiet_NaN();
        const Series& estr = set[S_ESTR];
        const Series& eonia = set[S_EONIA];
        if (!estr.empty() && day >= estr.firstDay()) on = estr.asOf(day);
        else if (!eonia.empty() && day >= eonia.firstDay()) on = eonia.asOf(day) - 0.085;
        // no overnight history at all: the 3-month yield is the nearest thing
        if (!std::isfinite(on)) on = node[0];

        mk.days.push_back(day);
        for (int k = 0; k < kCurveNodes; k++) mk.y[(size_t)k].push_back(node[k]);
        mk.on.push_back(on);
    }
    return mk;
}
