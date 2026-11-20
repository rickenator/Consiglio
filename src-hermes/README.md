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

`build/` is ignored (`src-hermes/.gitignore`). `test_acpclient` drives a scripted ACP agent
(`tests/fake_hermes_acp.py`, needs `python3`) through request-vs-response handling, the
permission round-trip, error recovery, protocol drift and the update-and-restart path. The
live test against a real `hermes` runs only when one is on `PATH`.

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
