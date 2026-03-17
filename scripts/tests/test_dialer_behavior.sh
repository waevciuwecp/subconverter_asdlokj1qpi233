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

pref_path="$tmp_dir/pref.ini"
gen_path="$tmp_dir/generate.ini"
out_path="$tmp_dir/output.yml"

ss_link_1='ss://YWVzLTEyOC1nY206cHdk@1.1.1.1:8388#awesome-node'
ss_link_2='ss://YWVzLTEyOC1nY206cHdk@2.2.2.2:8388#plain-node'

proxy_providers='[{"name":"sub-dialer-provider-1","type":"http","url":"https://example.com/sub.yaml"},{"name":"relay-provider-1","type":"http","url":"https://example.com/relay.yaml"}]'

cat > "$pref_path" <<PREF
[common]
api_mode=false
base_path=$repo_root/base
clash_rule_base=$repo_root/base/base/all_base.tpl

[node_pref]
clash_use_new_field_name=true
clash_proxies_style=flow
clash_proxy_groups_style=block

[rulesets]
enabled=false
overwrite_original_rules=true

[proxy_groups]
custom_proxy_group=dialer-select\`select-use\`(sub|dialer|relay)
custom_proxy_group=dialer-lb\`load-balance-use\`(sub|dialer|relay)\`http://www.gstatic.com/generate_204\`6537,,100\`round-robin
custom_proxy_group=dialer\`select\`[]dialer-select\`[]dialer-lb\`[]DIRECT
PREF

cat > "$gen_path" <<GEN
[dialer_case]
path=$out_path
target=clash
url=$ss_link_1|$ss_link_2
use_dialer=true
dialer_group_name=dialer
apply_dialer_to=awesome
proxy_providers=$proxy_providers
GEN

(
  cd "$tmp_dir"
  "$bin_path" -f "$pref_path" -g --artifact dialer_case >/dev/null 2>&1
)

if [[ ! -s "$out_path" ]]; then
  echo "expected non-empty output file: $out_path" >&2
  exit 1
fi

assert_contains() {
  local pattern="$1"
  if ! rg -n --fixed-strings "$pattern" "$out_path" >/dev/null; then
    echo "expected pattern not found: $pattern" >&2
    exit 1
  fi
}

assert_not_contains_regex() {
  local pattern="$1"
  if rg -n "$pattern" "$out_path" >/dev/null; then
    echo "unexpected pattern found: $pattern" >&2
    exit 1
  fi
}

assert_contains "proxy-providers:"
assert_contains "sub-dialer-provider-1:"
assert_contains "relay-provider-1:"
assert_contains "name: dialer-select"
assert_contains "name: dialer-lb"
assert_contains "strategy: round-robin"
assert_contains "use:"

# Exact dialer application: only the awesome node receives dialer-proxy.
assert_contains "awesome-node"
assert_contains "dialer-proxy: dialer"

dialer_count="$(rg -o --fixed-strings 'dialer-proxy: dialer' "$out_path" | wc -l | tr -d ' ')"
if [[ "$dialer_count" != "1" ]]; then
  echo "expected exactly one dialer-proxy assignment, got $dialer_count" >&2
  exit 1
fi

if ! rg -n 'awesome-node.*dialer-proxy: dialer' "$out_path" >/dev/null; then
  echo "expected awesome-node to carry dialer-proxy" >&2
  exit 1
fi

assert_not_contains_regex 'plain-node.*dialer-proxy'

echo "PASS: dialer/provider behavior is correct"
