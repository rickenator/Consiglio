# Consiglio Project State

Last updated: 2026-08-01 17:54 PDT

This is the durable restart point for active Consiglio work. Update it after each
material implementation, verification, commit, or newly discovered blocker.

## Current checkout

- Path: `/usr/export/rick/Projects/Consiglio`
- Branch: `update-1`
- Remote tracking branch: `origin/update-1`
- Latest pushed commits:
  - `b476d6a fix(electron): align multi-agent API contracts`
  - `8ab0714 feat(qt): add native provider discovery and structured sessions`
  - `7655de6 feat(qt): streamline sessions and rich timeline`
  - `0d31c4f feat(qt): animate agent thinking state`
- Feature code is pushed to `github.com/rickenator/Consiglio` on `update-1`.

## Current behavior

- First setup and provider rescan now collect provider, workspace, and permission
  mode in one dialog.
- Pressing **Start session** in that dialog immediately creates and activates the
  session, switches to Timeline, and records the session event. A separate
  **File -> New Session** action is no longer required after provider selection.
- Later sessions use a focused New Session dialog with the configured provider.
- Permission modes are `read-only`, `workspace-write`, and
  `danger-full-access`; the dangerous mode requires confirmation.
- Codex sandbox mode is passed with `-c sandbox_mode=...` for compatibility with
  both initial `codex exec` and `codex exec resume` commands.
- Agent transcript messages render GitHub-style Markdown, including headings,
  emphasis, lists, tables, and code blocks.
- Sending a prompt displays an animated `Thinking` icon beside the composer.
  Duplicate sends are blocked while it is active, and it clears on agent output,
  assistant response, error, session stop, or session switch.
- Inline `\(...\)` and display `\[...\]`/`$$...$$` math are presented as native
  rich text. Common TeX structures and symbols such as fractions, roots,
  superscripts, subscripts, Delta, sums, integrals, and infinity are converted
  to readable equation typography.

## Verification

- Build: `cmake --build src-qt/build -j2` passed.
- Full Qt suite: 7/7 tests passed on 2026-08-01.
- Added regression coverage:
  - provider dialog returns provider + workspace + permissions;
  - session manager emits the sandbox configuration correctly;
  - transcript consumes Markdown/table/math syntax instead of displaying raw
    markers;
  - the thinking indicator animates and returns to its idle state.
- Real structured remote session test passed for an initial prompt and resumed
  prompt after the sandbox argument fix.
- Current desktop runtime at this update:
  - PID `989871`: `src-qt/build/Consiglio`
  - visible X11 window title: `Consiglio`
  - log: `/tmp/consiglio-qt-live.log`

## Files in the active change

- `src-qt/src/mainwindow.cpp/.h`
- `src-qt/src/providerselectiondialog.cpp/.h`
- `src-qt/src/newsessiondialog.cpp/.h` (new)
- `src-qt/src/backend/sessionmanager.cpp/.h`
- `src-qt/src/eventtimeline.cpp`
- `src-qt/CMakeLists.txt`
- `src-qt/tests/test_sessionmanager.cpp`
- `src-qt/tests/test_settings.cpp`
- `src-qt/tests/test_providerselectiondialog.cpp` (new)
- `src-qt/tests/test_eventtimeline.cpp` (new)

## Resume commands

```bash
cd /usr/export/rick/Projects/Consiglio
git status --short --branch
cmake --build src-qt/build -j2
QT_QPA_PLATFORM=offscreen /home/rick/.local/lib/python3.10/site-packages/cmake/data/bin/ctest --test-dir src-qt/build --output-on-failure
src-qt/build/Consiglio
```

The settings test uses an isolated temporary QSettings store. Under a restricted
agent sandbox, Qt may need permission to create its lock/temp files; the same
suite passes outside that restriction.

## Next action

Manually exercise the visible app:

1. Rescan/select the remote provider.
2. Choose workspace and permissions in the same dialog.
3. Confirm **Start session** lands directly in the active Timeline.
4. Send a response containing headings, bullets, a table, inline math, and a
   display equation; inspect wrapping, spacing, and equation readability at the
   current display scale.
5. Confirm the animated Thinking icon appears immediately after send and clears
   when the response arrives.
6. Address any visual issues, rerun all tests, then commit and push when asked.
