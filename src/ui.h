#pragma once
// Shared UI state, palette and widgets for the ui / tabs translation units.
// main.cpp only needs App, drawUI, applyStyle and loadFonts.
#include <cstdint>
#include <string>
#include <vector>

#include "engine.h"
#include "imgui.h"
#include "model.h"
#include "updater.h"

// ---- palette (the one msmoney, pensionplan and dca share) -------------------
inline ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}
inline const ImVec4 C_BG = rgb(20, 24, 29);
inline const ImVec4 C_PANEL = rgb(29, 35, 43);
inline const ImVec4 C_PANEL2 = rgb(38, 46, 56);
inline const ImVec4 C_BORDER = rgb(55, 65, 78);
inline const ImVec4 C_TEXT = rgb(232, 234, 237);
inline const ImVec4 C_DIM = rgb(145, 155, 167);
inline const ImVec4 C_GREEN = rgb(46, 204, 113);
inline const ImVec4 C_RED = rgb(235, 87, 70);
inline const ImVec4 C_BLUE = rgb(82, 152, 219);
inline const ImVec4 C_YELLOW = rgb(241, 196, 15);
inline const ImVec4 C_ORANGE = rgb(230, 126, 34);
inline const ImVec4 C_PURPLE = rgb(165, 105, 189);
inline const ImVec4 C_TEAL = rgb(66, 179, 168);
inline const ImVec4 C_GRAY = rgb(120, 130, 140);
inline const ImVec4 C_DARK = rgb(15, 18, 22);

extern ImFont* fBody;
extern ImFont* fBig;
extern ImFont* fHuge;

constexpr int kTabs = 6;

// ---- app state --------------------------------------------------------------
struct App {
    std::string dataRoot = "data/";   // ends in '/'
    bool updateOnStart = true;

    Params p;
    SeriesSet series;
    Market mk;

    // the real fund (config.ini "fondo"): its NAVs from Finect
    std::string fundIsin;
    std::string fundName;             // "" until Finect has been asked
    Series fundNav;

    // Everything below is derived from (series, p) and rebuilt as a whole
    // whenever the parameters' signature changes: it takes a few milliseconds
    // and no widget has to remember to invalidate anything.
    std::string sig;
    // the parameters actually computed with: a.p, but on the synthetic fund
    // while there are no NAVs to trade
    Params eff;
    FundFit fit;
    std::vector<Indicators> ind;
    Indices ix;
    SignalStudy study;
    StratResult strat[4];
    std::vector<Trade> trades;
    Grid grid;

    Updater updater;
    std::vector<UpdateOutcome> lastUpdate;
    std::string lastUpdateAt;         // wall-clock time of the last refresh

    std::string status;
    bool statusBad = false;
    uint64_t statusAt = 0;
    int forceTab = -1;

    // view state
    int histRange = 2;                // index into the range buttons
    int histChart = 0;
    int horizon = 1;                  // 0: 6m, 1: 12m, 2: 24m
    int gridMetric = 0;
    bool showTrades = false;
};

std::string paramsPath(const App& a);
// "iShares Euro Investment Grade..." or the ISIN; the synthetic bond's label
// when the backtest is not on the real fund
std::string fundLabel(const App& a);
// what an UpdateOutcome refers to, for messages
std::string outcomeLabel(const App& a, int id);
// reads the cached series from disk and rebuilds the market table
void reloadData(App& a);
// recomputes the derived results if the parameters or the data changed
void ensureComputed(App& a);
void startRefresh(App& a, bool full = false);
void applyUpdate(App& a, const std::vector<UpdateOutcome>& outcomes);
bool exportObservations(App& a, std::string* path);

// ---- shared helpers (ui.cpp) ------------------------------------------------
void setStatus(App& a, const std::string& s, bool bad = false);
void TextRight(const std::string& s, const ImVec4& col = C_TEXT, ImFont* font = nullptr);
bool AccentButton(const char* label, const ImVec4& bg, const ImVec4& fg,
                  ImVec2 size = ImVec2(0, 0));
void StatTile(const char* label, const std::string& value, const ImVec4& col,
              const char* tooltip = nullptr, float width = 0);
void HelpMarker(const char* text);
void Tooltip(const char* text);        // on the last item, wrapped
const ImVec4& signColor(double v);
const ImVec4& stateColor(int st);
ImVec4 decisionColor(Decision d);
ImVec4 scoreColor(int score, const Params& p);
// a rounded, filled label: "Favorable", "Neutral"...
void Pill(const char* text, const ImVec4& col);

// ---- chart widget (ui.cpp) --------------------------------------------------
struct ChartSeries {
    std::string label;
    ImVec4 color = C_TEAL;
    std::vector<double> x;   // serial days
    std::vector<double> y;
    float thickness = 2.0f;
    bool fillToZero = false;
    bool stepped = false;    // hold each value until the next point
};
struct ChartBand {           // a shaded vertical span
    double x0 = 0, x1 = 0;
    ImVec4 color = C_GREEN;
};
struct ChartLine {           // a horizontal reference
    double y = 0;
    ImVec4 color = C_DIM;
    std::string label;
};
struct ChartOpts {
    const char* unit = "";
    int decimals = 2;
    std::vector<ChartBand> bands;
    std::vector<ChartLine> lines;
    bool fixedY = false;     // use yMin..yMax instead of fitting the data
    double yMin = 0, yMax = 1;
};

// A line chart with a date x axis, drawn into a bordered child of `size`
// (0 fills the remaining space). Hovering reads every series at that date.
void drawChart(const char* id, ImVec2 size, const std::vector<ChartSeries>& series,
               const ChartOpts& opts = ChartOpts());

// ---- tabs (tabs.cpp) --------------------------------------------------------
void tabMonitor(App& a);
void tabHistory(App& a);
void tabSignal(App& a);
void tabStrategy(App& a);
void tabThresholds(App& a);
void tabData(App& a);

// ---- shell (ui.cpp) ---------------------------------------------------------
void drawUI(App& a);
void applyStyle();
void loadFonts();
