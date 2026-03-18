#include <doctest/doctest.h>

#include <string_view>
#include <vector>

#include "utils/string.h"

TEST_CASE("string split/join basics")
{
    const string_array tokens = split("a,b,c", ",");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0] == "a");
    CHECK(tokens[1] == "b");
    CHECK(tokens[2] == "c");

    const std::vector<std::string_view> views = split(std::string_view("x|y|z"), '|');
    REQUIRE(views.size() == 3);
    CHECK(views[0] == "x");
    CHECK(views[2] == "z");

    CHECK(join(tokens, "-") == "a-b-c");
    CHECK(join(string_array{}, ",").empty());
    CHECK(join(string_array{"solo"}, ",") == "solo");
}

TEST_CASE("string split edge vectors")
{
    const string_array empty = split("", ",");
    CHECK(empty.empty());

    const string_array no_sep = split("abc", ",");
    REQUIRE(no_sep.size() == 1);
    CHECK(no_sep[0] == "abc");

    const string_array sparse = split(",a,,b,", ",");
    REQUIRE(sparse.size() == 4);
    CHECK(sparse[0] == "");
    CHECK(sparse[1] == "a");
    CHECK(sparse[2] == "");
    CHECK(sparse[3] == "b");

    const std::vector<std::string_view> views = split(std::string_view("|x||y|"), '|');
    REQUIRE(views.size() == 4);
    CHECK(views[0] == "");
    CHECK(views[1] == "x");
    CHECK(views[2] == "");
    CHECK(views[3] == "y");
}

TEST_CASE("string trimming and replacement")
{
    CHECK(trim("  abc  ") == "abc");
    CHECK(trimWhitespace(" \t abc \n", true, true) == "abc");
    CHECK(trimWhitespace(" \t\r\n", true, true).empty());
    CHECK(trimWhitespace("  a \n", false, true) == "  a");
    CHECK(trimQuote("\"quoted\"") == "quoted");
    CHECK(trimOf("__abc__", '_', true, false) == "abc__");
    CHECK(trimOf("__abc__", '_', false, true) == "__abc");
    CHECK(replaceAllDistinct("a--b--c", "--", ":") == "a:b:c");
    CHECK(replaceAllDistinct("aaa", "a", "a") == "aaa");
}

TEST_CASE("string query helpers")
{
    CHECK(getUrlArg("https://x.test/path?a=1&b=2", "a") == "1");
    CHECK(getUrlArg("a=1&b=2&a=3", "a") == "3");
    CHECK(getUrlArg("a=7&aa=8", "a") == "7");
    CHECK(getUrlArg("aa=1&ba=2&a=3", "a") == "3");
    CHECK(getUrlArg("k=v", "missing").empty());

    string_multimap args;
    args.emplace("token", "abc");
    args.emplace("token", "def");
    CHECK(getUrlArg(args, "token") == "abc");
}

TEST_CASE("string parseCommaKeyValue supports escaped separator")
{
    string_pair_array parsed;
    REQUIRE(parseCommaKeyValue("k1=v1,k2=v\\,2,solo", ",", parsed) == 0);
    REQUIRE(parsed.size() == 3);
    CHECK(parsed[0].first == "k1");
    CHECK(parsed[0].second == "v1");
    CHECK(parsed[1].first == "k2");
    CHECK(parsed[1].second == "v,2");
    CHECK(parsed[2].first == "{NONAME}");
    CHECK(parsed[2].second == "solo");

    parsed.clear();
    REQUIRE(parseCommaKeyValue("k1=v1\\,x\\,y,k2=,solo,=right", ",", parsed) == 0);
    REQUIRE(parsed.size() == 4);
    CHECK(parsed[0].first == "k1");
    CHECK(parsed[0].second == "v1,x,y");
    CHECK(parsed[1].first == "k2");
    CHECK(parsed[1].second == "");
    CHECK(parsed[2].first == "{NONAME}");
    CHECK(parsed[2].second == "solo");
    CHECK(parsed[3].first == "");
    CHECK(parsed[3].second == "right");
}

TEST_CASE("string escape conversion vectors")
{
    std::string escaped = "line1\\nline2\\r\\tend\\q";
    processEscapeChar(escaped);
    CHECK(escaped == std::string("line1\nline2\r\tend\\q"));

    processEscapeCharReverse(escaped);
    CHECK(escaped == "line1\\nline2\\r\\tend\\q");
}

TEST_CASE("string UTF-8 helpers")
{
    std::string with_bom = "\xEF\xBB\xBFhello";
    removeUTF8BOM(with_bom);
    CHECK(with_bom == "hello");
    CHECK(isStrUTF8("hello"));

    std::string without_bom = "hello";
    removeUTF8BOM(without_bom);
    CHECK(without_bom == "hello");

    const std::string utf8 = std::string("A") + "\xC2\xA2" + "\xE2\x82\xAC" + "\xF0\x9F\x98\x80";
    CHECK(UTF8ToCodePoint(utf8) == "A\\ua2\\u20ac\\u1f600");
    CHECK(isStrUTF8(utf8));

    const std::string invalid_continuation("\xE2\x28\xA1", 3);
    CHECK_FALSE(isStrUTF8(invalid_continuation));
    const std::string truncated_multibyte("\xE2\x82", 2);
    CHECK_FALSE(isStrUTF8(truncated_multibyte));
}
