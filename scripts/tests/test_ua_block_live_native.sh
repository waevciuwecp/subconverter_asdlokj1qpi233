#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
default_bin="$repo_root/build-ua-check/subconverter"
if [[ ! -x "$default_bin" ]]; then
  default_bin="$repo_root/build/subconverter"
fi
bin_path="${SUBCONVERTER_BIN:-$default_bin}"

if [[ ! -x "$bin_path" ]]; then
  echo "subconverter binary not found or not executable: $bin_path" >&2
  exit 1
fi

blocked_ua='Mozilla/5.0 (Linux; Android 14; M2102K1C Build/UKQ1.240624.001; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/142.0.7444.173 Mobile Safari/537.36 XWEB/1420273 MMWEBSDK/20260101 MMWEBID/3026 REV/04f9d4e638f33b1909b8f293dffa1cf978d8d0a3 MicroMessenger/8.0.68.3020(0x28004458) WeChat/arm64 Weixin NetType/4G Language/zh_CN ABI/arm64'
normal_ua='Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36'

tmp_dir="$(mktemp -d)"
cleanup() {
  if [[ -n "${server_pid:-}" ]] && kill -0 "$server_pid" >/dev/null 2>&1; then
    kill "$server_pid" >/dev/null 2>&1 || true
    wait "$server_pid" >/dev/null 2>&1 || true
  fi
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

pref_path="$tmp_dir/pref.ini"
cp "$repo_root/base/pref.example.ini" "$pref_path"
mkdir -p "$tmp_dir/base"
cp "$repo_root/base/ua_block_keywords.list" "$tmp_dir/base/ua_block_keywords.list"
cp "$repo_root/base/ua_block_keywords.list" "$tmp_dir/ua_block_keywords.list"

port="${UA_BLOCK_TEST_PORT:-$((29100 + RANDOM % 700))}"
rule_base="$repo_root/base/base/all_base.tpl"

perl -0pi -e "s#^base_path=.*#base_path=$repo_root/base#m; \
s#^clash_rule_base=.*#clash_rule_base=$rule_base#m; \
s#^surge_rule_base=.*#surge_rule_base=$rule_base#m; \
s#^surfboard_rule_base=.*#surfboard_rule_base=$rule_base#m; \
s#^mellow_rule_base=.*#mellow_rule_base=$rule_base#m; \
s#^quan_rule_base=.*#quan_rule_base=$rule_base#m; \
s#^quanx_rule_base=.*#quanx_rule_base=$rule_base#m; \
s#^loon_rule_base=.*#loon_rule_base=$rule_base#m; \
s#^sssub_rule_base=.*#sssub_rule_base=$rule_base#m; \
s#^singbox_rule_base=.*#singbox_rule_base=$rule_base#m; \
s#^listen=.*#listen=127.0.0.1#m; \
s#^port=.*#port=$port#m;" "$pref_path"

"$bin_path" -f "$pref_path" >"$tmp_dir/server.log" 2>&1 &
server_pid="$!"

for _ in $(seq 1 80); do
  if curl --noproxy '*' -fsS -A "$normal_ua" "http://127.0.0.1:${port}/version" >/dev/null 2>&1; then
    break
  fi
  sleep 0.25
done

if ! curl --noproxy '*' -fsS -A "$normal_ua" "http://127.0.0.1:${port}/version" >/dev/null 2>&1; then
  echo "backend failed to start on port $port" >&2
  echo "---- server.log ----" >&2
  cat "$tmp_dir/server.log" >&2 || true
  exit 1
fi

blocked_version_body="$tmp_dir/blocked_version.body"
blocked_version_code="$(curl --noproxy '*' -sS -A "$blocked_ua" \
  "http://127.0.0.1:${port}/version" \
  -o "$blocked_version_body" \
  -w "%{http_code}")"

if [[ "$blocked_version_code" != "404" ]]; then
  echo "expected blocked /version to return HTTP 404, got ${blocked_version_code}" >&2
  exit 1
fi
if rg -n --fixed-strings "Welcome to nginx!" "$blocked_version_body" >/dev/null; then
  echo "blocked /version response should not be nginx decoy page" >&2
  cat "$blocked_version_body" >&2 || true
  exit 1
fi

blocked_digest_body="$tmp_dir/blocked_digest.body"
blocked_digest_code="$(curl --noproxy '*' -sS -G -A "$blocked_ua" \
  "http://127.0.0.1:${port}/digest" \
  --data-urlencode "q=@@@@" \
  -o "$blocked_digest_body" \
  -w "%{http_code}")"

if [[ "$blocked_digest_code" != "404" ]]; then
  echo "expected blocked /digest to return HTTP 404, got ${blocked_digest_code}" >&2
  exit 1
fi
if rg -n --fixed-strings "Welcome to nginx!" "$blocked_digest_body" >/dev/null; then
  echo "blocked /digest response should not be nginx decoy page" >&2
  cat "$blocked_digest_body" >&2 || true
  exit 1
fi

normal_body="$tmp_dir/normal.body"
normal_code="$(curl --noproxy '*' -sS -A "$normal_ua" \
  "http://127.0.0.1:${port}/version" \
  -o "$normal_body" \
  -w "%{http_code}")"

if [[ "$normal_code" != "200" ]]; then
  echo "expected normal /version to return HTTP 200, got ${normal_code}" >&2
  exit 1
fi
if ! rg -n --fixed-strings "subconverter " "$normal_body" >/dev/null; then
  echo "expected normal UA to receive real backend response" >&2
  cat "$normal_body" >&2 || true
  exit 1
fi

echo "PASS: native live UA blocker behavior is correct on /version and /digest"
