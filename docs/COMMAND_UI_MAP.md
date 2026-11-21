# Hermes command → Consiglio UI map

Consiglio should wrap Hermes in UI, not echo `/command` lines into the chat
console. This document inventories every command known from Hermes **0.23.3**
(ACP `available_commands`, the `/commands` catalog, and the agent’s
`COMMAND_REGISTRY`) and records how Consiglio should present it, where the
state is read and written, and in which pass the UI lands.

The inventory is a snapshot of 0.23.3. Consiglio still **does not pin** a
Hermes path or version. Commands that appear after a `hermes update`, or that
this map does not give a native surface, open the **Command console** (the
auxiliary interface) and only go to chat when the user presses Send.

## Principles

- Prefer a structured write path over sending chat text: ACP methods first
  (`session/set_config_option`, `session/set_mode`, `session/new`, usage and
  config updates already on the session), then `hermes config get/set`, then a
  **quiet** slash command whose output is shown in the dialog (never as a user
  bubble).
- Passthrough (Send to chat) is a last resort, and only from the Command
  console so the user sees what will be sent.
- Pickers preview on select and **apply only on Save**. Cancel discards.
- Do not single out any one command in menus or the toolbar; `/model` (Rick’s
  `/models` example) is the reference layout, not a special menu of its own.
- Tolerate protocol drift: read model / reasoning / approvals / speed from
  whatever `configOptions` the session actually sent; fall back to quiet
  `/command` when an option is missing.
- Never break `hermes update`. Resolve the `hermes` executable on every call.

## Flow archetypes

| Archetype | Layout | Buttons | Apply |
| --- | --- | --- | --- |
| **Picker** | Info panel on top (current / preview). Selectable list below (radio / exclusive). Optional filter and “also save as default” where the command supports it. | Save, Cancel | Only on Save |
| **Settings form** | Labeled fields, validation, optional preview. | Save, Cancel | Only on Save |
| **Info** | Read-only body (monospace where the data is a report). Optional Refresh. | Close | None |
| **Confirm** | Short explanation of the effect; destructive actions are clearly worded. | Confirm, Cancel | On Confirm |
| **Text prompt** | One text field (and optional extras), syntax hint. | Save, Cancel | Only on Save |
| **Live panel** | Stays open and updates (memory review, queue, agents). | Close / action buttons | Per control |
| **App link** | No new dialog: reuse an existing Consiglio surface (settings, palette, session rail, Stop, New session). | — | Existing action |
| **Passthrough** | Command console: the `/` line, optional argument field and choice chips, “Will send” preview. | Send, Cancel | Send writes the line to chat |

## Data sources and apply channels

| Channel | Used for | Notes |
| --- | --- | --- |
| ACP `configOptions` + `session/set_config_option` | Model, reasoning effort, service tier (fast), file-edit policy | Session-scoped. Choices come from the agent so a newer Hermes can add rows without a Consiglio change. |
| ACP `session/set_mode` | Legacy edit modes, only when no `mode` config option is present | Already wired in the client. |
| ACP `session/new` / `resume` / `list` | `/new`, `/reset`, `/sessions` | Do not send `/new` as a prompt. |
| ACP usage / session info updates | Context gauge, current model label | Used to seed info views; quiet `/status` etc. still fills the report body. |
| `hermes config get/set` | Persistent profile keys (`approvals.mode`, default model) when no ACP option exists | Executable re-resolved every call. |
| Quiet slash command (`runQuietCommand`) | Info reports; applies with no ACP method (`/title`, `/undo`, `/personality`, …) | Output stays in the dialog (or refreshes the chat from the agent’s session updates). No user echo. |
| Command console → chat | Unknown, unmapped, or inherently free-form commands; anything a future Hermes adds | User must press Send. |

**Important distinction:** ACP’s `edit_approval_policy` (“Edits” in the
session toolbar: ask / accept workspace edits / don’t ask) is **file-edit**
policy. `/approvals manual|smart|off` is the **dangerous-command** approval
mode (`approvals.mode` in config). `/yolo` toggles skipping dangerous-command
approvals for the current session. Those are three related but different
controls; the UI must not fold them into one list.

## Discovery

Commands are still discovered at runtime from ACP `available_commands_update`
plus a quiet `/commands` pagination — never from a hard-coded “this is every
command” list in the client. This map only decides **how** a discovered (or
typed) name is presented. An unknown name always gets the Command console.

ACP 0.23.3 itself advertises a shorter working set than the CLI registry:

`help`, `model`, `tools`, `context`, `reset`, `compress`, `steer`, `queue`,
`usage`, `status`, `version`, `title`, `undo`, `retry`.

