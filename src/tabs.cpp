// The six views.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "ui.h"

namespace {

const ImGuiTableFlags kTable = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                               ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchProp;

void cellRight(const std::string& s, const ImVec4& col = C_TEXT) {
    ImGui::TableNextColumn();
    TextRight(s, col);
}

void cellSigned(double v, int dec, const char* unit = "") {
    ImGui::TableNextColumn();
    if (!std::isfinite(v)) TextRight("-", C_DIM);
    else TextRight(fmtSigned(v, dec) + unit, signColor(v));
}

// tints the current cell green or red in proportion to v / scale
void tintCell(double v, double scale) {
    if (!std::isfinite(v) || scale <= 0) return;
    ImVec4 c = v >= 0 ? C_GREEN : C_RED;
    c.w = (float)std::min(0.55, 0.08 + 0.47 * std::fabs(v) / scale);
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::ColorConvertFloat4ToU32(c));
}

void sectionTitle(const char* text, const char* help = nullptr) {
    ImGui::TextColored(C_TEXT, "%s", text);
    if (help) {
        ImGui::SameLine();
        HelpMarker(help);
    }
}

// a daily series of one figure per market row, from index `from`
template <class Fn>
ChartSeries daily(const App& a, size_t from, const char* label, const ImVec4& col, Fn value) {
    ChartSeries s;
    s.label = label;
    s.color = col;
    s.x.reserve(a.mk.size() - from);
    s.y.reserve(a.mk.size() - from);
    for (size_t i = from; i < a.mk.size(); i++) {
        s.x.push_back(a.mk.days[i]);
        s.y.push_back(value(i));
    }
    return s;
}

const double kNaN = std::nan("");

ChartSeries scoreSeries(const App& a, size_t from) {
    ChartSeries s = daily(a, from, "Puntuación", C_TEAL, [&](size_t i) {
        return a.ind[i].valid ? (double)a.ind[i].score : kNaN;
    });
    s.stepped = true;
    return s;
}

ChartOpts scoreOpts(const App& a) {
    ChartOpts o;
    o.unit = "";
    o.decimals = 0;
    o.fixedY = true;
    o.yMin = 0;
    o.yMax = 10;
    o.lines.push_back({(double)a.p.fullScore, C_GREEN, "entrada completa"});
    if (a.p.partialScore < a.p.fullScore)
        o.lines.push_back({(double)a.p.partialScore, C_YELLOW, "entrada parcial"});
    if (a.p.exitScore < a.p.partialScore)
        o.lines.push_back({(double)a.p.exitScore, C_RED, "salida"});
    return o;
}

size_t rangeStart(const App& a, int rangeIdx) {
    static const int years[] = {1, 3, 5, 10, 0};
    int y = years[std::clamp(rangeIdx, 0, 4)];
    if (!y || a.mk.empty()) return 0;
    int idx = a.mk.indexAsOf(a.mk.days.back() - (int)(y * 365.25));
    return (size_t)std::max(idx, 0);
}

void rangeButtons(int* rangeIdx) {
    static const char* names[] = {"1 año", "3 años", "5 años", "10 años", "Todo"};
    for (int i = 0; i < 5; i++) {
        if (i) ImGui::SameLine(0, 4);
        bool on = *rangeIdx == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, rgb(30, 132, 73));
        if (ImGui::Button(names[i])) *rangeIdx = i;
        if (on) ImGui::PopStyleColor();
    }
}

std::string rule(const Params& p, int k) {
    switch (k) {
        case 0:
            return "Favorable ≤ " + fmtBp(p.t2yFav) + " · desfavorable ≥ " + fmtBp(p.t2yUnf) +
                   " en 3 meses";
        case 1:
            return "Cuenta como movimiento a partir de " + fmtNum(p.slopeThr, 0) +
                   " pb en 3 meses";
        case 2:
            return "Favorable ≤ " + fmtBp(p.expFav) + " · desfavorable ≥ " + fmtBp(p.expUnf) +
                   " a 12 meses";
        case 3:
            return "Favorable > " + fmtSigned(p.premFav, 2) + " pp · desfavorable < " +
                   fmtSigned(p.premUnf, 2) + " pp";
        default:
            return "Favorable ≤ " + fmtBp(p.t10Fav) + " · desfavorable > " + fmtBp(p.t10Unf) +
                   " en 3 meses";
    }
}

std::string headline(const Indicators& in, int k) {
    switch (k) {
        case 0: return fmtBp(in.d2y3m);
        case 1: return fmtBp(in.slope);
        case 2: return fmtBp(in.exp12);
        case 3: return fmtSigned(in.premium, 2) + " pp";
        default: return fmtBp(in.d10y3m);
    }
}

const char* headlineCaption(int k) {
    static const char* c[5] = {"variación en 3 meses", "nivel de la pendiente",
                               "movimiento descontado a 12 meses", "ventaja estimada a 12 meses",
                               "variación en 3 meses"};
    return c[k];
}

const char* indicatorHelp(int k) {
    static const char* h[5] = {
        "El indicador con más peso del método. Si el rendimiento del 2 años sigue subiendo, el "
        "fondo sigue perdiendo valor; si se estabiliza o baja, la presión desaparece. La señal "
        "es la variación en 3 meses y el último mes sirve de confirmación.",
        "Rendimiento a 2 años menos rendimiento a 3 meses. Importa por qué se mueve: que "
        "aumente porque bajan los tipos cortos es favorable; que aumente porque el 2 años sube "
        "más deprisa no lo es.",
        "Tipo a 3 meses que la curva AAA lleva implícito para dentro de 6 y 12 meses, comparado "
        "con el de hoy. Es una aproximación a los forward OIS del €STR, que no se publican "
        "gratis: mide lo mismo (qué descuenta el mercado), pero sobre deuda pública.",
        "Rendimiento neto del fondo de renta fija menos lo que se espera que pague el monetario "
        "en los próximos 12 meses (el €STR de hoy más el movimiento que descuenta la curva), "
        "ambos netos de gastos.",
        "Indicador de contexto. Si el 2 años se calma pero el 10 años sigue subiendo, persisten "
        "los riesgos de inflación, prima por plazo u oferta de deuda: no impide entrar, pero "
        "aconseja prudencia."};
    return h[k];
}

