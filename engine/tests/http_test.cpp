import roboslop.platform.http;

#include <catch2/catch_test_macros.hpp>

TEST_CASE("httpRequest reports a refused connection as TransportFailed", "[platform][http]") {
    // Port 1 (tcpmux) is never listening on a developer machine; the
    // connect fails immediately, so this needs no network and no server.
    const roboslop::HttpRequest req{
        .url = "http://127.0.0.1:1/",
        .method = "GET",
        .headers = {},
        .body = {},
        .timeoutSeconds = 5,
    };
    const auto r = roboslop::httpRequest(req);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == static_cast<int>(roboslop::HttpError::TransportFailed));
    REQUIRE_FALSE(r.error().context.empty());
}
