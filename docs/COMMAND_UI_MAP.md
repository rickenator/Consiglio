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

## Command hosts

Each native command has its own `CommandHost` class. The dialog shell
(`CommandHostDialog`) owns the shared chrome (header, description, error banner,
busy row, Save / Cancel, apply lifecycle). Adding a surface means a host class
plus one factory row in `createCommandHost()` — not another branch in a shared
switch. Live-panel commands share `LivePanelHost` only for widgets and
apply-on-Save machinery; goal, queue, rollback, and the rest each have their
own host. Unknown or future names still open the Command console.

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
| `queue` (`q`) | Queue follow-up prompts; edit, reorder, or clear the list. | **Live panel** | Seed quiet `/queue list`. Local add/remove/move until Save. Save applies append-only `/queue add` when possible, otherwise `/queue clear` then re-add. Keep open and refresh. | P2 | Editable list + Add/Remove/Move + Save/Cancel |
| `steer` (`s`) | Inject guidance after the next tool call. | Text prompt | Quiet `/steer <prompt>`. | P1 | Guidance field + Save |
| `save` | Export the conversation as json/md/html. | Settings form | Quiet `/save <fmt> [file] [redact]`. | P1 | Format, filename, redact + Save |
| `plan` | Write a markdown plan under `.hermes/plans/` without executing it. | Text prompt | Quiet `/plan [task]`. | P1 | Task field + Save |
| `status` | Session, model, token, and context report. | Info | Quiet `/status`, seeded with ACP usage when present. | P1 | Read-only report |
| `context` (`ctx`) | Context-window breakdown. | Info | Quiet `/context` (`all` toggle). | P1 | Read-only report + optional All |
| `rollback` | List or restore filesystem checkpoints. | **Live panel** | Seed quiet `/rollback`. Restore is Confirm (`/rollback <n> [--all]`) and closes. Diff selected runs `/rollback --diff N` and stays open. | P2 | Status + list + Restore/Diff + Confirm |
| `resume` | Resume a named session. | App link / picker (P2) | Session rail covers the common case. | P2 | Console in phase 1 |
| `bg`, `btw`, `moa` | Side / background / mixture-of-agents prompt. | Text prompt | Quiet or send as a structured run once we have a result view. | P2 | Console in phase 1 |
| `agents` (`tasks`) | Active agents and tasks. | **Live panel** | Seed quiet `/agents`. View-only this pass (Refresh, no Save). | P2 | Status + list + report + Refresh |
| `goal`, `subgoal` | Standing goal and extra criteria across turns. | **Live panel** (shared Goal dialog) | Seed `/goal status` then `/subgoal`. Save applies quiet `/goal set\|pause\|resume\|clear` or `/subgoal add\|rm\|clear`. Keep open and refresh, except where a restore-style close is clearer. | P2 | Status + subgoal list + action + Save/Cancel |
| `heartbeat` (`hb`), `loop` (`proactive`) | Recurring prompts in this session. | **Live panel** | Seed `/heartbeat status` or `/loop status`. Save applies set/pause/resume/clear or set/stop. Keep open and refresh. | P2 | Status + interval + prompt + Save/Cancel |
| `refine`, `review` | Memory extraction / independent review subagent. | Text prompt + confirm | Quiet `/refine` / `/review`. | P2 | Console in phase 1 |
| `snapshot` (`snap`) | Create, list, restore, or prune workspace snapshots. | **Live panel** | Seed `/snapshot list`. Create is Save; restore and prune are Confirm. Keep open after create; close after restore. | P2 | Status + list + Create/Restore/Prune |
| `history`, `prompt` (`compose`), `rewind`, `clear`, `redraw`, `worktree` | CLI/TUI session tools. | — | Terminal-oriented; no ACP GUI equivalent. | P3 | Hidden or console if advertised |
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
| `usage` | Token usage and rate limits; `reset` redeems a banked Codex limit. | Info + in-dialog confirm | Seed quiet `/usage`. **Reset…** arms a confirmation, then Save-equivalent runs `/usage reset --force` and refreshes the report. | P1/P2 | Report + Reset… / Confirm reset + Close |
| `version` (`v`) | Hermes Agent version. | Info | Quiet `/version`. Do not pin or display a Consiglio-bundled path as “the” install. | P1 | Read-only report |
| `profile` | Active profile name and home directory. | Info | Quiet `/profile`. | P1 | Read-only report |
| `whoami` | Slash-command access (admin / user). | Info | Quiet `/whoami`. | P1 | Read-only report |
| `help` | Browse available commands. | App link / Info | Opens the command palette (already the fuzzy catalog). `/help <filter>` can pre-fill the search. | P1 | Palette |
| `commands` | Paginated command list (gateway). | App link | Palette / catalog refresh. | P1 | Palette |
| `palette` | Fuzzy command palette. | App link | Existing Ctrl+K/P palette. | P1 | Palette |
| `debug` | Debug report (versions, config keys; optional upload). | **Live panel** | Seed quiet `/debug`. Upload is Confirm (`/debug --upload`) and closes. | P2 | Report + Refresh + optional Confirm upload |
| `insights` | Usage insights. | **Live panel** | Seed `/insights`. Days spinner + Run refreshes with `/insights <days>` and stays open. | P2 | Report + days + Run / Refresh |
| `subscription` (`upgrade`), `topup` | Nous plan / billing portal. | Info + link | Quiet `/subscription` / `/topup`, then open the portal URL if present. | P2 | Console in phase 1 |
| `diff` | Git changes in the workspace. | **Live panel** | Seed `/diff --stat`. Mode / summary / optional path + Run stays open with the new report. | P2 | Report + mode + Run / Refresh |
| `platforms` (`gateway`), `platform`, `egress` | Gateway / Docker egress status. | Info | Meaningful mainly for gateway hosts. | P3 | Console if advertised |
| `copy`, `paste`, `image` | TUI clipboard / attach image. | — | Consiglio already has its own attach/copy path. | P3 | — |
| `quit` (`exit`) | Exit the CLI. | App link | App Quit, never `/quit` in chat. | P1 | App quit (if it appears in the catalog, route to Quit) |

