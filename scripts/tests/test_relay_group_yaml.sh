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

ss_link_1='ss://YWVzLTEyOC1nY206cHdk@1.1.1.1:8388#hop-1'
ss_link_2='ss://YWVzLTEyOC1nY206cHdk@2.2.2.2:8388#hop-2'

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
  proxy_groups:
    - name: relay-chain
      type: relay
      proxies:
        - hop-1
        - hop-2
YAML

cat > "$gen_path" <<GEN
[relay_yaml_case]
path=$out_path
target=clash
url=$ss_link_1|$ss_link_2
config=external.yaml
GEN

(
  cd "$tmp_dir"
  "$bin_path" -f "$pref_path" -g --artifact relay_yaml_case >/dev/null 2>&1
)

if [[ ! -s "$out_path" ]]; then
  echo "expected non-empty output file: $out_path" >&2
  exit 1
fi

if ! rg -U -n 'name: relay-chain\n\s+type: relay\n(?:.*\n){0,8}\s+proxies:\n\s+- hop-1\n\s+- hop-2' "$out_path" >/dev/null; then
  echo "expected relay group with ordered proxies hop-1 -> hop-2" >&2
  exit 1
fi

echo "PASS: yaml relay group behavior is correct"
