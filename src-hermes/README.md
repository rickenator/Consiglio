# Consiglio · Hermes (`src-hermes`)

A native Qt 5 desktop client for [Hermes Agent](https://github.com/NousResearch/hermes-agent),
driven over the Agent Client Protocol (`hermes acp`: newline-delimited JSON-RPC on stdio).
It streams answers, reasoning, tool calls, context usage and slash commands, lists and
resumes saved Hermes sessions, and shows Hermes's permission prompts as approval dialogs.

This is Consiglio's primary client. The Electron app in `src/` and the earlier Qt client in
`src-qt/` are optional, legacy backends; see the top-level [README](../README.md#optional-and-legacy-backends).

## Download

Prebuilt packages are on the [releases page](https://github.com/rickenator/Consiglio/releases/latest)
as `Consiglio-Hermes-<version>-*`: `.deb` and `.AppImage` for Linux x64, `.dmg` for macOS
(Apple Silicon and Intel) and a portable `.zip` for Windows x64. See the top-level
[README](../README.md#1-download-consiglio--hermes) for which to pick.

## First-run setup (an existing Hermes is preserved)

`HermesSetupDialog` runs before the main window on first launch (and from
**Settings → Hermes Agent → Detect Hermes again…**). `HermesSetup::detect()` is read-only: it
checks `$HERMES_BIN`, `PATH` and the standard folders, runs `hermes --version`, and looks at the
data folder (`$HERMES_HOME`, `%LOCALAPPDATA%\hermes`, `~/.hermes`) for `config.yaml`, `.env`
(presence only; never read), sessions, skills and the `hermes-agent` checkout.

| Found | Default | Other choices |
| --- | --- | --- |
| an executable | **Use this Hermes as it is**: nothing stored, pinned or changed | a different executable; a separate copy |
| data but no executable | a different executable | a separate copy; the official installer *with* the existing data, only after ticking a consent box |
| nothing | open the official installer in a terminal | a different executable; **Skip for now** |

A separate copy (Linux and macOS) runs the official `install.sh` with `HOME` and
`HERMES_HOME` pointed into its own folder, so it can't touch `~/.hermes`, `~/.local/bin/hermes`
or shell profiles; the client then launches it with that `HERMES_HOME`. Windows doesn't offer
it because `install.ps1` always edits the user `PATH`. Choices are stored in `QSettings`
(`hermes/setupChoice`, `hermes/separateRoot`, and `hermes/executable` only when you pick a path).
The packages themselves (the `.deb` has no maintainer scripts) never install or change Hermes.

`setuptour` renders the dialog offscreen:
`QT_QPA_PLATFORM=offscreen ./setuptour --out DIR [--theme light|dark] [--real]`
(`--real` adds the two screens for the Hermes really installed on this machine, detected read-only).

## Testing safely

`ConsiglioHermes --sandbox` (or `CONSIGLIO_SANDBOX=1`, or `--sandbox=DIR`) runs the client
against a throwaway folder for both Consiglio settings and `HERMES_HOME`. It does not read
or write `~/.hermes`. A **Sandbox** badge is shown in the status bar. `HERMES_BIN` /
`HERMES_HOME` and `--hermes` also work for a lighter override. See the top-level
[README](../README.md#testing-safely).

The session sidebar lists **current** Hermes sessions from `$HERMES_HOME/state.db` (and
from `session/list`), newest first, read-only. It is not a separate Consiglio transcript
store.

## Prerequisites (building from source)

- **Hermes Agent**, installed with its official installer
  (`curl -fsSL https://hermes-agent.nousresearch.com/install.sh | bash` on Linux, macOS and WSL2;
  `iex (irm https://hermes-agent.nousresearch.com/install.ps1)` on Windows). Then configure a
  provider with `hermes setup` or `hermes model`, and check it with `hermes doctor`.
- **CMake 3.16+, a C++17 compiler and Qt 5** (Core, Gui, Widgets, Svg, Test). On Debian/Ubuntu: `sudo apt install build-essential cmake qtbase5-dev libqt5svg5-dev`.
  On macOS: `brew install cmake qt@5`, then pass `-DCMAKE_PREFIX_PATH="$(brew --prefix qt@5)"`
  to CMake. On Windows: Qt 5.15 for MSVC and Visual Studio 2019+. CI builds and tests it on
  Linux, Windows, macOS Intel and Apple Silicon.
- `python3` only for the test suites, which use a scripted fake agent.

Run it with `src-hermes/build/ConsiglioHermes [--hermes /path/to/hermes]`.

## Build and test

```sh
cmake -S src-hermes -B src-hermes/build
cmake --build src-hermes/build -j
cd src-hermes/build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

`build/` is ignored (`src-hermes/.gitignore`). Six suites run under `ctest` (on Windows,
`ctest -LE posix` skips the two that drive the fake agent through a POSIX shell wrapper):

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
- `test_hermessetup` builds a fake `HOME` and checks detection order (`HERMES_BIN`, `PATH`,
  standard folders), that detection is read-only, that an existing Hermes is offered first and
  **Use as it is** leaves every file, `HERMES_HOME` and the stored path untouched, that the
  installer only runs when chosen, the consent box for existing data, the separate copy's
  isolation, and that Windows doesn't offer one.
- `test_sessioncatalog` parses `session/list` under camelCase, snake_case and Unix
  timestamps, sorts newest-first, follows a paged list in the ACP suite, and checks that
  a read-only look at `state.db` adds current Hermes sessions without writing the file.
- `test_appsandbox` checks `--sandbox` / `CONSIGLIO_SANDBOX` parsing and that activation
  never touches a planted real `~/.hermes`.

`packaging/package.sh <linux|mac|win> <arch> <out>` builds the release packages
(CPack `.deb`, linuxdeploy AppImage, macdeployqt `.dmg`, windeployqt `.zip`).

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
  the active session. **Hermes → Restart Agent** (Ctrl+Shift+R) does the same on demand, which is how
  you pick up an update applied while the agent was idle.
