# Consiglio — Evidence-Based Codebase Analysis

> Generated from full source inspection. No assumptions used.

## 1. System Overview

**Consiglio** is an Electron desktop application (React + TypeScript) that serves as a control plane for the Codex CLI agent. It runs three bundled processes:

- **Main process** (`src/main.ts`, ~2569 lines): Electron process owning subprocess management, persistence, encryption, IPC, and application menu.
- **Preload script** (`src/preload.ts`): Narrow `contextBridge` exposing a bounded `codexApi` to the renderer.
- **Renderer** (`src/renderer.tsx` → `src/App.tsx`, 1271 lines): React UI providing session management, provider configuration, secrets, file browsing, and mobile pairing.

The app manages sessions (long-running Codex tasks via PTY), providers (Ollama, remote llama.cpp, LAN-discovered servers, GPT-5.6, default Codex), encrypted secrets, command approvals, file browsing, and a mobile companion bridge. Vite builds three targets (main, preload, renderer) with a custom `consiglio://` protocol for production asset serving.

---

## 2. Runtime Architecture

### Process Startup
1. `electron .` launches `dist/main.js` (compiled from `src/main.ts`)
2. `app.whenReady()` registers the `consiglio` protocol, initializes store/settings/secrets/mobile bridge, builds the application menu, creates the main `BrowserWindow`
3. Main window loads renderer assets via `consiglio://app/` in production or `file://` during dev
4. `src/renderer.tsx` mounts React into `#root`, wrapping `<App />` in an ErrorBoundary

### Task Execution Flow
1. User starts a session → `session:start` IPC → main resolves Codex command via `platform.ts`, builds args with provider config, spawns `node-pty` PTY
2. PTY output parsed by `CodexAdapter.handleTerminalOutput()` → JSONL accumulation → `consumeExecEvents()` emits unified `AgentEvent`s via `codex:event` IPC
3. Approval requests captured, registered in `AgentApprovalRouter`, broadcast via `codex:approval-request`
4. User approves/rejects → `agents:resolve-approval` IPC → router routes to owning `ApprovalAwareAdapter` which writes `y\n` or `n\n` to PTY stdin

### Session Lifecycle
- `launch()` → spawns PTY, registers session state
- `sendPrompt()` → writes prompt to PTY stdin, waits for response via promise + polling
- `stopSession()` → kills PTY, clears pending approvals, removes from store
- `reconnectSession()` → re-spawns PTY with same thread ID

---

## 3. Component Map

| Directory/File | Responsibility |
|---|---|
| `src/main.ts` (~2569 lines) | Electron main: window management, IPC handlers, session lifecycle, persistence (JSON store), settings, secrets encryption, mobile bridge init, application menu |
| `src/preload.ts` | Narrow `contextBridge` exposing bounded `codexApi` methods to renderer |
| `src/App.tsx` (1271 lines) | React shell: state management, banner rendering, settings panel, session start/reconnect/stop, provider setup screen, main layout with sidebar + timeline |
| `src/features/sessions/SessionList.tsx` | Sidebar: session list, new task dialog, provider selection, "Open folder" button |
| `src/features/sessions/EventTimeline.tsx` | Main panel: event stream display, prompt input, attachment handling, reconnect button, thinking indicator |
| `src/features/files/FileBrowser.tsx` | File browser overlay: directory listing, text/image preview |
| `src/features/secrets/SecretsManager.tsx` | Modal: encrypted credential management (CRUD) |
| `src/features/mobile/MobilePairing.tsx` | Modal: mobile bridge status, QR code pairing, token rotation |
| `src/features/discussions/DiscussionPanel.tsx` | Multi-agent discussion UI: agent selection, message bubbles, synthesis |
| `src/main/adapters/` | Agent adapter implementations (Codex, OI, Aider, Claude Code, Gemini, Copilot, Amazon Q) |
| `src/main/agent-adapter.ts` | Interface definitions: `AgentAdapter`, `AgentEvent`, `AgentApproval`, `AgentSession` |
| `src/main/approval-aware-adapter.ts` | Decorator that tracks approvals and routes resolution to owning PTY |
| `src/main/approval-router.ts` | Single-use approval routing with tombstoning against replay |
| `src/main/discussion-session.ts` | Multi-agent discussion orchestrator: round-robin/context-aware moderation, synthesis |
| `src/main/agent-readiness.ts` | Agent detection: version checks, auth status, support tier classification |
| `src/main/startup-bootstrap.ts` | Managed agent installation (Codex, Open Interpreter) with progress reporting |
| `src/main/platform.ts` | Cross-platform Codex executable resolution |
| `src/main/lan-discovery.ts` | Network probe for llama.cpp/Ollama servers on local subnet |
| `src/main/mobile-bridge.ts` | HTTP server for mobile companion: auth, session ops, approval routing |
| `src/main/mobile-pairing-config.ts` | Port/URL validation, pairing URI generation |
| `src/main/app-protocol.ts` | Custom protocol handler, renderer URL validation, path traversal protection |
| `src/types.d.ts` | Global type declarations for `Window.codexApi` and all IPC interfaces |
| `src/styles.css` (1940 lines) | All CSS: dark theme variables, layout, components, animations |

