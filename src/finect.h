#pragma once
// Daily NAV history of a real fund from Finect, addressed by ISIN: the same
// api.finect.com "timeseries" endpoint msmoney and fondos-openbank use (the
// one behind the chart on every finect.com fund page). It goes through the
// system `curl`, like the ECB client. An undocumented API: it can change
// without notice.
#include <string>

#include "model.h"

// The fund the app follows unless config.ini says otherwise:
// iShares Euro Investment Grade Corporate Bond Index Fund (IE), Institutional
// Acc EUR. Accumulating, so its NAV is already a total-return index.
constexpr const char* kDefaultFundIsin = "IE00B67T5G21";

// true for a plausible ISIN: two letters and ten letters or digits
bool validIsin(const std::string& isin);

// Parses a timeseries answer ({"code":200,"data":[{"datetime":"2012-02-09T...",
// "price":10}, ...]}) into `out`. Days without a price (a not-yet-published
// null) are skipped. Returns false when the text is not that format; *error,
// if given, gets Finect's own message when it sent one.
bool finectParseTimeseries(const std::string& body, Series& out, std::string* error = nullptr);

// Fetches the NAVs from `fromDay` on (kNoDay: the whole history).
bool finectFetchNav(const std::string& isin, int fromDay, Series& out, std::string* error);

// The share class's name as Finect lists it ("... Institutional Acc EUR").
bool finectFetchName(const std::string& isin, std::string& name, std::string* error);
