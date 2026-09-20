# Aerofly Link

> Open-source FSD connectivity client for Aerofly FS 4 — native C, single binary.

Aerofly Link is an unofficial community project. It is not affiliated with or endorsed by IPACS.

See the Chinese [README.md](README.md) for the full documentation.

## Features

- FSD server connectivity for position sharing and ATC text communication
- Telemetry/command bridge for Aerofly FS 4 (external open-source AeroflyBridge.dll)
- Nearby-traffic awareness (10 nm great-circle range)
- Transponder controls and full flight-plan submission
- Mock DLL mode for development without Aerofly FS 4

## Build

```cmd
cd client-c
build.cmd
:: outputs: build\aeroflylink.exe (GUI), build\aeroflylink-cli.exe (headless)
:: requires zig cc; tests run automatically at the end
```

## Documentation

- [docs/C_REWRITE_PLAN.md](docs/C_REWRITE_PLAN.md) — architecture and plan
- [docs/RELEASE.md](docs/RELEASE.md) — release checklist
- [docs/adr/](docs/adr/) — architecture decision records
