#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bin_path="${SUBCONVERTER_BIN:-$repo_root/build/subconverter}"

if [[ ! -x "$bin_path" ]]; then
  echo "subconverter binary not found or not executable: $bin_path" >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

cp "$repo_root/base/base/singbox.json" "$tmp_dir/singbox.json"

pref_path="$tmp_dir/pref.ini"
gen_path="$tmp_dir/generate.ini"
out_path="$tmp_dir/singbox_fallback.json"

ss_link_1='ss://YWVzLTEyOC1nY206cHdk@1.1.1.1:8388#awesome-node'
ss_link_2='ss://YWVzLTEyOC1nY206cHdk@2.2.2.2:8388#plain-node'

sub_provider_yaml="$(cat <<'YAML'
proxies:
  - name: sub-provider-node
    type: ss
    server: 8.8.8.8
    port: 8388
    cipher: aes-128-gcm
    password: pwd
YAML
)"

relay_provider_yaml="$(cat <<'YAML'
proxies:
  - name: relay-provider-node
    type: ss
    server: 9.9.9.9
    port: 8388
    cipher: aes-128-gcm
    password: pwd
YAML
)"

sub_provider_url="data:text/plain;base64,$(printf '%s' "$sub_provider_yaml" | base64 | tr -d '\n')"
relay_provider_url="data:text/plain;base64,$(printf '%s' "$relay_provider_yaml" | base64 | tr -d '\n')"
proxy_providers="[{\"name\":\"sub-provider-1\",\"type\":\"http\",\"url\":\"$sub_provider_url\"},{\"name\":\"relay-provider-1\",\"type\":\"http\",\"url\":\"$relay_provider_url\"}]"

cat > "$pref_path" <<PREF
[common]
api_mode=false
base_path=.
singbox_rule_base=singbox.json

[template]
template_path=.

[rulesets]
enabled=true
overwrite_original_rules=true
ruleset=Proxy,[]DOMAIN,example.com

[proxy_groups]
custom_proxy_group=dialer-select\`select-use\`(sub-provider|relay-provider)
custom_proxy_group=dialer-lb\`load-balance-use\`(sub-provider|relay-provider)\`http://www.gstatic.com/generate_204\`6537,,100\`round-robin
custom_proxy_group=dialer\`select\`[]dialer-select\`[]dialer-lb\`[]DIRECT
custom_proxy_group=Proxy\`select\`[]awesome-node\`[]plain-node\`[]DIRECT
PREF

cat > "$gen_path" <<GEN
[singbox_provider_fallback]
path=$out_path
target=singbox
singbox_ver=1.14.0
url=$ss_link_1|$ss_link_2
use_dialer=true
dialer_group_name=dialer
apply_dialer_to=awesome
proxy_providers=$proxy_providers
GEN

(
  cd "$tmp_dir"
  "$bin_path" -f "$pref_path" -g --artifact singbox_provider_fallback >/dev/null 2>&1
)

if [[ ! -s "$out_path" ]]; then
  echo "expected non-empty output file: $out_path" >&2
  exit 1
fi

assert_contains_fixed() {
  local path="$1"
  local pattern="$2"
  if ! rg -n --fixed-strings "$pattern" "$path" >/dev/null; then
    echo "expected pattern not found in $path: $pattern" >&2
    exit 1
  fi
}

assert_contains_fixed "$out_path" "\"sub-provider-node\""
assert_contains_fixed "$out_path" "\"relay-provider-node\""
assert_contains_fixed "$out_path" "\"tag\":\"dialer-select\""
assert_contains_fixed "$out_path" "\"outbounds\":[\"sub-provider-node\",\"relay-provider-node\"]"
assert_contains_fixed "$out_path" "\"detour\":\"dialer\""

detour_count="$(rg -o --fixed-strings '"detour":"dialer"' "$out_path" | wc -l | tr -d ' ')"
if [[ "$detour_count" != "1" ]]; then
  echo "expected exactly one detour in fallback output, got $detour_count" >&2
  exit 1
fi

echo "PASS: singbox provider fallback materializes dialer group outbounds"
