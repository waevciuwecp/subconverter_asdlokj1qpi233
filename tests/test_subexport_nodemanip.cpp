#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <rapidjson/document.h>

#include "generator/config/subexport.h"
#include "generator/config/nodemanip.h"
#include "handler/settings.h"
#include "parser/subparser.h"
#include "utils/base64/base64.h"

namespace
{
Proxy makeShadowsocksProxy(const std::string &remark, const std::string &group = "UnitGroup")
{
    Proxy node;
    node.Type = ProxyType::Shadowsocks;
    node.Remark = remark;
    node.Group = group;
    node.GroupId = 1;
    node.Hostname = "1.1.1.1";
    node.Port = 8388;
    node.EncryptMethod = "aes-128-gcm";
    node.Password = "password";
    return node;
}

Proxy makeWireGuardProxy(const std::string &remark)
{
    Proxy node;
    node.Type = ProxyType::WireGuard;
    node.Remark = remark;
    node.Group = "WG";
    node.GroupId = 1;
    node.Hostname = "wg.example.com";
    node.Port = 51820;
    node.SelfIP = "10.0.0.2";
    node.PrivateKey = "private-key";
    node.PublicKey = "public-key";
    node.AllowedIPs = "0.0.0.0/0";
    node.Mtu = 1280;
    return node;
}

const rapidjson::Value *findOutboundByTag(const rapidjson::Document &doc, const std::string &tag)
{
    if(!doc.HasMember("outbounds") || !doc["outbounds"].IsArray())
        return nullptr;

    for(const auto &outbound : doc["outbounds"].GetArray())
    {
        if(!outbound.IsObject() || !outbound.HasMember("tag") || !outbound["tag"].IsString())
            continue;
        if(tag == outbound["tag"].GetString())
            return &outbound;
    }
    return nullptr;
}

bool jsonArrayContainsString(const rapidjson::Value &array, const std::string &value)
{
    if(!array.IsArray())
        return false;
    for(const auto &item : array.GetArray())
    {
        if(item.IsString() && value == item.GetString())
            return true;
    }
    return false;
}

std::string makeDataUrl(const std::string &content)
{
    return "data:text/plain;base64," + urlSafeBase64Encode(content);
}

struct SingBoxModesGuard
{
    bool old_value = global.singBoxAddClashModes;

    explicit SingBoxModesGuard(bool replacement)
    {
        global.singBoxAddClashModes = replacement;
    }

    ~SingBoxModesGuard()
    {
        global.singBoxAddClashModes = old_value;
    }
};

} // namespace

TEST_CASE("nodemanip matcher handles GROUP/INSERT/TYPE/PORT/SERVER branches deterministically")
{
    Proxy node;
    node.Type = ProxyType::VMess;
    node.Group = "HK-Edge";
    node.GroupId = 3;
    node.Port = 443;
    node.Hostname = "edge.example.com";
    node.Remark = "HK vmess";

    std::string real_rule;
    REQUIRE(applyMatcher("!!GROUP=^HK-.*$!!HK", real_rule, node));
    CHECK(real_rule == "HK");

    REQUIRE(applyMatcher("!!GROUPID=2-4!!ok", real_rule, node));
    CHECK(real_rule == "ok");

    REQUIRE(applyMatcher("!!INSERT=-3!!ok", real_rule, node));
    CHECK(real_rule == "ok");

    REQUIRE(applyMatcher("!!TYPE=VMESS!!ok", real_rule, node));
    CHECK(real_rule == "ok");

    REQUIRE(applyMatcher("!!PORT=443!!ok", real_rule, node));
    CHECK(real_rule == "ok");

    REQUIRE(applyMatcher("!!SERVER=edge\\.example\\.com!!ok", real_rule, node));
    CHECK(real_rule == "ok");

    CHECK_FALSE(applyMatcher("!!TYPE=SS!!ignored", real_rule, node));
}

TEST_CASE("nodemanip filterNodes applies matcher-driven include/exclude and rewrites ids")
{
    std::vector<Proxy> nodes;

    Proxy a;
    a.Type = ProxyType::VMess;
    a.Remark = "HK-A";
    a.Group = "G";
    a.GroupId = 1;
    a.Port = 443;
    a.Hostname = "a.example.com";
    nodes.push_back(a);

    Proxy b = a;
    b.Remark = "US-B";
    b.Hostname = "b.example.com";
    nodes.push_back(b);

    Proxy c = a;
    c.Type = ProxyType::Shadowsocks;
    c.Remark = "HK-C";
    c.Port = 80;
    c.Hostname = "c.example.com";
    nodes.push_back(c);

    string_array exclude_remarks = {"!!TYPE=VMESS!!US"};
    string_array include_remarks = {"!!PORT=443!!HK"};

    filterNodes(nodes, exclude_remarks, include_remarks, 9);

    REQUIRE(nodes.size() == 1);
    CHECK(nodes[0].Remark == "HK-A");
    CHECK(nodes[0].Id == 0);
    CHECK(nodes[0].GroupId == 9);
}

TEST_CASE("subexport clash resolves provider use list with dedupe and regex filters")
{
    std::vector<Proxy> nodes = {makeShadowsocksProxy("BaseNode")};
    std::vector<RulesetContent> rulesets;

    ProxyGroupConfig group;
    group.Name = "AUTO";
    group.Type = ProxyGroupType::Select;
    group.UsingProvider = {"A"};
    group.ProviderFilterRules = {"^B$", "^A$"};

    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.clash_new_field_name = true;
    ext.clash_proxy_providers = {
        ClashProxyProviderConfig{"A", "http", "https://example.invalid/a.yaml", "a.yaml", 600},
        ClashProxyProviderConfig{"B", "http", "https://example.invalid/b.yaml", "b.yaml", 0},
    };

    const std::string output = proxyToClash(nodes, "{}", rulesets, ProxyGroupConfigs{group}, false, ext);
    YAML::Node root = YAML::Load(output);

    REQUIRE(root["proxy-groups"].IsDefined());
    REQUIRE(root["proxy-groups"].size() == 1);

    const YAML::Node use = root["proxy-groups"][0]["use"];
    REQUIRE(use.IsSequence());
    REQUIRE(use.size() == 2);
    CHECK(use[0].as<std::string>() == "A");
    CHECK(use[1].as<std::string>() == "B");

    REQUIRE(root["proxy-providers"].IsDefined());
    CHECK(root["proxy-providers"]["A"]["interval"].as<int>() == 600);
    CHECK(root["proxy-providers"]["B"]["url"].as<std::string>() == "https://example.invalid/b.yaml");
}

