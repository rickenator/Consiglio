<div align="center">
  <img src="build/icons/128x128.png" width="96" height="96" alt="Consiglio icon">
  <h1>Consiglio</h1>
  <p><strong>The desktop workspace for Hermes Agent tasks that outlive the terminal.</strong></p>
  <p>Start typing immediately. Consiglio keeps your tasks, files, models, drafts and Hermes threads ready after restarts and crashes.</p>
  <p>
    <img alt="License: Apache 2.0" src="https://img.shields.io/badge/license-Apache--2.0-5ca8ff?style=flat-square">
    <img alt="Electron" src="https://img.shields.io/badge/shell-Electron-9de4f2?style=flat-square">
    <img alt="React" src="https://img.shields.io/badge/UI-React-61dafb?style=flat-square">
    <img alt="Backend: Hermes Agent" src="https://img.shields.io/badge/backend-Hermes%20Agent-9b8cf0?style=flat-square">
    <img alt="Platforms: Linux, Windows, macOS, Android, iOS" src="https://img.shields.io/badge/platforms-Desktop%20%7C%20Android%20%7C%20iOS-f5c542?style=flat-square">
    <a href="https://github.com/rickenator/Consiglio/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/rickenator/Consiglio?style=flat-square&color=5ca8ff"></a>
  </p>
  <p>
    <a href="https://github.com/rickenator/Consiglio/releases/latest"><strong>Download Consiglio</strong></a>
    &nbsp;&middot;&nbsp;
    <a href="#start-here">Install from source</a>
    &nbsp;&middot;&nbsp;
    <a href="https://github.com/rickenator/Consiglio/issues">Report an issue</a>
  </p>
</div>

---

