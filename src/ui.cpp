// Shared widgets, the chart, the style, the data cycle and the window shell.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>

#include "ui.h"

ImFont* fBody = nullptr;
ImFont* fBig = nullptr;
ImFont* fHuge = nullptr;

void setStatus(App& a, const std::string& s, bool bad) {
    a.status = s;
    a.statusBad = bad;
    a.statusAt = SDL_GetTicks();
}

const ImVec4& signColor(double v) { return v >= 0 ? C_GREEN : C_RED; }

const ImVec4& stateColor(int st) {
    return st == Favourable ? C_GREEN : st == Neutral ? C_YELLOW : C_RED;
}

ImVec4 decisionColor(Decision d) {
    return d == Decision::Full ? C_GREEN : d == Decision::Partial ? C_YELLOW : C_RED;
}

ImVec4 scoreColor(int score, const Params& p) { return decisionColor(decide(score, p)); }

void TextRight(const std::string& s, const ImVec4& col, ImFont* font) {
    if (font) ImGui::PushFont(font);
    float off = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(s.c_str()).x;
    if (off > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
    ImGui::TextColored(col, "%s", s.c_str());
    if (font) ImGui::PopFont();
}

bool AccentButton(const char* label, const ImVec4& bg, const ImVec4& fg, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(bg.x * 1.15f, bg.y * 1.15f, bg.z * 1.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(bg.x * 0.9f, bg.y * 0.9f, bg.z * 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

void Tooltip(const char* text) {
    if (text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void HelpMarker(const char* text) {
    ImGui::TextColored(C_DIM, "(?)");
    Tooltip(text);
}

void StatTile(const char* label, const std::string& value, const ImVec4& col,
              const char* tooltip, float width) {
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::BeginChild(label, ImVec2(width, ImGui::GetTextLineHeight() + fBig->FontSize + 22),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(C_DIM, "%s", label);
    ImGui::PushFont(fBig);
    ImGui::TextColored(col, "%s", value.c_str());
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    Tooltip(tooltip);
}

void Pill(const char* text, const ImVec4& col) {
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(ts.x + 18, ts.y + 6);
    ImVec4 bg = col;
    bg.w = 0.20f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::ColorConvertFloat4ToU32(bg),
                      size.y * 0.5f);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::ColorConvertFloat4ToU32(col),
                size.y * 0.5f);
    dl->AddText(ImVec2(p.x + 9, p.y + 3), ImGui::ColorConvertFloat4ToU32(col), text);
    ImGui::Dummy(size);
}

// ---- chart ------------------------------------------------------------------

namespace {

// a round-ish axis step: 1, 2 or 5 times a power of ten
double niceStep(double range, int maxTicks) {
    if (!(range > 0)) return 1.0;
    double raw = range / std::max(1, maxTicks);
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 5.0})
        if (mag * m >= raw) return mag * m;
    return mag * 10.0;
}

// `step` is the gap between two ticks: it decides how many decimals the whole
// axis needs, so the labels do not mix "8,00" with "16"
std::string axisLabel(double v, const char* unit, double step) {
    int dec = step < 0.1 ? 2 : step < 1 ? 1 : 0;
    std::string s = fmtNum(v, dec);
    return unit && *unit ? s + unit : s;
}

}  // namespace

void drawChart(const char* id, ImVec2 size, const std::vector<ChartSeries>& series,
               const ChartOpts& opts) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_DARK);
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();

    bool anyData = false;
    double xLo = 1e18, xHi = -1e18, yLo = 1e18, yHi = -1e18;
    for (auto& s : series) {
        for (size_t i = 0; i < s.x.size() && i < s.y.size(); i++) {
            if (!std::isfinite(s.y[i])) continue;
            anyData = true;
            xLo = std::min(xLo, s.x[i]);
            xHi = std::max(xHi, s.x[i]);
            yLo = std::min(yLo, s.y[i]);
            yHi = std::max(yHi, s.y[i]);
        }
    }
    if (!anyData) {
        ImGui::TextColored(C_DIM, "Sin datos que dibujar.");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }
    for (auto& s : series)
        if (s.fillToZero) {
            yLo = std::min(yLo, 0.0);
            yHi = std::max(yHi, 0.0);
        }

    int labelled = 0;
    for (auto& s : series)
        if (!s.label.empty()) labelled++;
    float legendH = labelled > 1 ? ImGui::GetTextLineHeightWithSpacing() + 6 : 0;
    ImVec2 pl0(p0.x + 64, p0.y + 10);
    ImVec2 pl1(p0.x + avail.x - 16, p0.y + avail.y - 24 - legendH);
    if (pl1.x <= pl0.x + 40 || pl1.y <= pl0.y + 40) {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    double step, gLo, gHi;
    if (opts.fixedY) {
        gLo = opts.yMin;
        gHi = opts.yMax;
        step = niceStep(gHi - gLo, 6);
    } else {
        step = niceStep((yHi - yLo) * 1.08, 6);
        gLo = std::floor(yLo / step) * step;
        gHi = std::ceil((yHi + step * 0.15) / step) * step;
    }
    if (gHi <= gLo) gHi = gLo + 1.0;
    double spanX = std::max(1.0, xHi - xLo);

    auto px = [&](double x) { return pl0.x + (float)((x - xLo) / spanX) * (pl1.x - pl0.x); };
    auto py = [&](double v) {
        return pl1.y - (float)((v - gLo) / (gHi - gLo)) * (pl1.y - pl0.y);
    };

    ImU32 cGrid = ImGui::ColorConvertFloat4ToU32(rgb(42, 50, 61));
    ImU32 cDim = ImGui::ColorConvertFloat4ToU32(C_DIM);
    ImU32 cZero = ImGui::ColorConvertFloat4ToU32(rgb(95, 106, 120));

    // shaded spans go under everything else
    for (auto& b : opts.bands) {
        double x0 = std::max(b.x0, xLo), x1 = std::min(b.x1, xHi);
        if (x1 <= x0) continue;
        ImVec4 c = b.color;
        c.w = 0.13f;
        dl->AddRectFilled(ImVec2(px(x0), pl0.y), ImVec2(px(x1), pl1.y),
                          ImGui::ColorConvertFloat4ToU32(c));
    }

    // y grid
    for (double v = gLo; v <= gHi + step * 0.01; v += step) {
        float y = py(v);
        dl->AddLine(ImVec2(pl0.x, y), ImVec2(pl1.x, y),
                    std::fabs(v) < step * 0.01 ? cZero : cGrid);
        std::string lab = axisLabel(v, opts.unit, step);
        float tw = ImGui::CalcTextSize(lab.c_str()).x;
        dl->AddText(ImVec2(pl0.x - tw - 8, y - ImGui::GetTextLineHeight() * 0.5f), cDim,
                    lab.c_str());
    }

    // x grid: one label per year while they fit, else one per month
    {
        Date from = dateFromSerial((int)xLo), to = dateFromSerial((int)xHi);
        int months = (to.y * 12 + to.m) - (from.y * 12 + from.m);
        float roomFor = (pl1.x - pl0.x) / 70.0f;
        int stepMonths = 120;
        for (int cand : {1, 3, 6, 12, 24, 60, 120})
            if ((float)months / cand <= roomFor) {
                stepMonths = cand;
                break;
            }
        int firstOrd = monthOrdinal(from.y, from.m);
        if (stepMonths >= 12) {
            int y = from.y + (from.m > 1 ? 1 : 0), every = stepMonths / 12;
            while (y % every) y++;
            firstOrd = monthOrdinal(y, 1);
        }
        for (int ord = firstOrd; ord <= monthOrdinal(to.y, to.m); ord += stepMonths) {
            double d = serialOfMonthDay(ord, 1);
            if (d < xLo || d > xHi) continue;
            float x = px(d);
            dl->AddLine(ImVec2(x, pl0.y), ImVec2(x, pl1.y), cGrid);
            std::string lab = stepMonths >= 12 ? std::to_string(ord / 12) : monthStr(ord);
            float tw = ImGui::CalcTextSize(lab.c_str()).x;
            dl->AddText(ImVec2(x - tw * 0.5f, pl1.y + 6), cDim, lab.c_str());
        }
    }

    // horizontal references
    for (auto& l : opts.lines) {
        if (l.y < gLo || l.y > gHi) continue;
        float y = py(l.y);
        ImVec4 c = l.color;
        c.w = 0.75f;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(c);
        for (float x = pl0.x; x < pl1.x; x += 10) // dashed
            dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + 5, pl1.x), y), col);
        if (!l.label.empty()) {
            float tw = ImGui::CalcTextSize(l.label.c_str()).x;
            dl->AddText(ImVec2(pl1.x - tw - 6, y - ImGui::GetTextLineHeight() - 1), col,
                        l.label.c_str());
        }
    }
    dl->AddRect(pl0, pl1, ImGui::ColorConvertFloat4ToU32(C_BORDER));

    // the series themselves
    dl->PushClipRect(pl0, ImVec2(pl1.x + 1, pl1.y + 1), true);
    std::vector<ImVec2> line;
    for (auto& s : series) {
        size_t n = std::min(s.x.size(), s.y.size());
        if (n < 2) continue;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(s.color);
        if (s.fillToZero) {
            ImVec4 faded = s.color;
            faded.w = 0.18f;
            ImU32 fc = ImGui::ColorConvertFloat4ToU32(faded);
            float zero = py(std::clamp(0.0, gLo, gHi));
            for (size_t i = 1; i < n; i++) {
                ImVec2 a(px(s.x[i - 1]), py(s.y[i - 1])), b(px(s.x[i]), py(s.y[i]));
                dl->AddQuadFilled(a, b, ImVec2(b.x, zero), ImVec2(a.x, zero), fc);
            }
        }
        line.clear();
        for (size_t i = 0; i < n; i++) {
            if (!std::isfinite(s.y[i])) continue;
            ImVec2 pt(px(s.x[i]), py(s.y[i]));
            if (s.stepped && !line.empty()) line.push_back(ImVec2(pt.x, line.back().y));
            line.push_back(pt);
        }
        dl->AddPolyline(line.data(), (int)line.size(), col, 0, s.thickness);
    }
    dl->PopClipRect();

    // legend
    if (legendH > 0) {
        float x = pl0.x;
        float y = pl1.y + 24;
        for (auto& s : series) {
            if (s.label.empty()) continue;
            dl->AddRectFilled(ImVec2(x, y + 5), ImVec2(x + 11, y + 14),
                              ImGui::ColorConvertFloat4ToU32(s.color), 2.0f);
            dl->AddText(ImVec2(x + 16, y), ImGui::ColorConvertFloat4ToU32(C_TEXT),
                        s.label.c_str());
            x += 16 + ImGui::CalcTextSize(s.label.c_str()).x + 22;
        }
    }

    // hover: a crosshair plus every series read at that date
    if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(pl0, pl1)) {
        float mx = ImGui::GetIO().MousePos.x;
        double atX = xLo + (double)((mx - pl0.x) / (pl1.x - pl0.x)) * spanX;
        dl->AddLine(ImVec2(mx, pl0.y), ImVec2(mx, pl1.y), cDim);
        ImGui::BeginTooltip();
        bool first = true;
        for (auto& s : series) {
            size_t n = std::min(s.x.size(), s.y.size());
            if (n == 0) continue;
            // a stepped series holds its last value; a line reads the nearest
            size_t best = (size_t)(std::lower_bound(s.x.begin(), s.x.begin() + (long)n, atX) -
                                   s.x.begin());
            if (best >= n) best = n - 1;
            if (best > 0 && (s.stepped ? s.x[best] > atX
                                       : atX - s.x[best - 1] < s.x[best] - atX))
                best--;
            if (!std::isfinite(s.y[best])) continue;
            if (first) {
                ImGui::TextColored(C_DIM, "%s", dateStr((int)std::lround(atX)).c_str());
                first = false;
            }
            dl->AddCircleFilled(ImVec2(px(s.x[best]), py(s.y[best])), 4.0f,
                                ImGui::ColorConvertFloat4ToU32(s.color));
            ImGui::TextColored(s.color, "%s", s.label.empty() ? "valor" : s.label.c_str());
            ImGui::SameLine(ImGui::GetFontSize() * 13.0f);
            ImGui::TextColored(s.color, "%s%s", fmtNum(s.y[best], opts.decimals).c_str(),
                               opts.unit);
        }
        ImGui::EndTooltip();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---- the data cycle ---------------------------------------------------------

std::string paramsPath(const App& a) { return a.dataRoot + "renta-fija.cfg"; }

void reloadData(App& a) {
    loadSeries(a.dataRoot, a.series);
    a.mk = buildMarket(a.series);
    a.sig.clear();
}

void ensureComputed(App& a) {
    std::string sig = a.p.signature() + "#" + std::to_string(a.mk.size()) + "#" +
                      (a.mk.empty() ? "" : std::to_string(a.mk.days.back()));
    if (sig == a.sig) return;
    bool first = a.sig.empty();
    a.sig = sig;
    a.ind = computeAll(a.mk, a.p);
    a.ix = buildIndices(a.mk, a.p);
    a.study = studySignal(a.mk, a.ix, a.ind, a.p);
    a.strat[0] = simulate(a.mk, a.ix, a.ind, a.p, Strategy::Signal, &a.trades);
    a.strat[1] = simulate(a.mk, a.ix, a.ind, a.p, Strategy::AllMM);
    a.strat[2] = simulate(a.mk, a.ix, a.ind, a.p, Strategy::AllBond);
    a.strat[3] = simulate(a.mk, a.ix, a.ind, a.p, Strategy::Half);
    a.grid = thresholdGrid(a.mk, a.ix, a.ind, a.p);
    // a changed parameter is kept for the next run; a mere reload is not a
    // reason to touch the file
    if (!first) a.p.save(paramsPath(a));
}

void startRefresh(App& a, bool full) {
    if (a.updater.busy()) return;
    a.updater.start(a.dataRoot, full);
}

void applyUpdate(App& a, const std::vector<UpdateOutcome>& outcomes) {
    a.lastUpdate = outcomes;
    time_t now = time(nullptr);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", localtime(&now));
    a.lastUpdateAt = buf;

    int failed = 0, added = 0;
    std::string firstError;
    for (auto& o : outcomes) {
        if (!o.error.empty()) {
            if (!failed++) firstError = std::string(seriesInfo(o.id).label) + ": " + o.error;
        }
        added = std::max(added, o.added);
    }
    reloadData(a);
    if (failed) {
        setStatus(a, "BCE: " + std::to_string(failed) + " series sin actualizar (" + firstError +
                         ")", true);
    } else if (a.mk.empty()) {
        setStatus(a, "BCE: la descarga no dejó datos utilizables", true);
    } else {
        setStatus(a, (added ? "BCE: " + std::to_string(added) + " días nuevos; "
                            : std::string("BCE: sin novedades; ")) +
                         "curva a " + dateStr(a.mk.days.back()));
    }
}

bool exportObservations(App& a, std::string* path) {
    *path = a.dataRoot + "observaciones.csv";
    std::ofstream f(*path, std::ios::trunc);
    if (!f) return false;
    // semicolons and decimal commas: what a Spanish spreadsheet opens as-is
    f << "fecha;estr;aaa_3m;aaa_2a;aaa_10a;pendiente_pb;var_2a_1m_pb;var_2a_3m_pb;"
         "var_pendiente_3m_pb;exp_6m_pb;exp_12m_pb;prima_pp;var_10a_3m_pb;"
         "ind1;ind2;ind3;ind4;ind5;puntuacion;decision;"
         "exceso_6m_pp;exceso_12m_pp;exceso_24m_pp\n";
    auto num = [](double v, int dec) { return std::isfinite(v) ? fmtNum(v, dec) : std::string(); };
    for (auto& o : a.study.obs) {
        const Indicators& in = a.ind[o.idx];
        f << dateStr(o.day) << ';' << num(in.on, 3) << ';' << num(in.y3m, 3) << ';'
          << num(in.y2, 3) << ';' << num(in.y10, 3) << ';' << num(in.slope, 1) << ';'
          << num(in.d2y1m, 1) << ';' << num(in.d2y3m, 1) << ';' << num(in.dSlope3m, 1) << ';'
          << num(in.exp6, 1) << ';' << num(in.exp12, 1) << ';' << num(in.premium, 3) << ';'
          << num(in.d10y3m, 1) << ';';
        for (int k = 0; k < 5; k++) f << in.st[k] << ';';
        f << in.score << ';'
          << (entryBlocked(in, a.p) ? "Prima insuficiente" : decisionName(decide(in, a.p)))
          << ';' << num(o.excess[0], 3)
          << ';' << num(o.excess[1], 3) << ';' << num(o.excess[2], 3) << '\n';
    }
    return (bool)f;
}

// ---- shell ------------------------------------------------------------------

void drawUI(App& a) {
    ensureComputed(a);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    if (!ImGui::GetIO().WantTextInput) {
        for (int i = 0; i < kTabs; i++)
            if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i))) a.forceTab = i;
        if (ImGui::IsKeyPressed(ImGuiKey_F5)) startRefresh(a);
    }

    ImGui::PushFont(fHuge);
    ImGui::TextColored(C_GREEN, "renta fija");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(C_DIM, "cuándo pasar del monetario al bono a 2 años");
    ImGui::SameLine();
    {
        float w = 170.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
        bool busy = a.updater.busy();
        ImGui::BeginDisabled(busy);
        if (AccentButton(busy ? "Actualizando..." : "Actualizar datos", C_BLUE, C_DARK,
                         ImVec2(w, 0)))
            startRefresh(a);
        ImGui::EndDisabled();
        Tooltip("Pide al BCE lo publicado desde el último día en caché (F5)");
    }

    float statusH = ImGui::GetTextLineHeightWithSpacing() + 6;
    if (ImGui::BeginTabBar("tabs")) {
        const char* names[kTabs] = {"Monitor", "Histórico", "Señal", "Estrategia",
                                    "Umbrales", "Datos"};
        for (int i = 0; i < kTabs; i++) {
            ImGuiTabItemFlags flags = a.forceTab == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(names[i], nullptr, flags)) {
                ImGui::BeginChild("content", ImVec2(0, -statusH));
                if (a.mk.empty() && i != kTabs - 1) {
                    ImGui::Dummy(ImVec2(0, 30));
                    ImGui::PushFont(fBig);
                    ImGui::TextColored(C_YELLOW, a.updater.busy()
                                                     ? "Descargando el histórico del BCE..."
                                                     : "Todavía no hay datos.");
                    ImGui::PopFont();
                    ImGui::TextColored(C_DIM,
                                       a.updater.busy()
                                           ? "La primera vez son unos 6 MB: curva AAA desde 2004, "
                                             "€STR y EONIA."
                                           : "Pulsa «Actualizar datos» para descargar las series "
                                             "del BCE.");
                } else if (i == 0) tabMonitor(a);
                else if (i == 1) tabHistory(a);
                else if (i == 2) tabSignal(a);
                else if (i == 3) tabStrategy(a);
                else if (i == 4) tabThresholds(a);
                else tabData(a);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    a.forceTab = -1;

    ImGui::Separator();
    if (a.updater.busy()) {
        std::string p = a.updater.progress();
        ImGui::TextColored(C_YELLOW, "BCE: %s", p.empty() ? "conectando..." : p.c_str());
    } else if (!a.status.empty() && SDL_GetTicks() - a.statusAt < (a.statusBad ? 20000u : 8000u)) {
        ImGui::TextColored(a.statusBad ? C_RED : C_GREEN, "%s", a.status.c_str());
    } else {
        ImGui::TextUnformatted(" ");
    }
    ImGui::SameLine();
    std::string right = "1-7: pestañas  |  F5: actualizar";
    if (!a.mk.empty()) right += "  |  curva del BCE a " + dateStr(a.mk.days.back());
    TextRight(right, C_DIM);

    ImGui::End();
}

void applyStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 0;
    st.ChildRounding = 6;
    st.FrameRounding = 4;
    st.PopupRounding = 6;
    st.TabRounding = 4;
    st.GrabRounding = 4;
    st.ScrollbarSize = 12;
    // the input background sits only a few levels away from the window's, so
    // without an outline a field - and an unticked checkbox - is invisible
    st.FrameBorderSize = 1.0f;
    st.WindowPadding = ImVec2(16, 12);
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(10, 8);
    st.CellPadding = ImVec2(6, 5);

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = C_BG;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = C_PANEL;
    c[ImGuiCol_Border] = C_BORDER;
    c[ImGuiCol_Text] = C_TEXT;
    c[ImGuiCol_TextDisabled] = C_DIM;
    c[ImGuiCol_FrameBg] = C_DARK;
    c[ImGuiCol_FrameBgHovered] = C_PANEL2;
    c[ImGuiCol_FrameBgActive] = C_PANEL2;
    c[ImGuiCol_Button] = C_PANEL2;
    c[ImGuiCol_ButtonHovered] = rgb(52, 62, 74);
    c[ImGuiCol_ButtonActive] = rgb(46, 56, 68);
    c[ImGuiCol_Header] = C_PANEL2;
    c[ImGuiCol_HeaderHovered] = rgb(44, 53, 64);
    c[ImGuiCol_HeaderActive] = rgb(50, 60, 72);
    c[ImGuiCol_Tab] = C_PANEL2;
    c[ImGuiCol_TabHovered] = rgb(40, 120, 80);
    c[ImGuiCol_TabSelected] = rgb(30, 132, 73);
    c[ImGuiCol_TabSelectedOverline] = C_GREEN;
    c[ImGuiCol_TableHeaderBg] = C_PANEL2;
    c[ImGuiCol_TableRowBg] = rgb(25, 30, 37);
    c[ImGuiCol_TableRowBgAlt] = C_BG;
    c[ImGuiCol_TableBorderLight] = rgb(40, 48, 58);
    c[ImGuiCol_TableBorderStrong] = C_BORDER;
    c[ImGuiCol_Separator] = C_BORDER;
    c[ImGuiCol_CheckMark] = C_GREEN;
    c[ImGuiCol_SliderGrab] = C_GREEN;
    c[ImGuiCol_SliderGrabActive] = C_GREEN;
    c[ImGuiCol_ScrollbarBg] = C_BG;
    c[ImGuiCol_ScrollbarGrab] = C_BORDER;
    c[ImGuiCol_TitleBg] = C_PANEL2;
    c[ImGuiCol_TitleBgActive] = C_PANEL2;
    c[ImGuiCol_TitleBgCollapsed] = C_PANEL2;
    c[ImGuiCol_NavCursor] = C_GREEN;
}

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* reg = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    const char* bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    auto exists = [](const char* p) {
        if (FILE* f = fopen(p, "rb")) {
            fclose(f);
            return true;
        }
        return false;
    };
    // Latin-1 covers the Spanish accents; the euro sign, the true minus and
    // the comparison signs live further up and have to be asked for
    static ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddText("€−≥≤«»…·→Δ★");
    builder.BuildRanges(&ranges);
    if (exists(reg) && exists(bold)) {
        fBody = io.Fonts->AddFontFromFileTTF(reg, 17.0f, nullptr, ranges.Data);
        fBig = io.Fonts->AddFontFromFileTTF(bold, 23.0f, nullptr, ranges.Data);
        fHuge = io.Fonts->AddFontFromFileTTF(bold, 34.0f, nullptr, ranges.Data);
    }
    if (!fBody) fBody = io.Fonts->AddFontDefault();
    if (!fBig) fBig = fBody;
    if (!fHuge) fHuge = fBody;
}
