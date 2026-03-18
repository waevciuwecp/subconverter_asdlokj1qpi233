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
out_v110="$tmp_dir/singbox_v110.json"
out_v111="$tmp_dir/singbox_v111.json"

ss_link_1='ss://YWVzLTEyOC1nY206cHdk@1.1.1.1:8388#awesome-node'
ss_link_2='ss://YWVzLTEyOC1nY206cHdk@2.2.2.2:8388#plain-node'

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
custom_proxy_group=Proxy\`select\`[]awesome-node\`[]plain-node\`[]DIRECT
PREF

cat > "$gen_path" <<GEN
[singbox_v110]
path=$out_v110
target=singbox
ver=1.10.0
url=$ss_link_1|$ss_link_2
use_dialer=true
dialer_group_name=dialer
apply_dialer_to=awesome

[singbox_v111]
path=$out_v111
target=singbox
singbox_ver=1.11.0
url=$ss_link_1|$ss_link_2
use_dialer=true
dialer_group_name=dialer
apply_dialer_to=awesome
GEN

(
  cd "$tmp_dir"
  "$bin_path" -f "$pref_path" -g --artifact singbox_v110 >/dev/null 2>&1
  "$bin_path" -f "$pref_path" -g --artifact singbox_v111 >/dev/null 2>&1
)

assert_non_empty() {
  local path="$1"
  if [[ ! -s "$path" ]]; then
    echo "expected non-empty output file: $path" >&2
    exit 1
  fi
}

assert_contains_fixed() {
  local path="$1"
  local pattern="$2"
  if ! rg -n --fixed-strings "$pattern" "$path" >/dev/null; then
    echo "expected pattern not found in $path: $pattern" >&2
    exit 1
  fi
}

assert_not_contains_fixed() {
  local path="$1"
  local pattern="$2"
  if rg -n --fixed-strings "$pattern" "$path" >/dev/null; then
    echo "unexpected pattern found in $path: $pattern" >&2
    exit 1
  fi
}

assert_non_empty "$out_v110"
assert_non_empty "$out_v111"

assert_contains_fixed "$out_v110" "\"awesome-node\""
assert_contains_fixed "$out_v111" "\"awesome-node\""
assert_contains_fixed "$out_v110" "\"detour\":\"dialer\""
assert_contains_fixed "$out_v111" "\"detour\":\"dialer\""

detour_count_v110="$(rg -o --fixed-strings '"detour":"dialer"' "$out_v110" | wc -l | tr -d ' ')"
detour_count_v111="$(rg -o --fixed-strings '"detour":"dialer"' "$out_v111" | wc -l | tr -d ' ')"
if [[ "$detour_count_v110" != "1" ]]; then
  echo "expected exactly one detour in v1.10 output, got $detour_count_v110" >&2
  exit 1
fi
if [[ "$detour_count_v111" != "1" ]]; then
  echo "expected exactly one detour in v1.11 output, got $detour_count_v111" >&2
  exit 1
fi

assert_not_contains_fixed "$out_v110" "\"action\":\"route\""
assert_contains_fixed "$out_v111" "\"action\":\"route\""

echo "PASS: singbox dialer and versioned route action behavior is correct"
