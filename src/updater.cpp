#include "updater.h"

#include <filesystem>

#include <fstream>

#include "ecb.h"
#include "finect.h"

UpdateOutcome Updater::runFund(const std::string& dataRoot, bool full, const std::string& isin) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        progress_ = "Finect  valores liquidativos de " + isin;
    }
    UpdateOutcome o;
    o.id = kFundOutcome;
    std::string path = fundNavPath(dataRoot, isin);
    Series cached;
    if (!full) cached.load(path);
    o.lastDay = cached.lastDay();
    // a couple of weeks of overlap, like the curve: a late NAV can be corrected
    int from = cached.empty() ? kNoDay : cached.lastDay() - 14;
    Series fresh;
    if (!finectFetchNav(isin, from, fresh, &o.error)) return o;
    if (cached.empty() && fresh.empty()) {
        o.error = "Finect no tiene valores liquidativos de " + isin;
        return o;
    }
    o.added = cached.merge(fresh);
    o.lastDay = cached.lastDay();
    if (!cached.save(path)) o.error = "no se pudo escribir " + path;

    // the name only once: it does not change
    if (loadFundName(dataRoot, isin).empty()) {
        std::string name;
        if (finectFetchName(isin, name, nullptr)) std::ofstream(fundNamePath(dataRoot, isin)) << name << '\n';
    }
    return o;
}

Updater::~Updater() {
    if (th_.joinable()) th_.join();
}

void Updater::start(const std::string& dataRoot, bool full, const std::string& isin) {
    if (running_.load()) return;
    if (th_.joinable()) th_.join();
    running_ = true;
    finished_ = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        progress_.clear();
        results_.clear();
    }
    th_ = std::thread(&Updater::run, this, dataRoot, full, isin);
}

std::string Updater::progress() const {
    std::lock_guard<std::mutex> lk(mu_);
    return progress_;
}

bool Updater::collect(std::vector<UpdateOutcome>& out) {
    if (!finished_.load()) return false;
    finished_ = false;
    if (th_.joinable()) th_.join();
    std::lock_guard<std::mutex> lk(mu_);
    out = std::move(results_);
    results_.clear();
    return true;
}

void Updater::run(std::string dataRoot, bool full, std::string isin) {
    std::error_code ec;
    std::filesystem::create_directories(dataDir(dataRoot), ec);

    std::vector<UpdateOutcome> outcomes;
    for (int id = 0; id < S_COUNT; id++) {
        const SeriesInfo& si = seriesInfo(id);
        {
            std::lock_guard<std::mutex> lk(mu_);
            progress_ = "BCE " + std::to_string(id + 1) + "/" + std::to_string((int)S_COUNT) +
                        "  " + si.label;
        }
        UpdateOutcome o;
        o.id = id;
        std::string path = seriesPath(dataRoot, id);
        Series cached;
        if (!full) cached.load(path);
        o.lastDay = cached.lastDay();

        // a discontinued series that is already on file never changes again
        if (!si.live && !cached.empty()) {
            outcomes.push_back(o);
            continue;
        }
        // overlap a couple of weeks: recent curve fits do get revised
        int from = cached.empty() ? kNoDay : cached.lastDay() - 14;
        Series fresh;
        if (!ecbFetch(id, from, fresh, &o.error)) {
            outcomes.push_back(o);
            continue;
        }
        if (cached.empty() && fresh.empty()) {
            o.error = "el BCE no devolvio ninguna observacion";
        } else if (!fresh.empty()) {
            o.added = cached.merge(fresh);
            o.lastDay = cached.lastDay();
            if (!cached.save(path)) o.error = "no se pudo escribir " + path;
        }
        outcomes.push_back(o);
    }
    if (!isin.empty()) outcomes.push_back(runFund(dataRoot, full, isin));
    {
        std::lock_guard<std::mutex> lk(mu_);
        results_ = std::move(outcomes);
        progress_.clear();
    }
    finished_ = true;
    running_ = false;
}