TEST_CASE("subexport sing-box migration rewrites legacy dns and ntp structures for >=1.12.0")
{
    std::vector<Proxy> nodes;
    std::vector<RulesetContent> rulesets;

    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.singbox_version = "1.12.0";

    const std::string base_conf = R"({
  "dns": {
    "fakeip": {
      "inet4_range": "198.18.0.0/15",
      "inet6_range": "fc00::/18"
    },
    "servers": [
      {
        "tag": "dns_resolver",
        "address": "https://dns.example:8443/dns-query",
        "detour": "DIRECT",
        "address_resolver": "bootstrap",
        "address_strategy": "prefer_ipv4"
      },
      {
        "tag": "drop_rcode",
        "address": "rcode://success"
      },
      {
        "tag": "udp_default",
        "address": "8.8.8.8"
      }
    ],
    "rules": [
      {
        "server": "dns_resolver",
        "domain_suffix": ["example.com"]
      },
      {
        "outbound": "DIRECT",
        "server": "dns_resolver"
      },
      {
        "server": "missing",
        "domain": ["drop.test"]
      }
    ]
  },
  "ntp": {
    "enabled": true,
    "detour": "DIRECT"
  }
})";

    const std::string output = proxyToSingBox(nodes, base_conf, rulesets, ProxyGroupConfigs{}, ext);
    rapidjson::Document doc;
    doc.Parse(output.c_str());

    REQUIRE(!doc.HasParseError());
    REQUIRE(doc.HasMember("dns"));
    CHECK_FALSE(doc["dns"].HasMember("fakeip"));

    const auto &servers = doc["dns"]["servers"];
    REQUIRE(servers.IsArray());
    CHECK(servers.Size() == 2);

    const rapidjson::Value *dns_resolver = nullptr;
    for(const auto &server : servers.GetArray())
    {
        if(server.IsObject() && server.HasMember("tag") && server["tag"].IsString() &&
           std::string(server["tag"].GetString()) == "dns_resolver")
        {
            dns_resolver = &server;
            break;
        }
    }
    REQUIRE(dns_resolver != nullptr);
    CHECK(std::string((*dns_resolver)["type"].GetString()) == "https");
    CHECK(std::string((*dns_resolver)["server"].GetString()) == "dns.example");
    CHECK((*dns_resolver)["server_port"].GetInt() == 8443);
    CHECK(std::string((*dns_resolver)["path"].GetString()) == "/dns-query");
    CHECK_FALSE(dns_resolver->HasMember("detour"));
    CHECK(std::string((*dns_resolver)["domain_resolver"].GetString()) == "bootstrap");
    CHECK(std::string((*dns_resolver)["domain_strategy"].GetString()) == "prefer_ipv4");

    const auto &rules = doc["dns"]["rules"];
    REQUIRE(rules.IsArray());
    REQUIRE(rules.Size() == 1);
    CHECK(rules[0].HasMember("action"));
    CHECK(std::string(rules[0]["action"].GetString()) == "route");

    REQUIRE(doc.HasMember("route"));
    CHECK(std::string(doc["route"]["default_domain_resolver"].GetString()) == "dns_resolver");

    REQUIRE(doc.HasMember("ntp"));
    CHECK_FALSE(doc["ntp"].HasMember("detour"));
}

TEST_CASE("subexport sing-box provider fallback via data URL is deterministic and isolated from GLOBAL")
{
    SingBoxModesGuard guard(true);

    std::vector<Proxy> nodes = {makeShadowsocksProxy("LocalNode", "LocalGroup")};
    std::vector<RulesetContent> rulesets;

    const std::string provider_yaml =
        "proxies:\n"
        "  - name: ProviderNode\n"
        "    type: ss\n"
        "    server: 2.2.2.2\n"
        "    port: 443\n"
        "    cipher: aes-128-gcm\n"
        "    password: provider-pass\n";

    ProxyGroupConfig group;
    group.Name = "AUTO";
    group.Type = ProxyGroupType::Select;
    group.UsingProvider = {"pp"};

    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.singbox_version = "1.12.0";
    ext.clash_proxy_providers = {
        ClashProxyProviderConfig{"pp", "http", makeDataUrl(provider_yaml), "providers/pp.yaml", 300},
    };

    const std::string output = proxyToSingBox(nodes, "{}", rulesets, ProxyGroupConfigs{group}, ext);

    rapidjson::Document doc;
    doc.Parse(output.c_str());
    REQUIRE(!doc.HasParseError());

    const rapidjson::Value *provider_proxy = findOutboundByTag(doc, "ProviderNode");
    REQUIRE(provider_proxy != nullptr);

    const rapidjson::Value *auto_group = findOutboundByTag(doc, "AUTO");
    REQUIRE(auto_group != nullptr);
    REQUIRE(auto_group->HasMember("outbounds"));
    CHECK(jsonArrayContainsString((*auto_group)["outbounds"], "ProviderNode"));

    const rapidjson::Value *global_group = findOutboundByTag(doc, "GLOBAL");
    REQUIRE(global_group != nullptr);
    REQUIRE(global_group->HasMember("outbounds"));
    CHECK(jsonArrayContainsString((*global_group)["outbounds"], "DIRECT"));
    CHECK(jsonArrayContainsString((*global_group)["outbounds"], "LocalNode"));
    CHECK_FALSE(jsonArrayContainsString((*global_group)["outbounds"], "ProviderNode"));
}