---

## 4. Distinctions: Providers vs Adapters vs Agents vs Sessions vs Discussions

### Providers
Type `Provider = 'default' | 'remote_llamacpp' | 'gpt56' | 'lan' | 'ollama'` — configuration targets that determine which model endpoint a session connects to. They are settings-level concepts stored in `CodexSettings`. A provider is not a runtime object; it's a string tag influencing how `CodexAdapter.buildLaunchArgs()` constructs CLI arguments.

### Adapters
Runtime wrappers around specific CLI front-ends (Codex, Open Interpreter, Aider, Claude Code, Gemini, Copilot, Amazon Q). Each implements `AgentAdapter` interface with `launch()`, `sendPrompt()`, `stopSession()`, `reconnectSession()`. Currently only `CodexAdapter` is wired into the main app; others are standalone classes exported but never instantiated by `getAdapter()` (which only accepts `'codex'`).

### Agents
Higher-level identity that an adapter wraps. The `AgentAdapter` interface is parameterized by agent ID. In the discussion system, agents are selected from `{ 'codex', 'open-interpreter', 'aider', 'claude-code' }`. The `index.ts` registry currently only knows about `'codex'` in `KNOWN_AGENTS`.

### Sessions
PTY-backed runtime instances with lifecycle (running/stopped/failed/completed). Each session has a unique ID, repository, branch, provider config, terminal buffer, and processed event IDs. Sessions are persisted to JSON store and recovered on restart via stored thread IDs.

### Discussions
Multi-agent orchestration layer that spawns multiple adapter sessions simultaneously, moderates turns (round-robin or context-aware), captures streamed responses, and optionally synthesizes a final answer. Currently only wired into the UI (`DiscussionPanel`) but not connected to the actual `DiscussionSession` class — the panel uses `window.codexApi` directly rather than the `DiscussionSession` orchestrator.

---

## 5. Data Flow

### Starting a Session
1. User clicks "New task" → `SessionList.startTask()` → calls `handleStartSession()` in App.tsx
2. `handleStartSession()` calls `window.codexApi.startSession(opts)` → IPC `session:start`
3. Main process resolves Codex command (`platform.ts`), builds args with provider config, applies secrets as env vars
4. Spawns `node-pty` with `codex exec --json --skip-git-repo-check ... prompt`
5. PTY output flows through `CodexAdapter.handleTerminalOutput()` → JSONL parsing → `emitEvent()` → IPC `codex:event` → React state update

### Streaming Model Output
1. PTY `onData` callback accumulates raw text in `jsonRemainder`
2. When `{...}` JSON boundaries are detected, `consumeExecEvents()` parses structured events
3. Events emitted via `emitEvent()` to IPC → renderer `onEvent` listener → `setEvents(prev => [...prev, event])`
4. Auto-scroll triggers on state change

