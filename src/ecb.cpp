#include "ecb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>

namespace {

constexpr const char* kHost = "https://data-api.ecb.europa.eu/service/data";

}  // namespace

std::string runCommand(std::string cmd) {
#ifdef _WIN32
    // popen goes through cmd.exe, which does not honour single quotes; the
    // URLs are built from constants and never contain '"'
    for (char& c : cmd)
        if (c == '\'') c = '"';
#endif
    std::array<char, 8192> buf{};
    std::string out;
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "";
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe.get())) > 0) out.append(buf.data(), n);
    return out;
}

namespace {

std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> cells;
    std::string cur;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') quoted = !quoted;
        else if (c == ',' && !quoted) {
            cells.push_back(cur);
            cur.clear();
        } else if (c != '\r') cur += c;
    }
    cells.push_back(cur);
    return cells;
}

}  // namespace

std::string ecbUrl(int id, int fromDay) {
    const SeriesInfo& si = seriesInfo(id);
    std::string url = std::string(kHost) + "/" + si.flow + "/" + si.key +
                      "?format=csvdata&detail=dataonly";
    if (fromDay != kNoDay) url += "&startPeriod=" + dateStr(fromDay);
    return url;
}

bool ecbParseCsv(const std::string& body, Series& out) {
    out.days.clear();
    out.vals.clear();
    std::istringstream in(body);
    std::string line;
    if (!std::getline(in, line)) return false;
    std::vector<std::string> head = splitCsv(line);
    int cDate = -1, cVal = -1;
    for (size_t i = 0; i < head.size(); i++) {
        if (head[i] == "TIME_PERIOD") cDate = (int)i;
        if (head[i] == "OBS_VALUE") cVal = (int)i;
    }
    if (cDate < 0 || cVal < 0) return false;
    while (std::getline(in, line)) {
        std::vector<std::string> c = splitCsv(line);
        if ((int)c.size() <= std::max(cDate, cVal)) continue;
        int day = parseDate(c[(size_t)cDate]);
        const std::string& vs = c[(size_t)cVal];
        if (day == kNoDay || vs.empty()) continue;  // holidays come as blanks
        char* end = nullptr;
        double v = strtod(vs.c_str(), &end);
        if (end == vs.c_str() || !std::isfinite(v)) continue;
        if (!out.days.empty() && day <= out.days.back()) continue;
        out.days.push_back(day);
        out.vals.push_back(v);
    }
    return true;
}

bool ecbFetch(int id, int fromDay, Series& out, std::string* error) {
    out.days.clear();
    out.vals.clear();
    // the status code goes on a line of its own after the body
    std::string cmd = "curl -s --compressed --max-time 60 --retry 2 -A 'renta-fija/1.0' "
                      "-w '\\n%{http_code}' '" + ecbUrl(id, fromDay) + "'";
    std::string code, body;
    // the portal drops the odd connection; one more try costs nothing
    for (int attempt = 0; attempt < 3; attempt++) {
        std::string raw = runCommand(cmd);
        size_t nl = raw.find_last_of('\n');
        code = nl == std::string::npos ? raw : raw.substr(nl + 1);
        body = nl == std::string::npos ? std::string() : raw.substr(0, nl);
        if (code == "200" || code == "404") break;
    }
    if (code == "404") return true;  // "no results" for the requested range
    if (code != "200") {
        if (error)
            *error = code.empty() || code == "000" ? "sin conexion con el BCE (curl)"
                                                   : "el BCE respondio HTTP " + code;
        return false;
    }
    if (body.find_first_not_of(" \r\n\t") == std::string::npos) return true;
    if (!ecbParseCsv(body, out)) {
        if (error) *error = "respuesta del BCE con formato inesperado";
        return false;
    }
    return true;
}