TEST_CASE("subexport sing-box wireguard outbound obeys 1.13.0 removal gate")
{
    std::vector<RulesetContent> rulesets;
    const ProxyGroupConfigs groups;

    SUBCASE(">=1.13.0 skips wireguard outbounds")
    {
        std::vector<Proxy> nodes = {makeWireGuardProxy("WG-Node")};
        extra_settings ext;
        ext.enable_rule_generator = false;
        ext.nodelist = true;
        ext.singbox_version = "1.13.0";

        const std::string output = proxyToSingBox(nodes, "{}", rulesets, groups, ext);
        rapidjson::Document doc;
        doc.Parse(output.c_str());
        REQUIRE(!doc.HasParseError());

        CHECK(findOutboundByTag(doc, "WG-Node") == nullptr);
    }

    SUBCASE("<1.13.0 keeps wireguard outbounds")
    {
        std::vector<Proxy> nodes = {makeWireGuardProxy("WG-Node")};
        extra_settings ext;
        ext.enable_rule_generator = false;
        ext.nodelist = true;
        ext.singbox_version = "1.12.0";

        const std::string output = proxyToSingBox(nodes, "{}", rulesets, groups, ext);
        rapidjson::Document doc;
        doc.Parse(output.c_str());
        REQUIRE(!doc.HasParseError());

        const rapidjson::Value *wg = findOutboundByTag(doc, "WG-Node");
        REQUIRE(wg != nullptr);
        CHECK(std::string((*wg)["type"].GetString()) == "wireguard");
    }
}

TEST_CASE("vless std link keeps ws host distinct from tls sni")
{
    const std::string link =
        "vless://11111111-1111-1111-1111-111111111111@example.com:443"
        "?encryption=none&security=tls&type=ws&host=ws.example.com&sni=tls.example.com&path=%2Fws#ws-host-sni";

    Proxy node;
    explode(link, node);

    REQUIRE(node.Type == ProxyType::VLESS);
    CHECK(node.TransferProtocol == "ws");
    CHECK(node.TLSSecure);
    CHECK(node.Host == "ws.example.com");
    CHECK(node.ServerName == "tls.example.com");
    CHECK(node.Path == "/ws");
}

TEST_CASE("vless std link keeps ws host fallback to sni when host missing")
{
    const std::string link =
        "vless://22222222-2222-2222-2222-222222222222@example.com:443"
        "?encryption=none&security=tls&type=ws&sni=tls-only.example.com&path=%2Fedge#ws-sni-only";

    Proxy node;
    explode(link, node);

    REQUIRE(node.Type == ProxyType::VLESS);
    CHECK(node.TransferProtocol == "ws");
    CHECK(node.TLSSecure);
    CHECK(node.Host == "tls-only.example.com");
    CHECK(node.ServerName == "tls-only.example.com");
    CHECK(node.Path == "/edge");
}

TEST_CASE("vless ws/tls export to clash preserves ws host and tls sni")
{
    const std::string link =
        "vless://11111111-1111-1111-1111-111111111111@example.com:443"
        "?encryption=none&security=tls&type=ws&host=ws.example.com&sni=tls.example.com&path=%2Fws#ws-host-sni";

    Proxy node;
    explode(link, node);
    REQUIRE(node.Type == ProxyType::VLESS);

    std::vector<Proxy> nodes = {node};
    std::vector<RulesetContent> rulesets;
    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.nodelist = true;
    ext.clash_new_field_name = true;

    const std::string output = proxyToClash(nodes, "{}", rulesets, ProxyGroupConfigs{}, false, ext);
    YAML::Node root = YAML::Load(output);

    REQUIRE(root["proxies"].IsDefined());
    REQUIRE(root["proxies"].IsSequence());
    REQUIRE(root["proxies"].size() == 1);

    const YAML::Node proxy = root["proxies"][0];
    CHECK(proxy["type"].as<std::string>() == "vless");
    CHECK(proxy["network"].as<std::string>() == "ws");
    CHECK(proxy["tls"].as<bool>());
    CHECK(proxy["ws-opts"]["path"].as<std::string>() == "/ws");
    CHECK(proxy["ws-opts"]["headers"]["Host"].as<std::string>() == "ws.example.com");
    CHECK(proxy["servername"].as<std::string>() == "tls.example.com");
}

TEST_CASE("vless ws/tls export to sing-box preserves ws host and tls sni")
{
    const std::string link =
        "vless://11111111-1111-1111-1111-111111111111@example.com:443"
        "?encryption=none&security=tls&type=ws&host=ws.example.com&sni=tls.example.com&path=%2Fws#ws-host-sni";

    Proxy node;
    explode(link, node);
    REQUIRE(node.Type == ProxyType::VLESS);

    std::vector<Proxy> nodes = {node};
    std::vector<RulesetContent> rulesets;
    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.nodelist = true;
    ext.singbox_version = "1.14.0";

    const std::string output = proxyToSingBox(nodes, "{}", rulesets, ProxyGroupConfigs{}, ext);
    rapidjson::Document doc;
    doc.Parse(output.c_str());

    REQUIRE(!doc.HasParseError());
    const rapidjson::Value *outbound = findOutboundByTag(doc, node.Remark);
    REQUIRE(outbound != nullptr);
    REQUIRE(outbound->HasMember("type"));
    REQUIRE(outbound->HasMember("transport"));
    REQUIRE(outbound->HasMember("tls"));

    CHECK(std::string((*outbound)["type"].GetString()) == "vless");
    CHECK(std::string((*outbound)["transport"]["type"].GetString()) == "ws");
    CHECK(std::string((*outbound)["transport"]["path"].GetString()) == "/ws");
    CHECK(std::string((*outbound)["transport"]["headers"]["Host"].GetString()) == "ws.example.com");
    CHECK((*outbound)["tls"]["enabled"].GetBool());
    CHECK(std::string((*outbound)["tls"]["server_name"].GetString()) == "tls.example.com");
}

