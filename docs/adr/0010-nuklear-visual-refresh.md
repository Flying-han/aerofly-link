# ADR 0010: Nuklear operations UI for v0.3.1

- Status: Accepted (2026-09-29)
- Supersedes the visual and control-layout sections of [ADR 0006](0006-win32-ui.md)

## Context

The v0.3.0 interface used fixed-position Win32 controls in a non-resizable window. Connection labels such as `ECO` and `TYPE` exposed internal fields, server deletion used a minus symbol, and advanced authentication/proxy settings required editing `settings.json`.

## Decision

1. Use Nuklear v4.13.3 with its Win32/GDI backend, pinned in `client-c/vendor/nuklear/` at commit `a53ad2c658151071501372a5e0e5e978153835aa`.
2. Keep one resizable Win32 application window and the current `WinMain`/Windows GUI subsystem. The GUI executable must not allocate a console window.
3. Build a dark flight-operations workspace with direct labels, a visible connection path, separated flight/plan/transponder/messages, responsive one/two-column layouts, DPI-aware sizes, and an optional light appearance.
4. Keep the password field as a native masked Win32 edit control. Passwords remain memory-only and are cleared after authentication or disconnect.
5. Expose language, theme, server history, authentication mode, HTTPS token endpoint, proxy, bypass list, and mock telemetry controls in the GUI. Persist language as `zh-CN`, `zh-HK`, or `en-US`.
6. Keep the application's product copy independent from network-provided FSD messages. The VATSIM address is reference-only; see ADR 0009.
7. v0.3.1 supports Windows 10 and later only. Linux/macOS are deferred; the external AeroflyBridge source and a non-Windows renderer/transport are not in this repository.

## Consequences

- Nuklear owns the widget layout and drawing commands; the Win32/GDI adapter handles the native window, input, DPI, and masked password edit.
- The GDI backend is Windows-specific. Vendoring Nuklear does not itself make the current application cross-platform.
- GDI bitmap resources are released on resize/shutdown; the backing surface is capped at 32 megapixels and clipboard paste is capped at 8192 UTF-16 characters.
- The normal installer contains the production GUI only. `build-e2e-gui.cmd` creates a separate ignored test variant that accepts a synthetic local-E2E password and test JWT provider.

## Verification

- `client-c/build.cmd`: C protocol/session/config tests and the normal GUI build.
- `client-c/build-e2e-gui.cmd` plus `tools/smoke_gui.py`: isolated GUI/FSD E2E with connection, flight-deck, settings, language, and appearance screenshots.
- Linux/macOS builds and live Aerofly telemetry on those systems are not claimed by this release.