### Requesting Command Approval
1. PTY output contains an approval request JSON → parsed as `AgentApproval`
2. `emitApproval()` broadcasts via IPC `codex:approval-request`
3. `ApprovalAwareAdapter.trackApproval()` registers with `AgentApprovalRouter`
4. UI shows approval banner with Approve/Reject buttons
5. User clicks → `approveCommand()` / `rejectCommand()` → IPC `agents:resolve-approval`
6. Router validates single-use, resolves to owning session, writes `y\n` or `n\n` to PTY stdin

### Reconnecting to a Session
1. On startup, `initialize()` loads sessions from store, finds the last selected session
2. If session status is not `'running'`, calls `session:reconnect` IPC
3. Main process re-spawns PTY using stored `codexThreadId` and repository
4. New events flow through the same streaming pipeline; old events loaded via `session:events` IPC

---

## 6. Implementation Status

### Implemented
- **CodexAdapter** (`src/main/adapters/codex-adapter.ts`, 562 lines): Full PTY lifecycle, JSONL parsing, prompt sending, session management. Only adapter actually used by the app.
- **Approval system**: `ApprovalAwareAdapter`, `AgentApprovalRouter` — single-use routing with tombstoning, cross-session isolation
- **Session persistence**: JSON store with recovery across restarts/crashes
- **Settings management**: Provider config, LAN providers, local provider behavior checkboxes
- **Secrets manager**: Encrypted credential storage via Electron `safeStorage`
- **Mobile bridge**: HTTP server with auth, session ops, approval routing (disabled by default)
- **LAN discovery**: Network probe for llama.cpp/Ollama servers
- **Agent readiness detection**: Version checks, auth status for 7 agents
- **Startup bootstrap**: Managed installation of Codex and Open Interpreter
- **DiscussionPanel UI**: Full multi-agent discussion interface with message bubbles, agent selection
- **File browser**: Directory listing, text/image preview
- **Application menu**: File/Edit/View/Window menus with keyboard shortcuts

### Stubbed / Not Wired
- **Non-Codex adapters** (Aider, Claude Code, Gemini, Copilot, Amazon Q, Open Interpreter): Each has a class with basic PTY lifecycle (`launch`, `sendPrompt`, `stopSession`) but no output parsing. Exported but never instantiated by `getAdapter()` — registry only accepts `'codex'`.
- **DiscussionSession orchestrator** (`src/main/discussion-session.ts`): Full moderation logic (round-robin, context-aware, synthesis) but not connected to the UI. `DiscussionPanel.tsx` uses `window.codexApi` directly instead of creating a `DiscussionSession` instance.
- **`parseOutput` method**: Expected by all 14 failing tests on every adapter class, but does not exist in any adapter. The `AgentAdapter` interface does not declare it.

### Experimental
- **Mobile companion bridge**: Functional but behind a toggle; pairing via QR code
- **Managed agent installation**: Auto-installs Codex/OI if missing; works on Linux/macOS
- **Discussion system**: UI is complete, backend orchestrator exists but is disconnected

### Dead Code
- **`src/main.ts` crash handlers** (lines ~30-33): Empty `uncaughtException` and `unhandledRejection` handlers that silently swallow errors
- **`CodexAdapter.sessions` static Map**: Session storage lives in both `CodexAdapter` (static) and `main.ts` (instance-level `sessions` Map) — potential duplication
- **`applySecretsToEnvironment`** in `codex-adapter.ts` (line ~340): Commented as "Simplified — full logic in main.ts... This is a placeholder for Phase 2"

---

## 7. Failing Tests — Root Cause Analysis

All 14 failing tests are in `tests/adapters.test.mts` and share the same root cause: **the tests call `adapter.parseOutput(state, chunks)` on adapter instances, but no adapter class implements a `parseOutput` method.**