TEST_CASE("vless std link keeps grpc authority distinct from tls sni")
{
    const std::string link =
        "vless://33333333-3333-3333-3333-333333333333@example.com:443"
        "?encryption=none&security=tls&type=grpc&serviceName=my-grpc&mode=multi"
        "&authority=grpc-authority.example.com&sni=tls.example.com#grpc-host-sni";

    Proxy node;
    explode(link, node);

    REQUIRE(node.Type == ProxyType::VLESS);
    CHECK(node.TransferProtocol == "grpc");
    CHECK(node.TLSSecure);
    CHECK(node.Host == "grpc-authority.example.com");
    CHECK(node.ServerName == "tls.example.com");
    CHECK(node.GRPCServiceName == "my-grpc");
    CHECK(node.GRPCMode == "multi");
}

TEST_CASE("vless std link keeps grpc host fallback to sni when authority missing")
{
    const std::string link =
        "vless://44444444-4444-4444-4444-444444444444@example.com:443"
        "?encryption=none&security=tls&type=grpc&serviceName=grpc-only&sni=tls-only.example.com#grpc-sni-only";

    Proxy node;
    explode(link, node);

    REQUIRE(node.Type == ProxyType::VLESS);
    CHECK(node.TransferProtocol == "grpc");
    CHECK(node.TLSSecure);
    CHECK(node.Host == "tls-only.example.com");
    CHECK(node.ServerName == "tls-only.example.com");
    CHECK(node.GRPCServiceName == "grpc-only");
}

TEST_CASE("vless grpc/tls export to clash preserves grpc fields and tls sni")
{
    const std::string link =
        "vless://33333333-3333-3333-3333-333333333333@example.com:443"
        "?encryption=none&security=tls&type=grpc&serviceName=my-grpc&mode=multi"
        "&authority=grpc-authority.example.com&sni=tls.example.com#grpc-host-sni";

    Proxy node;
    explode(link, node);
    REQUIRE(node.Type == ProxyType::VLESS);

    std::vector<Proxy> nodes = {node};
    std::vector<RulesetContent> rulesets;
    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.nodelist = true;
    ext.clash_new_field_name = true;

    const std::string output = proxyToClash(nodes, "{}", rulesets, ProxyGroupConfigs{}, false, ext);
    YAML::Node root = YAML::Load(output);

    REQUIRE(root["proxies"].IsDefined());
    REQUIRE(root["proxies"].IsSequence());
    REQUIRE(root["proxies"].size() == 1);

    const YAML::Node proxy = root["proxies"][0];
    CHECK(proxy["type"].as<std::string>() == "vless");
    CHECK(proxy["network"].as<std::string>() == "grpc");
    CHECK(proxy["tls"].as<bool>());
    CHECK(proxy["grpc-opts"]["grpc-service-name"].as<std::string>() == "my-grpc");
    CHECK(proxy["grpc-opts"]["grpc-mode"].as<std::string>() == "multi");
    CHECK(proxy["servername"].as<std::string>() == "tls.example.com");
}

TEST_CASE("vless grpc/tls export to sing-box preserves grpc fields and tls sni")
{
    const std::string link =
        "vless://33333333-3333-3333-3333-333333333333@example.com:443"
        "?encryption=none&security=tls&type=grpc&serviceName=my-grpc&mode=multi"
        "&authority=grpc-authority.example.com&sni=tls.example.com#grpc-host-sni";

    Proxy node;
    explode(link, node);
    REQUIRE(node.Type == ProxyType::VLESS);

    std::vector<Proxy> nodes = {node};
    std::vector<RulesetContent> rulesets;
    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.nodelist = true;
    ext.singbox_version = "1.14.0";

    const std::string output = proxyToSingBox(nodes, "{}", rulesets, ProxyGroupConfigs{}, ext);
    rapidjson::Document doc;
    doc.Parse(output.c_str());

    REQUIRE(!doc.HasParseError());
    const rapidjson::Value *outbound = findOutboundByTag(doc, node.Remark);
    REQUIRE(outbound != nullptr);
    REQUIRE(outbound->HasMember("type"));
    REQUIRE(outbound->HasMember("transport"));
    REQUIRE(outbound->HasMember("tls"));

    CHECK(std::string((*outbound)["type"].GetString()) == "vless");
    CHECK(std::string((*outbound)["transport"]["type"].GetString()) == "grpc");
    CHECK(std::string((*outbound)["transport"]["service_name"].GetString()) == "my-grpc");
    CHECK((*outbound)["tls"]["enabled"].GetBool());
    CHECK(std::string((*outbound)["tls"]["server_name"].GetString()) == "tls.example.com");
}

TEST_CASE("vless std link keeps xhttp transport and core fields")
{
    const std::string link =
        "vless://55555555-5555-5555-5555-555555555555@example.com:443"
        "?encryption=none&pqv=pqv-token&security=tls&type=xhttp&host=xhttp.example.com&path=%2Fxhttp&mode=stream-up"
        "&alpn=h3&sni=tls.example.com#xhttp-node";

    Proxy node;
    explode(link, node);

    REQUIRE(node.Type == ProxyType::VLESS);
    CHECK(node.TransferProtocol == "xhttp");
    CHECK(node.Host == "xhttp.example.com");
    CHECK(node.Path == "/xhttp");
    CHECK(node.XHTTPMode == "stream-up");
    CHECK(node.PQV == "pqv-token");
    CHECK(node.ServerName == "tls.example.com");
    REQUIRE(node.AlpnList.size() == 1);
    CHECK(node.AlpnList[0] == "h3");
}

TEST_CASE("vless std link keeps httpupgrade transport")
{
    const std::string link =
        "vless://66666666-6666-6666-6666-666666666666@example.com:443"
        "?encryption=none&security=tls&type=httpupgrade&host=hu.example.com&path=%2Fupgrade&sni=tls.example.com#httpupgrade-node";

    Proxy node;
    explode(link, node);

    REQUIRE(node.Type == ProxyType::VLESS);
    CHECK(node.TransferProtocol == "httpupgrade");
    CHECK(node.Host == "hu.example.com");
    CHECK(node.Path == "/upgrade");
    CHECK(node.ServerName == "tls.example.com");
}

