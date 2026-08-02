# Consiglio Project State

Last updated: 2026-08-01 18:18 PDT

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
- The SQLite/Projects/preferred-name/history-MCP work described below is
  currently verified locally but uncommitted and unpushed.

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
- Settings includes **Your name**, defaults to `Dude`, persists in SQLite, and
  is injected into Codex sessions through supported developer instructions.
- `Discussions` has been replaced by `Projects`. A project is a persisted
  workspace containing multiple sessions; each session owns its retained
  timeline.
- Sessions, timeline events/conversations, projects, window state, provider
  configuration, and app preferences use one SQLite database:
  `/home/rick/.local/share/Aniviza/Consiglio/consiglio.sqlite`.
- Existing QSettings values migrate into SQLite once. Sessions left marked
  running after an app crash are changed to `interrupted` at next startup.
- Conversation content is indexed with SQLite FTS5 and exposed through the
  read-only `consiglio_history` MCP server. Its tools list projects/sessions,
  retrieve chronological conversations, and search retained history.
- New Codex-backed Consiglio sessions automatically register the history MCP
  with the active database path.
- Inline `\(...\)` and display `\[...\]`/`$$...$$` math are presented as native
  rich text. Common TeX structures and symbols such as fractions, roots,
  superscripts, subscripts, Delta, sums, integrals, and infinity are converted
  to readable equation typography.

## Verification

- Build: `cmake --build src-qt/build -j2` passed.
- Full suite: 10/10 tests passed on 2026-08-01.
- Added regression coverage:
  - provider dialog returns provider + workspace + permissions;
  - session manager emits the sandbox configuration correctly;
  - transcript consumes Markdown/table/math syntax instead of displaying raw
    markers;
  - the thinking indicator animates and returns to its idle state.
- SQLite reopen/persistence, project/session/event relationships, crash-state
  recovery, preferred-name settings UI, MCP initialization/tool listing,
  chronological retrieval, and FTS conversation search all have regression
  coverage.
- Codex `mcp list` recognized `consiglio_history` as enabled using the generated
  runtime command and production database path.
- Real structured remote session test passed for an initial prompt and resumed
  prompt after the sandbox argument fix.
- Current desktop runtime at this update:
  - PID `1007124`: `src-qt/build/Consiglio`
  - visible X11 window title: `Consiglio`
  - log: `/tmp/consiglio-qt-live.log`

## Files in the active change

- `src-qt/src/mainwindow.cpp/.h`
- `src-qt/src/providerselectiondialog.cpp/.h`
- `src-qt/src/newsessiondialog.cpp/.h` (new)
- `src-qt/src/backend/sessionmanager.cpp/.h`
- `src-qt/src/eventtimeline.cpp`
- `src-qt/src/backend/appdatabase.cpp/.h` (new)
- `src-qt/src/projectpanel.cpp/.h` (new; replaces deleted discussion panel)
- `src-qt/mcp/consiglio_history_mcp.py` (new)
- `src-qt/CMakeLists.txt`
- `src-qt/tests/test_sessionmanager.cpp`
- `src-qt/tests/test_settings.cpp`
- `src-qt/tests/test_providerselectiondialog.cpp` (new)
- `src-qt/tests/test_eventtimeline.cpp` (new)
- `src-qt/tests/test_settingsdialog.cpp` (new)
- `src-qt/tests/test_appdatabase.cpp` (new)
- `src-qt/tests/test_consiglio_history_mcp.py` (new)

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

1. Open Settings and confirm **Your name** defaults to `Dude`; change it if desired.
2. Start two sessions in the same workspace and one in a different workspace.
3. Confirm Projects groups those sessions by workspace and recorded sessions
   remain after restarting Consiglio.
4. Open a recorded session and verify its retained timeline is read-only while
   an active session still accepts messages.
5. Send a response containing headings, bullets, a table, inline math, and a
   display equation; inspect wrapping, spacing, and equation readability at the
   current display scale.
6. Confirm the animated Thinking icon appears immediately after send and clears
   when the response arrives.
7. Ask a Codex session to use `consiglio_history` to search a phrase from an
   earlier retained conversation.
8. Address any visual issues, rerun all tests, then commit and push when asked.
