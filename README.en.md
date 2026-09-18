# Aerofly Link

> Open-source FSD connectivity client for Aerofly FS 4.

Aerofly Link is an unofficial community project. It is not affiliated with or endorsed by IPACS.

## Features

- FSD server connectivity for position sharing and ATC text communication
- Local telemetry and command bridge for Aerofly FS 4
- Nearby-traffic awareness (10 nm great-circle range)
- Transponder controls and flight-plan submission
- Native C rewrite in progress (`client-c/`, see docs/C_REWRITE_PLAN.md)
- Mock DLL mode for development without Aerofly FS 4

## Quick start

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
Copy-Item config/settings.example.json config/settings.json
python main.py
```

Edit `config/settings.json` with your own server and account settings. Do not commit that file.

See [README.md](README.md) for the complete Chinese documentation, build instructions, architecture notes, and troubleshooting information.

## License

The project-owned code is released under [LGPL-3.0-only](LICENSE). Third-party assets retain their original licenses; see [NOTICE.md](NOTICE.md).
