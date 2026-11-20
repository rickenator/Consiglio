# Consiglio · Hermes (`src-hermes`)

A native Qt 5 desktop client for [Hermes Agent](https://github.com/NousResearch/hermes-agent),
driven over the Agent Client Protocol (`hermes acp`: newline-delimited JSON-RPC on stdio).
It streams answers, reasoning, tool calls, context usage and slash commands, lists and
resumes saved Hermes sessions, and shows Hermes's permission prompts as approval dialogs.

## Build and test

```sh
cmake -S src-hermes -B src-hermes/build
cmake --build src-hermes/build -j
cd src-hermes/build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

`build/` is ignored (`src-hermes/.gitignore`). Three suites run under `ctest`:

- `test_acpclient` drives a scripted ACP agent
(`tests/fake_hermes_acp.py`, needs `python3`) through request-vs-response handling, the
permission round-trip, error recovery, protocol drift and the update-and-restart path. The
live test against a real `hermes` runs only when one is on `PATH`.
- `test_commandcatalog` parses real `/commands` pages captured from Hermes v0.23.3
  (`tests/data/`), and covers merging, removal after an update, the per-version cache,
  `hermes config show` and secret masking.
- `test_mainwindow` runs the window offscreen against the fake agent. It checks that every
  discovered command is in the menus and palette, that a command added by an update
  appears, that every control has a tooltip and accessible name, that `/goal` is handled
  like any other command, the config pickers, Stop and the disconnected state.

`uitour` is a screenshot tour, not a test:
`QT_QPA_PLATFORM=offscreen ./uitour --out DIR [--theme light|dark] [--real]`. Without
`--real` it uses the fake agent. With `--real` it uses the `hermes` on `PATH`, which
needs a configured model provider.

## The interface

```
 menu bar: File · Session · Hermes · View · Help
 toolbar:  sidebar · new · model ▾ · reasoning ▾ · approvals ▾ ……………  Commands  ⚙
 left rail: workspace, New Session, current and recent sessions (filterable)
 centre:   banner · conversation · empty state · composer ("/" completes commands)
 status:   ● state · Hermes version … model · context bar · turn timer · tokens
```

- **Every Hermes command is reachable in three ways:** from the menus, from the
  **command palette** (Ctrl+K, fuzzy search over names, aliases and descriptions) and
  from the composer (type `/`).
  - The **Hermes** menu has one submenu per group: Model & Behavior; Tools, Skills &
    Memory; Planning & Automation (`/goal`, `/loop`, `/heartbeat`, `/plan`…); Info &
    Diagnostics; Gateway & Account; Skills; More.
  - Session commands (`/undo`, `/retry`, `/compress`, `/branch`…) are also listed
    in the **Session** menu.
  - No command gets special treatment. `/goal` is handled the same way as the rest.
  - A command with fixed choices (`[on|off|status]`) gets a submenu with those choices.
  - A command that takes text opens a small form that shows the exact command line
    before it runs.
- **Session settings** that Hermes reports as ACP `configOptions` (model, reasoning
  effort, approval policy, plus anything a future Hermes adds) become toolbar pickers.
  They are also listed under Settings → Session. When the window is narrow, the
  secondary pickers step out of the toolbar first.
- **Setup and configuration** are under Settings → Hermes Agent and Settings →
  Configuration:
  - executable resolution;
  - health and updates (`hermes doctor`, `hermes status`, `hermes update --check`, `hermes update`);
  - provider setup (`hermes setup`, `hermes model`, run in a terminal);
  - updates;
  - `hermes config show` rendered as sections, and `hermes config set`.
  API keys are never displayed.
- Light, dark and system themes, Ctrl+= / Ctrl+- zoom, and a shortcut on every common
  action. Every control has a tooltip and an accessible name.

### Commands come from Hermes, not from this repo

The command list is discovered from the running agent every time:

1. The ACP `available_commands_update` list.
2. Hermes's own paged registry. `/commands 1…N` is run quietly in the background and
   never shown in the chat; Hermes answers registry commands without a model turn.

The result is cached per Hermes version, so the menus are populated at the next launch
before discovery finishes. A version change, or **Hermes → Refresh Command List**,
re-runs discovery. After a `hermes update`, new commands appear and removed ones
disappear. Only presentation is static: group, icon and friendly label for known
names. An unknown command still appears, under **More**, with a generic icon and a
label derived from its name.

## Hermes updates itself — how the client copes

`hermes update` swaps the install (a git checkout under `~/.hermes/hermes-agent`) behind a
stable launcher on `PATH` (usually `~/.local/bin/hermes`), re-exposes that launcher, and
restarts the gateways it owns. A `hermes acp` process that was already running keeps the old
code until it exits, and work interrupted by a restart is not replayed. Saved sessions live in
`~/.hermes/state.db` and survive restarts. The client is built around that:

- **No pinned path or version.** The executable is resolved on every launch: an explicit
  path, else `$HERMES_BIN`, else `PATH`, else the standard install locations
  (`~/.local/bin`, `~/.hermes/hermes-agent/venv/bin`, `/usr/local/bin`, `/opt/homebrew/bin`,
  `~/bin`). Nothing about a particular version is stored.
- **Negotiated, not assumed.** `initialize` offers ACP v1, `clientInfo` and no client-side
  `fs`/`terminal` methods. The client records whatever `protocolVersion`, `agentInfo` and
  `agentCapabilities` the agent returns. It lists sessions only when
  `sessionCapabilities.list` is advertised, and it resumes with `session/resume` when
  advertised, falling back to `session/load`. Agents that predate `sessionCapabilities` get
  the legacy calls, and a "method not found" reply is treated as "no sessions".
- **Drift-tolerant.** Unknown notifications, `sessionUpdate` kinds, fields and capabilities
  are ignored. Agent→client requests are told apart from responses by their `method`, never
  by the id. Unsupported agent requests get a JSON-RPC `-32601` reply instead of silence.
  Permission options are shown exactly as the agent sends them, and their ids are echoed back
  verbatim.
- **Restart after exit.** If the agent exits on its own (for example during an update), an
  in-flight turn is reported as failed and not replayed. The client then relaunches with
  backoff (re-resolving the executable), renegotiates, reports a version change, and re-opens
  the active session. **File → Restart Hermes Agent** does the same on demand, which is how
  you pick up an update applied while the agent was idle.
