# Vendored Nuklear

- Upstream: <https://github.com/Immediate-Mode-UI/Nuklear>
- Release: `v4.13.3`
- Commit: `a53ad2c658151071501372a5e0e5e978153835aa`
- Files: upstream `nuklear.h`, `demo/gdi/nuklear_gdi.h`, and `LICENSE`.
- The GDI backend is Windows-specific. Nuklear's widget core is platform-independent, but this release does not claim Linux or macOS support.

The upstream license offers MIT or public-domain terms. This project retains the full upstream license file and its notices.

## Local backend hardening

The vendored GDI demo backend has small local safety fixes: use `DeleteDC` for device contexts, select a replacement bitmap before deleting the previous resize buffer, reject invalid/oversized UTF-8 text and clipboard pastes, and release clipboard memory when ownership transfer fails. These changes do not alter the Nuklear widget API.
