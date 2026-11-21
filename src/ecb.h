#pragma once
// Downloads daily series from the ECB Data Portal (data-api.ecb.europa.eu),
// the public SDMX service behind the yield-curve and €STR pages. Goes through
// the system `curl`, like the Finect client in msmoney and dca, so there is no
// link-time dependency on libcurl.
#include <string>

#include "model.h"

// the URL a series is fetched from; `fromDay` (or kNoDay) limits the range
std::string ecbUrl(int seriesId, int fromDay);

// Parses the ECB's csvdata answer into `out`. Returns false if the text is
// not that format at all (an HTML error page, say).
bool ecbParseCsv(const std::string& body, Series& out);

// Fetches one series from `fromDay` on (kNoDay: the whole history). An empty
// answer is a success with no observations: nothing published in that range.
bool ecbFetch(int seriesId, int fromDay, Series& out, std::string* error);
