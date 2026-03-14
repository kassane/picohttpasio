#pragma once

// ---------------------------------------------------------------------------
// static_files.hpp — unified static file serving for HTTPServer + CoroServer
//
// Features:
//   - 30+ MIME type mappings (configurable via StaticConfig::extra_mime_types)
//   - ETag + Last-Modified headers + HTTP 304 Not Modified (conditional GET)
//   - Range requests: HTTP 206 Partial Content (single byte-range)
//   - Directory listing (off by default, opt-in via StaticConfig::directory_listing)
//   - Gzip compression (requires PICO_ENABLE_COMPRESSION + zlib)
//
// Usage:
//   pico::StaticConfig cfg;
//   cfg.directory_listing = true;
//   cfg.compression       = true;
//   auto res = pico::serve_static(req, "./www", cfg);
// ---------------------------------------------------------------------------

#include "request.hpp"
#include "response.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#ifdef PICO_ENABLE_COMPRESSION
#include <zlib.h>
#endif

namespace pico {

// ---------------------------------------------------------------------------
// StaticConfig — tuneable options passed to serve_static()
// ---------------------------------------------------------------------------
struct StaticConfig {
    /// Emit HTML directory listings when no index.html is found (default: off)
    bool directory_listing = false;

    /// Add ETag, Last-Modified, Cache-Control headers and honour If-None-Match /
    /// If-Modified-Since conditional requests (default: on)
    bool caching = true;

    /// Support Range: bytes=N-M requests and return 206 Partial Content (default: on)
    bool range_requests = true;

    /// Compress responses with gzip when the client advertises Accept-Encoding: gzip.
    /// Only takes effect when the library is built with PICO_ENABLE_COMPRESSION. (default: off)
    bool compression = false;

    /// User-defined MIME type overrides / additions, keyed by file extension (".ext").
    std::unordered_map<std::string, std::string> extra_mime_types;
};

// ---------------------------------------------------------------------------
// Implementation helpers (not part of the public API)
// ---------------------------------------------------------------------------
namespace detail {

inline std::string_view builtin_mime(std::string_view fpath) noexcept {
    auto dot = fpath.rfind('.');
    std::string_view ext = (dot != std::string_view::npos) ? fpath.substr(dot) : std::string_view{};

    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css")   return "text/css";
    if (ext == ".js" || ext == ".mjs")   return "application/javascript";
    if (ext == ".json")  return "application/json";
    if (ext == ".xml")   return "application/xml";
    if (ext == ".png")   return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif")   return "image/gif";
    if (ext == ".webp")  return "image/webp";
    if (ext == ".svg")   return "image/svg+xml";
    if (ext == ".ico")   return "image/x-icon";
    if (ext == ".txt")   return "text/plain; charset=utf-8";
    if (ext == ".md")    return "text/markdown; charset=utf-8";
    if (ext == ".pdf")   return "application/pdf";
    if (ext == ".zip")   return "application/zip";
    if (ext == ".gz")    return "application/gzip";
    if (ext == ".tar")   return "application/x-tar";
    if (ext == ".wasm")  return "application/wasm";
    if (ext == ".mp4")   return "video/mp4";
    if (ext == ".webm")  return "video/webm";
    if (ext == ".mp3")   return "audio/mpeg";
    if (ext == ".ogg")   return "audio/ogg";
    if (ext == ".wav")   return "audio/wav";
    if (ext == ".ttf")   return "font/ttf";
    if (ext == ".woff")  return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".eot")   return "application/vnd.ms-fontobject";
    if (ext == ".csv")   return "text/csv";
    return "application/octet-stream";
}

// Format a filesystem::file_time_type as an RFC 7231 HTTP-date string.
// e.g.  "Thu, 01 Jan 2026 00:00:00 GMT"
inline std::string format_http_date(std::filesystem::file_time_type ft) {
    // Convert filesystem clock to system_clock
    auto sys_tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now()
           + std::chrono::system_clock::now());
    std::time_t t = std::chrono::system_clock::to_time_t(sys_tp);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return std::string(buf);
}

// Build a quoted ETag from file modification time and size.
inline std::string make_etag(std::filesystem::file_time_type mtime, std::uintmax_t size) {
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        mtime.time_since_epoch()).count();
    std::ostringstream oss;
    oss << '"' << std::hex << (static_cast<uint64_t>(ns) ^ static_cast<uint64_t>(size)) << '"';
    return oss.str();
}

struct ByteRange { std::size_t first, last; };

