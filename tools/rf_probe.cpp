// The app without the window: fetches the eleven series from the ECB (or
// reads them from a cache folder), and prints today's scoreboard and the
// backtest summary. Handy to check the ECB endpoints still answer the way the
// app expects, and to read the numbers from a terminal or a cron job.
//
//   ./build/rf_probe                 download everything, keep nothing
//   ./build/rf_probe data/           use the app's cache instead of the network
//   ./build/rf_probe data/ <ISIN>    ... with another real fund (default
//                                    IE00B67T5G21; "no" for the synthetic one)
#include <cstdio>
#include <string>

#include "ecb.h"
#include "engine.h"
#include "finect.h"
#include "model.h"

int main(int argc, char** argv) {
    SeriesSet set;
    Params p;
    std::string isin = argc > 2 ? argv[2] : kDefaultFundIsin;
    if (isin == "no") isin.clear();
    Series nav;
    if (argc > 1) {
        std::string root = argv[1];
        if (root.back() != '/') root += '/';
        loadSeries(root, set);
        p.load(root + "renta-fija.cfg");
        if (!isin.empty()) nav.load(fundNavPath(root, isin));
    } else {
        for (int id = 0; id < S_COUNT; id++) {
            std::string err;
            if (!ecbFetch(id, kNoDay, set[(size_t)id], &err)) {
                std::printf("%-22s ERROR: %s\n", seriesInfo(id).label, err.c_str());
                return 1;
            }
            std::printf("%-22s %5zu dias  %s .. %s\n", seriesInfo(id).label,
                        set[(size_t)id].days.size(), dateStr(set[(size_t)id].firstDay()).c_str(),
                        dateStr(set[(size_t)id].lastDay()).c_str());
        }
        std::string err;
        if (!isin.empty() && !finectFetchNav(isin, kNoDay, nav, &err)) {
            std::printf("%-22s ERROR: %s\n", isin.c_str(), err.c_str());
            return 1;
        }
    }
    if (!isin.empty())
        std::printf("%-22s %5zu VL    %s .. %s\n", ("VL " + isin).c_str(), nav.days.size(),
                    dateStr(nav.firstDay()).c_str(), dateStr(nav.lastDay()).c_str());
    if (nav.empty()) p.fundSource = 0;
    Market mk = buildMarket(set);
    if (mk.empty()) {
        std::printf("sin datos\n");
        return 1;
    }
    std::vector<Indicators> ind = computeAll(mk, p);
    Indices ix = buildIndices(mk, p, &nav);
    if (ix.real) {
        FundFit f = fitFund(mk, nav);
        std::printf("\nFondo real %s: se mueve como un AAA a %.2f años (R2 %.2f); TAE %.2f %% "
                    "frente a %.2f %% del bono AAA: %+.2f pp al año (%+.2f en los últimos 3)\n"
                    "  prima calculada con duración %.2f, diferencial %.2f, gastos %.2f\n",
                    isin.c_str(), f.duration, f.r2, f.fundCagr, f.zeroCagr, f.excess, f.excess3y,
                    p.realDuration, p.realSpread, p.realTer);
    } else {
        std::printf("\nFondo sintético: bono AAA a %.2f años\n", p.duration);
    }
    const Indicators& now = ind.back();

    std::printf("\nDatos a %s   3M %.2f  2A %.2f  10A %.2f  €STR %.3f\n",
                dateStr(mk.days.back()).c_str(), now.y3m, now.y2, now.y10, now.on);
    for (int k = 0; k < 5; k++)
        std::printf("  %d. %-30s %-13s %s\n", k + 1, indicatorName(k), stateName(now.st[k]),
                    indicatorReading(now, k).c_str());
    std::printf("  Puntuacion %d/10 -> %s\n", now.score, entryBlocked(now, p) ? "Prima insuficiente" : decisionName(decide(now, p)));

    SignalStudy st = studySignal(mk, ix, ind, p);
    std::printf("\nExceso de la renta fija sobre el monetario segun la puntuacion (pp)\n");
    std::printf("  punt.    n   6m media  12m media  12m %%>0  24m media\n");
    for (int s = 0; s <= 10; s++) {
        if (!st.byScore[s][0].n) continue;
        std::printf("  %4d  %4d  %9.2f  %9.2f  %7.0f  %9.2f\n", s, st.byScore[s][1].n,
                    st.byScore[s][0].mean, st.byScore[s][1].mean, st.byScore[s][1].pctPos,
                    st.byScore[s][2].mean);
    }
    std::printf("  todo  %4d  %9.2f  %9.2f  %7.0f  %9.2f\n", st.all[1].n, st.all[0].mean,
                st.all[1].mean, st.all[1].pctPos, st.all[2].mean);

    std::printf("\nEstrategias (%s .. %s)\n", dateStr(mk.days[ix.first]).c_str(),
                dateStr(mk.days[ix.last]).c_str());
    std::printf("  %-26s   TAE    caida max   vol   en RF  cambios\n", "");
    for (Strategy s : {Strategy::Signal, Strategy::AllMM, Strategy::AllBond, Strategy::Half}) {
        StratResult r = simulate(mk, ix, ind, p, s);
        std::printf("  %-26s %5.2f %%  %7.2f %%  %4.2f  %4.0f %%  %d\n", r.name.c_str(), r.cagr,
                    r.maxDD, r.vol, r.timeInBond, r.switches);
    }
    Grid g = thresholdGrid(mk, ix, ind, p);
    const GridCell& b = g.cell[g.bestEntry][g.bestExit];
    std::printf("\nMejor par de umbrales: entrar con >= %d, salir con <= %d  (TAE %.2f %%, "
                "%+.2f pp sobre el monetario)\n",
                g.bestEntry, g.bestExit, b.cagr, b.excess);
    return 0;
}