### Tools, skills, memory, plugins

| Command | What it does | Archetype | Data / apply | Priority | Phase 1 dialog |
| --- | --- | --- | --- | --- | --- |
| `tools` | List, disable, or enable tools. | Info + picker (enable/disable is a second step) | Quiet `/tools` to list. Apply quiet `/tools enable\|disable <name…>` on Save. ACP advertises this even though the CLI marks it `cli_only`. | P1 | Report + optional selection + Save when changing |
| `skills` | Search, inspect, and manage skills; optional install with confirm. | **Live panel** | Seed `/skills`. Search/inspect refresh the report. Install is an explicit confirmed action (`/skills install …`). Pending write approve/reject and the write-approval gate use quiet `/skills …`. | P2 | Status + list + action + Save/Confirm |
| `memory` | Pending memory writes and the approval gate. | **Live panel** | Seed `/memory pending`. Save applies `/memory approve\|reject <id>` or `/memory write-approval on\|off`, then refreshes. | P2 | Status + pending list + action + Save/Cancel |
| `bundles`, `learn`, `init` | Skill bundles, learn-a-skill, generate `AGENTS.md`. | Text prompt / form | Quiet commands. | P2 | Console in phase 1 |
| `cron` | List or control scheduled tasks. | **Live panel** | Seed `/cron`. Pause/resume/run/remove the selected id. No add wizard this pass. | P2 | List + action + Confirm/Save |
| `suggestions` (`suggest`) | Review suggested follow-ups. | **Live panel** | Seed `/suggestions`. Save applies `/suggestions accept\|dismiss N` and refreshes. | P2 | List + Accept/Dismiss + Save |
| `blueprint` (`bp`) | List or apply a session blueprint. | **Live panel** | Seed `/blueprint`. Save runs `/blueprint <name> [slot=value…]` directly (bare name may start a conversation on real Hermes). | P2 | Catalog list + optional slots + Save |
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
14. `usage` (Reset… confirm added in phase 2)
15. `context` / `ctx`
16. `version` / `v`
17. `profile`
18. `whoami`
19. `tools`
20. `save`
21. `plan`
22. `queue` / `q` (upgraded to a live list in phase 2)
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

## Phase 2 surfaces (this pass)

The same dialog shell as phase 1, plus a reusable **Live panel** (status,
optional report, item list, action + fields, Refresh, apply-on-Save). Seeds
and applies go through quiet slash commands (ACP when a config option exists).
Unknown or post-update commands still use the **Command console**.

1. `goal` / `subgoal` — shared Goal live panel
2. `heartbeat` / `hb`, `loop` / `proactive`
3. `queue` / `q` — editable list (add/remove/move, apply on Save)
4. `agents` / `tasks` — view-only list + report
5. `memory` — pending writes + write-approval gate
6. `skills` — search/inspect; install only with Confirm
7. `rollback` — checkpoint list; restore Confirm (optional `--all`); in-dialog diff
8. `snapshot` / `snap` — list/create/restore/prune with Confirm where destructive
9. `debug` — report; upload Confirm
10. `insights` — days + refresh
11. `diff` — mode / `--stat` / path + refresh
12. `usage reset` — Reset… then Confirm reset (`/usage reset --force`)
13. `cron` — list; pause/resume/run/remove selected (no add wizard)
14. `suggestions` / `suggest` — accept/dismiss selected
15. `blueprint` / `bp` — pick a template + optional `slot=value`

## Remaining (phase 3+)

1. **Still console or later UI:** `learn` / `init` / `bundles`, `resume` by name
   (session rail already covers the common case), `bg` / `btw` / `moa`,
   `refine` / `review`, `autoprune`, `codex-runtime`, `busy`, `export` /
   `import`, `voice` / `wake`, billing / `subscription` / `topup` with a
   clearly-labeled link out.
2. **Cron add wizard**, richer skills marketplace, and any new Hermes command
   after an update (those stay on the Command console until mapped).
3. **Keep as console or hide:** gateway-only, TUI chrome, petdex, and any
   command whose only honest interface is a terminal wizard.

Dynamic, natural-language generation of command references and descriptions
is a future note, not part of this pass.
