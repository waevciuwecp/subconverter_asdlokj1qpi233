#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <thread>

#include "handler/settings.h"
#include "handler/webget.h"
#include "utils/file.h"
#include "utils/md5/md5.h"

namespace
{
std::string cachePathFor(const std::string &url)
{
    return "cache/" + getMD5(url);
}

struct ServeCacheGuard
{
    bool old = global.serveCacheOnFetchFail;
    explicit ServeCacheGuard(bool value)
    {
        global.serveCacheOnFetchFail = value;
    }
    ~ServeCacheGuard()
    {
        global.serveCacheOnFetchFail = old;
    }
};

struct BlockPrivateAddrGuard
{
    bool old = global.blockPrivateAddressRequests;
    explicit BlockPrivateAddrGuard(bool value)
    {
        global.blockPrivateAddressRequests = value;
    }
    ~BlockPrivateAddrGuard()
    {
        global.blockPrivateAddressRequests = old;
    }
};

} // namespace

TEST_CASE("webget buildSocks5ProxyString handles auth and no-auth")
{
    CHECK(buildSocks5ProxyString("127.0.0.1", 1080, "", "") == "socks5://127.0.0.1:1080");
    CHECK(buildSocks5ProxyString("proxy.local", 9000, "user", "pass") == "socks5://user:pass@proxy.local:9000");
}

TEST_CASE("webget data URL decoding is deterministic")
{
    CHECK(webGet("data:text/plain,hello%20world") == "hello world");
    CHECK(webGet("data:text/plain;base64,SGVsbG8=") == "Hello");
    CHECK(webGet("data:text/plain") == "");
    CHECK(webGet("data:text/plain,") == "");
}

TEST_CASE("webget rejects unsupported URL schemes")
{
    CHECK(webGet("file:///etc/passwd").empty());
    CHECK(webGet("ftp://example.com/payload").empty());
}

TEST_CASE("webget blocks private and loopback destinations when enabled")
{
    BlockPrivateAddrGuard guard(true);

    int status_code = 0;
    std::string content = "placeholder";
    FetchArgument arg {HTTP_GET, "http://127.0.0.1:65535/test", "", nullptr, nullptr, nullptr, 0, false};
    FetchResult result {&status_code, &content, nullptr, nullptr};

    CHECK(webGet(arg, result) == 403);
    CHECK(status_code == 403);
    CHECK(content.empty());
}

TEST_CASE("webget cache branches work without network when NO_WEBGET is enabled")
{
    const std::string url = "https://unit.test/cache-path";
    const std::string body_path = cachePathFor(url);
    const std::string header_path = body_path + "_header";

    flushCache();
    md("cache");

    REQUIRE(fileWrite(body_path, "cached-body", true) == 0);
    REQUIRE(fileWrite(header_path, "cached-header", true) == 0);

    std::string response_headers;
    CHECK(webGet(url, "", 60, &response_headers) == "cached-body");
    CHECK(response_headers == "cached-header");

    {
        ServeCacheGuard guard(true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        response_headers.clear();
        CHECK(webGet(url, "", 1, &response_headers) == "cached-body");
        CHECK(response_headers == "cached-header");
    }

    {
        ServeCacheGuard guard(false);
        response_headers.clear();
        CHECK(webGet(url, "", 1, &response_headers).empty());
    }
}
