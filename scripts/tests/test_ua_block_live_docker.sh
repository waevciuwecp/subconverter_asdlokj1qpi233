#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
image_name="${UA_BLOCK_TEST_IMAGE:-subconverter-local-ua-block:debug}"
container_name="${UA_BLOCK_TEST_CONTAINER:-subconverter-local-ua-block}"
host_port="${UA_BLOCK_TEST_PORT:-25580}"

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required" >&2
  exit 1
fi

blocked_ua='Mozilla/5.0 (Linux; Android 14; M2102K1C Build/UKQ1.240624.001; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/142.0.7444.173 Mobile Safari/537.36 XWEB/1420273 MMWEBSDK/20260101 MMWEBID/3026 REV/04f9d4e638f33b1909b8f293dffa1cf978d8d0a3 MicroMessenger/8.0.68.3020(0x28004458) WeChat/arm64 Weixin NetType/4G Language/zh_CN ABI/arm64'
normal_ua='Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36'

tmp_dir="$(mktemp -d)"
cleanup() {
  docker rm -f "$container_name" >/dev/null 2>&1 || true
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

"$repo_root/scripts/tests/run_local_backend.sh" "$image_name" "$container_name" "$host_port" >/dev/null

blocked_version_body="$tmp_dir/blocked_version.body"
blocked_version_code="$(curl --noproxy '*' -sS -A "$blocked_ua" \
  "http://127.0.0.1:${host_port}/version" \
  -o "$blocked_version_body" \
  -w "%{http_code}")"

if [[ "$blocked_version_code" != "404" ]]; then
  echo "expected blocked /version to return HTTP 404, got ${blocked_version_code}" >&2
  exit 1
fi
if rg -n --fixed-strings "Welcome to nginx!" "$blocked_version_body" >/dev/null; then
  echo "blocked /version response should not be nginx decoy page" >&2
  cat "$blocked_version_body" >&2 || true
  exit 1
fi

blocked_digest_body="$tmp_dir/blocked_digest.body"
blocked_digest_code="$(curl --noproxy '*' -sS -G -A "$blocked_ua" \
  "http://127.0.0.1:${host_port}/digest" \
  --data-urlencode "q=@@@@" \
  -o "$blocked_digest_body" \
  -w "%{http_code}")"

if [[ "$blocked_digest_code" != "404" ]]; then
  echo "expected blocked /digest to return HTTP 404, got ${blocked_digest_code}" >&2
  exit 1
fi
if rg -n --fixed-strings "Welcome to nginx!" "$blocked_digest_body" >/dev/null; then
  echo "blocked /digest response should not be nginx decoy page" >&2
  cat "$blocked_digest_body" >&2 || true
  exit 1
fi

normal_body="$tmp_dir/normal.body"
normal_code="$(curl --noproxy '*' -sS -A "$normal_ua" \
  "http://127.0.0.1:${host_port}/version" \
  -o "$normal_body" \
  -w "%{http_code}")"

if [[ "$normal_code" != "200" ]]; then
  echo "expected normal /version to return HTTP 200, got ${normal_code}" >&2
  exit 1
fi
if ! rg -n --fixed-strings "subconverter " "$normal_body" >/dev/null; then
  echo "expected normal UA to receive real backend response" >&2
  cat "$normal_body" >&2 || true
  exit 1
fi

echo "PASS: docker live UA blocker behavior is correct on /version and /digest"
