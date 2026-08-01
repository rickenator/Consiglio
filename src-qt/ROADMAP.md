# Consiglio Qt5 Rewrite — Progress Diary

**Started:** 2025-07-31  
**Target:** Full native C++/Qt5 desktop app replacing Electron  
**Status:** IN PROGRESS — M4: UI Panels (M5 Dialogs ✅ DONE, Build ✅ CLEAN)

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
  - Status bar with approval badge
  - System tray icon
  - Signals: sessionStarted, sessionStopped, approvalRequested, approvalResolved
- [x] `sidebar.h` / `sidebar.cpp` — Left nav panel with icons + labels
  - Panels: Welcome, Sessions, Timeline, Files, Discussions, Secrets, Mobile
  - Keyboard shortcuts (Ctrl+1..4)
  - selectPanel() method for programmatic navigation
- [x] `panelmanager.h` / `panelmanager.cpp` — Creates and manages all panel widgets

### M3: Backend Layer ✅ DONE
- [x] `backend/settings.h` / `.cpp` — QSettings wrapper, load/save AppSettings
- [x] `backend/agentdetector.h` / `.cpp` — Detects Codex, Ollama, llama.cpp, Open Interpreter
- [x] `backend/sessionmanager.h` / `.cpp` — Start/stop/reconnect sessions, manages PTY processes
- [x] `backend/approvalrouter.h` / `.cpp` — Routes approval requests, tracks pending/resolved
- [x] `backend/ptyhandler.h` / `.cpp` — Wraps QProcess for PTY interaction

### M4: UI Panels 🔄 IN PROGRESS (stub widgets exist)
- [ ] `sessionlist.h` / `.cpp` — QTreeView of active/past sessions
- [ ] `eventtimeline.h` / `.cpp` — Scrollable event log (messages, code, console)
- [ ] `filebrowser.h` / `.cpp` — QFileSystemModel tree view
- [ ] `secretsmanager.h` / `.cpp` — Encrypted key-value store UI
- [ ] `mobilepairing.h` / `.cpp` — QR code pairing for mobile clients
- [ ] `discussionpanel.h` / `.cpp` — Threaded discussion view

### M5: Dialogs ✅ DONE (implemented with real UI)
- [x] `startupwizard.h` / `.cpp` — First-run agent detection + setup flow
  - Shows detected agents with status icons
  - Indeterminate progress bar during detection
  - Save default provider on continue
- [x] `settingsdialog.h` / `.cpp` — Provider config with tabs
  - General tab: default provider, default model
  - Ollama tab: base URL, model, API key
  - Remote llama.cpp tab: base URL, model, API key
  - LAN Providers tab: shows detected agents
  - Local Behavior tab: isolate profile, web search, multi-agent
- [x] `approvaldialog.h` / `.cpp` — Modal approval/reject for pending commands
  - Shows command, working dir, sandbox policy, affected paths
  - Approve/Reject buttons with styling

### M6: Data Models 📋 TODO (stub implementations)
- [ ] `models/sessionmodel.h` / `.cpp` — QAbstractListModel for session list
- [ ] `models/eventmodel.h` / `.cpp` — QAbstractListModel for event timeline

### M7: Wiring & Polish 📋 TODO
- [ ] Connect MainWindow signals to SessionManager/ApprovalRouter
- [ ] Populate panels from backend data
- [ ] Handle session start/stop/reconnect flow end-to-end
- [ ] Handle approval request → dialog → resolve flow end-to-end
- [ ] Qt resource file (icons.qrc) with app icons
- [ ] Dark theme customization (Fusion palette tweaks)
- [ ] Window state persistence (geometry, sidebar width, last panel)
- [ ] CMake install targets

---

## Build Status: ✅ CLEAN (compiles successfully)

**Build command:**
```bash
cd src-qt && rm -rf build && mkdir build && cd build
cmake .. && make -j$(nproc)
./Consiglio   # requires display
```

---

## Current Work

**Last updated:** 2025-07-31 — M5 Dialogs implemented, build clean

### Files Created/Modified:
1. `src-qt/CMakeLists.txt` — Build config
2. `src-qt/src/main.cpp` — App entry point
3. `src-qt/src/mainwindow.h` / `.cpp` — Main window (menu bar, sidebar, tray, status bar)
4. `src-qt/src/sidebar.h` / `.cpp` — Left navigation panel with selectPanel()
5. `src-qt/src/panelmanager.h` / `.cpp` — Panel factory/manager
6. `src-qt/src/backend/settings.h` / `.cpp` — Settings persistence (load/save + value getter)
7. `src-qt/src/backend/agentdetector.h` / `.cpp` — Agent detection
8. `src-qt/src/backend/sessionmanager.h` / `.cpp` — Session lifecycle management
9. `src-qt/src/backend/approvalrouter.h` / `.cpp` — Approval request routing
10. `src-qt/src/backend/ptyhandler.h` / `.cpp` — PTY/process wrapper
11. `src-qt/src/startupwizard.h` / `.cpp` — First-run wizard (real UI)
12. `src-qt/src/settingsdialog.h` / `.cpp` — Settings dialog (real UI, 5 tabs)
13. `src-qt/src/approvaldialog.h` / `.cpp` — Command approval dialog (real UI)
14. `src-qt/src/models/sessionmodel.h` / `.cpp` — Stub
15. `src-qt/src/models/eventmodel.h` / `.cpp` — Stub
16. `src-qt/src/sessionlist.h` / `.cpp` — Stub
17. `src-qt/src/eventtimeline.h` / `.cpp` — Stub
18. `src-qt/src/filebrowser.h` / `.cpp` — Stub
19. `src-qt/src/secretsmanager.h` / `.cpp` — Stub
20. `src-qt/src/mobilepairing.h` / `.cpp` — Stub
21. `src-qt/src/discussionpanel.h` / `.cpp` — Stub

### Next Up:
- **M4: UI Panels** — Implement actual panel widgets (SessionList, EventTimeline, FileBrowser)
- **M7: Wiring** — Connect backend to frontend, wire up session/approval flows
- **M7: Polish** — Icons, window state persistence, theme tweaks

### Key Decisions:
- Qt5 (not Qt6) — Ubuntu 24.04 ships Qt5 in repos
- Fusion dark style — no external deps, cross-platform consistent
- QSettings for persistence — Qt native, handles INI/registry
- QProcess for PTY management — replacing node-pty from Electron
- Class names prefixed/unique to avoid Qt macro conflicts (e.g., ConsiglioCmdApproval)

---

## Session Recovery Notes

If session is interrupted:
1. Check `src-qt/ROADMAP.md` for current milestone
2. All source files are in `src-qt/src/`
3. Build with: `cd src-qt && rm -rf build && mkdir build && cd build && cmake .. && make -j$(nproc)`
4. Run with: `./Consiglio` (requires X11/Wayland display)
5. Electron code in `src/` is the reference — do NOT modify it