// Parse a "bytes=N-M" range specification against a known total file size.
// Returns nullopt for unsupported or out-of-bounds ranges.
inline std::optional<ByteRange> parse_range(std::string_view hdr, std::size_t total) {
    if (total == 0) return std::nullopt;
    if (!hdr.starts_with("bytes=")) return std::nullopt;
    hdr.remove_prefix(6);
    auto dash = hdr.find('-');
    if (dash == std::string_view::npos) return std::nullopt;
    try {
        std::size_t first = 0, last = total - 1;
        std::string_view s_first = hdr.substr(0, dash);
        std::string_view s_last  = hdr.substr(dash + 1);
        if (!s_first.empty())
            first = static_cast<std::size_t>(std::stoull(std::string(s_first)));
        if (!s_last.empty())
            last  = static_cast<std::size_t>(std::stoull(std::string(s_last)));
        if (first > last || last >= total) return std::nullopt;
        return ByteRange{first, last};
    } catch (...) {
        return std::nullopt;
    }
}

#ifdef PICO_ENABLE_COMPRESSION
// Compress data with gzip (RFC 1952) using zlib.
// Returns nullopt if zlib initialisation or compression fails.
inline std::optional<std::string> gzip_compress(std::string_view data) {
    z_stream zs{};
    // windowBits = 15 | 16  → gzip wrapper
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return std::nullopt;

    zs.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());

    std::string out;
    out.reserve(data.size() / 2 + 32);
    char buf[32768];
    int ret;
    do {
        zs.next_out  = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = static_cast<uInt>(sizeof(buf));
        ret = deflate(&zs, Z_FINISH);
        out.append(buf, sizeof(buf) - zs.avail_out);
    } while (ret == Z_OK);
    deflateEnd(&zs);

    if (ret != Z_STREAM_END) return std::nullopt;
    return out;
}
#endif // PICO_ENABLE_COMPRESSION

// HTML-escape a string to prevent XSS in generated HTML pages.
inline std::string html_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;
        }
    }
    return out;
}

// Build a simple HTML directory listing page.
inline std::string make_dir_listing(const std::string& url_path,
                                     const std::filesystem::path& dir_path) {
    std::string safe_path = html_escape(url_path);
    std::string html;
    html.reserve(4096);
    html += "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">"
            "<title>Index of ";
    html += safe_path;
    html += "</title></head><body><h1>Index of ";
    html += safe_path;
    html += "</h1><hr><pre>\n";

    if (url_path != "/")
        html += "<a href=\"../\">../</a>\n";

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
        auto name = entry.path().filename().string();
        if (entry.is_directory(ec)) name += '/';
        auto safe_name = html_escape(name);
        html += "<a href=\"";
        html += safe_name;
        html += "\">";
        html += safe_name;
        html += "</a>\n";
    }
    html += "</pre><hr></body></html>";
    return html;
}

} // namespace detail

// ---------------------------------------------------------------------------
// serve_static — single entry-point used by both HTTPServer and CoroServer
// ---------------------------------------------------------------------------