TEST_CASE("vless std link keeps splithttp transport and h3 alpn")
{
    const std::string link =
        "vless://77777777-7777-7777-7777-777777777777@example.com:443"
        "?encryption=none&security=tls&type=splithttp&host=split.example.com&path=%2Fsplit&mode=packet-up"
        "&alpn=h3&sni=tls.example.com#splithttp-node";

    Proxy node;
    explode(link, node);

    REQUIRE(node.Type == ProxyType::VLESS);
    CHECK(node.TransferProtocol == "splithttp");
    CHECK(node.Host == "split.example.com");
    CHECK(node.Path == "/split");
    CHECK(node.XHTTPMode == "packet-up");
    REQUIRE(node.AlpnList.size() == 1);
    CHECK(node.AlpnList[0] == "h3");
}

TEST_CASE("vless single export keeps xhttp type and mode")
{
    Proxy node;
    explode("vless://88888888-8888-8888-8888-888888888888@example.com:443"
            "?encryption=mlkem768x25519plus.native.1rtt.QUJDREVGR0g&pqv=pqv-export&security=tls&type=xhttp&host=xhttp.example.com&path=%2Fnode&mode=stream-one#xhttp-export",
            node);

    REQUIRE(node.Type == ProxyType::VLESS);
    std::vector<Proxy> nodes = {node};
    extra_settings ext;
    ext.nodelist = true;
    const std::string output = proxyToSingle(nodes, 32, ext);
    const std::string plain = output.find("vless://") != std::string::npos ? output : urlSafeBase64Decode(output);

    CHECK(plain.find("type=xhttp") != std::string::npos);
    CHECK(plain.find("mode=stream-one") != std::string::npos);
    CHECK(plain.find("path=") != std::string::npos);
    CHECK(plain.find("encryption=mlkem768x25519plus.native.1rtt.QUJDREVGR0g") != std::string::npos);
    CHECK(plain.find("pqv=pqv-export") != std::string::npos);
}

TEST_CASE("vless sing-box export keeps httpupgrade transport")
{
    Proxy node;
    explode("vless://99999999-9999-9999-9999-999999999999@example.com:443"
            "?encryption=none&security=tls&type=httpupgrade&host=hu.example.com&path=%2Fup&sni=tls.example.com#hu-export",
            node);

    REQUIRE(node.Type == ProxyType::VLESS);
    std::vector<Proxy> nodes = {node};
    std::vector<RulesetContent> rulesets;
    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.nodelist = true;
    ext.singbox_version = "1.14.0";

    const std::string output = proxyToSingBox(nodes, "{}", rulesets, ProxyGroupConfigs{}, ext);
    rapidjson::Document doc;
    doc.Parse(output.c_str());
    REQUIRE(!doc.HasParseError());

    const rapidjson::Value *outbound = findOutboundByTag(doc, node.Remark);
    REQUIRE(outbound != nullptr);
    REQUIRE(outbound->HasMember("transport"));
    CHECK(std::string((*outbound)["transport"]["type"].GetString()) == "httpupgrade");
    CHECK(std::string((*outbound)["transport"]["path"].GetString()) == "/up");
}

TEST_CASE("vless clash export keeps xhttp transport")
{
    Proxy node;
    explode("vless://aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa@example.com:443"
            "?encryption=none&security=tls&type=xhttp&host=xhttp.example.com&path=%2Fxhttp&mode=stream-one#xhttp-clash",
            node);

    REQUIRE(node.Type == ProxyType::VLESS);
    std::vector<Proxy> nodes = {node};
    std::vector<RulesetContent> rulesets;
    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.nodelist = true;
    ext.clash_new_field_name = true;

    const std::string output = proxyToClash(nodes, "{}", rulesets, ProxyGroupConfigs{}, false, ext);
    YAML::Node root = YAML::Load(output);
    REQUIRE(root["proxies"].IsDefined());
    REQUIRE(root["proxies"].size() == 1);
    YAML::Node clashNode = root["proxies"][0];
    CHECK(clashNode["network"].as<std::string>() == "xhttp");
    CHECK(clashNode["xhttp-opts"]["path"].as<std::string>() == "/xhttp");
    CHECK(clashNode["xhttp-opts"]["host"].as<std::string>() == "xhttp.example.com");
    CHECK(clashNode["xhttp-opts"]["mode"].as<std::string>() == "stream-one");
    CHECK(clashNode["xhttp-opts"]["headers"]["Host"].as<std::string>() == "xhttp.example.com");
}

TEST_CASE("vless clash export keeps mlkem encryption")
{
    Proxy node;
    explode("vless://abababab-abab-abab-abab-abababababab@example.com:443"
            "?encryption=mlkem768x25519plus.native.1rtt.QUJDREVGR0g&security=tls&type=tcp#mlkem-clash",
            node);

    REQUIRE(node.Type == ProxyType::VLESS);
    std::vector<Proxy> nodes = {node};
    std::vector<RulesetContent> rulesets;
    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.nodelist = true;
    ext.clash_new_field_name = true;

    const std::string output = proxyToClash(nodes, "{}", rulesets, ProxyGroupConfigs{}, false, ext);
    YAML::Node root = YAML::Load(output);
    REQUIRE(root["proxies"].IsDefined());
    REQUIRE(root["proxies"].size() == 1);
    YAML::Node clashNode = root["proxies"][0];
    CHECK(clashNode["name"].as<std::string>() == "mlkem-clash");
    CHECK(clashNode["encryption"].as<std::string>() == "mlkem768x25519plus.native.1rtt.QUJDREVGR0g");
}