// ---- the parameter panel shared by the three backtest views -----------------

bool numField(const char* label, double* v, double step, const char* fmt,
              const char* help = nullptr) {
    ImGui::SetNextItemWidth(128);
    bool ch = ImGui::InputDouble(label, v, step, step * 5, fmt);
    Tooltip(help);
    return ch;
}

void paramsPanel(App& a, int focus) {  // focus: 0 signal, 1 strategy, 2 thresholds
    Params& p = a.p;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::BeginChild("params", ImVec2(338, 0), ImGuiChildFlags_Borders);
    auto open = [&](bool wanted) { return wanted ? ImGuiTreeNodeFlags_DefaultOpen : 0; };

    if (ImGui::CollapsingHeader("Periodo y revisión", open(true))) {
        int y0 = dateFromSerial(a.mk.days.front()).y, y1 = dateFromSerial(a.mk.days.back()).y;
        int from = p.startYear ? std::clamp(p.startYear, y0, y1) : y0;
        int to = p.endYear ? std::clamp(p.endYear, y0, y1) : y1;
        ImGui::SetNextItemWidth(128);
        if (ImGui::SliderInt("Desde", &from, y0, y1)) to = std::max(to, from);
        ImGui::SetNextItemWidth(128);
        if (ImGui::SliderInt("Hasta", &to, y0, y1)) from = std::min(from, to);
        Tooltip("Acota el backtest. Sirve para ajustar los umbrales con unos años y comprobar "
                "después si siguen funcionando en los otros.");
        p.startYear = from == y0 ? 0 : from;
        p.endYear = to == y1 ? 0 : to;
        ImGui::TextColored(C_DIM, "Revisión");
        ImGui::SameLine();
        ImGui::RadioButton("mensual", &p.freq, 0);
        ImGui::SameLine();
        ImGui::RadioButton("semanal", &p.freq, 1);
    }

    if (ImGui::CollapsingHeader("Regla de decisión", open(focus != 2))) {
        ImGui::SetNextItemWidth(128);
        ImGui::SliderInt("Entrada completa ≥", &p.fullScore, 1, 10);
        p.partialScore = std::min(p.partialScore, p.fullScore);
        ImGui::SetNextItemWidth(128);
        ImGui::SliderInt("Entrada parcial ≥", &p.partialScore, 1, p.fullScore);
        Tooltip("Igual que la entrada completa: sin escalón parcial.");
        ImGui::SetNextItemWidth(128);
        ImGui::SliderInt("Salida ≤", &p.exitScore, 0, 9);
        Tooltip("Con la puntuación en este nivel o por debajo se vuelve al monetario. Entre "
                "la salida y la entrada parcial se mantiene lo que haya.");
        numField("% en parcial", &p.partialPct, 5, "%.0f");
        p.partialPct = std::clamp(p.partialPct, 0.0, 100.0);
        numField("Prima mínima para entrar (pp)", &p.premEntry, 0.05, "%.2f",
                 "Aunque la puntuación llegue a la entrada, no se compra renta fija si la "
                 "prima frente al monetario está por debajo de esto. No afecta a la salida. "
                 "Con -0,30 se evitan las entradas de 2024-25, cuando el bono rendía mucho "
                 "menos que el monetario, y se mantiene la de finales de 2008. Por encima de "
                 "-0,15 se pierde esa entrada; con -5 el filtro queda desactivado.");
        numField("Coste de cambio (pb)", &p.switchCostBp, 1, "%.0f",
                 "Coste de mover el 100 % de la cartera de un fondo al otro, en puntos "
                 "básicos. Entre fondos traspasables suele ser cero.");
        p.switchCostBp = std::clamp(p.switchCostBp, 0.0, 200.0);
    }

    if (ImGui::CollapsingHeader("Fondos modelados", open(focus != 2))) {
        numField("Duración (años)", &p.duration, 0.25, "%.2f",
                 "Duración del fondo de renta fija. Se simula como un bono cupón cero de ese "
                 "plazo renovado cada día sobre la curva AAA: cupón, descenso por la curva y "
                 "variación de precio. Pon aquí la duración modificada de tu fondo.");
        p.duration = std::clamp(p.duration, 0.5, 5.0);
        numField("Diferencial (pp)", &p.fundSpread, 0.05, "%.2f",
                 "Rentabilidad adicional del fondo sobre la curva AAA: crédito, periferia. "
                 "TIR de la cartera del fondo menos el tipo AAA al mismo plazo.");
        numField("Gastos RF (%)", &p.fundTer, 0.05, "%.2f");
        numField("Gastos monetario (%)", &p.mmTer, 0.05, "%.2f");
        p.fundTer = std::clamp(p.fundTer, 0.0, 3.0);
        p.mmTer = std::clamp(p.mmTer, 0.0, 3.0);
    }

    if (ImGui::CollapsingHeader("Umbrales de los indicadores", open(focus == 2))) {
        ImGui::TextColored(C_DIM, "1. 2 años, variación 3 meses (pb)");
        numField("favorable ≤##1", &p.t2yFav, 5, "%.0f");
        numField("desfavorable ≥##1", &p.t2yUnf, 5, "%.0f");
        ImGui::Checkbox("confirmar con el último mes", &p.confirm1m);
        Tooltip("Una caída en 3 meses no cuenta como favorable si en el último mes el 2 años "
                "ha vuelto a subir más de lo indicado debajo.");
        ImGui::BeginDisabled(!p.confirm1m);
        numField("subida máx. 1 mes", &p.t2yConfirm, 5, "%.0f");
        ImGui::EndDisabled();

        ImGui::TextColored(C_DIM, "2. Pendiente, movimiento mínimo (pb)");
        numField("en 3 meses##2", &p.slopeThr, 5, "%.0f");
        p.slopeThr = std::max(p.slopeThr, 1.0);

        ImGui::TextColored(C_DIM, "3. Expectativas a 12 meses (pb)");
        numField("favorable ≤##3", &p.expFav, 5, "%.0f");
        numField("desfavorable ≥##3", &p.expUnf, 5, "%.0f");

        ImGui::TextColored(C_DIM, "4. Prima frente al monetario (pp)");
        numField("favorable >##4", &p.premFav, 0.05, "%.2f");
        numField("desfavorable <##4", &p.premUnf, 0.05, "%.2f");

        ImGui::TextColored(C_DIM, "5. 10 años, variación 3 meses (pb)");
        numField("favorable ≤##5", &p.t10Fav, 5, "%.0f");
        numField("desfavorable >##5", &p.t10Unf, 5, "%.0f");
    }

    ImGui::Spacing();
    if (ImGui::Button("Restablecer valores del método", ImVec2(-1, 0))) {
        p = Params();
        setStatus(a, "Parámetros devueltos a los de explicacion.md");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

std::string periodLabel(const App& a) {
    size_t first, last;
    if (!periodRange(a.mk, a.p, first, last)) return "periodo vacío";
    return dateStr(a.mk.days[first]) + " a " + dateStr(a.mk.days[last]);
}

}  // namespace

// ---- 1. Monitor -------------------------------------------------------------

void tabMonitor(App& a) {
    const Indicators& in = a.ind.back();
    const Params& p = a.p;
    Decision dec = decide(in, p);
    bool blocked = entryBlocked(in, p);
    ImVec4 dcol = decisionColor(dec);
    float rowH = 156;

    // the verdict
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::BeginChild("verdict", ImVec2(340, rowH), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(C_DIM, "PUNTUACIÓN A %s", dateStr(a.mk.days.back()).c_str());
    ImGui::PushFont(fHuge);
    ImGui::TextColored(dcol, "%d", in.score);
    ImGui::SameLine(0, 6);
    ImGui::TextColored(C_DIM, "/ 10");
    ImGui::PopFont();
    ImGui::PushFont(fBig);
    ImGui::TextColored(dcol, "%s", decisionName(dec));
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0);
    if (blocked)
        ImGui::TextColored(C_DIM,
                           "La puntuación pide entrar, pero la prima (%s pp) está por debajo "
                           "del mínimo de %s pp: el bono rinde demasiado poco frente al "
                           "monetario.",
                           fmtSigned(in.premium, 2).c_str(), fmtSigned(p.premEntry, 2).c_str());
    else
    ImGui::TextColored(C_DIM, "%s",
                       dec == Decision::Full
                           ? "La mayoría de las señales favorecen la renta fija a 2 años."
                           : dec == Decision::Partial
                                 ? "Señales mixtas: cabe una entrada escalonada, sin "
                                   "comprometer todo el capital."
                                 : "Las señales no compensan todavía el riesgo de duración.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // levels and the weekly check
    ImGui::SameLine();
    ImGui::BeginChild("levels", ImVec2(0, rowH), 0, ImGuiWindowFlags_NoScrollbar);
    {
        float w = (ImGui::GetContentRegionAvail().x - 5 * ImGui::GetStyle().ItemSpacing.x) / 6;
        StatTile("€STR", fmtPct(in.on, 3), C_TEXT,
                 "Tipo a un día del euro: lo que rinde, antes de gastos, un fondo monetario.", w);
        ImGui::SameLine();
        StatTile("AAA 3 meses", fmtPct(in.y3m), C_TEXT,
                 "Rendimiento al contado de la deuda pública AAA de la zona euro a 3 meses.", w);
        ImGui::SameLine();
        StatTile("AAA 2 años", fmtPct(in.y2), C_BLUE, nullptr, w);
        ImGui::SameLine();
        StatTile("AAA 10 años", fmtPct(in.y10), C_PURPLE, nullptr, w);
        ImGui::SameLine();
        StatTile("Pendiente", fmtBp(in.slope), signColor(in.slope), nullptr, w);
        ImGui::SameLine();
        StatTile("Prima", fmtSigned(in.premium, 2) + " pp", signColor(in.premium),
                 "Rendimiento neto del fondo de renta fija menos el esperado del monetario a "
                 "12 meses.", w);

        ImGui::Spacing();
        ImGui::TextColored(C_DIM, "Control semanal:");
        ImGui::SameLine();
        ImGui::Text("el 2 años varía");
        ImGui::SameLine(0, 5);
        ImGui::TextColored(signColor(-in.d2y1w), "%s", fmtBp(in.d2y1w).c_str());
        ImGui::SameLine(0, 5);
        ImGui::Text("en 7 días,");
        ImGui::SameLine(0, 5);
        ImGui::TextColored(signColor(-in.d2y1m), "%s", fmtBp(in.d2y1m).c_str());
        ImGui::SameLine(0, 5);
        ImGui::Text("en un mes y");
        ImGui::SameLine(0, 5);
        ImGui::TextColored(signColor(-in.d2y3m), "%s", fmtBp(in.d2y3m).c_str());
        ImGui::SameLine(0, 5);
        ImGui::Text("en tres.");

        ImGui::TextColored(C_DIM, "Margen de seguridad:");
        ImGui::SameLine();
        if (in.premium > 0)
            ImGui::Text("una subida de tipos de %s pb en 12 meses anularía la prima.",
                        fmtNum(in.breakevenBp, 0).c_str());
        else
            ImGui::Text("no hay prima: el fondo rinde menos de lo que se espera del monetario.");
        ImGui::SameLine();
        HelpMarker("Aproximación para una subida repartida a lo largo del año: cuesta "
                   "duración × subida en precio y devuelve en torno a la mitad en cupón.");

        // days since the last curve: the ECB publishes with one working day's lag
        time_t now = time(nullptr);
        int today = (int)(now / 86400);
        int age = today - a.mk.days.back();
        if (age > 5)
            ImGui::TextColored(C_ORANGE,
                               "Datos de hace %d días: pulsa «Actualizar datos» (F5).", age);
    }
    ImGui::EndChild();

    // the five cards
    float cardH = 248;
    float cw = (ImGui::GetContentRegionAvail().x - 4 * ImGui::GetStyle().ItemSpacing.x) / 5;
    for (int k = 0; k < 5; k++) {
        if (k) ImGui::SameLine();
        ImGui::PushID(k);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
        ImGui::BeginChild("card", ImVec2(cw, cardH), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::Text("%d. %s", k + 1, indicatorName(k));
        Tooltip(indicatorHelp(k));
        if (in.valid) {
            Pill(stateName(in.st[k]), stateColor(in.st[k]));
            ImGui::SameLine();
            TextRight(std::to_string(in.st[k]) + " / 2", C_DIM);
            ImGui::PushFont(fBig);
            ImGui::TextColored(stateColor(in.st[k]), "%s", headline(in, k).c_str());
            ImGui::PopFont();
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(C_DIM, "%s", headlineCaption(k));
            ImGui::TextUnformatted(indicatorReading(in, k).c_str());
            ImGui::TextColored(C_DIM, "%s", rule(p, k).c_str());
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextColored(C_DIM, "Hacen falta 3 meses de histórico.");
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    // how the score got here
    sectionTitle("Puntuación de los últimos 5 años",
                 "La puntuación diaria con los umbrales actuales. Las líneas discontinuas son "
                 "los niveles de la regla de decisión.");
    size_t from = rangeStart(a, 2);
    drawChart("scoreChart", ImVec2(0, 0), {scoreSeries(a, from)}, scoreOpts(a));
}

// ---- 2. Histórico -----------------------------------------------------------

void tabHistory(App& a) {
    static const char* charts[] = {"Tipos", "Pendiente", "Expectativas", "Prima",
                                   "Tendencias", "Fondos"};
    for (int i = 0; i < 6; i++) {
        if (i) ImGui::SameLine();
        ImGui::RadioButton(charts[i], &a.histChart, i);
    }
    ImGui::SameLine(0, 40);
    rangeButtons(&a.histRange);

    size_t from = rangeStart(a, a.histRange);
    const Market& mk = a.mk;
    const auto& ind = a.ind;
    const Params& p = a.p;
    std::vector<ChartSeries> s;
    ChartOpts o;
    auto valid = [&](size_t i, double v) { return ind[i].valid ? v : kNaN; };

    switch (a.histChart) {
        case 0:
            o.unit = " %";
            s.push_back(daily(a, from, "€STR", C_GRAY, [&](size_t i) { return mk.on[i]; }));
            s.push_back(daily(a, from, "AAA 3 meses", C_TEAL, [&](size_t i) { return ind[i].y3m; }));
            s.push_back(daily(a, from, "AAA 2 años", C_BLUE, [&](size_t i) { return ind[i].y2; }));
            s.push_back(daily(a, from, "AAA 10 años", C_PURPLE, [&](size_t i) { return ind[i].y10; }));
            break;
        case 1:
            o.unit = " pb";
            o.decimals = 0;
            s.push_back(daily(a, from, "Pendiente 2 años − 3 meses", C_BLUE,
                              [&](size_t i) { return ind[i].slope; }));
            s.back().fillToZero = true;
            break;
        case 2:
            o.unit = " pb";
            o.decimals = 0;
            s.push_back(daily(a, from, "Movimiento descontado a 6 meses", C_TEAL,
                              [&](size_t i) { return ind[i].exp6; }));
            s.push_back(daily(a, from, "a 12 meses", C_ORANGE,
                              [&](size_t i) { return ind[i].exp12; }));
            o.lines.push_back({p.expFav, C_GREEN, "favorable"});
            o.lines.push_back({p.expUnf, C_RED, "desfavorable"});
            break;
        case 3:
            o.unit = " pp";
            s.push_back(daily(a, from, "Prima frente al monetario", C_BLUE,
                              [&](size_t i) { return ind[i].premium; }));
            s.back().fillToZero = true;
            o.lines.push_back({p.premFav, C_GREEN, "favorable"});
            if (p.premUnf != 0) o.lines.push_back({p.premUnf, C_RED, "desfavorable"});
            break;
        case 4:
            o.unit = " pb";
            o.decimals = 0;
            s.push_back(daily(a, from, "2 años, variación en 3 meses", C_BLUE,
                              [&](size_t i) { return valid(i, ind[i].d2y3m); }));
            s.push_back(daily(a, from, "10 años, variación en 3 meses", C_PURPLE,
                              [&](size_t i) { return valid(i, ind[i].d10y3m); }));
            o.lines.push_back({p.t2yFav, C_GREEN, "2 años favorable"});
            o.lines.push_back({p.t2yUnf, C_RED, "2 años desfavorable"});
            break;
        default: {
            double b0 = a.ix.bond[from], m0 = a.ix.mm[from];
            s.push_back(daily(a, from, "Monetario", C_GRAY,
                              [&](size_t i) { return a.ix.mm[i] / m0 * 100.0; }));
            s.push_back(daily(a, from, "Renta fija (duración modelada)", C_BLUE,
                              [&](size_t i) { return a.ix.bond[i] / b0 * 100.0; }));
            break;
        }
    }
    float h = ImGui::GetContentRegionAvail().y;
    drawChart("histTop", ImVec2(0, h * 0.62f), s, o);
    drawChart("histScore", ImVec2(0, 0), {scoreSeries(a, from)}, scoreOpts(a));
}

// ---- 3. Señal ---------------------------------------------------------------

void tabSignal(App& a) {
    paramsPanel(a, 0);
    ImGui::SameLine();
    ImGui::BeginChild("right");
    const SignalStudy& st = a.study;
    int h = a.horizon;

    ImGui::TextColored(C_DIM, "Rentabilidad posterior a");
    for (int i = 0; i < kHorizons; i++) {
        ImGui::SameLine();
        char lab[24];
        snprintf(lab, sizeof lab, "%d meses", kHorizonMonths[i]);
        ImGui::RadioButton(lab, &a.horizon, i);
    }
    ImGui::SameLine(0, 30);
    ImGui::TextColored(C_DIM, "%s · %zu revisiones", periodLabel(a).c_str(), st.obs.size());
    ImGui::SameLine();
    HelpMarker("En cada revisión se anota la puntuación y lo que hizo después el fondo de "
               "renta fija menos el monetario (exceso, en puntos porcentuales). Las ventanas "
               "se solapan: 250 revisiones mensuales a 12 meses equivalen a unos 20 periodos "
               "independientes, así que las diferencias pequeñas no significan nada.");

    {
        float w = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4;
        const Bucket& all = st.all[h];
        StatTile("Cualquier momento", all.n ? fmtSigned(all.mean, 2) + " pp" : "-",
                 signColor(all.mean),
                 "Exceso medio de la renta fija entrando en una revisión cualquiera: la "
                 "referencia con la que comparar las demás.", w);
        static const char* names[3] = {"Con «seguir en monetario»", "Con «entrada parcial»",
                                       "Con «entrada completa»"};
        for (int d = 0; d < 3; d++) {
            ImGui::SameLine();
            const Bucket& b = st.byDecision[d][h];
            std::string v = b.n ? fmtSigned(b.mean, 2) + " pp" : "sin casos";
            std::string tip = b.n ? std::to_string(b.n) + " revisiones; la renta fija ganó en el " +
                                        fmtNum(b.pctPos, 0) + " % de ellas; peor caso " +
                                        fmtSigned(b.worst, 2) + " pp."
                                  : "Ninguna revisión del periodo cayó en este tramo.";
            StatTile(names[d], v, b.n ? signColor(b.mean) : C_DIM, tip.c_str(), w);
        }
    }

    // by score
    sectionTitle("Por puntuación");
    double scale = 0.5;
    for (int s = 0; s <= 10; s++)
        if (st.byScore[s][h].n) scale = std::max(scale, std::fabs(st.byScore[s][h].mean));
    if (ImGui::BeginTable("byScore", 7, kTable)) {
        ImGui::TableSetupColumn("Puntuación", 0, 1.3f);
        ImGui::TableSetupColumn("Revisiones");
        ImGui::TableSetupColumn("Exceso medio");
        ImGui::TableSetupColumn("Mediana");
        ImGui::TableSetupColumn("Gana la RF");
        ImGui::TableSetupColumn("Peor");
        ImGui::TableSetupColumn("Mejor");
        ImGui::TableHeadersRow();
        for (int s = 10; s >= 0; s--) {
            const Bucket& b = st.byScore[s][h];
            if (!b.n) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(scoreColor(s, a.p), "%d", s);
            ImGui::SameLine();
            ImGui::TextColored(C_DIM, "%s", decisionName(decide(s, a.p)));
            cellRight(std::to_string(b.n));
            cellSigned(b.mean, 2, " pp");
            tintCell(b.mean, scale);
            cellSigned(b.median, 2, " pp");
            cellRight(fmtNum(b.pctPos, 0) + " %");
            cellSigned(b.worst, 2, " pp");
            cellSigned(b.best, 2, " pp");
        }
        ImGui::EndTable();
    }

    // by indicator state
    sectionTitle("Por indicador",
                 "Exceso medio posterior según el estado de cada indicador por separado. La "
                 "última columna es lo que separa un estado favorable de uno desfavorable: "
                 "cuanto mayor, más información lleva ese indicador.");
    if (ImGui::BeginTable("byState", 5, kTable)) {
        ImGui::TableSetupColumn("Indicador", 0, 2.0f);
        ImGui::TableSetupColumn("Desfavorable");
        ImGui::TableSetupColumn("Neutral");
        ImGui::TableSetupColumn("Favorable");
        ImGui::TableSetupColumn("Favorable − desfav.");
        ImGui::TableHeadersRow();
        for (int k = 0; k < 5; k++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d. %s", k + 1, indicatorName(k));
            for (int s = 0; s < 3; s++) {
                const Bucket& b = st.byState[k][s][h];
                ImGui::TableNextColumn();
                if (!b.n) {
                    TextRight("-", C_DIM);
                    continue;
                }
                tintCell(b.mean, scale);
                TextRight(fmtSigned(b.mean, 2) + " pp  (" + std::to_string(b.n) + ")",
                          signColor(b.mean));
            }
            const Bucket& fav = st.byState[k][2][h];
            const Bucket& unf = st.byState[k][0][h];
            if (fav.n && unf.n) cellSigned(fav.mean - unf.mean, 2, " pp");
            else cellRight("-", C_DIM);
        }
        ImGui::EndTable();
    }

    // by quintile of the raw figure
    sectionTitle("Niveles empíricos",
                 "El valor bruto de cada indicador repartido en cinco grupos de igual tamaño, "
                 "del más bajo al más alto, con el exceso medio posterior de cada grupo y el "
                 "tramo de valores que abarca. Donde el exceso cambia de signo es donde tendría "
                 "sentido poner el umbral.");
    if (ImGui::BeginTable("byQuint", 6, kTable)) {
        ImGui::TableSetupColumn("Indicador", 0, 1.6f);
        static const char* q[5] = {"Quintil 1 (bajo)", "Quintil 2", "Quintil 3", "Quintil 4",
                                   "Quintil 5 (alto)"};
        for (int i = 0; i < 5; i++) ImGui::TableSetupColumn(q[i]);
        ImGui::TableHeadersRow();
        static const char* what[5] = {"Var. 2 años 3 m (pb)", "Var. pendiente 3 m (pb)",
                                      "Expectativa 12 m (pb)", "Prima (pp)",
                                      "Var. 10 años 3 m (pb)"};
        for (int k = 0; k < 5; k++) {
            int dec = k == 3 ? 2 : 0;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d. %s", k + 1, what[k]);
            for (int i = 0; i < 5; i++) {
                const Bucket& b = st.byQuintile[k][i][h];
                ImGui::TableNextColumn();
                if (!b.n) {
                    TextRight("-", C_DIM);
                    continue;
                }
                tintCell(b.mean, scale);
                TextRight(fmtSigned(b.mean, 2) + " pp", signColor(b.mean));
                std::string range =
                    i == 0 ? "< " + fmtNum(st.quintileEdge[k][0], dec)
                    : i == 4 ? "≥ " + fmtNum(st.quintileEdge[k][3], dec)
                             : fmtNum(st.quintileEdge[k][i - 1], dec) + " a " +
                                   fmtNum(st.quintileEdge[k][i], dec);
                TextRight(range, C_DIM);
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

// ---- 4. Estrategia ----------------------------------------------------------

void tabStrategy(App& a) {
    paramsPanel(a, 1);
    ImGui::SameLine();
    ImGui::BeginChild("right");
    const StratResult& sig = a.strat[0];
    const StratResult& mm = a.strat[1];
    const StratResult& bond = a.strat[2];
    if (sig.days.size() < 2) {
        ImGui::TextColored(C_YELLOW, "El periodo elegido no contiene datos.");
        ImGui::EndChild();
        return;
    }

    ImGui::TextColored(C_DIM, "%s · %s años · revisión %s", periodLabel(a).c_str(),
                       fmtNum(sig.years, 1).c_str(), a.p.freq ? "semanal" : "mensual");
    ImGui::SameLine();
    HelpMarker("La cartera empieza en el monetario. En cada revisión se mira la puntuación y "
               "se aplica la regla de decisión; la orden se ejecuta al día siguiente, porque "
               "el BCE publica cada curva la mañana posterior. Los dos fondos son sintéticos: "
               "el monetario capitaliza el €STR (EONIA − 8,5 pb antes de octubre de 2019) y "
               "la renta fija es un bono AAA de la duración indicada, ambos netos de gastos.");
    {
        float w = (ImGui::GetContentRegionAvail().x - 5 * ImGui::GetStyle().ItemSpacing.x) / 6;
        StatTile("TAE de la señal", fmtPct(sig.cagr), C_TEXT, nullptr, w);
        ImGui::SameLine();
        StatTile("vs monetario", fmtSigned(sig.cagr - mm.cagr, 2) + " pp",
                 signColor(sig.cagr - mm.cagr), "Diferencia de TAE con no moverse del monetario.",
                 w);
        ImGui::SameLine();
        StatTile("vs renta fija", fmtSigned(sig.cagr - bond.cagr, 2) + " pp",
                 signColor(sig.cagr - bond.cagr),
                 "Diferencia de TAE con haber estado siempre en el fondo de renta fija.", w);
        ImGui::SameLine();
        StatTile("Caída máxima", fmtPct(sig.maxDD), C_RED, nullptr, w);
        ImGui::SameLine();
        StatTile("Tiempo en RF", fmtNum(sig.timeInBond, 0) + " %", C_BLUE, nullptr, w);
        ImGui::SameLine();
        StatTile("Cambios", std::to_string(sig.switches), C_TEXT, nullptr, w);
    }

    static const ImVec4 cols[4] = {C_GREEN, C_GRAY, C_BLUE, C_ORANGE};
    if (ImGui::BeginTable("strats", 8, kTable)) {
        ImGui::TableSetupColumn("Estrategia", 0, 2.2f);
        ImGui::TableSetupColumn("Valor final");
        ImGui::TableSetupColumn("TAE");
        ImGui::TableSetupColumn("vs monetario");
        ImGui::TableSetupColumn("Caída máx.");
        ImGui::TableSetupColumn("Volatilidad");
        ImGui::TableSetupColumn("En RF");
        ImGui::TableSetupColumn("Cambios");
        ImGui::TableHeadersRow();
        for (int i = 0; i < 4; i++) {
            const StratResult& r = a.strat[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(cols[i], "%s", r.name.c_str());
            cellRight(fmtNum(r.value.back(), 2));
            cellRight(fmtPct(r.cagr));
            if (i == 1) cellRight("-", C_DIM);
            else cellSigned(r.cagr - mm.cagr, 2, " pp");
            cellRight(fmtPct(r.maxDD), r.maxDD < -0.005 ? C_RED : C_TEXT);
            cellRight(fmtPct(r.vol));
            cellRight(fmtNum(r.timeInBond, 0) + " %");
            cellRight(std::to_string(r.switches));
        }
        ImGui::EndTable();
    }

    ImGui::Checkbox("Ver operaciones", &a.showTrades);
    ImGui::SameLine(0, 20);
    ImGui::TextColored(C_DIM, "Sombreado: periodos en renta fija (verde, completa; amarillo, "
                              "parcial).");

    std::vector<ChartSeries> series;
    for (int i : {1, 2, 3, 0}) {  // the signal last, so it is drawn on top
        ChartSeries s;
        s.label = a.strat[i].name;
        s.color = cols[i];
        s.thickness = i == 0 ? 2.5f : 1.5f;
        s.x.assign(a.strat[i].days.begin(), a.strat[i].days.end());
        s.y = a.strat[i].value;
        series.push_back(std::move(s));
    }
    ChartOpts o;
    for (size_t i = 0; i < sig.weight.size();) {
        size_t j = i;
        while (j < sig.weight.size() && sig.weight[j] == sig.weight[i]) j++;
        if (sig.weight[i] > 0)
            o.bands.push_back({(double)sig.days[i], (double)sig.days[std::min(j, sig.days.size() - 1)],
                               sig.weight[i] >= 0.999 ? C_GREEN : C_YELLOW});
        i = j;
    }
    float h = ImGui::GetContentRegionAvail().y;
    drawChart("stratChart", ImVec2(0, a.showTrades ? std::max(220.0f, h * 0.55f) : 0), series, o);

    if (a.showTrades && ImGui::BeginTable("trades", 4, kTable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Fecha");
        ImGui::TableSetupColumn("Puntuación");
        ImGui::TableSetupColumn("Operación", 0, 2.5f);
        ImGui::TableSetupColumn("Peso en RF");
        ImGui::TableHeadersRow();
        for (size_t i = a.trades.size(); i-- > 0;) {
            const Trade& t = a.trades[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(dateStr(t.day).c_str());
            cellRight(std::to_string(t.score), scoreColor(t.score, a.p));
            ImGui::TableNextColumn();
            bool in = t.to > t.from;
            ImGui::TextColored(in ? C_GREEN : C_RED, "%s",
                               in ? (t.to >= 0.999 ? "Entrada completa en renta fija"
                                                   : "Entrada parcial en renta fija")
                                  : "Vuelta al monetario");
            cellRight(fmtNum(t.from * 100, 0) + " % → " + fmtNum(t.to * 100, 0) + " %");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

// ---- 5. Umbrales ------------------------------------------------------------

void tabThresholds(App& a) {
    paramsPanel(a, 2);
    ImGui::SameLine();
    ImGui::BeginChild("right");
    const Grid& g = a.grid;
    Params& p = a.p;

    ImGui::TextColored(C_DIM, "%s", periodLabel(a).c_str());
    ImGui::SameLine(0, 24);
    static const char* metrics[] = {"Exceso de TAE", "Caída máxima", "Tiempo en RF", "Cambios"};
    for (int i = 0; i < 4; i++) {
        ImGui::RadioButton(metrics[i], &a.gridMetric, i);
        ImGui::SameLine();
    }
    HelpMarker("La estrategia de la señal para cada pareja de umbrales, sin escalón parcial: "
               "todo a renta fija cuando la puntuación alcanza la fila, todo al monetario "
               "cuando cae hasta la columna. Los umbrales de cada indicador (panel izquierdo) "
               "se quedan como están. Un clic en una casilla aplica esa pareja.");

    {
        float w = (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4;
        StatTile("TAE siempre monetario", fmtPct(g.mmCagr), C_GRAY, nullptr, w);
        ImGui::SameLine();
        StatTile("TAE siempre renta fija", fmtPct(g.bondCagr), C_BLUE, nullptr, w);
        ImGui::SameLine();
        StatTile("Regla actual", fmtSigned(a.strat[0].cagr - g.mmCagr, 2) + " pp",
                 signColor(a.strat[0].cagr - g.mmCagr),
                 "Exceso de TAE sobre el monetario de la regla de decisión actual, con su "
                 "escalón parcial si lo tiene.", w);
        ImGui::SameLine();
        const GridCell& best = g.cell[g.bestEntry][g.bestExit];
        std::string label = "Mejor pareja: ≥ " + std::to_string(g.bestEntry) + " / ≤ " +
                            std::to_string(g.bestExit);
        StatTile("Mejor pareja", fmtSigned(best.excess, 2) + " pp", signColor(best.excess),
                 label.c_str(), w);
    }

    double scale = 0.2;
    for (int e = 1; e <= 10; e++)
        for (int x = 0; x < e; x++)
            if (g.cell[e][x].valid) scale = std::max(scale, std::fabs(g.cell[e][x].excess));

    ImGui::TextColored(C_DIM, "Filas: entrar con puntuación ≥   ·   Columnas: salir con "
                              "puntuación ≤");
    if (ImGui::BeginTable("grid", 11, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Entrada");
        for (int x = 0; x <= 9; x++) {
            char lab[16];
            snprintf(lab, sizeof lab, "≤ %d", x);
            ImGui::TableSetupColumn(lab);
        }
        ImGui::TableHeadersRow();
        for (int e = 10; e >= 1; e--) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("≥ %d", e);
            for (int x = 0; x <= 9; x++) {
                ImGui::TableNextColumn();
                const GridCell& c = g.cell[e][x];
                if (!c.valid) continue;
                bool current = p.fullScore == e && p.partialScore == e && p.exitScore == x;
                bool best = g.bestEntry == e && g.bestExit == x;
                std::string txt;
                switch (a.gridMetric) {
                    case 0: txt = fmtSigned(c.excess, 2); break;
                    case 1: txt = fmtNum(c.maxDD, 1) + " %"; break;
                    case 2: txt = fmtNum(c.timeInBond, 0) + " %"; break;
                    default: txt = std::to_string(c.switches); break;
                }
                if (best) txt += " ★";
                tintCell(c.excess, scale);
                ImGui::PushID(e * 16 + x);
                if (current) ImGui::PushStyleColor(ImGuiCol_Text, C_YELLOW);
                if (ImGui::Selectable(txt.c_str(), current)) {
                    p.fullScore = p.partialScore = e;
                    p.exitScore = x;
                }
                if (current) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Entrar con ≥ %d, salir con ≤ %d", e, x);
                    ImGui::TextColored(C_DIM, "TAE %s (%s pp sobre el monetario)",
                                       fmtPct(c.cagr).c_str(), fmtSigned(c.excess, 2).c_str());
                    ImGui::TextColored(C_DIM, "Caída máxima %s · %s %% del tiempo en RF · %d "
                                              "cambios",
                                       fmtPct(c.maxDD).c_str(), fmtNum(c.timeInBond, 0).c_str(),
                                       c.switches);
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Aplicar la mejor pareja")) {
        p.fullScore = p.partialScore = g.bestEntry;
        p.exitScore = g.bestExit;
    }
    ImGui::SameLine();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM,
                       "★ es la pareja con más TAE en este periodo, no una garantía: son 55 "
                       "combinaciones probadas sobre unos pocos ciclos de tipos. Para no "
                       "engañarse, ajusta con «Hasta» en un año intermedio y comprueba después "
                       "con «Desde» ese mismo año si la pareja elegida sigue funcionando. Fíate "
                       "más de una zona entera en verde que de una casilla suelta.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

// ---- 6. Datos ---------------------------------------------------------------

void tabData(App& a) {
    sectionTitle("Series del BCE",
                 "Todo sale del portal de datos del BCE (data-api.ecb.europa.eu): la curva de "
                 "deuda pública AAA de la zona euro, que se publica cada día hábil desde "
                 "septiembre de 2004, el €STR y, para los años anteriores a este, el EONIA.");
    ImGui::SameLine(0, 30);
    ImGui::BeginDisabled(a.updater.busy());
    if (ImGui::Button("Descargar todo de nuevo")) startRefresh(a, true);
    Tooltip("Ignora la caché y vuelve a pedir el histórico completo de las once series.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(a.mk.empty());
    if (ImGui::Button("Exportar observaciones a CSV")) {
        std::string path;
        if (exportObservations(a, &path)) setStatus(a, "Escrito " + path);
        else setStatus(a, "No se pudo escribir " + path, true);
    }
    Tooltip("Una fila por revisión del periodo del backtest: tipos, indicadores, puntuación y "
            "lo que hicieron después los dos fondos.");
    ImGui::EndDisabled();
    if (!a.lastUpdateAt.empty()) {
        ImGui::SameLine(0, 20);
        ImGui::TextColored(C_DIM, "Última actualización: %s", a.lastUpdateAt.c_str());
    }

    if (ImGui::BeginTable("series", 6, kTable)) {
        ImGui::TableSetupColumn("Serie", 0, 1.6f);
        ImGui::TableSetupColumn("Clave en el BCE", 0, 2.6f);
        ImGui::TableSetupColumn("Desde");
        ImGui::TableSetupColumn("Hasta");
        ImGui::TableSetupColumn("Días");
        ImGui::TableSetupColumn("Última descarga", 0, 2.2f);
        ImGui::TableHeadersRow();
        for (int id = 0; id < S_COUNT; id++) {
            const Series& s = a.series[(size_t)id];
            const SeriesInfo& si = seriesInfo(id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(si.label);
            ImGui::TableNextColumn();
            ImGui::TextColored(C_DIM, "%s/%s", si.flow, si.key);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(dateStr(s.firstDay()).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(dateStr(s.lastDay()).c_str());
            cellRight(std::to_string(s.days.size()));
            ImGui::TableNextColumn();
            const UpdateOutcome* o = nullptr;
            for (auto& u : a.lastUpdate)
                if (u.id == id) o = &u;
            if (!o) ImGui::TextColored(C_DIM, s.empty() ? "sin datos" : "en caché");
            else if (!o->error.empty()) ImGui::TextColored(C_RED, "%s", o->error.c_str());
            else if (o->added) ImGui::TextColored(C_GREEN, "%d días nuevos", o->added);
            else ImGui::TextColored(C_DIM, si.live ? "sin novedades" : "serie cerrada");
        }
        ImGui::EndTable();
    }
    if (a.mk.empty()) return;

    sectionTitle("Cuaderno de observaciones",
                 "Lo que el método propone anotar en cada control: fecha, tipos, pendiente, "
                 "indicadores, puntuación y la rentabilidad posterior de la renta fija sobre "
                 "el monetario. Las revisiones más recientes no tienen todavía rentabilidad "
                 "posterior.");
    const auto& obs = a.study.obs;
    if (ImGui::BeginTable("obs", 16, kTable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Fecha", 0, 1.5f);
        ImGui::TableSetupColumn("€STR");
        ImGui::TableSetupColumn("3 m");
        ImGui::TableSetupColumn("2 a");
        ImGui::TableSetupColumn("10 a");
        ImGui::TableSetupColumn("Pend.");
        ImGui::TableSetupColumn("Δ2a 3m");
        ImGui::TableSetupColumn("Exp. 12m");
        ImGui::TableSetupColumn("Prima");
        ImGui::TableSetupColumn("Δ10a 3m");
        ImGui::TableSetupColumn("Indicadores", 0, 1.5f);
        ImGui::TableSetupColumn("Punt.", 0, 0.8f);
        ImGui::TableSetupColumn("Decisión", 0, 2.2f);
        ImGui::TableSetupColumn("+6 m");
        ImGui::TableSetupColumn("+12 m");
        ImGui::TableSetupColumn("+24 m");
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin((int)obs.size());
        while (clip.Step()) {
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; row++) {
                const Obs& o = obs[obs.size() - 1 - (size_t)row];  // newest first
                const Indicators& in = a.ind[o.idx];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(dateStr(o.day).c_str());
                cellRight(fmtNum(in.on, 2));
                cellRight(fmtNum(in.y3m, 2));
                cellRight(fmtNum(in.y2, 2));
                cellRight(fmtNum(in.y10, 2));
                cellRight(fmtSigned(in.slope, 0));
                cellRight(fmtSigned(in.d2y3m, 0), stateColor(in.st[0]));
                cellRight(fmtSigned(in.exp12, 0), stateColor(in.st[2]));
                cellRight(fmtSigned(in.premium, 2), stateColor(in.st[3]));
                cellRight(fmtSigned(in.d10y3m, 0), stateColor(in.st[4]));
                ImGui::TableNextColumn();
                for (int k = 0; k < 5; k++) {  // five dots, one per indicator
                    if (k) ImGui::SameLine(0, 3);
                    ImGui::TextColored(stateColor(in.st[k]), "%d", in.st[k]);
                }
                cellRight(std::to_string(in.score), scoreColor(in.score, a.p));
                ImGui::TableNextColumn();
                if (entryBlocked(in, a.p))
                    ImGui::TextColored(C_RED, "Prima insuficiente");
                else
                    ImGui::TextColored(scoreColor(in.score, a.p), "%s",
                                       decisionName(decide(in.score, a.p)));
                for (int h = 0; h < kHorizons; h++) cellSigned(o.excess[h], 2);
            }
        }
        ImGui::EndTable();
    }
}
