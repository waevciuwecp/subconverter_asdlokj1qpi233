#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
image_name="${1:-subconverter-local-dialer:debug}"
container_name="${2:-subconverter-local-dialer}"
host_port="${3:-25500}"
healthcheck_ua="${UA_BLOCK_HEALTHCHECK_UA:-Subconverter-LiveTest/1.0}"

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required" >&2
  exit 1
fi

docker build -f "$repo_root/scripts/Dockerfile.local" -t "$image_name" "$repo_root"
docker rm -f "$container_name" >/dev/null 2>&1 || true
docker run -d --name "$container_name" -p "$host_port:25500" \
  "$image_name" sh -ec "cp /base/pref.example.toml /base/pref.toml && sed -i -e 's#^base_path *=.*#base_path = \"/base\"#g' -e 's#^listen *=.*#listen = \"0.0.0.0\"#g' /base/pref.toml && exec subconverter" >/dev/null

for i in $(seq 1 30); do
  if curl --noproxy '*' -fsS -A "$healthcheck_ua" "http://127.0.0.1:${host_port}/version" >/dev/null 2>&1; then
    break
  fi
  sleep 1
done

version="$(curl --noproxy '*' -fsS -A "$healthcheck_ua" "http://127.0.0.1:${host_port}/version")"
echo "Local backend is ready: http://127.0.0.1:${host_port}"
echo "Version: $version"
echo "Container: $container_name"
