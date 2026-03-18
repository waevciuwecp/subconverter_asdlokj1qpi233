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
  if [[ -n "${server_pid:-}" ]] && kill -0 "$server_pid" >/dev/null 2>&1; then
    kill "$server_pid" >/dev/null 2>&1 || true
    wait "$server_pid" >/dev/null 2>&1 || true
  fi
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

pref_path="$tmp_dir/pref.ini"
cp "$repo_root/base/pref.example.ini" "$pref_path"

port="$((29100 + RANDOM % 700))"
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
  if curl --noproxy '*' -fsS "http://127.0.0.1:${port}/version" >/dev/null 2>&1; then
    break
  fi
  sleep 0.25
done

if ! curl --noproxy '*' -fsS "http://127.0.0.1:${port}/version" >/dev/null 2>&1; then
  echo "backend failed to start on port $port" >&2
  echo "---- server.log ----" >&2
  cat "$tmp_dir/server.log" >&2 || true
  exit 1
fi

eval "$(
python3 - <<'PY'
import base64
import urllib.parse
import zlib

source = "ss://YWVzLTEyOC1nY206cHdk@1.1.1.1:8388#digest-test"
query = "target=ss&url={}&filename=old-name".format(urllib.parse.quote(source, safe=""))

co = zlib.compressobj(level=9, wbits=-zlib.MAX_WBITS)
raw = co.compress(query.encode("utf-8")) + co.flush()

q_plain = query
q_base64 = base64.urlsafe_b64encode(query.encode("utf-8")).decode("ascii").rstrip("=")
q_deflate = base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")

def q(v):
    return "'" + v.replace("'", "'\"'\"'") + "'"

print("Q_PLAIN=" + q(q_plain))
print("Q_BASE64=" + q(q_base64))
print("Q_DEFLATE=" + q(q_deflate))
PY
)"

check_digest_ok() {
  local case_name="$1"
  local q_value="$2"
  local header_file="$tmp_dir/${case_name}.headers"
  local body_file="$tmp_dir/${case_name}.body"
  local code

  code="$(curl --noproxy '*' -sS -G "http://127.0.0.1:${port}/digest" \
    --data-urlencode "a=new-name" \
    --data-urlencode "q=${q_value}" \
    -D "$header_file" \
    -o "$body_file" \
    -w "%{http_code}")"

  if [[ "$code" != "200" ]]; then
    echo "expected HTTP 200 for ${case_name}, got ${code}" >&2
    cat "$tmp_dir/server.log" >&2 || true
    exit 1
  fi

  if [[ ! -s "$body_file" ]]; then
    echo "expected non-empty response body for ${case_name}" >&2
    exit 1
  fi

  tr -d '\r' < "$header_file" > "${header_file}.norm"
  if ! rg -n 'Content-Disposition: attachment; filename="new-name"' "${header_file}.norm" >/dev/null; then
    echo "expected alias override Content-Disposition filename for ${case_name}" >&2
    cat "${header_file}.norm" >&2 || true
    exit 1
  fi
}

check_digest_ok "plain_q" "$Q_PLAIN"
check_digest_ok "base64_q" "$Q_BASE64"
check_digest_ok "deflate_q" "$Q_DEFLATE"

bad_code="$(curl --noproxy '*' -sS -G "http://127.0.0.1:${port}/digest" \
  --data-urlencode "q=@@@@" \
  -o "$tmp_dir/bad.body" \
  -w "%{http_code}")"

if [[ "$bad_code" != "400" ]]; then
  echo "expected malformed q to return HTTP 400, got ${bad_code}" >&2
  cat "$tmp_dir/bad.body" >&2 || true
  exit 1
fi

echo "PASS: digest alias precedence and q format compatibility are correct"
