# Contributing

Issues and pull requests are welcome.

Before opening a pull request:

1. Explain the user-visible problem or improvement.
2. Keep changes focused and avoid committing credentials, logs, build outputs, or local configuration.
3. Run `python -m compileall main.py main_window.py core ui installer` and `python -m pytest tests core -q`.
4. For changes under `client-c/`, run `build.cmd` (zig cc) and keep the C test suite green.
5. Include reproduction steps for bug fixes. User-facing behavior changes need an ADR under `docs/adr/`.

For protocol or simulator-integration changes, include a Mock DLL test case whenever practical.
