// EmuHub-only one-use Vita provider ticket. No game, licence or account bytes are logged.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <string>
#include <utility>

namespace emuhub_vita {
struct Request {
    std::string title_id;
    std::string row_hash;
    std::string zip_path;
};

inline bool title_id_valid(const std::string &id) {
    if (id.size() != 9) return false;
    for (int i = 0; i < 4; ++i) if (id[i] < 'A' || id[i] > 'Z') return false;
    for (int i = 4; i < 9; ++i) if (id[i] < '0' || id[i] > '9') return false;
    return true;
}
inline bool row_hash_valid(const std::string &hash) {
    if (hash.size() != 16) return false;
    for (char c : hash) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
inline bool read_bounded(const char *path, std::string &body, std::size_t maximum) {
    FILE *file = std::fopen(path, "rb");
    if (!file) return false;
    char bytes[512];
    const std::size_t n = std::fread(bytes, 1, sizeof bytes, file);
    const bool okay = !std::ferror(file) && n > 0 && n <= maximum && std::feof(file);
    std::fclose(file);
    if (okay) body.assign(bytes, n);
    return okay;
}
inline std::uint64_t hash_body(const std::string &body) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : body) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}
inline bool arm(const char *ticket_path, const char *marker_path, const Request &request,
                std::time_t now = std::time(nullptr)) {
    if (!title_id_valid(request.title_id) || !row_hash_valid(request.row_hash) ||
        request.zip_path != "/app0/content/eh-" + request.row_hash + ".zip") return false;
    const std::string body = "EHVITA1\nid=" + request.title_id + "\nrow=" + request.row_hash +
        "\nzip=" + request.zip_path + "\n";
    const std::string temporary = std::string(ticket_path) + ".tmp";
    FILE *out = std::fopen(temporary.c_str(), "wb");
    if (!out) return false;
    const bool written = std::fwrite(body.data(), 1, body.size(), out) == body.size();
    const bool closed = std::fclose(out) == 0;
    if (!written || !closed || std::rename(temporary.c_str(), ticket_path) != 0) {
        std::remove(temporary.c_str()); return false;
    }
    const std::string marker_tmp = std::string(marker_path) + ".tmp";
    out = std::fopen(marker_tmp.c_str(), "wb");
    if (!out) { std::remove(ticket_path); return false; }
    const int count = std::fprintf(out, "ps5-game-handoff 1\n%lld\n%016llx\n",
        static_cast<long long>(now), static_cast<unsigned long long>(hash_body(body)));
    const bool marker_closed = std::fclose(out) == 0;
    if (count < 0 || !marker_closed || std::rename(marker_tmp.c_str(), marker_path) != 0) {
        std::remove(marker_tmp.c_str()); std::remove(ticket_path); return false;
    }
    return true;
}
inline bool consume(const char *ticket_path, const char *marker_path, Request &request,
                    std::time_t now = std::time(nullptr)) {
    std::string marker, body;
    const bool marker_read = read_bounded(marker_path, marker, 80);
    const bool ticket_read = read_bounded(ticket_path, body, 256);
    // Consume even malformed, stale or mismatched requests. A later launch needs a fresh wheel action.
    std::remove(marker_path);
    std::remove(ticket_path);
    if (!marker_read || !ticket_read) return false;
    char magic[32]{};
    int version = 0;
    long long issued = 0;
    unsigned long long expected = 0;
    char extra = 0;
    if (std::sscanf(marker.c_str(), "%31s %d %lld %llx %c", magic, &version,
                    &issued, &expected, &extra) != 4 ||
        std::strcmp(magic, "ps5-game-handoff") != 0 || version != 1 ||
        issued > static_cast<long long>(now) ||
        static_cast<long long>(now) - issued > 30 ||
        hash_body(body) != static_cast<std::uint64_t>(expected)) return false;
    constexpr char prefix[] = "EHVITA1\nid=";
    if (body.rfind(prefix, 0) != 0 || body.back() != '\n') return false;
    const std::size_t row = body.find("\nrow=", sizeof prefix - 1);
    const std::size_t zip = body.find("\nzip=", row == std::string::npos ? 0 : row + 5);
    if (row == std::string::npos || zip == std::string::npos ||
        body.find('\n', zip + 5) != body.size() - 1) return false;
    Request candidate{ body.substr(sizeof prefix - 1, row - (sizeof prefix - 1)),
        body.substr(row + 5, zip - (row + 5)), body.substr(zip + 5, body.size() - zip - 6) };
    if (!title_id_valid(candidate.title_id) || !row_hash_valid(candidate.row_hash) ||
        candidate.zip_path != "/app0/content/eh-" + candidate.row_hash + ".zip") return false;
    request = std::move(candidate);
    return true;
}
} // namespace emuhub_vita
