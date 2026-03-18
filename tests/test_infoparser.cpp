#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "config/regmatch.h"
#include "helpers/test_helpers.h"
#include "parser/infoparser.h"
#include "utils/base64/base64.h"

TEST_CASE("infoparser extracts Subscription-UserInfo from header")
{
    std::string result;
    const bool ok = getSubInfoFromHeader("Subscription-UserInfo: upload=1; download=2; total=3;\n", result);
    REQUIRE(ok);
    CHECK(result == "upload=1; download=2; total=3;");
}

TEST_CASE("infoparser returns false when Subscription-UserInfo header is absent")
{
    std::string result;
    CHECK_FALSE(getSubInfoFromHeader("Date: x\nServer: y\n", result));
    CHECK(result.empty());
}

TEST_CASE("infoparser extracts stream and time info from nodes")
{
    std::vector<Proxy> nodes = {makeProxyWithRemark("node A")};
    const RegexMatchConfigs stream_rules = {RegexMatchConfig{"^.*$", "total=10GB&used=2GB", ""}};
    const RegexMatchConfigs time_rules = {RegexMatchConfig{"^.*$", "left=1d", ""}};

    std::string result;
    REQUIRE(getSubInfoFromNodes(nodes, stream_rules, time_rules, result));
    CHECK(containsText(result, "download=2147483648;"));
    CHECK(containsText(result, "total=10737418240;"));
    CHECK(containsText(result, "expire="));
}

TEST_CASE("infoparser handles percent stream formulas and invalid date format")
{
    std::vector<Proxy> nodes = {makeProxyWithRemark("node B")};
    std::string result;

    {
        const RegexMatchConfigs stream_rules = {RegexMatchConfig{"^.*$", "total=80%&used=2GB", ""}};
        const RegexMatchConfigs time_rules = {RegexMatchConfig{"^.*$", "2026:03:18:10:20:30", ""}};
        REQUIRE(getSubInfoFromNodes(nodes, stream_rules, time_rules, result));
        CHECK(containsText(result, "download=2147483648;"));
        CHECK(containsText(result, "total=10737418240;"));
        CHECK(containsText(result, "expire="));
    }

    {
        const RegexMatchConfigs stream_rules = {RegexMatchConfig{"^.*$", "total=25%&left=1GB", ""}};
        const RegexMatchConfigs time_rules = {RegexMatchConfig{"^.*$", "left=1d", ""}};
        REQUIRE(getSubInfoFromNodes(nodes, stream_rules, time_rules, result));
        CHECK(containsText(result, "download=3221225472;"));
        CHECK(containsText(result, "total=4294967296;"));
        CHECK(containsText(result, "expire="));
    }

    {
        const RegexMatchConfigs stream_rules = {RegexMatchConfig{"^.*$", "total=5GB&used=1GB", ""}};
        const RegexMatchConfigs time_rules = {RegexMatchConfig{"^.*$", "bad-date", ""}};
        REQUIRE(getSubInfoFromNodes(nodes, stream_rules, time_rules, result));
        CHECK(containsText(result, "download=1073741824;"));
        CHECK(containsText(result, "total=5368709120;"));
        CHECK_FALSE(containsText(result, "expire="));
    }
}

TEST_CASE("infoparser handles left greater than total and unmatched rules")
{
    std::vector<Proxy> nodes = {makeProxyWithRemark("node C")};
    std::string result;

    const RegexMatchConfigs stream_rules = {RegexMatchConfig{"^.*$", "total=1GB&left=2GB", ""}};
    const RegexMatchConfigs no_time_rules = {};
    REQUIRE(getSubInfoFromNodes(nodes, stream_rules, no_time_rules, result));
    CHECK(result == "upload=0; download=1073741824; total=1073741824;");

    const RegexMatchConfigs miss_rules = {RegexMatchConfig{"^unmatched$", "total=1GB&used=1GB", ""}};
    result.clear();
    CHECK_FALSE(getSubInfoFromNodes(nodes, miss_rules, miss_rules, result));
}

TEST_CASE("infoparser decodes SSD payload")
{
    const std::string json = R"({"traffic_used":"1","traffic_total":"5","expiry":"2025-01-02 03:04:05"})";
    const std::string sub = "ssd://" + urlSafeBase64Encode(json);

    std::string result;
    REQUIRE(getSubInfoFromSSD(sub, result));
    CHECK(containsText(result, "download=1073741824;"));
    CHECK(containsText(result, "total=5368709120;"));
    CHECK(containsText(result, "expire="));
}

TEST_CASE("infoparser rejects malformed SSD payload")
{
    std::string result;
    const std::string missing_total_json = R"({"traffic_used":"1.5","expiry":"2026-03-18 10:20:30"})";
    const std::string invalid_json = "{broken json";
    CHECK_FALSE(getSubInfoFromSSD("ssd://" + urlSafeBase64Encode(missing_total_json), result));
    CHECK_FALSE(getSubInfoFromSSD("ssd://" + urlSafeBase64Encode(invalid_json), result));
}

TEST_CASE("infoparser streamToInt supports binary units")
{
    CHECK(streamToInt("") == 0ULL);
    CHECK(streamToInt("512MB") == 536870912ULL);
    CHECK(streamToInt("1.5GB") == 1610612736ULL);
    CHECK(streamToInt("3TB") == 3298534883328ULL);
}

