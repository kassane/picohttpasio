#pragma once

#include "http_types.hpp"
#include "request.hpp"
#include "response.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pico {

using Handler    = std::function<void(Request&, Response&)>;
using Middleware = std::function<void(Request&, Response&, std::function<void()>)>;

// ---------------------------------------------------------------------------
// Router
//
// Supports:
//   - Static segments:   /about
//   - Named parameters:  /users/:id/posts/:pid
//   - Wildcard suffix:   /static/*   (remainder goes into param "*")
//   - All HTTP methods or per-method registration
//   - Global and prefix-scoped middleware
// ---------------------------------------------------------------------------
class Router {
public:
    // ---- Route registration -------------------------------------------

    Router& get    (std::string_view pattern, Handler h) { return add(Method::GET,     pattern, std::move(h)); }
    Router& post   (std::string_view pattern, Handler h) { return add(Method::POST,    pattern, std::move(h)); }
    Router& put    (std::string_view pattern, Handler h) { return add(Method::PUT,     pattern, std::move(h)); }
    Router& del    (std::string_view pattern, Handler h) { return add(Method::DEL,     pattern, std::move(h)); }
    Router& patch  (std::string_view pattern, Handler h) { return add(Method::PATCH,   pattern, std::move(h)); }
    Router& head   (std::string_view pattern, Handler h) { return add(Method::HEAD,    pattern, std::move(h)); }
    Router& options(std::string_view pattern, Handler h) { return add(Method::OPTIONS, pattern, std::move(h)); }

    // Register handler for any HTTP method.
    Router& any(std::string_view pattern, Handler h) {
        return add(Method::UNKNOWN, pattern, std::move(h));
    }

    Router& add(Method m, std::string_view pattern, Handler h) {
        routes_.push_back({m, std::string(pattern), std::move(h)});
        return *this;
    }

    // ---- Middleware ------------------------------------------------------

    // Global middleware — runs for every matched request.
    Router& use(Middleware mw) {
        middlewares_.push_back({"", std::move(mw)});
        return *this;
    }

    // Prefix-scoped middleware — runs only when request path starts with prefix.
    Router& use(std::string_view prefix, Middleware mw) {
        middlewares_.push_back({std::string(prefix), std::move(mw)});
        return *this;
    }

    // ---- Dispatch -------------------------------------------------------

    // Fills req.pathParams, runs middleware chain, then calls the matching handler.
    // Returns true if a route was matched (even if it returned an error response).
    // Returns false if no route matched; caller should send 404.
    bool dispatch(Request& req, Response& res) const {
        // Find a matching route
        std::optional<std::map<std::string,std::string>> params;
        const Route* matched = nullptr;

        for (const auto& route : routes_) {
            if (route.method != Method::UNKNOWN && route.method != req.method)
                continue;
            auto m = match(route.pattern, req.path);
            if (m.has_value()) {
                params  = std::move(m);
                matched = &route;
                break;
            }
        }

        if (!matched) return false;

        req.pathParams = std::move(*params);

        // Collect applicable middleware (in registration order)
        std::vector<const Middleware*> active_mw;
        for (const auto& [prefix, mw] : middlewares_) {
            if (prefix.empty() || req.path.starts_with(prefix))
                active_mw.push_back(&mw);
        }

        // Build and execute the middleware + handler chain
        // We use an index-based runner to avoid recursive lambdas.
        size_t idx = 0;
        std::function<void()> run_next;
        run_next = [&]() {
            if (idx < active_mw.size()) {
                const Middleware* mw = active_mw[idx++];
                (*mw)(req, res, run_next);
            } else {
                matched->handler(req, res);
            }
        };
        run_next();
        return true;
    }

    // ---- Pattern matching (public for testing) --------------------------

    // Matches a route pattern against an actual request path.
    // Pattern segments:
    //   - literal "foo"  → must equal "foo"
    //   - ":name"        → matches any non-empty segment; captured as name
    //   - "*"            → matches remaining path (greedy); captured as "*"
    // Returns nullopt on no match; otherwise map of captured params (may be empty).
    static std::optional<std::map<std::string,std::string>>
    match(std::string_view pattern, std::string_view path) {
        std::map<std::string,std::string> params;

        // Tokenize both into segments (split on '/')
        auto pseg = split(pattern);
        auto rseg = split(path);

        for (size_t i = 0; i < pseg.size(); ++i) {
            std::string_view ps = pseg[i];

            if (ps == "*") {
                // Wildcard: capture everything remaining
                // Reconstruct the remainder of the path
                std::string rest;
                for (size_t j = i; j < rseg.size(); ++j) {
                    if (j > i) rest += '/';
                    rest += rseg[j];
                }
                params["*"] = std::move(rest);
                return params;
            }

            if (i >= rseg.size()) return std::nullopt;  // pattern longer than path

            if (ps.starts_with(':')) {
                // Named parameter
                std::string name(ps.substr(1));
                params[std::move(name)] = std::string(rseg[i]);
            } else {
                // Literal — must match exactly
                if (ps != rseg[i]) return std::nullopt;
            }
        }

        // All pattern segments consumed; path must also be fully consumed
        if (pseg.size() != rseg.size()) return std::nullopt;
        return params;
    }

private:
    struct Route {
        Method      method;
        std::string pattern;
        Handler     handler;
    };

    std::vector<Route>                              routes_;
    std::vector<std::pair<std::string, Middleware>> middlewares_;

    // Split a path by '/', ignoring leading/trailing slashes.
    static std::vector<std::string_view> split(std::string_view s) {
        std::vector<std::string_view> parts;
        // Strip leading '/'
        if (!s.empty() && s[0] == '/') s.remove_prefix(1);
        // Strip trailing '/'
        if (!s.empty() && s.back() == '/') s.remove_suffix(1);
        if (s.empty()) return parts;

        size_t start = 0;
        for (size_t i = 0; i <= s.size(); ++i) {
            if (i == s.size() || s[i] == '/') {
                parts.push_back(s.substr(start, i - start));
                start = i + 1;
            }
        }
        return parts;
    }
};

// ---------------------------------------------------------------------------
// Common built-in middleware factories
// ---------------------------------------------------------------------------

// Logging middleware: prints method + path + status to stderr
inline Middleware logging_middleware() {
    return [](Request& req, Response& res, std::function<void()> next) {
        next();
        auto code = static_cast<unsigned>(res.statusCode());
        // Use stderr so it doesn't interfere with response bodies
        std::string line{method_to_string(req.method)};
        line += ' ';
        line += req.path;
        line += " -> ";
        line += std::to_string(code);
        line += ' ';
        line += std::string{status_message(res.statusCode())};
        line += '\n';
        // write to stderr — no fmt dependency
        fwrite(line.data(), 1, line.size(), stderr);
    };
}

// CORS middleware: adds permissive CORS headers
inline Middleware cors_middleware(std::string_view origin = "*") {
    return [o = std::string(origin)](Request&, Response& res, std::function<void()> next) {
        res.header("Access-Control-Allow-Origin",  o);
        res.header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
        res.header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        next();
    };
}

} // namespace pico
