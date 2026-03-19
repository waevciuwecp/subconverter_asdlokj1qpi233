# subconverter

Utility to convert between various proxy subscription formats.

original git: https://github.com/asdlokj1qpi23/subconverter

[![Build Status](https://github.com/asdlokj1qpi233/subconverter/actions/workflows/docker.yml/badge.svg)](https://github.com/asdlokj1qpi233/subconverter/actions)
[![GitHub tag (latest SemVer)](https://img.shields.io/github/tag/asdlokj1qpi233/subconverter.svg)](https://github.com/asdlokj1qpi23/subconverter/tags)
[![GitHub release](https://img.shields.io/github/release/asdlokj1qpi233/subconverter.svg)](https://github.com/asdlokj1qpi233/subconverter/releases)
[![GitHub license](https://img.shields.io/github/license/asdlokj1qpi233/subconverter.svg)](https://github.com/tindy2013/subconverter/blob/master/LICENSE)

[Docker README](https://github.com/asdlokj1qpi23/subconverter/blob/master/README-docker.md)

[中文文档](https://github.com/asdlokj1qpi23/subconverter/blob/master/README-cn.md)

- [subconverter](#subconverter)
  - [Docker](#docker)
  - [Supported Types](#supported-types)
  - [Quick Usage](#quick-usage)
    - [Access Interface](#access-interface)
    - [Description](#description)
  - [Advanced Usage](#advanced-usage)
    - [UA Defense](#ua-defense)
  - [Auto Upload](#auto-upload)
  
## Docker

For running this docker, simply use the following commands:
```bash
# run the container detached, forward internal port 25500 to host port 25500
docker run -d --restart=always -p 25500:25500 asdlokj1qpi23/subconverter:latest
# then check its status
curl http://localhost:25500/version
# if you see `subconverter vx.x.x backend` then the container is up and running
```
Or run in docker-compose:
```yaml
---
version: '3'
services:
  subconverter:
    image: asdlokj1qpi23/subconverter:latest
    container_name: subconverter
    ports:
      - "15051:25500"
    restart: always
```
## Supported Types

| Type                              | As Source | As Target    | Target Name    |
|-----------------------------------|:---------:| :----------: |----------------|
| Clash                             |     ✓     |      ✓       | clash          |
| ClashR                            |     ✓     |      ✓       | clashr         |
| Quantumult                        |     ✓     |      ✓       | quan           |
| Quantumult X                      |     ✓     |      ✓       | quanx          |
| Loon                              |     ✓     |      ✓       | loon           |
| SS (SIP002)                       |     ✓     |      ✓       | ss             |
| SS Android                        |     ✓     |      ✓       | sssub          |
| SSD                               |     ✓     |      ✓       | ssd            |
| SSR                               |     ✓     |      ✓       | ssr            |
| Surfboard                         |     ✓     |      ✓       | surfboard      |
| Surge 2                           |     ✓     |      ✓       | surge&ver=2    |
| Surge 3                           |     ✓     |      ✓       | surge&ver=3    |
| Surge 4                           |     ✓     |      ✓       | surge&ver=4    |
| Surge 5                           |     ✓     |      ✓       | surge&ver=5    |
| V2Ray                             |     ✓     |      ✓       | v2ray          |
| Telegram-liked HTTP/Socks 5 links |     ✓     |      ×       | Only as source |
| Singbox                           |     ✓      |      ✓       | singbox        |

Notice:

1. Shadowrocket users should use `ss`, `ssr` or `v2ray` as target.

2. You can add `&remark=` to Telegram-liked HTTP/Socks 5 links to set a remark for this node. For example:

   - tg://http?server=1.2.3.4&port=233&user=user&pass=pass&remark=Example

   - https://t.me/http?server=1.2.3.4&port=233&user=user&pass=pass&remark=Example


---

## Quick Usage

> Using default groups and rulesets configuration directly, without changing any settings

### Access Interface

```txt
http://127.0.0.1:25500/sub?target=%TARGET%&url=%URL%&config=%CONFIG%
```

### Description

| Argument | Required | Example | Description |
| -------- | :------: | :------ | ----------- |
| target   | Yes      | clash   | Target subscription type. Acquire from Target Name in [Supported Types](#supported-types). |
| url      | Yes      | https%3A%2F%2Fwww.xxx.com | Subscription to convert. Supports URLs and file paths. Process with [URLEncode](https://www.urlencoder.org/) first. |
| config   | No       | https%3A%2F%2Fwww.xxx.com | External configuration file path. Supports URLs and file paths. Process with [URLEncode](https://www.urlencoder.org/) first. More examples can be found in [this](https://github.com/lzdnico/subconverteriniexample) repository. |

If you need to merge two or more subscription, you should join them with '|' before the URLEncode process.

Example:

```txt
You have 2 subscriptions and you want to merge them and generate a Clash subscription:
1. https://dler.cloud/subscribe/ABCDE?clash=vmess
2. https://rich.cloud/subscribe/ABCDE?clash=vmess

First use '|' to separate 2 subscriptions:
https://dler.cloud/subscribe/ABCDE?clash=vmess|https://rich.cloud/subscribe/ABCDE?clash=vmess

Then process it with URLEncode to get %URL%:
https%3A%2F%2Fdler.cloud%2Fsubscribe%2FABCDE%3Fclash%3Dvmess%7Chttps%3A%2F%2Frich.cloud%2Fsubscribe%2FABCDE%3Fclash%3Dvmess

Then fill %TARGET% and %URL% in Access Interface with actual values:
http://127.0.0.1:25500/sub?target=clash&url=https%3A%2F%2Fdler.cloud%2Fsubscribe%2FABCDE%3Fclash%3Dvmess%7Chttps%3A%2F%2Frich.cloud%2Fsubscribe%2FABCDE%3Fclash%3Dvmess

Finally subscribe this link in Clash and you are done!
```

---

## Advanced Usage

Please refer to [中文文档](https://github.com/asdlokj1qpi23/subconverter/blob/master/README-cn.md#%E8%BF%9B%E9%98%B6%E7%94%A8%E6%B3%95).

### Packed Query (`a` + `q`)

Use `/digest` for packed query strings:

```txt
http://127.0.0.1:25500/digest?a=<alias>&q=<packed_query>
```

- `a`: digest alias. In `/digest`, this value overrides `filename`.
- `q`: packed request parameters.
  - recommended: deflate payload (prefer `deflateRaw`) encoded with base64/base64url
  - plain query string (`target=...&url=...`)
  - base64/base64url encoded query string
  - pako deflate payload (zlib/raw/gzip) encoded with base64/base64url

If normal parameters are also present in URL, they take precedence over values decoded from `q`.
For filename selection in `/digest`, priority is: `a` > `filename`.

Compact digest mode (`m=1`) is supported in `q`:
- Short aliases: `t->target`, `u->url`, `c->config`, `i->include`, `e->exclude`, `r->rename`, `d->dev_id`, `iv->interval`, `p->proxy_providers`, `v->ver`, `sv->singbox_ver`, `dg->dialer_group_name`, `da->apply_dialer_to`.
- Boolean bitsets:
  - `bt`: base36 bitmask for explicit `true`
  - `bf`: base36 bitmask for explicit `false`
  - bit order: `insert,emoji,list,xudp,udp,tfo,expand,scv,fdn,append_type,tls13,sort,use_dialer,new_name,surge.doh,clash.doh,singbox.ipv6`
- Full-length keys are still accepted for manual editing and backward compatibility.

`/sub` keeps the original plain-parameter behavior.

### Dialer / Providers

For Clash/ClashR/Singbox targets, the following advanced query parameters are available:

- `use_dialer=true|false`: enable writing dialer fields for matched nodes (`dialer-proxy` for Clash/ClashR, `detour` for Singbox).
- `dialer_group_name=<name>`: dialer group name (default: `dialer`).
- `apply_dialer_to=<regex>`: apply dialer only to node remarks matching this regex (empty = all nodes).
- `singbox_ver=<version>`: explicit sing-box config version in supported range `1.12.x` to `1.14.x` (for example `1.12.0`, `1.13.0`, `1.14.0`). For all supported versions, generated route rules include `action: route`. Default is `1.12.0`.
- `ver=<version>`: when `target=singbox`, `ver` is accepted as a fallback for `singbox_ver`.
- `proxy_providers=<urlencoded-json-array>`: inject Clash `proxy-providers` from request input.

Sing-box compatibility notes:
- `1.12.x` to `1.14.x` is supported explicitly.
- For `>=1.13.0`, WireGuard **outbound** was removed upstream; WireGuard nodes are skipped in Sing-box output.
- `detour` targets can point to dialer strategy outbounds (`selector` / `urltest`) by tag; this backend intentionally keeps `detour` pointing to the dialer group tag.
- For `>=1.12.0`, deprecated database rule types (`GEOIP`, `SRC-GEOIP`, `GEOSITE`) are skipped to avoid invalid configs; use `RULE-SET` based routing instead.

`custom_proxy_group` also supports:

- `relay` (deprecated in some clients, but supported by this project)
- provider-oriented types:
  - `select-use`
  - `url-test-use`
  - `fallback-use`
  - `load-balance-use` (supports optional trailing strategy, e.g. `` `round-robin` ``)

Notes:

- For `load-balance` / `load-balance-use`, strategy is checked. Valid values are `consistent-hashing` and `round-robin`. Invalid/unknown strategy falls back to `consistent-hashing`.
- In external YAML/TOML group definitions, both `rule` and `proxies` are accepted as the list field for group members or provider-match rules.
- Preset examples:
  - `base/config/nodnsleak.dialer.ini`: dialer with both `dialer-select` and `dialer-lb`.
  - `base/config/nodnsleak.dialer-non_lb.ini`: dialer without `dialer-lb` (select-only + `DIRECT` fallback).

### UA Defense

Incoming HTTP requests are protected by a User-Agent blocker before route handling.

- Keyword list file: `base/ua_block_keywords.list`
- Match rule: case-insensitive substring match, one keyword per line
- File format: empty lines and lines starting with `#` are ignored
- Reload behavior: keywords are reloaded automatically at runtime (no restart required)
- Response on match: a fake nginx welcome page is returned (`200`, `text/html`)

Default keywords include:
- mobile brands (for example `huawei`, `xiaomi`, `oppo`, `vivo`, `zte`, `lenovo`)
- sensitive app/browser signatures (for example `miuibrowser`, `ucbrowser`, `baiduboxapp`, `micromessenger`)

## Auto Upload

> Upload Gist automatically

Add a [Personal Access Token](https://github.com/settings/tokens/new) into [gistconf.ini](./gistconf.ini) in the root directory, then add `&upload=true` to the local subscription link, then when you access this link, the program will automatically update the content to Gist repository.

Example:

```ini
[common]
;uncomment the following line and enter your token to enable upload function
token = xxxxxxxxxxxxxxxxxxxxxxxx(Your Personal Access Token)
```
## Thanks
[tindy2013](https://github.com/tindy2013)
[https://github.com/tindy2013/subconverter](https://github.com/tindy2013/subconverter)
