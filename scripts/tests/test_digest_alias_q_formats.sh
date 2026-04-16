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
s#^block_private_address_requests=.*#block_private_address_requests=false#m; \
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
compact_query = "m=1&t=ss&u={}&bf=2".format(urllib.parse.quote(source, safe=""))

co = zlib.compressobj(level=9, wbits=-zlib.MAX_WBITS)
raw = co.compress(query.encode("utf-8")) + co.flush()
co2 = zlib.compressobj(level=9, wbits=-zlib.MAX_WBITS)
raw_compact = co2.compress(compact_query.encode("utf-8")) + co2.flush()

q_plain = query
q_base64 = base64.urlsafe_b64encode(query.encode("utf-8")).decode("ascii").rstrip("=")
q_deflate = base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")
q_compact = compact_query
q_compact_deflate = base64.urlsafe_b64encode(raw_compact).decode("ascii").rstrip("=")
nested_inner_query = "target=ss&url={}".format(urllib.parse.quote(source, safe=""))
co3 = zlib.compressobj(level=9, wbits=-zlib.MAX_WBITS)
raw_nested = co3.compress(nested_inner_query.encode("utf-8")) + co3.flush()
nested_inner_q = base64.urlsafe_b64encode(raw_nested).decode("ascii").rstrip("=")

def q(v):
    return "'" + v.replace("'", "'\"'\"'") + "'"

print("Q_PLAIN=" + q(q_plain))
print("Q_BASE64=" + q(q_base64))
print("Q_DEFLATE=" + q(q_deflate))
print("Q_COMPACT=" + q(q_compact))
print("Q_COMPACT_DEFLATE=" + q(q_compact_deflate))
print("Q_NESTED_INNER=" + q(nested_inner_q))
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
check_digest_ok "compact_q" "$Q_COMPACT"
check_digest_ok "compact_deflate_q" "$Q_COMPACT_DEFLATE"

Q_NESTED_OUTER="$(
PORT="$port" Q_INNER="$Q_NESTED_INNER" python3 - <<'PY'
import base64
import os
import zlib

port = os.environ["PORT"]
inner_q = os.environ["Q_INNER"]
outer_query = f"target=ss&url=http://127.0.0.1:{port}/digest?a=inner&q={inner_q}"
co = zlib.compressobj(level=9, wbits=-zlib.MAX_WBITS)
raw = co.compress(outer_query.encode("utf-8")) + co.flush()
print(base64.urlsafe_b64encode(raw).decode("ascii").rstrip("="))
PY
)"

nested_body="$tmp_dir/nested_digest_compact.body"
nested_code="$(curl --noproxy '*' -sS -G "http://127.0.0.1:${port}/digest" \
  --data-urlencode "q=${Q_NESTED_OUTER}" \
  -o "$nested_body" \
  -w "%{http_code}")"

if [[ "$nested_code" != "200" ]]; then
  echo "expected packed digest with raw nested '&q=' in u= to return HTTP 200, got ${nested_code}" >&2
  cat "$tmp_dir/server.log" >&2 || true
  cat "$nested_body" >&2 || true
  exit 1
fi

if ! python3 - "$nested_body" <<'PY'
import base64
import pathlib
import sys

body = pathlib.Path(sys.argv[1]).read_text().strip()
if "ss://" in body:
    raise SystemExit(0)
try:
    decoded = base64.b64decode(body + "=" * ((4 - len(body) % 4) % 4)).decode("utf-8")
except Exception:
    raise SystemExit(1)
raise SystemExit(0 if "ss://" in decoded else 1)
PY
then
  echo "expected packed nested digest compatibility case to output ss node content (plain or base64)" >&2
  cat "$nested_body" >&2 || true
  exit 1
fi

bad_code="$(curl --noproxy '*' -sS -G "http://127.0.0.1:${port}/digest" \
  --data-urlencode "q=@@@@" \
  -o "$tmp_dir/bad.body" \
  -w "%{http_code}")"

if [[ "$bad_code" != "400" ]]; then
  echo "expected malformed q to return HTTP 400, got ${bad_code}" >&2
  cat "$tmp_dir/bad.body" >&2 || true
  exit 1
fi

echo "PASS: digest alias precedence, q formats, and nested digest url compatibility are correct"
