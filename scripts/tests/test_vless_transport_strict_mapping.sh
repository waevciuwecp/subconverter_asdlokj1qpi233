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

cat > "$tmp_dir/source.json" <<'JSON'
{
  "inbounds": [],
  "outbounds": [
    {
      "type": "vless",
      "tag": "xhttp-node",
      "server": "xhttp.example.com",
      "server_port": 443,
      "uuid": "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
      "tls": {
        "enabled": true,
        "server_name": "xhttp.example.com",
        "alpn": ["h3"]
      },
      "transport": {
        "type": "xhttp",
        "host": "xhttp.example.com",
        "path": "/xhttp",
        "mode": "stream-up"
      }
    },
    {
      "type": "vless",
      "tag": "splithttp-node",
      "server": "split.example.com",
      "server_port": 443,
      "uuid": "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
      "tls": {
        "enabled": true,
        "server_name": "split.example.com",
        "alpn": ["h3"]
      },
      "transport": {
        "type": "splithttp",
        "host": "split.example.com",
        "path": "/split",
        "mode": "packet-up"
      }
    },
    {
      "type": "vless",
      "tag": "httpupgrade-node",
      "server": "hu.example.com",
      "server_port": 443,
      "uuid": "cccccccc-cccc-cccc-cccc-cccccccccccc",
      "tls": {
        "enabled": true,
        "server_name": "hu.example.com"
      },
      "transport": {
        "type": "httpupgrade",
        "host": "upgrade.example.com",
        "path": "/upgrade"
      }
    }
  ],
  "route": {}
}
JSON

pref_path="$tmp_dir/pref.ini"
gen_path="$tmp_dir/generate.ini"
out_path="$tmp_dir/out.json"

cat > "$pref_path" <<PREF
[common]
api_mode=false
base_path=.
singbox_rule_base=singbox.json

[template]
template_path=.
PREF

cat > "$gen_path" <<GEN
[case]
path=$out_path
target=singbox
singbox_ver=1.14.0
url=$tmp_dir/source.json
GEN

(
  cd "$tmp_dir"
  "$bin_path" -f "$pref_path" -g --artifact case >/dev/null 2>&1
)

if [[ ! -s "$out_path" ]]; then
  echo "expected non-empty output file: $out_path" >&2
  exit 1
fi

assert_jq_true() {
  local path="$1"
  local expr="$2"
  if ! jq -e "$expr" "$path" >/dev/null; then
    echo "expected jq expression to be true for $path: $expr" >&2
    exit 1
  fi
}

assert_jq_true "$out_path" '.outbounds[] | select(.tag == "httpupgrade-node") | .transport.type == "httpupgrade"'
assert_jq_true "$out_path" '.outbounds | map(select(.tag == "xhttp-node")) | length == 0'
assert_jq_true "$out_path" '.outbounds | map(select(.tag == "splithttp-node")) | length == 0'

echo "PASS: strict VLESS transport mapping skips xhttp/splithttp and keeps httpupgrade"
