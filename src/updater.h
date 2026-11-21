// Background refresh of the ECB series.
//
// Eleven series, one HTTP round trip each, the first time several hundred
// kilobytes apiece: done inline it would freeze the window for seconds. One
// worker thread does the network and the file writing, publishes its progress
// behind a mutex for the status bar, and the main thread picks the result up
// on a later frame.
//
// The worker only touches its own copies and the files under <data>/series/.
// It never sees the live tables: when it is done the main thread reloads the
// cache from disk and recomputes.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "model.h"

struct UpdateOutcome {
    int id = 0;
    std::string error;  // empty on success
    int added = 0;      // days new to the cache
    int lastDay = kNoDay;
};

class Updater {
public:
    ~Updater();

    bool busy() const { return running_.load(); }
    // refreshes every series from the last day already cached; `full` discards
    // that shortcut and downloads the whole history again
    void start(const std::string& dataRoot, bool full);
    std::string progress() const;
    // true exactly once, on the first frame after the job finished
    bool collect(std::vector<UpdateOutcome>& out);

private:
    void run(std::string dataRoot, bool full);

    std::thread th_;
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};
    mutable std::mutex mu_;
    std::string progress_;
    std::vector<UpdateOutcome> results_;
};
