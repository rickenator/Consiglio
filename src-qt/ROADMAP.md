# Consiglio Qt5 Rewrite — Progress Diary

**Started:** 2025-07-31  
**Target:** Full native C++/Qt5 desktop app replacing Electron  
**Status:** IN PROGRESS — M8: Testing (✅ DONE), M9: Real Agent Integration (TODO)

---

## Milestones

### M1: Project Skeleton ✅ DONE
- [x] CMakeLists.txt with Qt5 dependencies (Core, Widgets, Network, Concurrent, Sql)
- [x] Directory structure: src/, src/backend/, src/models/, resources/
- [x] main.cpp — QApplication with Fusion dark style, Segoe UI font

### M2: Core Window & Navigation ✅ DONE
- [x] `mainwindow.h` / `mainwindow.cpp`
  - QStackedWidget content area
  - Menu bar (File, View, Tools, Settings, Help) with keyboard shortcuts
  - Status bar with approval badge + session state label
  - System tray icon
  - Signals: sessionStarted, sessionStopped, approvalRequested, approvalResolved
- [x] `sidebar.h` / `sidebar.cpp` — Left nav panel with icons + labels
  - Panels: Welcome, Sessions, Timeline, Files, Discussions, Secrets, Mobile
  - Keyboard shortcuts (Ctrl+1..4)
  - selectPanel() method for programmatic navigation
- [x] `panelmanager.h` / `panelmanager.cpp` — Creates and manages all panel widgets

### M3: Backend Layer ✅ DONE
- [x] `backend/settings.h` / `.cpp` — QSettings wrapper, load/save AppSettings
  - Fixed: LAN providers JSON serialization/deserialization (was storing string, loading as list)
- [x] `backend/agentdetector.h` / `.cpp` — Detects Codex, Ollama, llama.cpp, Open Interpreter
- [x] `backend/sessionmanager.h` / `.cpp` — Start/stop/reconnect sessions, manages PTY processes
  - Added `sendCommand(sessionId, command)` public method for M7 wiring
  - Fixed: `stopSession()` now properly disconnects signals, kills process, deletes it, erases from map
- [x] `backend/approvalrouter.h` / `.cpp` — Routes approval requests, tracks pending/resolved
- [x] `backend/ptyhandler.h` / `.cpp` — Wraps QProcess for PTY interaction

### M4: UI Panels ✅ DONE (all panels implemented)
- [x] `sessionlist.h` / `.cpp` — QTreeView of active/past sessions with SessionModel
  - Shows session ID, provider, status, repository, branch, timestamps
  - Double-click to select session, stop button for selected session
- [x] `eventtimeline.h` / `.cpp` — Scrollable event log with command input
  - Styled events: system, user, assistant, command output, error, approval
  - Auto-scroll to bottom, command input with send button
- [x] `filebrowser.h` / `.cpp` — QFileSystemModel tree view
  - Browse directory, path bar, file selection
  - Hide size/type columns, show only name
- [x] `secretsmanager.h` / `.cpp` — Key-value store UI
  - Add/delete secrets with key and password-protected value
  - Table display with duplicate detection
- [x] `mobilepairing.h` / `.cpp` — QR code pairing for mobile clients
  - Placeholder QR code display, manual 6-digit code pairing
  - Status indicator with connection simulation
- [x] `discussionpanel.h` / `.cpp` — Threaded discussion view
  - Message list with sender/timestamp styling
  - Text input with auto-resize, send button

### M5: Dialogs ✅ DONE (implemented with real UI)
- [x] `startupwizard.h` / `.cpp` — First-run agent detection + setup flow
- [x] `settingsdialog.h` / `.cpp` — Provider config with 5 tabs
- [x] `approvaldialog.h` / `.cpp` — Modal approval/reject for pending commands

### M6: Data Models ✅ DONE
- [x] `models/sessionmodel.h` / `.cpp` — QAbstractListModel for session list
  - Roles: id, provider, status, repository, branch, startedAt, lastActivity
- [x] `models/eventmodel.h` / `.cpp` — QAbstractListModel for event timeline
  - Event types: system, user, assistant, command, error, approval

