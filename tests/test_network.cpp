#include <doctest/doctest.h>

#include <string>

#include "utils/network.h"

TEST_CASE("network IP validators")
{
    CHECK(isIPv4("192.168.1.1"));
    CHECK(isIPv4("0.0.0.0"));
    CHECK(isIPv4("255.255.255.255"));
    CHECK_FALSE(isIPv4("256.1.1.1"));
    CHECK_FALSE(isIPv4("1.1.1"));
    CHECK_FALSE(isIPv4("1.1.1.1.1"));
    CHECK_FALSE(isIPv4("-1.2.3.4"));

    CHECK(isIPv6("2001:db8::1"));
    CHECK(isIPv6("2001:0db8:0000:0000:0000:ff00:0042:8329"));
    CHECK(isIPv6("::1"));
    CHECK(isIPv6("1::"));
    CHECK_FALSE(isIPv6("2001:::1"));
    CHECK_FALSE(isIPv6("2001:db8::g1"));
}

TEST_CASE("network urlParse handles scheme host port and path")
{
    std::string url = "https://example.com/api?q=1";
    std::string host;
    std::string path;
    int port = 0;
    bool is_tls = false;
    urlParse(url, host, path, port, is_tls);
    CHECK(host == "example.com");
    CHECK(path == "/api?q=1");
    CHECK(port == 443);
    CHECK(is_tls);

    std::string url_v6 = "http://[2001:db8::1]:8443/a";
    is_tls = false;
    urlParse(url_v6, host, path, port, is_tls);
    CHECK(host == "2001:db8::1");
    CHECK(path == "/a");
    CHECK(port == 8443);
    CHECK_FALSE(is_tls);

    std::string plain_host = "example.com";
    is_tls = false;
    port = 0;
    urlParse(plain_host, host, path, port, is_tls);
    CHECK(host == "example.com");
    CHECK(path == "/");
    CHECK(port == 80);
    CHECK_FALSE(is_tls);

    std::string zero_port = "http://example.com:0/api";
    is_tls = false;
    port = 0;
    urlParse(zero_port, host, path, port, is_tls);
    CHECK(host == "example.com");
    CHECK(path == "/api");
    CHECK(port == 80);
    CHECK_FALSE(is_tls);

    std::string v6_no_port = "https://[2001:db8::5]/v";
    is_tls = false;
    port = 0;
    urlParse(v6_no_port, host, path, port, is_tls);
    CHECK(host == "2001:db8::5");
    CHECK(path == "/v");
    CHECK(port == 443);
    CHECK(is_tls);
}

TEST_CASE("network isLink covers accepted schemes")
{
    CHECK(isLink("http://example.com"));
    CHECK(isLink("https://example.com"));
    CHECK(isLink("data:text/plain,abc"));
    CHECK_FALSE(isLink("ftp://example.com"));
}

TEST_CASE("network getFormData multipart vectors")
{
    const std::string multipart =
        "--boundary\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"a.txt\"\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "hello\r\n"
        "world\r\n"
        "--boundary--\r\n";
    CHECK(getFormData(multipart) == "hello\r\nworld\r\n");

    const std::string empty_payload =
        "--b\r\n"
        "--b--\r\n";
    CHECK(getFormData(empty_payload).empty());
}

TEST_CASE("network hostnameToIPAddr numeric host")
{
    CHECK(hostnameToIPAddr("127.0.0.1") == "127.0.0.1");
}
