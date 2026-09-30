# Aerofly Link

> Aerofly Link 0.3.1 — Open-source Aerofly FS 4 community-flying client, implemented in C.

Aerofly Link is an unofficial community project. It is not affiliated with or endorsed by IPACS.

See the Chinese [README.md](README.md) for the full documentation.

## Features

- ASC default server: `flight.skeet.top:6809`; community and self-hosted FSD servers can be managed in Settings
- Telemetry/command bridge for Aerofly FS 4 (external open-source AeroflyBridge.dll)
- Nearby-traffic awareness (10 nm great-circle range)
- Transponder controls and full flight-plan submission
- Mock DLL mode for development without Aerofly FS 4
- Resizable single-window Nuklear GUI with Simplified Chinese, Hong Kong Traditional Chinese, and American English
- GUI controls for server profiles, authentication, HTTPS token URL, proxy, theme, and simulated telemetry

New settings use `auth_mode: "fsd-jwt"` (revision 100); legacy revision 9 remains available for servers that require it. Settings are managed in the GUI and stored in `%APPDATA%\AeroflyLink\settings.json`. Passwords remain in memory only.

**VATSIM policy:** VATSIM requires approved clients. Aerofly Link is not on its current approved list, so this application blocks direct VATSIM connections and only displays the official `AUTOMATIC` address for reference. Use a [VATSIM-approved client](https://vatsim.net/docs/policy/approved-software/) to join that network. FSD-JWT is an authentication dialect; it does not mean Aerofly Link is approved for VATSIM.

v0.3.1 supports Windows 10 and later. Windows 10 Home/Pro reached end of regular support on 2025-10-14; use a Windows release that still receives security updates or an enrolled ESU device ([Microsoft lifecycle details](https://learn.microsoft.com/en-us/windows/release-health/release-information)). Linux and macOS support is not promised yet. Live simulator telemetry also requires the external AeroflyBridge DLL.

## Build

```cmd
cd client-c
build.cmd
:: outputs: build\aeroflylink.exe (GUI), build\aeroflylink-cli.exe (headless)
:: requires zig cc from the active vfox Zig toolchain; C tests run automatically at the end
```

## Documentation

- [docs/C_REWRITE_PLAN.md](docs/C_REWRITE_PLAN.md) — architecture and plan
- [docs/GITHUB_ACTIONS.md](docs/GITHUB_ACTIONS.md) — CI, E2E, artifacts, and release workflow guide (Chinese)
- [docs/RELEASE.md](docs/RELEASE.md) — release checklist
- [docs/releases/v0.3.1.md](docs/releases/v0.3.1.md) — v0.3.1 changes and local verification scope
- [docs/adr/](docs/adr/) — architecture decision records