### M7: Wiring & Polish ✅ DONE
- [x] Connect SessionManager signals to panels (sessionStarted → refresh list, outputReceived → timeline, sessionStopped → refresh, sessionError → error event)
- [x] Connect panel signals to SessionManager (sessionSelected → switch context, sessionStopped → stopSession, commandExecuted → sendCommand)
- [x] Implement `onNewSession()` — file dialog for workspace, reads provider from settings, starts session, switches to timeline
- [x] Wire approval flow end-to-end (approvalRequested → ConsiglioCmdApproval dialog → resolve)
- [x] Window state persistence — save/restore geometry, window state, last panel index
- [x] Dark theme customization — Fusion palette + global stylesheet (GitHub-dark inspired: #0d1117, #161b22, #30363d, #58a6ff)
- [x] Status bar updates — shows "Running: ollama", "Stopped", "Error" based on session state
- [x] Fix About dialog text (Qt5 / C++17, was Qt6 / C++20)

### M8: Testing ✅ DONE
- [x] CMakeLists.txt — Backend library (ConsiglioBackend), 4 test executables, ctest integration
- [x] `tests/test_approvalrouter.cpp` — 14 tests covering register, resolve, pending, has, signals, duplicates
- [x] `tests/test_settings.cpp` — 8 tests covering defaults, save/load roundtrip, Ollama config, LAN providers, behavior flags, changed signal
- [x] `tests/test_sessionmanager.cpp` — 9 tests covering start/stop/list/has/sendCommand/multiple sessions
- [x] `tests/test_agentdetector.cpp` — 7 tests covering detectAll, detectAvailable, path checks
- [x] **Total: 38 tests, 0 failures**

---

## Build Status: ✅ CLEAN (all targets build, all tests pass)

**Build command:**
```bash
cd src-qt && rm -rf build && mkdir build && cd build
cmake .. && make -j$(nproc)
./Consiglio   # requires X11/Wayland display
./test_approvalrouter   # unit tests
./test_settings
./test_sessionmanager
./test_agentdetector
```

---

## Current Work

**Last updated:** 2025-07-31 — M8 Testing complete, build clean, 38/38 tests passing

### Files Created/Modified in M7–M8:
1. `src/mainwindow.h` — Added wiring slots, session state label, activeSessionId, lastPanelIndex
2. `src/mainwindow.cpp` — Full rewrite: backend→frontend signal wiring, onNewSession() with file dialog, sendCommandToActiveSession(), window state persistence, status bar updates
3. `src/backend/sessionmanager.h` — Added `sendCommand(sessionId, command)` public method
4. `src/backend/sessionmanager.cpp` — Implemented sendCommand(), fixed stopSession() proper cleanup (disconnect signals, kill, delete process, erase from map)
5. `src/backend/settings.cpp` — Fixed LAN providers JSON serialization/deserialization
6. `src/main.cpp` — Dark theme customization (Fusion palette + global stylesheet)
7. `CMakeLists.txt` — Added ConsiglioBackend static library, 4 test executables, ctest integration
8. `tests/test_approvalrouter.cpp` — 14 unit tests
9. `tests/test_settings.cpp` — 8 unit tests
10. `tests/test_sessionmanager.cpp` — 9 unit tests
11. `tests/test_agentdetector.cpp` — 7 unit tests

### Next Up:
- **M9: Real Agent Integration** — Replace echo/ollama stub with actual Codex CLI/Ollama protocol
- **M10: Polish & Packaging** — App icons, .desktop file, AppImage/flatpak packaging

### Key Decisions:
- Qt5 (not Qt6) — Ubuntu 24.04 ships Qt5 in repos
- Fusion dark style + custom stylesheet — no external deps, cross-platform consistent
- QSettings for persistence — Qt native, handles INI/registry
- QProcess for PTY management — replacing node-pty from Electron
- Class names prefixed/unique to avoid Qt macro conflicts (e.g., ConsiglioCmdApproval)
- Backend as static library — shared by app and tests

---

## Session Recovery Notes

If session is interrupted:
1. Check `src-qt/ROADMAP.md` for current milestone
2. All source files are in `src-qt/src/`
3. Build with: `cd src-qt && rm -rf build && mkdir build && cd build && cmake .. && make -j$(nproc)`
4. Run with: `./Consiglio` (requires X11/Wayland display)
5. Electron code in `src/` is the reference — do NOT modify it