/// Serve a static file (or directory listing) for the given GET request.
///
/// @param req      Parsed HTTP request (method must be GET; HEAD not handled here)
/// @param root_dir Filesystem root to serve from (e.g. "./www")
/// @param cfg      Feature flags and MIME overrides (optional)
/// @returns        A complete Response ready to serialise and send
inline Response serve_static(const Request& req,
                              const std::string& root_dir,
                              const StaticConfig& cfg = {}) {
    namespace fs = std::filesystem;
    const std::string& url_path = req.path;

    // ── Security: reject directory traversal ─────────────────────────────
    if (url_path.find("..") != std::string::npos)
        return Response::bad_request("Invalid path");

    // ── Resolve filesystem path ───────────────────────────────────────────
    // Trim leading '/' so fs::path concatenation works portably.
    fs::path fpath = fs::path(root_dir);
    if (url_path == "/" || url_path.empty()) {
        fpath /= "index.html";
    } else {
        // strip leading slash before appending
        std::string_view rel = url_path;
        if (rel.front() == '/') rel.remove_prefix(1);
        fpath /= rel;
    }

    // Verify the resolved path stays inside root_dir (defense-in-depth
    // against encoded traversal sequences or symlink escapes).
    {
        auto canonical_root = fs::weakly_canonical(root_dir);
        auto canonical_path = fs::weakly_canonical(fpath);
        auto root_str = canonical_root.string();
        if (canonical_path.string().substr(0, root_str.size()) != root_str)
            return Response::bad_request("Invalid path");
    }

    std::error_code ec;
    auto st = fs::status(fpath, ec);

    // ── Directory handling ────────────────────────────────────────────────
    if (!ec && fs::is_directory(st)) {
        auto index = fpath / "index.html";
        if (fs::exists(index, ec)) {
            fpath = index;
            st    = fs::status(fpath, ec);
        } else if (cfg.directory_listing) {
            try {
                auto listing = detail::make_dir_listing(url_path, fpath);
                return Response::make().html(std::move(listing));
            } catch (...) {
                return Response::not_found();
            }
        } else {
            return Response::not_found();
        }
    }

    if (ec || !fs::is_regular_file(st))
        return Response::not_found();

    // ── File metadata ─────────────────────────────────────────────────────
    auto mtime     = fs::last_write_time(fpath, ec);
    auto file_size = fs::file_size(fpath, ec);
    if (ec) return Response::not_found();

    // ── MIME type ─────────────────────────────────────────────────────────
    std::string fpath_str = fpath.string();
    std::string_view ct   = detail::builtin_mime(fpath_str);
    {
        auto dot = fpath_str.rfind('.');
        if (dot != std::string::npos) {
            auto it = cfg.extra_mime_types.find(fpath_str.substr(dot));
            if (it != cfg.extra_mime_types.end())
                ct = it->second;
        }
    }

    // ── Caching: ETag + Last-Modified + conditional GET ──────────────────
    std::string etag, last_modified;
    if (cfg.caching) {
        etag          = detail::make_etag(mtime, file_size);
        last_modified = detail::format_http_date(mtime);

        auto inm = req.header("If-None-Match");
        if (!inm.empty() && (inm == "*" || inm == etag))
            return Response::make(StatusCode::NotModified)
                .header("ETag",          etag)
                .header("Last-Modified", last_modified)
                .header("Cache-Control", "public, max-age=3600");

        auto ims = req.header("If-Modified-Since");
        if (!ims.empty() && ims == last_modified)
            return Response::make(StatusCode::NotModified)
                .header("ETag",          etag)
                .header("Last-Modified", last_modified)
                .header("Cache-Control", "public, max-age=3600");
    }

    // ── Open file ─────────────────────────────────────────────────────────
    std::ifstream file(fpath, std::ios::binary);
    if (!file) return Response::not_found();

    // ── Range requests (HTTP 206) ─────────────────────────────────────────
    auto range_hdr = req.header("Range");
    if (cfg.range_requests && !range_hdr.empty()) {
        auto opt = detail::parse_range(range_hdr, static_cast<std::size_t>(file_size));
        if (!opt)
            return Response::make(StatusCode::RangeNotSatisfiable)
                .header("Content-Range", "bytes */" + std::to_string(file_size));

        auto [first, last] = *opt;
        std::string partial(last - first + 1, '\0');
        file.seekg(static_cast<std::streamoff>(first));
        file.read(partial.data(), static_cast<std::streamsize>(partial.size()));

        auto res = Response::make(StatusCode::PartialContent)
            .body(std::move(partial), ct)
            .header("Content-Range",
                "bytes " + std::to_string(first) + '-'
                         + std::to_string(last)  + '/'
                         + std::to_string(file_size))
            .header("Accept-Ranges", "bytes");
        if (cfg.caching)
            res.header("ETag", etag).header("Last-Modified", last_modified);
        return res;
    }

    // ── Full file ─────────────────────────────────────────────────────────
    std::string contents((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());

#ifdef PICO_ENABLE_COMPRESSION
    if (cfg.compression) {
        auto accept_enc = req.header("Accept-Encoding");
        if (accept_enc.find("gzip") != std::string_view::npos) {
            if (auto compressed = detail::gzip_compress(contents)) {
                auto res = Response::make()
                    .body(std::move(*compressed), ct)
                    .header("Content-Encoding", "gzip")
                    .header("Vary",             "Accept-Encoding")
                    .header("Accept-Ranges",    "bytes");
                if (cfg.caching)
                    res.header("ETag",          etag)
                       .header("Last-Modified", last_modified)
                       .header("Cache-Control", "public, max-age=3600");
                return res;
            }
        }
    }
#endif

    auto res = Response::make()
        .body(std::move(contents), ct)
        .header("Accept-Ranges", "bytes");
    if (cfg.caching)
        res.header("ETag",          etag)
           .header("Last-Modified", last_modified)
           .header("Cache-Control", "public, max-age=3600");
    return res;
}

} // namespace pico