Consiglio is a desktop shell over [Hermes Agent](https://github.com/NousResearch/hermes-agent). It talks to Hermes over ACP (`hermes acp`), so sessions, transcripts, models, providers and credentials belong to Hermes — Consiglio adds the durable desktop layer around them: tabs, an in-app transcript you can edit and copy from, approval prompts, a Hermes settings configurator, and update handling.

> Consiglio is an independent open-source desktop client. It requires a working Hermes Agent installation. The Codex CLI remains selectable as an alternative backend.

## Why Consiglio

Work with an agent rarely fits into one terminal session. Consiglio adds:

- **Tabs for parallel tasks.** Every open task keeps its transcript mounted, so switching never rebuilds a conversation or loses a draft.
- **Hermes-owned sessions.** Task ids are Hermes session ids, so a task resumes in the same conversation after a restart.
- **No mystery waiting.** Streaming activity, tool calls, errors and working state stay visible.
- **Approvals you can see.** Hermes' permission requests surface in one banner with allow/deny.
- **Models from your config.** The model picker reads Hermes' own model catalogue; nothing has to be typed into Consiglio.
- **Settings you can change safely.** The configurator writes Hermes settings through `hermes config set`, never by editing `config.yaml`.
- **Updates on your terms.** Consiglio checks for Hermes releases at startup and offers to update, with a switch for automatic updates.

## What It Does

| Surface | Behavior |
| --- | --- |
| **Conversation** | Streams prompts, responses, tool calls, errors and a clear working indicator. |
| **Task rail** | Lists stored tasks with their backend and model; selecting one opens its tab. |
| **Tabs** | `Ctrl+W` closes a tab, `Ctrl+Tab` cycles, `Ctrl+1..9` jumps; drafts survive switching. |
| **Recovery** | Hermes persists sessions in `~/.hermes/state.db`; Consiglio resumes them by id. |
| **Files** | Browses and previews the active workspace inside the app, or opens it in your file manager. |
| **Models** | `Agent → Model` lists every model Hermes reports; choosing one calls `session/set_model`. |
| **Approvals** | Hermes permission requests appear with the command and the session they came from. |
| **Hermes settings** | Reads and writes whitelisted Hermes settings with a real before/after. |
| **Hermes updates** | Startup check via `hermes update --check`, an update button and an auto-update switch. |
| **Mobile companion** | Monitors tasks, sends prompts, reconnects or stops work, and handles approvals from Android or iOS. |

## Start Here

### Requirements

- [Hermes Agent](https://github.com/NousResearch/hermes-agent) installed and configured (`hermes acp --check` must succeed)
- A supported 64-bit desktop platform:
  - Debian/Ubuntu-compatible Linux x64 or another modern x64 distribution using the AppImage
  - Windows 10 or 11 x64
  - macOS 12 Monterey or newer on Intel or Apple Silicon
- Optional: the Codex CLI, if you want to run the Codex backend

Node.js 22 or newer and npm are required only when installing from source.

### Install From Source

```bash
git clone https://github.com/rickenator/Consiglio.git
cd Consiglio
npm install
npm run dev:all
```

Linux and macOS users can optionally install the convenience launcher:

```bash
mkdir -p ~/bin
ln -sf "$PWD/bin/consiglio" ~/bin/consiglio
```

## Everyday Workflow

1. Launch `consiglio`.
2. Consiglio checks Hermes, reopens your tabs and resumes the active task.
3. Type a request and press Enter.
4. Watch tool calls and responses as they arrive.
5. Open another task with `+ New task`; both transcripts stay live.
6. To change something: `Hermes → Hermes settings…` for model, context window and compaction.

`Enter` sends, `Shift+Enter` inserts a newline. The composer is a real text editor: caret placement, selection, cut/copy/paste and undo behave normally, every transcript block has its own Copy button, and `View → Copy whole transcript` copies the conversation.

## Backends

### Hermes Agent (default)

Consiglio spawns `hermes acp` and speaks JSON-RPC with it:

- `session/new` creates a task; `session/resume` / `session/load` reattach to a stored one.
- Models, providers and credentials come from your Hermes configuration (`~/.hermes/config.yaml`, `~/.hermes/.env`).
- Permission requests map to the approval banner (`allow once`, `deny`); no answer within two minutes is denied, as ACP requires.
- The ACP connection log is written to Consiglio's user-data directory; `Hermes → Copy Hermes log path` puts it on your clipboard.

### Codex CLI (optional)

`Agent → Backend → Codex CLI` runs tasks through the Codex CLI instead, using its own profile and MCP servers. When you first select it, Consiglio can install the CLI if it is missing.

## Hermes Settings Configurator

`Hermes → Hermes settings…` reads and writes these keys through `hermes config`, showing the value read back from Hermes after every write:

| Key | Meaning |
| --- | --- |
| `model.default`, `model.provider`, `model.base_url` | Which model Hermes runs and where it is served |
| `model.context_length` | Token window Hermes assumes (restart tasks to apply) |
| `compression.threshold`, `compression.threshold_tokens`, `compression.enabled` | When Hermes compacts long conversations (restart tasks to apply) |
| `auxiliary.compression.model`, `auxiliary.compression.provider` | Model used to summarise during compaction |
| `agent.max_turns` | Upper bound on tool-calling turns per request |
| `display.skin`, `display.interface` | Active Hermes skin and default surface |

Everything else is refused, and every key that looks like a credential is rejected outright — secrets belong in `~/.hermes/.env`. Values containing control characters are rejected before any process starts, so a value cannot inject YAML into `config.yaml`.

The agent can change the same keys itself with its terminal tool; Consiglio reports the change when it happens.

## Hermes Updates

Consiglio runs `hermes update --check` at startup. If a newer release exists it shows the installed and available versions and offers **Update now** plus an **auto-update on startup** switch.

Auto-update is idle-only by design: `hermes update` restarts the Hermes gateway and would kill a task that is mid-response, so Consiglio defers while any task is answering and says why.

## API Keys And MCP

Open `File → API keys and MCP secrets…` to add credentials for the Codex backend. Consiglio stores only encrypted values on disk, restricts the secrets file to the current OS user, and never sends saved values back to the renderer.

Credentials for Hermes itself live in `~/.hermes/.env` and are managed with `hermes setup`; Consiglio does not read or write them.

### STDIO MCP Server

```toml
[mcp_servers.example]
command = "example-mcp"
env_vars = ["EXAMPLE_API_KEY"]
```

### Streamable HTTP MCP Server

```toml
[mcp_servers.example]
url = "https://example.com/mcp"
bearer_token_env_var = "EXAMPLE_API_KEY"
```

The **MCP config** button beside a credential copies both patterns with the correct variable name.

## Keyboard And Desktop Behavior

| Action | Shortcut or behavior |
| --- | --- |
| New task | `Ctrl/Cmd+N` |
| Close tab | `Ctrl/Cmd+W` |
| Next / previous tab | `Ctrl+Tab` / `Ctrl+Shift+Tab` |
| Jump to tab | `Ctrl+1` … `Ctrl+9` |
| Send prompt | `Enter` |
| Newline in prompt | `Shift+Enter` |
| Restore tasks | Automatic at startup (tabs and the active task) |
| Window size | Remembered between launches |
| Copy and paste | Native menu and standard platform shortcuts |

## Mobile Companion

The native Capacitor client lives in [`mobile/`](mobile/) and connects to a deliberately narrow bridge hosted by the desktop app. It can view tasks and timelines, send prompts, reconnect or stop work, and approve or reject pending requests. Successful pairings survive restarts through Android Keystore or iOS Keychain. See [Mobile Companion](docs/MOBILE.md).

## Architecture

```text
Consiglio
├── bin/consiglio                   durable launcher
├── src/main.ts                     Electron process, session records, IPC surface
├── src/main/hermes/                acp-codec, acp-client, acp-events,
│                                   hermes-acp-adapter, hermes-config, hermes-update
├── src/main/adapters/              adapter registry (Hermes first, Codex optional)
├── src/main/app-protocol.ts        bounded renderer protocol and path validation
├── src/preload.ts                  narrow contextBridge API
├── src/App.tsx                     desktop shell: menus, tabs, banners
├── src/features/sessions/          task rail, tab strip, conversation timeline
├── src/features/hermes/            settings configurator and update dialog
├── src/features/files/             workspace browser and previews
└── tests/                          ACP, config, update and protocol unit tests
```

The renderer never launches commands or reads credentials directly. Electron's main process owns the ACP connection to Hermes, filesystem access, persistence and encryption; the preload bridge exposes a bounded IPC surface to React.

Packaged builds serve renderer assets through Consiglio's private secure protocol rather than privileged `file://` pages.

## Development

```bash
npm run dev:all     # run the complete desktop app
npm run verify      # typecheck + unit tests + build all three targets
```

Run a single test file:

```bash
node --test tests/acp-client.test.mts
```

## Troubleshooting

### Hermes is not detected

```bash
hermes --version
hermes acp --check     # must print "Hermes ACP check OK"
```

Set `HERMES_BIN` if Hermes lives outside the standard install locations.

### A task will not resume

Hermes stores sessions in `~/.hermes/state.db`. If a record has no Hermes session behind it (for example a task created before this version), Consiglio starts a fresh Hermes task and says so in the transcript. Inspect Hermes' own store with `hermes sessions list`.

### A settings change did not take effect

Keys marked **restart sessions** in the configurator affect tasks that are already running. Stop and resume the task, or change the setting before starting one.

## Design Rule

Consiglio must not become a toy abstraction over Hermes. Every visual action should map to an inspectable event, command, file operation, setting, or protocol message. Errors remain selectable and copyable so failures can be debugged from evidence rather than hidden behind generic UI states.

## License

Copyright 2026 Rick Goldberg, Aniviza LLC Productions.

Licensed under the [Apache License 2.0](LICENSE). See [NOTICE](NOTICE) for attribution information.
