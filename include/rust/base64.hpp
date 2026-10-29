#pragma once
#include <string>
#include <vector>

namespace rust {

inline std::string base64Encode(const std::vector<unsigned char>& d) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; o.reserve((d.size() + 2) / 3 * 4);
    for (size_t i = 0; i < d.size(); i += 3) {
        unsigned v = d[i] << 16;
        if (i + 1 < d.size()) v |= d[i + 1] << 8;
        if (i + 2 < d.size()) v |= d[i + 2];
        o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63];
        o += i + 1 < d.size() ? T[(v >> 6) & 63] : '=';
        o += i + 2 < d.size() ? T[v & 63] : '=';
    }
    return o;
}

}  // namespace rust