TEST_CASE("vless clash export keeps pqv-only node")
{
    Proxy node;
    explode("vless://acacacac-acac-acac-acac-acacacacacac@example.com:443"
            "?encryption=none&pqv=pqv-token&security=tls&type=tcp#pqv-clash",
            node);

    REQUIRE(node.Type == ProxyType::VLESS);
    std::vector<Proxy> nodes = {node};
    std::vector<RulesetContent> rulesets;
    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.nodelist = true;
    ext.clash_new_field_name = true;

    const std::string output = proxyToClash(nodes, "{}", rulesets, ProxyGroupConfigs{}, false, ext);
    YAML::Node root = YAML::Load(output);
    REQUIRE(root["proxies"].IsDefined());
    REQUIRE(root["proxies"].size() == 1);
    YAML::Node clashNode = root["proxies"][0];
    CHECK(clashNode["name"].as<std::string>() == "pqv-clash");
    CHECK(!clashNode["pqv"].IsDefined());
}

TEST_CASE("vless sing-box export fail-closes splithttp transport")
{
    Proxy node;
    explode("vless://bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb@example.com:443"
            "?encryption=none&security=tls&type=splithttp&host=split.example.com&path=%2Fsplit&mode=stream-up#split-singbox",
            node);

    REQUIRE(node.Type == ProxyType::VLESS);
    std::vector<Proxy> nodes = {node};
    std::vector<RulesetContent> rulesets;
    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.nodelist = true;
    ext.singbox_version = "1.14.0";

    const std::string output = proxyToSingBox(nodes, "{}", rulesets, ProxyGroupConfigs{}, ext);
    rapidjson::Document doc;
    doc.Parse(output.c_str());
    REQUIRE(!doc.HasParseError());

    const rapidjson::Value *outbound = findOutboundByTag(doc, node.Remark);
    CHECK(outbound == nullptr);
}

TEST_CASE("vless sing-box export fail-closes mlkem and pqv nodes")
{
    Proxy mlkem;
    explode("vless://bcbcbcbc-bcbc-bcbc-bcbc-bcbcbcbcbcbc@example.com:443"
            "?encryption=mlkem768x25519plus.native.1rtt.QUJDREVGR0g&security=tls&type=tcp#mlkem-singbox",
            mlkem);
    Proxy pqv;
    explode("vless://cdcdcdcd-cdcd-cdcd-cdcd-cdcdcdcdcdcd@example.com:443"
            "?encryption=none&pqv=pqv-token&security=tls&type=tcp#pqv-singbox",
            pqv);

    REQUIRE(mlkem.Type == ProxyType::VLESS);
    REQUIRE(pqv.Type == ProxyType::VLESS);
    std::vector<Proxy> nodes = {mlkem, pqv};
    std::vector<RulesetContent> rulesets;
    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.nodelist = true;
    ext.singbox_version = "1.14.0";

    const std::string output = proxyToSingBox(nodes, "{}", rulesets, ProxyGroupConfigs{}, ext);
    rapidjson::Document doc;
    doc.Parse(output.c_str());
    REQUIRE(!doc.HasParseError());

    CHECK(findOutboundByTag(doc, "mlkem-singbox") == nullptr);
    CHECK(findOutboundByTag(doc, "pqv-singbox") == nullptr);
}

TEST_CASE("vless quanx export keeps post-quantum encryption field")
{
    Proxy node;
    explode("vless://dededede-dede-dede-dede-dededededede@example.com:443"
            "?encryption=mlkem768x25519plus.native.1rtt.QUJDREVGR0g&security=tls&type=tcp#mlkem-quanx",
            node);

    REQUIRE(node.Type == ProxyType::VLESS);
    std::vector<Proxy> nodes = {node};
    std::vector<RulesetContent> rulesets;
    extra_settings ext;
    ext.enable_rule_generator = false;
    ext.nodelist = true;

    const std::string output = proxyToQuanX(nodes, "", rulesets, ProxyGroupConfigs{}, ext);
    CHECK(output.find("vless = ") != std::string::npos);
    CHECK(output.find("encryption=mlkem768x25519plus.native.1rtt.QUJDREVGR0g") != std::string::npos);
}

TEST_CASE("clash full yaml with top-level keys parses vless encryption variants")
{
    const std::string content = R"yaml(
port: 7890
mode: Rule
proxies:
  - {name: awesome x25519, server: good-idea.yaoyy.moe, port: 2053, type: vless, uuid: d7e236b8-f483-4628-90b3-47f38a60e572, tls: true, flow: xtls-rprx-vision, encryption: mlkem768x25519plus.native.0rtt.AAAA, skip-cert-verify: false, reality-opts: {public-key: R6R8_dlcka-upDfSFkyOLuU4GEcHQXiRzyrJNbt9Ojs, short-id: "942e34625bf371" }, servername: www.oracle.com, client-fingerprint: chrome, network: http, http-opts: {method: GET, path: [/good], headers: {Host: [www.oracle.com]}}}
  - {name: awesome post-quantum, server: good-idea.yaoyy.moe, port: 2096, type: vless, uuid: aa3d175b-dba0-4c7a-aa31-a7986dc600e0, tls: true, flow: xtls-rprx-vision, encryption: mlkem768x25519plus.native.0rtt.BBBB, skip-cert-verify: false, reality-opts: {public-key: aRcEpcPWntX6JpI6aWFgS_9Z7rdkgMZafhMRJmDcS0E, short-id: "e4b18de4dc" }, servername: www.oracle.com, client-fingerprint: chrome, network: http, http-opts: {method: GET, path: [/bad], headers: {Host: [www.oracle.com]}}}
  - {name: awesome no-encr, server: good-idea.yaoyy.moe, port: 2087, type: vless, uuid: 37cdd9ef-b8d4-4fd2-95c7-6b3435f757d0, tls: true, flow: xtls-rprx-vision, skip-cert-verify: false, reality-opts: {public-key: 7O91nNzw4aOtRTW1S3qrChi3kjkRLAlG7Nu7E9qKwC4, short-id: "f6e6a78714e8" }, servername: www.oracle.com, client-fingerprint: chrome, network: http, http-opts: {method: GET, path: [/noway], headers: {Host: [www.oracle.com]}}}
)yaml";

    std::vector<Proxy> nodes;
    REQUIRE(explodeConfContent(content, nodes) == 1);
    REQUIRE(nodes.size() == 3);

    const auto findNode = [&](const std::string &remark) -> const Proxy * {
        for (const auto &node: nodes) {
            if (node.Remark == remark)
                return &node;
        }
        return nullptr;
    };

    const Proxy *x25519 = findNode("awesome x25519");
    REQUIRE(x25519 != nullptr);
    CHECK(x25519->Type == ProxyType::VLESS);
    CHECK(x25519->TransferProtocol == "http");
    CHECK(x25519->Encryption == "mlkem768x25519plus.native.0rtt.AAAA");

    const Proxy *postQuantum = findNode("awesome post-quantum");
    REQUIRE(postQuantum != nullptr);
    CHECK(postQuantum->Type == ProxyType::VLESS);
    CHECK(postQuantum->TransferProtocol == "http");
    CHECK(postQuantum->Encryption == "mlkem768x25519plus.native.0rtt.BBBB");

    const Proxy *noEncr = findNode("awesome no-encr");
    REQUIRE(noEncr != nullptr);
    CHECK(noEncr->Type == ProxyType::VLESS);
    CHECK(noEncr->TransferProtocol == "http");
    CHECK(noEncr->Encryption.empty());
}

