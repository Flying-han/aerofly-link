## Summary

<!-- What does this change do? -->

## Testing

- [ ] `client-c\build.cmd` (zig cc) for changes to the C client
- [ ] Relevant C unit tests and Mock FSD/bridge checks
- [ ] GUI changes: `client-c\build-e2e-gui.cmd` and `python tools/smoke_gui.py`
- [ ] Workflow changes: reviewed permissions, action pins, artifact contents, and release triggers

## Checklist

- [ ] No credentials, local paths, logs, or build artifacts are included.
- [ ] Documentation is updated when behavior changes.