The full registry below is what `/commands` and the CLI expose. Gateway-only
and terminal-only commands are listed so we do not pretend they have a
Consiglio surface when they cannot run here.

Priority: **P1** = this pass (native dialog or app link). **P2** = next pass
(still a real UI, more state or a live panel). **P3** = later or never in the
GUI (CLI/TUI/gateway-only). **PT** = Command console only.

## Command inventory

Aliases share the canonical row (they open the same surface).

### Session

| Command | What it does | Archetype | Data / apply | Priority | Phase 1 dialog |
| --- | --- | --- | --- | --- | --- |
| `new` (`reset`) | Fresh session (new id + history). The current thread stays in the catalog. | Confirm | ACP `session/new` (existing New Session). Never echo `/new`. | P1 | “Start a new session?” Confirm/Cancel |
| `title` | Set or show the session title. | Text prompt | Seed from the session title; apply quiet `/title <name>`. | P1 | Field + Save/Cancel |
| `undo` | Drop the last *N* user turns and their replies (default 1) and re-prompt. | Settings form + confirm | Quiet `/undo [N]`. | P1 | Count field; Save applies |
| `retry` | Resend the last user message. | Confirm | Quiet `/retry`. | P1 | Confirm/Cancel |
| `compress` (`compact`) | Shrink conversation context. Optional `here [N]`, `focus <topic>`, `--preview`. | Settings form | Quiet `/compress …`. Preview checkbox runs `--preview` in the dialog first. | P1 | Options + Save/Cancel |
| `branch` (`fork`) | Branch this session (new thread on messaging; `--here` stays here). | Text prompt | Quiet `/branch [--here] [name]`. | P1 | Name + “stay here” + Save |
| `stop` | Stop the in-flight turn / background work. | App link | Existing Stop action. | P1 | No extra dialog |
| `sessions` | Browse and resume previous sessions. | App link | Existing session rail. | P1 | Focus the sidebar |
| `queue` (`q`) | Queue a follow-up prompt, or list/edit/remove queued items. | Text prompt (list is P2 live panel) | Quiet `/queue <prompt>` for the common case; other subcommands stay in the console. | P1 | Prompt field + Save |
| `steer` (`s`) | Inject guidance after the next tool call. | Text prompt | Quiet `/steer <prompt>`. | P1 | Guidance field + Save |
| `save` | Export the conversation as json/md/html. | Settings form | Quiet `/save <fmt> [file] [redact]`. | P1 | Format, filename, redact + Save |
| `plan` | Write a markdown plan under `.hermes/plans/` without executing it. | Text prompt | Quiet `/plan [task]`. | P1 | Task field + Save |
| `status` | Session, model, token, and context report. | Info | Quiet `/status`, seeded with ACP usage when present. | P1 | Read-only report |
| `context` (`ctx`) | Context-window breakdown. | Info | Quiet `/context` (`all` toggle). | P1 | Read-only report + optional All |
| `rollback` | List or restore filesystem checkpoints. | Picker (P2) | Quiet `/rollback` to list; apply quiet `/rollback <n>`. | P2 | Console in phase 1 |
| `resume` | Resume a named session. | App link / picker (P2) | Session rail covers the common case. | P2 | Console in phase 1 |
| `bg`, `btw`, `moa` | Side / background / mixture-of-agents prompt. | Text prompt | Quiet or send as a structured run once we have a result view. | P2 | Console in phase 1 |
| `agents` (`tasks`) | Active agents and tasks. | Live panel | Quiet `/agents`; refresh while open. | P2 | Console in phase 1 |
| `goal`, `subgoal` | Standing goal and extra criteria across turns. | Settings form + live status | Quiet `/goal …` / `/subgoal …`. Many subcommands. | P2 | Console in phase 1 (still in the catalog) |
| `heartbeat` (`hb`), `loop` (`proactive`) | Recurring prompts in this session. | Settings form | Quiet `/heartbeat …` / `/loop …`. | P2 | Console in phase 1 |
| `refine`, `review` | Memory extraction / independent review subagent. | Text prompt + confirm | Quiet `/refine` / `/review`. | P2 | Console in phase 1 |
| `history`, `prompt` (`compose`), `rewind`, `clear`, `redraw`, `worktree`, `snapshot` (`snap`) | CLI/TUI session tools. | — | Terminal-oriented; no ACP GUI equivalent. | P3 | Hidden or console if advertised |
| `start`, `topic`, `discard`, `pause`, `approve`, `deny`, `sethome`, `restart` | Gateway / messaging only. | — | Not meaningful in the desktop ACP client. | P3 | Console only if advertised |

