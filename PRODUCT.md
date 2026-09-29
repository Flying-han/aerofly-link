# Product

<!-- impeccable:product-schema 1 -->

## Platform

adaptive

## Users

Aerofly FS 4 online-flying participants who need to join a community FSD server, manage a flight plan and transponder, and exchange text with other network users.

## Product Purpose

Aerofly Link connects Aerofly FS 4 telemetry through the external AeroflyBridge DLL to an FSD flight-simulation network. Success means a pilot can select a server, connect, see connection and flight state, and handle common flight operations from one desktop application.

## Positioning

An open-source C desktop client whose connection and flight-operation workflow is centered on Aerofly FS 4 and community FSD servers.

## Operating Context

Pilots use the client alongside a flight simulator on a desktop. Connection state, transponder controls, flight-plan fields, and messages need to be read and operated quickly. Community server addresses and authentication requirements can differ.

## Capabilities and Constraints

- The current shipped client is Win32 and depends on Windows networking APIs plus an external AeroflyBridge DLL whose source is not part of this repository.
- v0.3.1 is scoped to Windows 10 and later. Linux and macOS support are future targets with no v0.3.1 support claim.
- The ASC community FSD default is `flight.skeet.top:6809`, as specified by the product owner.
- VATSIM publishes distinct network and Sweatbox server feeds. VATSIM requires approved software and the current public pilot-client list does not include Aerofly Link; v0.3.1 may display the official address for reference but must not provide a one-click VATSIM connection profile.
- The UI must support Simplified Chinese for Mainland China, Traditional Chinese for Hong Kong, and American English.
- Connection passwords remain memory-only and are never saved to settings.

## Evidence on Hand

- FSD client and local E2E contracts: `docs/testing/e2e-fsd-client.md`.
- AeroflyBridge integration boundary: `docs/adr/0002-external-bridge-dll.md`.
- Credential handling: `docs/adr/0003-credential-handling.md`.
- Existing GUI smoke driver: `tools/smoke_gui.py`.

## Product Principles

- Make the next safe action obvious to a first-time pilot.
- Put current connection and flight state where it can be read at a glance.
- Keep common operations available in the main window and move rare protocol details into settings.
- Label network profiles and authentication in community terms, not unexplained abbreviations.
- Preserve open-source customization without making routine setup depend on editing JSON.
