# Sing-box Support Plan: 1.12 -> 1.14

## Goal

Keep backend-generated sing-box configs stable and valid for `1.12.x` to `1.14.x`, while preventing reintroduction of removed/deprecated fields.

## Scope

- In scope:
  - sing-box target generation in backend (`/sub?target=singbox`).
  - Dialer integration (`detour`) behavior for sing-box outbounds/groups.
  - DNS and route bootstrap behavior required by post-1.12 runtime.
  - CI/runtime validation for sing-box 1.12-1.14 compatibility.
- Out of scope:
  - Supporting sing-box `< 1.12`.
  - Adding new features unrelated to migration stability.

## Upstream Changes That Drive This Plan

1. `1.12.0`: DNS server format refactor.
- Legacy `dns.servers[].address` format is deprecated.
- Compatibility for legacy format is removed in `1.14.0`.

2. `1.12.0`: legacy DNS rule `outbound` item is deprecated.
- Should migrate to dial fields/domain resolver pattern.

3. `1.12.0`: dial-field `domain_strategy` is deprecated.
- Should migrate to `domain_resolver` options.

4. `1.13.0`: legacy special outbounds and legacy inbound fields removed.
- Legacy `type: dns` outbound path and old inbound sniff/domain options are no longer valid.

5. `1.13.0`: WireGuard outbound removed.
- WireGuard outbound nodes must not be emitted for `>=1.13.0`.

## Current Backend Baseline (Required State)

- Version policy:
  - Accept only `singbox_ver` in `1.12.x` to `1.14.x`.
  - Default `singbox_ver=1.12.0`.
- DNS template policy:
  - Use new DNS server format (`type`, `server`, `server_port`, etc.).
  - Do not emit legacy `dns.fakeip` block.
  - Do not emit legacy `address` / `address_resolver` fields.
- Route bootstrap policy:
  - Always inject route bootstrap rules for `>=1.12`:
    - `{"action":"sniff","inbound":["tun-in","mixed-in"]}`
    - `{"type":"logical","mode":"or","action":"hijack-dns","rules":[{"port":53},{"protocol":"dns"}]}`
- Dialer policy:
  - Keep `detour` pointing to selector/urltest outbound tag when dialer is enabled.
- Provider fallback policy:
  - Provider-fetched nodes only appear in provider-scoped dialer groups.
  - Do not leak provider nodes into general groups (`自动选择`/`节点选择`/`故障转移`, etc.).

## Migration Work Items

1. Freeze compatibility window at `1.12.x-1.14.x`.
- Reject `<1.12` requests with explicit error.
- Keep docs/examples aligned with this range.

2. Hard-remove deprecated DNS structures from generated output.
- Ensure generated base/template never requires legacy migration rewrites to be valid.
- Keep migration shim only as protective compatibility when custom user base is old.

3. Ensure route bootstrap is always present for modern sing-box.
- Add/retain sniff + hijack-dns rules before generated rule-set rules.

4. Keep REJECT dependency and dialer group topology valid.
- Ensure selector/urltest references only existing outbounds.

5. Guard against generator artifact cross-contamination.
- Ensure request arguments/items are reset per artifact in generator mode.

## Test Plan

### Unit/Regression (existing scripts)

- `scripts/tests/test_singbox_dialer_version.sh`
  - validates: 1.12-1.14 valid, 1.11 rejected, dialer groups + detour intact.
- `scripts/tests/test_singbox_geoip_deprecation.sh`
  - validates deprecated database rules are skipped for 1.12+.
- `scripts/tests/test_singbox_dialer_provider_fallback.sh`
  - validates provider fallback nodes are isolated to provider-scoped groups.
- `scripts/tests/test_singbox_reject_dependency.sh`
  - validates REJECT dependency exists for relevant selector paths.
- `scripts/tests/test_singbox_udp_global_flag.sh`
  - validates no tcp-only regression from global udp flag.
- `scripts/tests/test_singbox_transport_roundtrip.sh`
  - validates transport serialization roundtrip stability.

### Runtime Validation (Docker)

For generated output (including real subscription samples):

1. `sing-box check`
- `docker run --rm -v <config>:/config.json ghcr.io/sagernet/sing-box:v1.13.3 check -c /config.json`

2. Startup smoke
- `docker run ... ghcr.io/sagernet/sing-box:v1.13.3 run -c /config.json`
- assert no fatal init errors (`dns_resolver not found`, empty direct detour, missing supported outbound, deprecated-removed field errors).

3. Structure assertion
- Verify route bootstrap rules exist in output head.
- Verify DNS servers use new format only.

## Deployment Rollout Plan

1. Build and deploy backend image from current `master`.
2. Canary verify with known problematic subscription links.
3. Compare local-generated vs deployed-generated normalized structure for:
- `dns.servers`
- `route.rules` (top bootstrap entries + dialer entries)
- dialer groups (`dialer-select`, `dialer-lb`, `dialer`).
4. Promote deployment only after parity and runtime checks pass.

## Acceptance Criteria

- All sing-box tests above pass.
- `sing-box check` passes for real-world samples.
- No legacy-DNS-format warnings/errors on 1.12+ runtimes.
- No DNS loop symptom caused by missing route bootstrap rules.
- No provider-node leakage into non-provider groups.

## Non-Regression Rules

- Never re-enable sing-box `<1.12` in backend.
- Never emit legacy `dns.servers[].address` format in default base output.
- Never remove route bootstrap (`sniff` + `hijack-dns`) for 1.12+ generation.

## References

- Migration: <https://sing-box.sagernet.org/migration/>
- Deprecated features: <https://sing-box.sagernet.org/deprecated/>
- DNS server (new format): <https://sing-box.sagernet.org/configuration/dns/server/>
- Legacy DNS server format: <https://sing-box.sagernet.org/configuration/dns/server/legacy/>
- FakeIP (legacy section deprecation note): <https://sing-box.sagernet.org/configuration/dns/fakeip/>
