#include <catch2/catch_test_macros.hpp>
#include "router.hpp"
#include <vector>
#include <string>

using namespace pico;

// Helper: create a minimal request with just method + path
static Request make_req(Method m, std::string path) {
    Request r;
    r.method      = m;
    r.path        = std::move(path);
    r.httpVersion = 1;
    return r;
}

// ---------------------------------------------------------------------------
// Router::match — unit tests
// ---------------------------------------------------------------------------

TEST_CASE("match: exact static routes", "[router][match]") {
    auto m = Router::match("/about", "/about");
    REQUIRE(m.has_value());
    REQUIRE(m->empty());

    REQUIRE_FALSE(Router::match("/about", "/contact").has_value());
    REQUIRE_FALSE(Router::match("/a/b",   "/a/c").has_value());
}

TEST_CASE("match: root path", "[router][match]") {
    REQUIRE(Router::match("/", "/").has_value());
    REQUIRE_FALSE(Router::match("/", "/other").has_value());
}

TEST_CASE("match: named parameters", "[router][match]") {
    auto m = Router::match("/users/:id", "/users/42");
    REQUIRE(m.has_value());
    REQUIRE(m->at("id") == "42");
}

TEST_CASE("match: multiple named parameters", "[router][match]") {
    auto m = Router::match("/users/:uid/posts/:pid", "/users/7/posts/99");
    REQUIRE(m.has_value());
    REQUIRE(m->at("uid") == "7");
    REQUIRE(m->at("pid") == "99");
}

TEST_CASE("match: wildcard captures rest of path", "[router][match]") {
    auto m = Router::match("/static/*", "/static/images/logo.png");
    REQUIRE(m.has_value());
    REQUIRE(m->at("*") == "images/logo.png");
}

TEST_CASE("match: wildcard at root", "[router][match]") {
    auto m = Router::match("/*", "/any/path/here");
    REQUIRE(m.has_value());
    REQUIRE(m->at("*") == "any/path/here");
}

TEST_CASE("match: pattern longer than path returns nullopt", "[router][match]") {
    REQUIRE_FALSE(Router::match("/a/b/c", "/a/b").has_value());
}

TEST_CASE("match: path longer than pattern returns nullopt", "[router][match]") {
    REQUIRE_FALSE(Router::match("/a/b", "/a/b/c").has_value());
}

// ---------------------------------------------------------------------------
// Router::dispatch — integration
// ---------------------------------------------------------------------------

TEST_CASE("dispatch: static GET route", "[router][dispatch]") {
    Router router;
    bool called = false;
    router.get("/hello", [&](Request&, Response& res) {
        called = true;
        res = Response::ok("world");
    });

    Request req  = make_req(Method::GET, "/hello");
    Response res = Response::make();
    bool matched = router.dispatch(req, res);

    REQUIRE(matched);
    REQUIRE(called);
    REQUIRE(res.statusCode() == StatusCode::OK);
    REQUIRE(res.getBody()    == "world");
}

TEST_CASE("dispatch: unmatched route returns false", "[router][dispatch]") {
    Router router;
    router.get("/hello", [](Request&, Response&) {});

    Request  req = make_req(Method::GET, "/goodbye");
    Response res = Response::make();
    REQUIRE_FALSE(router.dispatch(req, res));
}

TEST_CASE("dispatch: wrong method is not matched", "[router][dispatch]") {
    Router router;
    router.post("/data", [](Request&, Response&) {});

    Request  req = make_req(Method::GET, "/data");
    Response res = Response::make();
    REQUIRE_FALSE(router.dispatch(req, res));
}

TEST_CASE("dispatch: path param is populated", "[router][dispatch]") {
    Router router;
    std::string captured_id;
    router.get("/users/:id", [&](Request& req, Response& res) {
        captured_id = std::string(req.param("id"));
        res = Response::ok("ok");
    });

    Request  req = make_req(Method::GET, "/users/123");
    Response res = Response::make();
    REQUIRE(router.dispatch(req, res));
    REQUIRE(captured_id == "123");
}

