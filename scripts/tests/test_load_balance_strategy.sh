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

ss_link_1='ss://YWVzLTEyOC1nY206cHdk@1.1.1.1:8388#UK-1'
ss_link_2='ss://YWVzLTEyOC1nY206cHdk@2.2.2.2:8388#JP-1'

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
custom_proxy_group=uk-lb-rr\`load-balance\`(UK)\`[]REJECT\`http://www.gstatic.com/generate_204\`6537,,100\`round-robin
custom_proxy_group=uk-lb-invalid\`load-balance\`(UK)\`[]REJECT\`http://www.gstatic.com/generate_204\`6537,,100\`invalid-strategy
PREF

cat > "$gen_path" <<GEN
[lb_strategy_case]
path=$out_path
target=clash
url=$ss_link_1|$ss_link_2
GEN

(
  cd "$tmp_dir"
  "$bin_path" -f "$pref_path" -g --artifact lb_strategy_case >/dev/null 2>&1
)

if [[ ! -s "$out_path" ]]; then
  echo "expected non-empty output file: $out_path" >&2
  exit 1
fi

if ! rg -U -n 'name: uk-lb-rr\n\s+type: load-balance\n\s+strategy: round-robin\n\s+url: http://www.gstatic.com/generate_204\n\s+interval: 6537\n\s+tolerance: 100\n\s+proxies:\n\s+- UK-1\n\s+- REJECT' "$out_path" >/dev/null; then
  echo "expected valid round-robin strategy group to be parsed correctly" >&2
  exit 1
fi

if ! rg -U -n 'name: uk-lb-invalid\n\s+type: load-balance\n\s+strategy: consistent-hashing\n\s+url: http://www.gstatic.com/generate_204\n\s+interval: 6537\n\s+tolerance: 100\n\s+proxies:\n\s+- UK-1\n\s+- REJECT' "$out_path" >/dev/null; then
  echo "expected invalid strategy group to fallback to consistent-hashing with correct url/interval parsing" >&2
  exit 1
fi

echo "PASS: load-balance strategy parsing behavior is correct"