TEST_CASE("infoparser streamToInt edge suffix behavior is deterministic")
{
    CHECK(streamToInt("42") == 1ULL);
    CHECK(streamToInt("2gb") == 1ULL);
    CHECK(streamToInt("abcGB") == 0ULL);
    CHECK(streamToInt("1EB") == 1152921504606846976ULL);
}

TEST_CASE("infoparser handles lowercase header name and empty header value")
{
    std::string result;
    REQUIRE(getSubInfoFromHeader("subscription-userinfo: upload=9; download=8; total=7;   \r\n", result));
    CHECK(result == "upload=9; download=8; total=7;");

    result.clear();
    CHECK_FALSE(getSubInfoFromHeader("Subscription-UserInfo:   \n", result));
}

TEST_CASE("infoparser nodes supports stream-only and time-only paths")
{
    std::vector<Proxy> nodes = {makeProxyWithRemark("node D")};
    std::string result;

    const RegexMatchConfigs stream_rules = {RegexMatchConfig{"^.*$", "total=2GB&used=512MB", ""}};
    const RegexMatchConfigs no_rules = {};
    REQUIRE(getSubInfoFromNodes(nodes, stream_rules, no_rules, result));
    CHECK(result == "upload=0; download=536870912; total=2147483648;");

    result.clear();
    const RegexMatchConfigs time_rules = {RegexMatchConfig{"^.*$", "2026:03:18:10:20:30", ""}};
    REQUIRE(getSubInfoFromNodes(nodes, no_rules, time_rules, result));
    CHECK(containsText(result, "upload=0; download=0; total=0;"));
    CHECK(containsText(result, "expire="));
}

TEST_CASE("infoparser nodes handles no-op replacement, duplicate args, and percent clamp")
{
    std::vector<Proxy> nodes = {makeProxyWithRemark("node E")};
    std::string result;

    {
        const RegexMatchConfigs stream_rules = {
            RegexMatchConfig{"^(node E)$", "$1", ""},
            RegexMatchConfig{"^node E$", "total=3GB&used=1GB", ""}
        };
        const RegexMatchConfigs no_time_rules = {};
        REQUIRE(getSubInfoFromNodes(nodes, stream_rules, no_time_rules, result));
        CHECK(result == "upload=0; download=1073741824; total=3221225472;");
    }

    {
        const RegexMatchConfigs stream_rules = {RegexMatchConfig{"^.*$", "total=1GB&used=128MB&total=2GB&left=1GB", ""}};
        const RegexMatchConfigs no_time_rules = {};
        REQUIRE(getSubInfoFromNodes(nodes, stream_rules, no_time_rules, result));
        CHECK(result == "upload=0; download=134217728; total=2147483648;");
    }

    {
        const RegexMatchConfigs stream_rules = {RegexMatchConfig{"^.*$", "total=200%&left=1GB", ""}};
        const RegexMatchConfigs no_time_rules = {};
        REQUIRE(getSubInfoFromNodes(nodes, stream_rules, no_time_rules, result));
        CHECK(result == "upload=0; download=536870912; total=536870912;");
    }
}

TEST_CASE("infoparser nodes can extract stream and time across different nodes and stop early")
{
    std::vector<Proxy> nodes = {
        makeProxyWithRemark("S:total=3GB&used=1GB"),
        makeProxyWithRemark("T:bad-date"),
        makeProxyWithRemark("S:total=9GB&used=9GB")
    };
    const RegexMatchConfigs stream_rules = {RegexMatchConfig{"^S:(.*)$", "$1", ""}};
    const RegexMatchConfigs time_rules = {RegexMatchConfig{"^T:(.*)$", "$1", ""}};

    std::string result;
    REQUIRE(getSubInfoFromNodes(nodes, stream_rules, time_rules, result));
    CHECK(result == "upload=0; download=1073741824; total=3221225472;");
}

TEST_CASE("infoparser SSD accepts valid payload without expiry and does not enforce scheme")
{
    const std::string json = R"({"traffic_used":"1.5","traffic_total":"2.5","expiry":""})";
    const std::string encoded = urlSafeBase64Encode(json);
    std::string result;

    REQUIRE(getSubInfoFromSSD("ssd://" + encoded, result));
    CHECK(result == "upload=0; download=1610612736; total=2684354560;");

    result.clear();
    REQUIRE(getSubInfoFromSSD("foo://" + encoded, result));
    CHECK(result == "upload=0; download=1610612736; total=2684354560;");
}

TEST_CASE("infoparser throws on malformed percent and too-short SSD prefix")
{
    const std::vector<Proxy> nodes = {makeProxyWithRemark("node F")};
    const RegexMatchConfigs bad_percent_stream = {RegexMatchConfig{"^.*$", "total=abc%&used=1GB", ""}};
    const RegexMatchConfigs no_rules = {};
    std::string result;

    CHECK_THROWS_AS(getSubInfoFromNodes(nodes, bad_percent_stream, no_rules, result), std::invalid_argument);
    CHECK_THROWS_AS(getSubInfoFromSSD("ssd:/", result), std::out_of_range);
}
