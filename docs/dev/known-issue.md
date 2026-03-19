# Known Security Issues (Top 5) - 2026-03-19

This document records the top 5 vulnerabilities identified in this repository, the remediation applied, and verification status.

## Validation Summary

- Unit: `ctest --test-dir build-unit --output-on-failure` -> PASS
- Local runtime: `SUBCONVERTER_BIN="$(pwd)/build-live/subconverter" bash scripts/tests/test_ua_block_live_native.sh` -> PASS
- Docker runtime: `bash scripts/tests/test_ua_block_live_docker.sh` -> PASS

## Top 5 Issues

### 1) Request-controlled script execution (`filter_script` / `script:`)
- Risk: User-controlled JS execution in subscription processing can enable remote code execution-like behavior in privileged backend context.
- Fix:
  - Added secure default: `advanced.allow_request_scripts = false`.
  - `filter_script` request argument is only honored when both `authorized` and `allow_request_scripts` are true.
  - `script:` processing now checks the same gate.
- Key files:
  - `src/generator/config/nodemanip.h`
  - `src/generator/config/nodemanip.cpp`
  - `src/handler/interfaces.cpp`
  - `src/handler/settings.h`
  - `src/handler/settings.cpp`
- Status: **SOLVED** (tests passed)

### 2) Path traversal / path scope bypass in `/render`
- Risk: Arbitrary local file read when `path` argument bypasses weak prefix checks.
- Fix:
  - Replaced prefix-only checks with canonical path scope validation.
  - Rejects empty path, empty template scope, non-file targets, and out-of-scope paths.
- Key files:
  - `src/handler/interfaces.cpp`
- Status: **SOLVED** (tests passed)

### 3) Outbound TLS certificate verification disabled
- Risk: MITM exposure for all HTTPS outbound fetches.
- Fix:
  - Added secure default: `advanced.verify_outbound_tls = true`.
  - `CURLOPT_SSL_VERIFYPEER/VERIFYHOST` now follow `verify_outbound_tls`.
- Key files:
  - `src/handler/webget.cpp`
  - `src/handler/settings.h`
  - `src/handler/settings.cpp`
- Status: **SOLVED** (tests passed)

### 4) SSRF and over-forwarded client headers on outbound fetch
- Risk: Access to internal/private network targets and unnecessary leakage of inbound headers.
- Fix:
  - Added scheme allowlist for remote fetches (`http`/`https`; `data` handled separately).
  - Added private/loopback destination blocking (`advanced.block_private_address_requests = true`).
  - Added `advanced.forward_client_headers = false` default and conditional forwarding.
  - Added unit tests for unsupported schemes and private-address blocking.
- Key files:
  - `src/handler/webget.cpp`
  - `src/handler/interfaces.cpp`
  - `src/handler/settings.h`
  - `src/handler/settings.cpp`
  - `tests/test_webget.cpp`
- Status: **SOLVED** (tests passed)

### 5) Insecure shipped defaults in example preferences
- Risk: Unsafe bootstrap posture (remote exposure, weak API controls, permissive runtime behavior).
- Fix:
  - Secure-by-default examples:
    - `api_mode=true`
    - non-empty `api_access_token`
    - `listen=127.0.0.1`
    - empty `serve_file_root`
    - explicit `template_path`
    - `allow_request_scripts=false`
    - `forward_client_headers=false`
    - `verify_outbound_tls=true`
    - `block_private_address_requests=true`
  - Updated docker local test harness to override bind/base path for container integration testing only.
- Key files:
  - `base/pref.example.toml`
  - `base/pref.example.ini`
  - `base/pref.example.yml`
  - `scripts/tests/run_local_backend.sh`
- Status: **SOLVED** (tests passed)

## Unsolved Issues (Tracked)

### A) Management endpoints can still be unauthenticated if token is empty
- Risk: If operators set `api_access_token` to empty, critical endpoints such as `/updateconf`, `/readconf`, and `/refreshrules` can be used without authentication.
- Current state:
  - Example configs now ship with a non-empty token, reducing insecure default exposure.
  - Runtime behavior still permits empty-token operation for backward compatibility.
- Why unsolved:
  - Enforcing mandatory auth is a behavior change and may break existing deployments that intentionally run in trusted/private environments.
- Proposed follow-up:
  - Add a strict mode (default `true` for new installs) that always requires auth for management endpoints.
  - Emit startup warnings (or refuse startup in strict mode) when token is empty.
- Status: **UNSOLVED** (tracked)

## Notes

- Local and Docker runtime checks were executed in an unrestricted runtime context (outside sandbox networking limits).
- Existing unrelated working-tree modifications were preserved.