### Configuration

| Command | What it does | Archetype | Data / apply | Priority | Phase 1 dialog |
| --- | --- | --- | --- | --- | --- |
| `model` (also accept `models`) | Show or switch provider/model. Session-scoped by default; `--global` persists; `--refresh` re-fetches inventories. | **Picker** (reference) | Read ACP config option `id=model` (category `model`) and `availableModels`. Info panel shows provider + model for the current / highlighted row. Save calls `session/set_config_option`. Optional “also save as default” follows with quiet `/model <id> --global`. If no config option is present, Save uses quiet `/model …` instead. | P1 | Info panel + exclusive list + Save/Cancel |
| `reasoning` | Reasoning effort and display (`level`, `show`/`hide`/`full`/`clamp`, `--global`). | Picker | ACP `reasoning_effort` (`thought_level`). Display subcommands (`show`/`hide`) via quiet `/reasoning …` if still needed. | P1 | Info panel + list + Save/Cancel |
| `fast` | Service tier: normal / fast / auto / cold / ultrafast (when the model supports it). | Picker | ACP `service_tier` when present; else quiet `/fast …`. | P1 | Info panel + list + Save/Cancel |
| `approvals` | Persistent dangerous-command approval mode: `manual`, `smart`, `off`. | Picker | Current value from `hermes config get approvals.mode` or quiet `/approvals`. Apply `hermes config set` or quiet `/approvals <mode>`. **Not** the toolbar Edits policy. | P1 | Info panel + list + Save/Cancel |
| `yolo` | Toggle skipping dangerous-command approvals for this session. | Confirm | Quiet `/yolo`. | P1 | Confirm the toggle |
| `personality` | Choose a predefined (or configured) personality, including `none`. | Picker | List from quiet `/personality` (and `hermes config` when needed). Apply quiet `/personality <name>`. | P1 | Info panel + list + optional custom name + Save/Cancel |
| `config` | Show or edit configuration. | App link | Existing Settings → Configuration. | P1 | Opens Settings |
| `update` | Update the Hermes agent. | App link | Existing “Check for Updates” flow. Must keep `hermes update` working. | P1 | Existing updater dialog |
| `login` | Sign in with a Nous account. | App link | Existing setup / Settings path. | P1 | Existing setup |
| `autoprune` | Automatic pruning of old sessions. | Picker | Quiet `/autoprune on\|off\|later\|status`. | P2 | Console in phase 1 |
| `codex-runtime` | Toggle Codex app-server runtime. | Picker | Quiet `/codex-runtime`. | P2 | Console in phase 1 |
| `busy` | Mid-run message policy (queue / steer / interrupt). | Picker | Quiet `/busy`. | P2 | Console in phase 1 |
| `voice`, `wake` | Voice mode / wake word. | Settings form | Needs device UI; not this pass. | P2/P3 | Console if advertised |
| `statusbar`, `battery`, `timestamps`, `verbose`, `focus`, `footer`, `skin`, `indicator` | TUI display toggles. | — | Terminal-only. | P3 | — |
| `export`, `import` | Profile archive import/export. | Settings form | CLI; file picker later. | P2 | Console if advertised |
| `handoff` | Hand the session to a messaging platform. | Picker | CLI-oriented. | P3 | Console if advertised |

### Info & account

| Command | What it does | Archetype | Data / apply | Priority | Phase 1 dialog |
| --- | --- | --- | --- | --- | --- |
| `usage` | Token usage and rate limits; `reset` redeems a banked Codex limit. | Info (+ confirm for reset) | Quiet `/usage`. Reset is a Confirm, not a silent extra button. | P1 | Read-only report |
| `version` (`v`) | Hermes Agent version. | Info | Quiet `/version`. Do not pin or display a Consiglio-bundled path as “the” install. | P1 | Read-only report |
| `profile` | Active profile name and home directory. | Info | Quiet `/profile`. | P1 | Read-only report |
| `whoami` | Slash-command access (admin / user). | Info | Quiet `/whoami`. | P1 | Read-only report |
| `help` | Browse available commands. | App link / Info | Opens the command palette (already the fuzzy catalog). `/help <filter>` can pre-fill the search. | P1 | Palette |
| `commands` | Paginated command list (gateway). | App link | Palette / catalog refresh. | P1 | Palette |
| `palette` | Fuzzy command palette. | App link | Existing Ctrl+K/P palette. | P1 | Palette |
| `debug` | Debug report (versions, config keys; optional upload). | Info + confirm | Quiet `/debug`; upload is Confirm. | P2 | Console in phase 1 |
| `insights` | Usage insights. | Info | Quiet `/insights [days]`. | P2 | Console in phase 1 |
| `subscription` (`upgrade`), `topup` | Nous plan / billing portal. | Info + link | Quiet `/subscription` / `/topup`, then open the portal URL if present. | P2 | Console in phase 1 |
| `diff` | Git changes in the workspace. | Info | Quiet `/diff …`. | P2 | Console in phase 1 |
| `platforms` (`gateway`), `platform`, `egress` | Gateway / Docker egress status. | Info | Meaningful mainly for gateway hosts. | P3 | Console if advertised |
| `copy`, `paste`, `image` | TUI clipboard / attach image. | — | Consiglio already has its own attach/copy path. | P3 | — |
| `quit` (`exit`) | Exit the CLI. | App link | App Quit, never `/quit` in chat. | P1 | App quit (if it appears in the catalog, route to Quit) |

