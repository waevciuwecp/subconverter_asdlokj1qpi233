#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>

#include "utils/file.h"
#include "server/webserver.h"

TEST_CASE("webserver ua blocker matches brand and sensitive app keywords")
{
    namespace fs = std::filesystem;

    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path temp_dir = fs::temp_directory_path() / ("subconverter_webserver_ut_" + std::to_string(now));
    fs::create_directories(temp_dir);
    const fs::path keywords_path = temp_dir / "ua_keywords.list";

    const std::string keywords = R"(# comments and empty lines are ignored

huawei
miuibrowser
ucbrowser
baiduboxapp
micromessenger
)";
    REQUIRE(fileWrite(keywords_path.string(), keywords, true) == 0);

    WebServer server;
    server.ua_block_keywords_path = keywords_path.string();
    server.ua_block_enabled = true;

    std::string matched_keyword;

    CHECK(server.is_user_agent_blocked("Mozilla/5.0 (Linux; Android 10; HMA-AL00 Build/HUAWEIHMA-AL00) Safari/537.36", &matched_keyword));
    CHECK(matched_keyword == "huawei");

    CHECK(server.is_user_agent_blocked("Mozilla/5.0 XiaoMi/MiuiBrowser/11.10.8", &matched_keyword));
    CHECK(matched_keyword == "miuibrowser");

    CHECK(server.is_user_agent_blocked("Mozilla/5.0 UCBrowser/12.9.0.1070 Mobile", &matched_keyword));
    CHECK(matched_keyword == "ucbrowser");

    CHECK(server.is_user_agent_blocked("Mozilla/5.0 baiduboxapp/11.20.0.14 (Baidu; P1 10)", &matched_keyword));
    CHECK(matched_keyword == "baiduboxapp");

    CHECK(server.is_user_agent_blocked("Mozilla/5.0 MicroMessenger/7.0.12.1620", &matched_keyword));
    CHECK(matched_keyword == "micromessenger");

    CHECK(server.is_user_agent_blocked("Mozilla/5.0 (Linux; Android 14; M2102K1C Build/UKQ1.240624.001; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/142.0.7444.173 Mobile Safari/537.36 XWEB/1420273 MMWEBSDK/20260101 MMWEBID/3026 REV/04f9d4e638f33b1909b8f293dffa1cf978d8d0a3 MicroMessenger/8.0.68.3020(0x28004458) WeChat/arm64 Weixin NetType/4G Language/zh_CN ABI/arm64", &matched_keyword));

    CHECK_FALSE(server.is_user_agent_blocked("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Safari/537.36", &matched_keyword));

    fs::remove_all(temp_dir);
}

TEST_CASE("webserver ua blocker can be disabled")
{
    WebServer server;
    server.ua_block_enabled = false;
    CHECK_FALSE(server.is_user_agent_blocked("Mozilla/5.0 (Linux; Android 10; HMA-AL00)"));
}

TEST_CASE("webserver ua blocker has no active patterns when keyword file is missing")
{
    WebServer server;
    server.ua_block_enabled = true;
    server.ua_block_keywords_path = "/tmp/subconverter_keyword_file_does_not_exist.list";

    CHECK_FALSE(server.is_user_agent_blocked("Mozilla/5.0 (Linux; Android 14; M2102K1C Build/UKQ1.240624.001; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/142.0.7444.173 Mobile Safari/537.36 XWEB/1420273 MMWEBSDK/20260101 MMWEBID/3026 REV/04f9d4e638f33b1909b8f293dffa1cf978d8d0a3 MicroMessenger/8.0.68.3020(0x28004458) WeChat/arm64 Weixin NetType/4G Language/zh_CN ABI/arm64"));
}