TEST_CASE("xray jsonc vless xhttp and splithttp examples parse correctly")
{
    const std::string content = R"jsonc(
{
  "outbounds": [
    {
      "protocol": "vless",
      "tag": "xhttp-out",
      "settings": {
        "vnext": [{
          "address": "xhttp.example.com",
          "port": 443,
          "users": [{
            "id": "11111111-1111-1111-1111-111111111111",
            "encryption": "none",
            "flow": ""
          }]
        }]
      },
      "streamSettings": {
        "network": "xhttp",
        "security": "reality",
        "xhttpSettings": {
          "path": "/xhttp",
          "mode": "stream-up",
          "host": "xhttp.host.example"
        },
        "realitySettings": {
          "serverName": "reality.example.com",
          "publicKey": "pubkey",
          "shortId": "abcd",
          "fingerprint": "chrome"
        }
      }
    },
    {
      "protocol": "vless",
      "tag": "splithttp-out",
      "settings": {
        "vnext": [{
          "address": "split.example.com",
          "port": 443,
          "users": [{
            "id": "22222222-2222-2222-2222-222222222222",
            "encryption": "none"
          }]
        }]
      },
      "streamSettings": {
        "network": "splithttp",
        "security": "tls",
        "splithttpSettings": {
          "path": "/split",
          "mode": "packet-up",
          "host": "split.host.example"
        },
        "tlsSettings": {
          "serverName": "split.example.com",
          "alpn": ["h3"]
        }
      }
    }
  ]
}
)jsonc";

    std::vector<Proxy> nodes;
    REQUIRE(explodeConfContent(content, nodes) == 1);
    REQUIRE(nodes.size() == 2);

    const auto find_by_remark = [&](const std::string &remark) -> const Proxy * {
        for (const auto &node: nodes) {
            if (node.Remark == remark)
                return &node;
        }
        return nullptr;
    };

    const Proxy *xhttp = find_by_remark("xhttp-out");
    REQUIRE(xhttp != nullptr);
    CHECK(xhttp->Type == ProxyType::VLESS);
    CHECK(xhttp->TransferProtocol == "xhttp");
    CHECK(xhttp->Path == "/xhttp");
    CHECK(xhttp->Host == "xhttp.host.example");
    CHECK(xhttp->XHTTPMode == "stream-up");
    CHECK(xhttp->TLSStr == "reality");
    CHECK(xhttp->ServerName == "reality.example.com");
    CHECK(xhttp->PublicKey == "pubkey");
    CHECK(xhttp->ShortId == "abcd");

    const Proxy *splithttp = find_by_remark("splithttp-out");
    REQUIRE(splithttp != nullptr);
    CHECK(splithttp->Type == ProxyType::VLESS);
    CHECK(splithttp->TransferProtocol == "splithttp");
    CHECK(splithttp->Path == "/split");
    CHECK(splithttp->Host == "split.host.example");
    CHECK(splithttp->XHTTPMode == "packet-up");
    CHECK(splithttp->TLSStr == "tls");
    CHECK(splithttp->ServerName == "split.example.com");
    REQUIRE(splithttp->AlpnList.size() == 1);
    CHECK(splithttp->AlpnList[0] == "h3");
}

TEST_CASE("xray jsonc vless grpc example parses service and authority")
{
    const std::string content = R"jsonc(
{
  "outbounds": [
    {
      "protocol": "vless",
      "tag": "grpc-out",
      "settings": {
        "vnext": [{
          "address": "grpc.example.com",
          "port": 443,
          "users": [{
            "id": "33333333-3333-3333-3333-333333333333",
            "encryption": "none"
          }]
        }]
      },
      "streamSettings": {
        "network": "grpc",
        "security": "reality",
        "grpcSettings": {
          "serviceName": "my-grpc",
          "authority": "grpc-auth.example.com",
          "multiMode": true
        },
        "realitySettings": {
          "serverName": "reality.example.com",
          "publicKey": "pubkey",
          "shortId": "ef01",
          "fingerprint": "chrome"
        }
      }
    }
  ]
}
)jsonc";

    std::vector<Proxy> nodes;
    REQUIRE(explodeConfContent(content, nodes) == 1);
    REQUIRE(nodes.size() == 1);

    const Proxy &node = nodes[0];
    CHECK(node.Type == ProxyType::VLESS);
    CHECK(node.Remark == "grpc-out");
    CHECK(node.TransferProtocol == "grpc");
    CHECK(node.GRPCServiceName == "my-grpc");
    CHECK(node.Host == "grpc-auth.example.com");
    CHECK(node.GRPCMode == "multi");
    CHECK(node.TLSStr == "reality");
    CHECK(node.ServerName == "reality.example.com");
}