### Tools, skills, memory, plugins

| Command | What it does | Archetype | Data / apply | Priority | Phase 1 dialog |
| --- | --- | --- | --- | --- | --- |
| `tools` | List, disable, or enable tools. | Info + picker (enable/disable is a second step) | Quiet `/tools` to list. Apply quiet `/tools enable\|disable <name…>` on Save. ACP advertises this even though the CLI marks it `cli_only`. | P1 | Report + optional selection + Save when changing |
| `skills` | Search, install, inspect, manage skills. | Live panel / picker | Large surface; ACP does not advertise the full family. | P2 | Console in phase 1 (plus any advertised skill commands stay in the catalog) |
| `memory` | Pending memory writes and the approval gate. | Live panel | Quiet `/memory pending` etc. | P2 | Console in phase 1 |
| `bundles`, `learn`, `init` | Skill bundles, learn-a-skill, generate `AGENTS.md`. | Text prompt / form | Quiet commands. | P2 | Console in phase 1 |
| `cron`, `suggestions` (`suggest`), `blueprint` (`bp`) | Scheduled tasks and automations. | Live panel / form | CLI-heavy. | P2 | Console in phase 1 |
| `curator`, `kanban`, `plugins`, `reload`, `reload-mcp`, `reload-skills`, `browser`, `toolsets` | Maintenance and extras. | Confirm or info | Quiet command or console. | P2/P3 | Console in phase 1 |
| `pet`, `hatch` | Petdex mascot (CLI fun). | — | Out of scope for a polished desktop wrap. | P3 | Console if advertised |

## Phase 1 surfaces (this pass)

Native dialogs or existing-app links, aimed at the highest-value everyday
commands:

1. `model` / `models` — reference picker
2. `reasoning`
3. `fast`
4. `approvals`
5. `yolo`
6. `personality`
7. `title`
8. `new` / `reset`
9. `undo`
10. `retry`
11. `compress` / `compact`
12. `branch` / `fork`
13. `status`
14. `usage`
15. `context` / `ctx`
16. `version` / `v`
17. `profile`
18. `whoami`
19. `tools`
20. `save`
21. `plan`
22. `queue` / `q`
23. `steer` / `s`
24. `help`, `commands`, `palette` — command palette
25. `sessions` — session rail
26. `stop` — Stop
27. `config` — Settings → Configuration
28. `update` — existing updater
29. `login` — existing setup
30. `quit` — app quit (if advertised)

Everything else, including any command Hermes adds after an update, uses the
**Command console**. The palette marks each Hermes command as **Dialog**
(native or app link) or **Chat** (console / passthrough).

## Phase 2+ plan (remaining)

1. **Live session controls:** `goal` / `subgoal` status + editor, `heartbeat`
   and `loop` schedules, `queue` as a reorderable list, `agents`.
2. **Memory and skills:** pending memory approval queue, skills browse /
   install (without widening CLI-only verbs such as unattended install if the
   desktop surface restricts them), `learn` / `init` / `bundles`.
3. **Checkpoints and recovery:** `rollback` picker, `snapshot` if we can do
   it safely from the GUI, `resume` by name.
4. **Reports:** `debug`, `insights`, `diff`, `/usage reset` confirm, billing
   / subscription pages with a clearly-labeled link out.
5. **Automations:** `cron`, `suggestions`, `blueprint`.
6. **Keep as console or hide:** gateway-only, TUI chrome, petdex, and any
   command whose only honest interface is a terminal wizard.

Each of those should still prefer ACP or `hermes config` over chat text, and
should reuse the same dialog shell (info panel, list, Save/Cancel, error
banner, keyboard, light/dark) introduced in phase 1.
