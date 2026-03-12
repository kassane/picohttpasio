#include <catch2/catch_test_macros.hpp>
#include "http_types.hpp"

using namespace pico;

TEST_CASE("method_from_string round-trips known methods", "[types]") {
    REQUIRE(method_from_string("GET")     == Method::GET);
    REQUIRE(method_from_string("POST")    == Method::POST);
    REQUIRE(method_from_string("PUT")     == Method::PUT);
    REQUIRE(method_from_string("DELETE")  == Method::DEL);
    REQUIRE(method_from_string("PATCH")   == Method::PATCH);
    REQUIRE(method_from_string("HEAD")    == Method::HEAD);
    REQUIRE(method_from_string("OPTIONS") == Method::OPTIONS);
    REQUIRE(method_from_string("FOOBAR")  == Method::UNKNOWN);
    REQUIRE(method_from_string("")        == Method::UNKNOWN);
}

TEST_CASE("method_to_string produces correct strings", "[types]") {
    REQUIRE(method_to_string(Method::GET)     == "GET");
    REQUIRE(method_to_string(Method::POST)    == "POST");
    REQUIRE(method_to_string(Method::PUT)     == "PUT");
    REQUIRE(method_to_string(Method::DEL)  == "DELETE");
    REQUIRE(method_to_string(Method::PATCH)   == "PATCH");
    REQUIRE(method_to_string(Method::HEAD)    == "HEAD");
    REQUIRE(method_to_string(Method::OPTIONS) == "OPTIONS");
    REQUIRE(method_to_string(Method::UNKNOWN) == "UNKNOWN");
}

TEST_CASE("method round-trip: from_string -> to_string", "[types]") {
    for (auto s : {"GET","POST","PUT","DELETE","PATCH","HEAD","OPTIONS"}) {
        REQUIRE(method_to_string(method_from_string(s)) == s);
    }
}

TEST_CASE("status_message returns non-empty for all defined codes", "[types]") {
    REQUIRE(status_message(StatusCode::OK)                  == "OK");
    REQUIRE(status_message(StatusCode::Created)             == "Created");
    REQUIRE(status_message(StatusCode::NoContent)           == "No Content");
    REQUIRE(status_message(StatusCode::BadRequest)          == "Bad Request");
    REQUIRE(status_message(StatusCode::Unauthorized)        == "Unauthorized");
    REQUIRE(status_message(StatusCode::Forbidden)           == "Forbidden");
    REQUIRE(status_message(StatusCode::NotFound)            == "Not Found");
    REQUIRE(status_message(StatusCode::MethodNotAllowed)    == "Method Not Allowed");
    REQUIRE(status_message(StatusCode::InternalServerError) == "Internal Server Error");
    REQUIRE(status_message(StatusCode::SwitchingProtocols)  == "Switching Protocols");
}

TEST_CASE("status classification helpers", "[types]") {
    REQUIRE(status_is_success(StatusCode::OK));
    REQUIRE(status_is_success(StatusCode::Created));
    REQUIRE_FALSE(status_is_success(StatusCode::NotFound));

    REQUIRE(status_is_redirect(StatusCode::Found));
    REQUIRE(status_is_redirect(StatusCode::MovedPermanently));
    REQUIRE_FALSE(status_is_redirect(StatusCode::OK));

    REQUIRE(status_is_client_error(StatusCode::BadRequest));
    REQUIRE(status_is_client_error(StatusCode::NotFound));
    REQUIRE_FALSE(status_is_client_error(StatusCode::OK));

    REQUIRE(status_is_server_error(StatusCode::InternalServerError));
    REQUIRE_FALSE(status_is_server_error(StatusCode::OK));
}
