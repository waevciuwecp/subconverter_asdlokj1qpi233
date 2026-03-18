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
out_path="$tmp_dir/singbox_v114.json"

ss_link_1='ss://YWVzLTEyOC1nY206cHdk@1.1.1.1:8388#awesome-node'
ss_link_2='ss://YWVzLTEyOC1nY206cHdk@2.2.2.2:8388#relay-node'

cat > "$pref_path" <<PREF
[common]
api_mode=false
base_path=.
singbox_rule_base=singbox.json

[template]
template_path=.

[proxy_groups]
custom_proxy_group=auto\`url-test\`.*\`http://www.gstatic.com/generate_204\`300,,50
custom_proxy_group=Proxy\`select\`[]auto\`[]DIRECT
PREF

cat > "$gen_path" <<GEN
[singbox_v114]
path=$out_path
target=singbox
singbox_ver=1.14.0
url=$ss_link_1|$ss_link_2
udp=false
GEN

(
  cd "$tmp_dir"
  "$bin_path" -f "$pref_path" -g --artifact singbox_v114 >/dev/null 2>&1
)

if [[ ! -s "$out_path" ]]; then
  echo "expected non-empty output file: $out_path" >&2
  exit 1
fi

# Global udp=false must not force all sing-box proxy outbounds into tcp-only.
if jq -e '.outbounds[] | select(.tag == "awesome-node" or .tag == "relay-node") | select(.network == "tcp")' "$out_path" >/dev/null; then
  echo "unexpected tcp-only network forced by global udp=false in sing-box output" >&2
  exit 1
fi

echo "PASS: singbox global udp=false no longer forces tcp-only outbounds"
