# Contributing

Issues and pull requests are welcome.

Before opening a pull request:

1. Explain the user-visible problem or improvement.
2. Keep changes focused and avoid committing credentials, logs, build outputs, or local configuration.
3. For changes under `client-c/`, run `client-c\build.cmd` using `zig cc` from the active vfox Zig toolchain and keep the C test suite green.
4. Include reproduction steps for bug fixes. User-facing behavior changes need an ADR under `docs/adr/`.

The Python scripts under `tools/` are development utilities only; the client and its tests are implemented in C.
For protocol or simulator-integration changes, include a Mock FSD or bridge contract case whenever practical.