TEST_CASE("dispatch: any() matches all methods", "[router][dispatch]") {
    Router router;
    int calls = 0;
    router.any("/ping", [&](Request&, Response& res) {
        ++calls;
        res = Response::ok("pong");
    });

    for (auto m : {Method::GET, Method::POST, Method::PUT, Method::DELETE}) {
        Request  req = make_req(m, "/ping");
        Response res = Response::make();
        REQUIRE(router.dispatch(req, res));
    }
    REQUIRE(calls == 4);
}

TEST_CASE("dispatch: routes matched in registration order (first wins)", "[router][dispatch]") {
    Router router;
    router.get("/x", [](Request&, Response& res) { res = Response::ok("first"); });
    router.get("/x", [](Request&, Response& res) { res = Response::ok("second"); });

    Request  req = make_req(Method::GET, "/x");
    Response res = Response::make();
    router.dispatch(req, res);
    REQUIRE(res.getBody() == "first");
}

// ---------------------------------------------------------------------------
// Middleware
// ---------------------------------------------------------------------------

TEST_CASE("global middleware runs before handler", "[router][middleware]") {
    Router router;
    std::vector<std::string> order;

    router.use([&](Request&, Response&, std::function<void()> next) {
        order.push_back("mw1");
        next();
    });
    router.use([&](Request&, Response&, std::function<void()> next) {
        order.push_back("mw2");
        next();
    });
    router.get("/x", [&](Request&, Response&) {
        order.push_back("handler");
    });

    Request  req = make_req(Method::GET, "/x");
    Response res = Response::make();
    router.dispatch(req, res);

    REQUIRE(order == std::vector<std::string>{"mw1", "mw2", "handler"});
}

TEST_CASE("middleware can short-circuit the chain", "[router][middleware]") {
    Router router;
    bool handler_called = false;

    router.use([](Request&, Response& res, std::function<void()>) {
        // does NOT call next() — short-circuits
        res = Response::make(StatusCode::Unauthorized).body("no auth");
    });
    router.get("/secret", [&](Request&, Response&) {
        handler_called = true;
    });

    Request  req = make_req(Method::GET, "/secret");
    Response res = Response::make();
    router.dispatch(req, res);

    REQUIRE_FALSE(handler_called);
    REQUIRE(res.statusCode() == StatusCode::Unauthorized);
}

TEST_CASE("prefix-scoped middleware only runs for matching prefix", "[router][middleware]") {
    Router router;
    std::vector<std::string> log;

    router.use("/api", [&](Request&, Response&, std::function<void()> next) {
        log.push_back("api-mw");
        next();
    });
    router.get("/api/users", [&](Request&, Response&) { log.push_back("users"); });
    router.get("/health",    [&](Request&, Response&) { log.push_back("health"); });

    {
        Request  req = make_req(Method::GET, "/api/users");
        Response res = Response::make();
        router.dispatch(req, res);
    }
    {
        Request  req = make_req(Method::GET, "/health");
        Response res = Response::make();
        router.dispatch(req, res);
    }

    REQUIRE(log == std::vector<std::string>{"api-mw", "users", "health"});
}

TEST_CASE("wildcard route dispatch populates '*' param", "[router][dispatch]") {
    Router router;
    std::string rest;
    router.get("/files/*", [&](Request& req, Response&) {
        rest = std::string(req.param("*"));
    });

    Request  req = make_req(Method::GET, "/files/docs/readme.txt");
    Response res = Response::make();
    REQUIRE(router.dispatch(req, res));
    REQUIRE(rest == "docs/readme.txt");
}

TEST_CASE("DELETE route registered via del()", "[router][dispatch]") {
    Router router;
    bool called = false;
    router.del("/item/:id", [&](Request& req, Response& res) {
        called = true;
        res = Response::no_content();
    });

    Request  req = make_req(Method::DELETE, "/item/5");
    Response res = Response::make();
    REQUIRE(router.dispatch(req, res));
    REQUIRE(called);
    REQUIRE(res.statusCode() == StatusCode::NoContent);
}
