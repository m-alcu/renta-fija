#pragma once
// Calendar, formatting, the cached ECB series and the aligned daily market
// table the engine works on. No UI and no network in here.
#include <array>
#include <string>
#include <vector>

// ---- calendar ---------------------------------------------------------------
// Dates are a "serial day" (days since 1970-01-01) so they subtract and sort
// as plain ints.
struct Date {
    int y = 0, m = 0, d = 0;
};
constexpr int kNoDay = -1000000;

int serialFromDate(Date dt);
Date dateFromSerial(int serial);
std::string dateStr(int serial);          // YYYY-MM-DD
int parseDate(const std::string& s);      // serial, or kNoDay if malformed
inline int monthOrdinal(int y, int m) { return y * 12 + (m - 1); }
int monthOrdinalOf(int serial);
int serialOfMonthDay(int ord, int day);
std::string monthStr(int ord);            // 2024-03

// ---- formatting (Spanish decimal comma) -------------------------------------
std::string fmtNum(double v, int dec);              // 1234,56
std::string fmtSigned(double v, int dec);           // +1,23 / -0,50
std::string fmtPct(double v, int dec = 2);          // 2,44 %
std::string fmtBp(double v);                        // +15 pb
std::string plainNum(double v, int dec);            // dot decimal, for files

// ---- one daily series -------------------------------------------------------
struct Series {
    std::vector<int> days;
    std::vector<double> vals;

    bool empty() const { return days.empty(); }
    int firstDay() const { return days.empty() ? kNoDay : days.front(); }
    int lastDay() const { return days.empty() ? kNoDay : days.back(); }
    // index of the last observation on or before `serial`, or -1
    int indexAsOf(int serial) const;
    // value as of `serial` (same rule); NaN when the series does not reach back
    double asOf(int serial) const;
    // adds/overwrites observations; returns how many days were new
    int merge(const Series& other);

    bool load(const std::string& path);   // "YYYY-MM-DD,value" lines
    bool save(const std::string& path) const;
};

// ---- the series the app follows ---------------------------------------------
enum SeriesId {
    S_3M, S_6M, S_9M, S_1Y, S_1Y3M, S_2Y, S_3Y, S_5Y, S_10Y,  // AAA spot curve
    S_ESTR,
    S_EONIA,
    S_COUNT
};
constexpr int kCurveNodes = 9;

struct SeriesInfo {
    const char* file;      // cache file name, without extension
    const char* label;
    const char* flow;      // ECB dataflow
    const char* key;       // ECB series key
    double maturity;       // years, 0 for the overnight rates
    bool live;             // false: discontinued, download once
};
const SeriesInfo& seriesInfo(int id);

using SeriesSet = std::array<Series, S_COUNT>;

std::string dataDir(const std::string& dataRoot);              // <root>/series/
std::string seriesPath(const std::string& dataRoot, int id);
void loadSeries(const std::string& dataRoot, SeriesSet& set);

// the real fund's NAVs (same "YYYY-MM-DD,value" format) and its name, one line
std::string fundNavPath(const std::string& dataRoot, const std::string& isin);
std::string fundNamePath(const std::string& dataRoot, const std::string& isin);
std::string loadFundName(const std::string& dataRoot, const std::string& isin);

// ---- the aligned table ------------------------------------------------------
// One row per day the ECB published its yield curve. Yields are the
// continuously compounded spot rates of the euro-area AAA government curve, in
// percent. `on` is the overnight rate a money-market fund earns: the €STR
// since October 2019 and EONIA minus 8,5 pb (the ECB's fixed conversion
// spread) before that.
struct Market {
    std::vector<int> days;
    std::array<std::vector<double>, kCurveNodes> y;
    std::vector<double> on;

    size_t size() const { return days.size(); }
    bool empty() const { return days.empty(); }
    int indexAsOf(int serial) const;
    // spot rate at an arbitrary maturity, linear between the nodes
    double spot(size_t i, double maturityYears) const;
};
Market buildMarket(const SeriesSet& set);