### Root Cause
The test file imports each adapter class directly and calls `adapter.parseOutput(state, chunk)` expecting it to parse structured output into `AgentEvent`s and `AgentApproval`s. None of the adapter classes have this method — not even `CodexAdapter`, which handles parsing internally via `consumeExecEvents()` (a private method that processes JSONL from the PTY stream, not a public `parseOutput` API).

The `AgentAdapter` interface (`src/main/agent-adapter.ts`) does not declare a `parseOutput` method. The tests appear to have been written for a planned public parsing API that was never implemented.

### Grouped by Adapter

| Adapter | Failing Tests | Expected Input Format |
|---|---|---|
| OpenInterpreterAdapter | #12 (message chunks), #13 (confirmation/approval), #14 (image chunks) | JSON-type parsing (`{"type":"message",...}`, `{"type":"confirmation",...}`, `{"type":"image",...}`) |
| AiderAdapter | #16 (code blocks), #17 (diff blocks), #18 (confirmation prompts) | Markdown/code-block parsing from Aider's TUI output |
| ClaudeCodeAdapter | #20 (tool call blocks), #21 (terminal tool calls) | Tool-use block parsing |
| GeminiAdapter | #23 (code blocks), #24 (confirmation prompts) | Code/confirmation parsing |
| CopilotAdapter | #26 (code blocks), #27 (confirmation prompts) | Code/confirmation parsing |
| AmazonQAdapter | #29 (code blocks), #30 (confirmation prompts) | Code/confirmation parsing |

---

## 8. Architectural Risks and Inconsistencies

### 1. Session Storage Duplication
`CodexAdapter.sessions` is a static `Map<string, SessionState>` (`codex-adapter.ts:93`), while `main.ts` also maintains its own `sessions: Map<string, SessionState>` (line ~85). The adapter's static map is used by the adapter's methods but the main process has its own parallel copy — risk of state divergence.

### 2. `parseOutput` Gap
14 tests expect a public `parseOutput(state, input)` method on every adapter class. The `AgentAdapter` interface does not declare it. The only adapter in use (`CodexAdapter`) handles parsing internally via private `consumeExecEvents()`. This is either a test bug or an unimplemented feature.

### 3. Discussion System Disconnect
`DiscussionPanel.tsx` (the UI) does not use `DiscussionSession` (the orchestrator). Instead, it calls `window.codexApi` methods directly. The `DiscussionSession` class has full moderation logic but is never instantiated from the renderer.

### 4. Adapter Registry Incomplete
`index.ts` line 108: `const KNOWN_AGENTS = ['codex'] as const;` — only Codex is registered. The `getAdapter()` factory throws on any other agent ID. The other 6 adapter classes are exported but unreachable through the normal app flow.

### 5. Silent Error Swallowing
`main.ts` lines ~30-33: empty handlers for `uncaughtException` and `unhandledRejection`. Crashes are logged to console but not surfaced to the user or persisted for debugging.

### 6. `applySecretsToEnvironment` is a Placeholder
`codex-adapter.ts` line ~340: explicitly marked as "Simplified — full logic in main.ts... This is a placeholder for Phase 2." The adapter's session environment setup may not include all encrypted secrets.

### 7. Provider Type Duplication
`Provider` is defined as a type alias in both `main.ts` (line ~80) and `codex-adapter.ts` (line ~53). Identical but duplicated, risking drift.

### 8. Inline Styles in App.tsx
~200+ inline `style={{...}}` objects throughout `App.tsx` (lines 570–1100). Makes the component hard to maintain and inconsistent with the CSS-driven approach used elsewhere.

### 9. `/tmp` Fallback for userDataPath
`codex-adapter.ts` line ~32: `getUserDataPath()` returns `/tmp` when no getter is set. This is a test injection point but could cause issues if reached in production.

### 10. Mobile Bridge Token Format Hardcoded
`mobile-pairing-config.ts` line ~25: expects exactly 64 hex characters (`/^[a-f0-9]{64}$/`). SHA-256 hex string format — reasonable but inflexible if token generation strategy changes.
