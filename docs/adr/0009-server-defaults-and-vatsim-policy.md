# ADR 0009: ASC default endpoint and VATSIM connection boundary

- Status: Accepted (2026-09-29)
- Scope: v0.3.1 connection defaults and server selection

## Context

The previous default `sweatbox.vatsim.net:6809` names a VATSIM training environment. It is not the ASC community flight server requested for normal online flying. VATSIM also requires approved clients; Aerofly Link is not on its public approved pilot-client list.

## Decision

1. New settings default to the ASC FSD endpoint `flight.skeet.top:6809`.
2. Do not seed Sweatbox or VATSIM servers into the user's connectable saved-server list.
3. Show VATSIM's `AUTOMATIC` address, `fsd.connect.vatsim.net:6809`, on the Settings page for reference only. Block direct connections to `*.vatsim.net` until the client is approved.
4. Explain the approval boundary in the GUI and link the official approved-software page. Do not describe FSD-JWT/revision 100 support as approval to join VATSIM.
5. Rename the serialized authentication field to `auth_mode`, with values `fsd-jwt` and `legacy`. Continue reading old `type: vatsim|legacy` and `eco` fields for compatibility; new settings no longer write the ambiguous `eco` or `type` keys.

## Consequences

- First-run setup points to the intended ASC community server without editing JSON.
- Users can add and remove their own FSD endpoints, while the ASC default remains available.
- Existing settings retain their selected server and authentication mode. The old key mapping is a read-time compatibility path.
- VATSIM's network server list is dynamic; the authoritative feed is published by its status service. Sweatbox servers are listed separately from normal servers.

## Verification

- Config tests cover the ASC default, `auth_mode` read/write, old `type` compatibility, and safe JSON round-tripping.
- Local E2E never sends synthetic credentials to VATSIM; VATSIM network integration remains blocked on written software approval.

## Official references checked

- [VATSIM status JSON](https://status.vatsim.net/status.json) publishes separate `servers` and `servers_sweatbox` feeds.
- [VATSIM Data API server schema](https://vatsim.dev/api/data-api/list-all-fsd-servers/) distinguishes connectable servers from Sweatbox servers with `is_sweatbox`.
- A [VATSIM Pilot Software support thread](https://forum.vatsim.net/t/severe-audio-stuttering-diagnosis-and-request-for-solution/3807) identifies `fsd.connect.vatsim.net` as the AUTOMATIC hostname; the authoritative live server list is linked from [status JSON](https://status.vatsim.net/status.json).
- [VATSIM Code of Conduct](https://vatsim.net/docs/policy/code-of-conduct/) rule A7 requires an approved client; [Approved Software](https://vatsim.net/docs/policy/approved-software/) is the published list.
