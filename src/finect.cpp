#include "finect.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "ecb.h"  // runCommand

namespace {

constexpr const char* kApi = "https://api.finect.com/v4/products/collectives/funds/";
// the front-end key baked into Finect's own JS bundle: not a secret, every
// browser gets it, but the API refuses requests without it
constexpr const char* kKey = "OgcqanUxQ4S6Y5VVvnwlJayUuxeg8Ah5";

std::string get(const std::string& url) {
    std::string cmd = "curl -s --compressed --max-time 40 -A 'Mozilla/5.0' "
                      "-H 'Accept: application/json' -H 'key: " + std::string(kKey) + "' '" +
                      url + "'";
    std::string body;
    // the API drops the odd request; it costs nothing to ask again
    for (int attempt = 0; attempt < 3 && body.empty(); attempt++) body = runCommand(cmd);
    return body;
}

// the string value right after `"key":"` from `pos` on, with the JSON escapes
// a fund name can carry (\" \\ \/ \uXXXX) undone
std::string stringAfter(const std::string& json, const std::string& key, size_t pos = 0) {
    std::string marker = "\"" + key + "\":\"";
    pos = json.find(marker, pos);
    if (pos == std::string::npos) return "";
    std::string out;
    for (size_t i = pos + marker.size(); i < json.size() && json[i] != '"'; i++) {
        if (json[i] != '\\' || i + 1 >= json.size()) {
            out += json[i];
            continue;
        }
        char e = json[++i];
        if (e == 'u' && i + 4 < json.size()) {
            unsigned cp = (unsigned)strtoul(json.substr(i + 1, 4).c_str(), nullptr, 16);
            i += 4;
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            } else {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
        } else {
            out += e == 'n' ? ' ' : e;
        }
    }
    return out;
}

}  // namespace

bool validIsin(const std::string& isin) {
    if (isin.size() != 12) return false;
    for (size_t i = 0; i < isin.size(); i++) {
        unsigned char c = (unsigned char)isin[i];
        if (i < 2 ? !std::isupper(c) : !(std::isupper(c) || std::isdigit(c))) return false;
    }
    return true;
}

bool finectParseTimeseries(const std::string& body, Series& out, std::string* error) {
    out.days.clear();
    out.vals.clear();
    size_t data = body.find("\"data\":[");
    if (data == std::string::npos) {
        if (error) {
            std::string msg = stringAfter(body, "message");
            *error = msg.empty() ? "respuesta de Finect con formato inesperado" : "Finect: " + msg;
        }
        return false;
    }
    static const std::string dMarker = "\"datetime\":\"", pMarker = "\"price\":";
    std::vector<std::pair<int, double>> pts;
    size_t pos = data;
    while ((pos = body.find(dMarker, pos)) != std::string::npos) {
        pos += dMarker.size();
        int day = parseDate(body.substr(pos, 10));
        size_t close = body.find('}', pos);
        size_t pp = body.find(pMarker, pos);
        if (pp == std::string::npos || (close != std::string::npos && pp > close)) continue;
        pp += pMarker.size();
        char* end = nullptr;
        double v = strtod(body.c_str() + pp, &end);  // "null" leaves end == start
        if (day == kNoDay || end == body.c_str() + pp || !std::isfinite(v) || !(v > 0)) continue;
        pts.push_back({day, v});
    }
    std::sort(pts.begin(), pts.end());
    for (auto& [d, v] : pts) {
        if (!out.days.empty() && d == out.days.back()) {
            out.vals.back() = v;
            continue;
        }
        out.days.push_back(d);
        out.vals.push_back(v);
    }
    return true;
}

bool finectFetchNav(const std::string& isin, int fromDay, Series& out, std::string* error) {
    out.days.clear();
    out.vals.clear();
    if (!validIsin(isin)) {
        if (error) *error = "ISIN no válido: " + isin;
        return false;
    }
    std::string body = get(std::string(kApi) + isin + "/timeseries?start=" +
                           (fromDay == kNoDay ? std::string("1990-01-01") : dateStr(fromDay)));
    if (body.empty()) {
        if (error) *error = "sin respuesta de api.finect.com (curl)";
        return false;
    }
    return finectParseTimeseries(body, out, error);
}

bool finectFetchName(const std::string& isin, std::string& name, std::string* error) {
    if (!validIsin(isin)) {
        if (error) *error = "ISIN no válido: " + isin;
        return false;
    }
    std::string body = get(std::string(kApi) + isin);
    // the share class ("... Institutional Acc EUR") says more than the fund's name
    size_t cls = body.find("\"class\":{");
    name = cls == std::string::npos ? "" : stringAfter(body, "name", cls);
    if (name.empty()) name = stringAfter(body, "name");
    if (name.empty()) {
        if (error) *error = "Finect no devolvió el nombre del fondo";
        return false;
    }
    return true;
}