TEST_CASE("xray jsonc vless httpupgrade parses exact transport")
{
    const std::string content = R"jsonc(
{
  "outbounds": [
    {
      "protocol": "vless",
      "tag": "httpupgrade-out",
      "settings": {
        "vnext": [{
          "address": "hu.example.com",
          "port": 443,
          "users": [{
            "id": "44444444-4444-4444-4444-444444444444",
            "encryption": "none"
          }]
        }]
      },
      "streamSettings": {
        "network": "httpupgrade",
        "security": "tls",
        "httpupgradeSettings": {
          "path": "/upgrade",
          "host": "hu.host.example"
        },
        "tlsSettings": {
          "serverName": "hu.sni.example.com"
        }
      }
    }
  ]
}
)jsonc";

    std::vector<Proxy> nodes;
    REQUIRE(explodeConfContent(content, nodes) == 1);
    REQUIRE(nodes.size() == 1);

    const Proxy &node = nodes[0];
    CHECK(node.Type == ProxyType::VLESS);
    CHECK(node.Remark == "httpupgrade-out");
    CHECK(node.TransferProtocol == "httpupgrade");
    CHECK(node.Path == "/upgrade");
    CHECK(node.Host == "hu.host.example");
    CHECK(node.TLSStr == "tls");
    CHECK(node.ServerName == "hu.sni.example.com");
}

TEST_CASE("xray jsonc vless classic transports parse from stream settings")
{
    const std::string content = R"jsonc(
{
  "outbounds": [
    {
      "protocol": "vless",
      "tag": "tcp-out",
      "settings": {
        "vnext": [{
          "address": "tcp.example.com",
          "port": 443,
          "users": [{ "id": "55555555-5555-5555-5555-555555555555", "encryption": "none" }]
        }]
      },
      "streamSettings": {
        "network": "tcp",
        "security": "tls",
        "tcpSettings": {
          "header": {
            "type": "http",
            "request": {
              "path": ["/tcp"],
              "headers": {
                "Host": ["tcp.host.example"]
              }
            }
          }
        },
        "tlsSettings": {
          "serverName": "tcp.sni.example.com"
        }
      }
    },
    {
      "protocol": "vless",
      "tag": "ws-out",
      "settings": {
        "vnext": [{
          "address": "ws.example.com",
          "port": 443,
          "users": [{ "id": "66666666-6666-6666-6666-666666666666", "encryption": "none" }]
        }]
      },
      "streamSettings": {
        "network": "ws",
        "security": "tls",
        "wsSettings": {
          "path": "/ws",
          "headers": {
            "Host": "ws.host.example"
          }
        },
        "tlsSettings": {
          "serverName": "ws.sni.example.com"
        }
      }
    },
    {
      "protocol": "vless",
      "tag": "http-out",
      "settings": {
        "vnext": [{
          "address": "http.example.com",
          "port": 443,
          "users": [{ "id": "77777777-7777-7777-7777-777777777777", "encryption": "none" }]
        }]
      },
      "streamSettings": {
        "network": "http",
        "security": "tls",
        "httpSettings": {
          "path": ["/http"],
          "host": ["http.host.example"]
        },
        "tlsSettings": {
          "serverName": "http.sni.example.com"
        }
      }
    },
    {
      "protocol": "vless",
      "tag": "h2-out",
      "settings": {
        "vnext": [{
          "address": "h2.example.com",
          "port": 443,
          "users": [{ "id": "88888888-8888-8888-8888-888888888888", "encryption": "none" }]
        }]
      },
      "streamSettings": {
        "network": "h2",
        "security": "tls",
        "httpSettings": {
          "path": ["/h2"],
          "host": ["h2.host.example"]
        },
        "tlsSettings": {
          "serverName": "h2.sni.example.com"
        }
      }
    },
    {
      "protocol": "vless",
      "tag": "kcp-out",
      "settings": {
        "vnext": [{
          "address": "kcp.example.com",
          "port": 443,
          "users": [{ "id": "99999999-9999-9999-9999-999999999999", "encryption": "none" }]
        }]
      },
      "streamSettings": {
        "network": "kcp",
        "kcpSettings": {
          "seed": "kcp-seed",
          "header": {
            "type": "srtp"
          }
        }
      }
    }
  ]
}
)jsonc";

    std::vector<Proxy> nodes;
    REQUIRE(explodeConfContent(content, nodes) == 1);
    REQUIRE(nodes.size() == 5);

    const auto find_by_remark = [&](const std::string &remark) -> const Proxy * {
        for (const auto &node: nodes) {
            if (node.Remark == remark)
                return &node;
        }
        return nullptr;
    };

    const Proxy *tcp = find_by_remark("tcp-out");
    REQUIRE(tcp != nullptr);
    CHECK(tcp->TransferProtocol == "http");
    CHECK(tcp->FakeType == "http");
    CHECK(tcp->Path == "/tcp");
    CHECK(tcp->Host == "tcp.host.example");
    CHECK(tcp->ServerName == "tcp.sni.example.com");

    const Proxy *ws = find_by_remark("ws-out");
    REQUIRE(ws != nullptr);
    CHECK(ws->TransferProtocol == "ws");
    CHECK(ws->Path == "/ws");
    CHECK(ws->Host == "ws.host.example");
    CHECK(ws->ServerName == "ws.sni.example.com");

    const Proxy *http = find_by_remark("http-out");
    REQUIRE(http != nullptr);
    CHECK(http->TransferProtocol == "http");
    CHECK(http->Path == "/http");
    CHECK(http->Host == "http.host.example");
    CHECK(http->ServerName == "http.sni.example.com");

    const Proxy *h2 = find_by_remark("h2-out");
    REQUIRE(h2 != nullptr);
    CHECK(h2->TransferProtocol == "h2");
    CHECK(h2->Path == "/h2");
    CHECK(h2->Host == "h2.host.example");
    CHECK(h2->ServerName == "h2.sni.example.com");

    const Proxy *kcp = find_by_remark("kcp-out");
    REQUIRE(kcp != nullptr);
    CHECK(kcp->TransferProtocol == "kcp");
    CHECK(kcp->Path == "kcp-seed");
    CHECK(kcp->FakeType == "srtp");
}
