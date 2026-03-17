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
ext_yaml_path="$tmp_dir/external.yaml"
out_path="$tmp_dir/output.yml"

ss_link_1='ss://YWVzLTEyOC1nY206cHdk@1.1.1.1:8388#awesome-node'
ss_link_2='ss://YWVzLTEyOC1nY206cHdk@2.2.2.2:8388#plain-node'

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

[template]
template_path=$repo_root/base
PREF

cat > "$ext_yaml_path" <<YAML
custom:
  enable_rule_generator: false
  overwrite_original_rules: true
  use_dialer: true
  dialer_group_name: dialer
  apply_dialer_to: awesome
  proxy_providers:
    - name: sub-dialer-provider-1
      type: http
      url: https://example.com/sub.yaml
    - name: relay-provider-1
      type: http
      url: https://example.com/relay.yaml
  proxy_groups:
    - name: dialer-select
      type: select-use
      rule:
        - (sub|dialer|relay)
    - name: dialer-lb
      type: load-balance-use
      url: http://www.gstatic.com/generate_204
      interval: 6537
      tolerance: 100
      rule:
        - (sub|dialer|relay)
    - name: dialer
      type: select
      rule:
        - "[]dialer-select"
        - "[]dialer-lb"
        - "[]DIRECT"
YAML

cat > "$gen_path" <<GEN
[dialer_yaml_case]
path=$out_path
target=clash
url=$ss_link_1|$ss_link_2
config=external.yaml
GEN

(
  cd "$tmp_dir"
  "$bin_path" -f "$pref_path" -g --artifact dialer_yaml_case >/dev/null 2>&1
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
assert_contains "use:"

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

echo "PASS: yaml dialer/provider behavior is correct"
