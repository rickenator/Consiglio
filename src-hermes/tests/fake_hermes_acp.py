#!/usr/bin/env python3
"""A scripted stand-in for `hermes acp`, used by test_acpclient.

It speaks newline-delimited JSON-RPC over stdio like the real ACP adapter and
picks its behaviour from the prompt text, so tests can drive permission
requests, errors, crashes, cancellation and protocol drift deterministically.

Environment:
  FAKE_ACP_LOG      append every message received from the client (JSON lines)
  FAKE_ACP_VERSION  agentInfo.version to report (default 1.0.0)
  FAKE_ACP_CAPS     full | loadonly | legacy | drift   (default full)
  FAKE_ACP_NEW_COMMAND  name of an extra registry command to report, as if a
                    `hermes update` had just added it (tests dynamic discovery)
  FAKE_ACP_REPLY_DELAY  seconds to wait before answering a normal prompt

Slash commands mimic Hermes v0.23: the registry commands are answered as plain
prompt text without a model turn, and `/commands [page]` returns the paged
markdown catalogue the real agent prints.
"""
import json
import os
import sys
import time

LOG = os.environ.get("FAKE_ACP_LOG")
VERSION = os.environ.get("FAKE_ACP_VERSION", "1.0.0")
CAPS = os.environ.get("FAKE_ACP_CAPS", "full")
NEW_COMMAND = os.environ.get("FAKE_ACP_NEW_COMMAND", "")
REPLY_DELAY = float(os.environ.get("FAKE_ACP_REPLY_DELAY", "0") or 0)
session_counter = 0
config = {"edit_approval_policy": "ask", "model": "fake/model", "reasoning_effort": "medium"}
goal = {"text": "", "state": "none"}
subgoals = []
heartbeat = {"interval": "10m", "prompt": "", "state": "none"}
loop = {"interval": "", "prompt": "", "state": "none"}
queue_items = ["Review the latest diff"]
memory_pending = [("mem01", "Remember the project uses Qt 5")]
memory_gate = "on"
skills_pending = [("sk01", "Write a skill note about ACP sessions")]
skill_gate = "on"
checkpoints = [
    (1, "a1b2c3d", "2026-11-20 17:01", "before edit"),
    (2, "d4e5f6a", "2026-11-20 17:10", "after tools"),
]
snapshots = [("snap-1", "3 files", "12K", "before refactor")]
cron_jobs = [{"id": "job-1", "state": "active", "when": "every 1h", "label": "status ping"}]
suggestions = [(1, "Add a README section on ACP"), (2, "Extract the live panel into its own file")]
blueprints = [("weekly-review", "Recurring weekly review session"), ("bug-triage", "Triage open issues")]
agents = [("worker-1", "running", "summarize the log")]
usage = {"tokens": 1204, "reset_available": True}

# What ACP advertises (the adapter's own short list, like Hermes v0.23).
ADVERTISED = [
    {"name": "help", "description": "List available commands"},
    {"name": "model", "description": "Show or change current model",
     "input": {"hint": "model name to switch to"}},
    {"name": "tools", "description": "List available tools with descriptions"},
    {"name": "context", "description": "Show conversation message counts by role"},
    {"name": "reset", "description": "Clear conversation history"},
    {"name": "compress", "description": "Compress conversation context"},
    {"name": "steer", "description": "Inject guidance into the currently running agent turn",
     "input": {"hint": "guidance for the active turn"}},
    {"name": "queue", "description": "Queue a prompt for the next turn",
     "input": {"hint": "prompt to run after the current turn"}},
    {"name": "usage", "description": "Show token usage for the current session"},
    {"name": "status", "description": "Show session, model, token, and context info"},
    {"name": "version", "description": "Show Hermes version"},
    {"name": "title", "description": "Set or show the session title",
     "input": {"hint": "new session title"}},
    {"name": "undo", "description": "Remove the last user/assistant exchange"},
    {"name": "retry", "description": "Resend the last user message"},
]

# The registry, as `/commands` lists it (a representative subset).
REGISTRY = [
    ("new", "", "Start a new session (fresh session ID + history)", ["reset"]),
    ("retry", "", "Retry the last message (resend to agent)", []),
    ("undo", "", "Remove the last user/assistant exchange", []),
    ("title", "[name]", "Set a title for the current session", []),
    ("branch", "[name]", "Branch the current session (explore a different path)", ["fork"]),
    ("compress", "[focus topic]", "Manually compress conversation context", []),
    ("stop", "", "Kill all running background processes", []),
    ("queue", "<prompt>", "Queue a prompt for the next turn (doesn't interrupt)", ["q"]),
    ("steer", "<prompt>", "Inject a message after the next tool call without interrupting", []),
    ("goal", "[text | -- <text> | draft <text> | show | gate add <cmd> | pause | resume | clear | status | wait <pid> | unwait]",
     "Set a standing goal Hermes works on across turns until achieved", []),
    ("subgoal", "[text | remove N | clear]", "Add or manage extra criteria on the active goal", []),
    ("heartbeat", "[every <interval> <prompt> | status | pause | resume | clear]",
     "Set a recurring prompt that re-enters this session when idle", ["hb"]),
    ("loop", "[<interval> <prompt> | stop | status]", "Run a recurring instruction while the session is open", ["proactive"]),
    ("agents", "", "List active subagents and background tasks", ["tasks"]),
    ("rollback", "[N | --diff [N] | --all]", "Restore files from a checkpoint", []),
    ("snapshot", "[list | create | restore <id> | prune]", "Create or restore workspace snapshots", ["snap"]),
    ("debug", "[--upload]", "Write a debug report for this session", []),
    ("insights", "[days]", "Show usage insights for recent days", []),
    ("diff", "[--stat|--branch|--all] [path]", "Show a diff of session file changes", []),
    ("cron", "[pause|resume|run|remove] [id]", "List or control scheduled tasks", []),
    ("suggestions", "[accept|dismiss N]", "Review suggested follow-ups", ["suggest"]),
    ("blueprint", "[name [slot=value...]]", "List or apply a session blueprint", ["bp"]),
    ("plan", "[task]", "Write a markdown implementation plan to .hermes/plans/ without executing anything", []),
    ("status", "", "Show session, model, token, and context info", []),
    ("context", "[all]", "Show detailed context window view with usage gauge", ["ctx"]),
    ("model", "[model|reset] [--provider name] [--global|--session]", "Switch model (session-scoped; --global to persist)", []),
    ("personality", "[name]", "Set a predefined personality", []),
    ("reasoning", "[level|show|hide|full|clamp] [--global]", "Manage reasoning effort and display", []),
    ("fast", "[normal|fast|auto|status] [--global]", "Fast mode", []),
    ("approvals", "[manual|smart|off]", "Show or set the persistent dangerous-command approval mode", []),
    ("yolo", "", "Toggle YOLO mode (skip all dangerous command approvals)", []),
    ("tools", "[list|disable|enable] [name...]", "Manage tools", []),
    ("skills", "[search|browse|inspect|install]", "Search, install, inspect, or manage skills", []),
    ("memory", "[pending|approve|reject]", "Review pending memory writes", []),
    ("usage", "", "Show token usage and rate limits for the current session", []),
    ("help", "", "Show available commands", []),
    ("commands", "[page]", "Browse all commands and skills (paginated)", []),
    ("version", "", "Show Hermes version", []),
    ("whoami", "", "Show your slash command access (admin / user)", []),
    ("profile", "", "Show active profile name and home directory", []),
    ("update", "", "Update Hermes Agent to the latest version", []),
]
SKILLS = [
    ("arxiv", "Search arXiv papers by keyword, author, category, or ID."),
    ("ascii-video", "ASCII video: convert video/audio to colored ASCII MP4/GIF."),
]
PER_PAGE = 20
current_prompt = None  # (id, session_id, text)


def log(obj):
    if LOG:
        with open(LOG, "a") as f:
            f.write(json.dumps(obj) + "\n")


def send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def update(session_id, upd):
    send({"jsonrpc": "2.0", "method": "session/update",
          "params": {"sessionId": session_id, "update": upd}})


def chunk(session_id, text):
    update(session_id, {"sessionUpdate": "agent_message_chunk",
                        "content": {"type": "text", "text": text}})


def respond(rid, result):
    send({"jsonrpc": "2.0", "id": rid, "result": result})


def end_turn(rid, reason="end_turn", **extra):
    res = {"stopReason": reason, "usage": {"inputTokens": 12, "outputTokens": 3}}
    res.update(extra)
    respond(rid, res)


def initialize_result():
    if CAPS == "legacy":
        return {"protocolVersion": 1}
    if CAPS == "loadonly":
        return {"protocolVersion": 1,
                "agentInfo": {"name": "hermes-agent", "version": VERSION},
                "agentCapabilities": {"loadSession": True, "sessionCapabilities": {}}}
    caps = {"loadSession": True,
            "promptCapabilities": {"image": True},
            "sessionCapabilities": {"fork": {}, "list": {}, "resume": {}}}
    res = {"protocolVersion": 1,
           "agentInfo": {"name": "hermes-agent", "version": VERSION},
           "agentCapabilities": caps, "authMethods": []}
    if CAPS == "drift":
        res["protocolVersion"] = 2
        res["somethingNew"] = {"nested": [1, 2, 3]}
        caps["futureCapability"] = {"enabled": True}
        caps["sessionCapabilities"] = {"resume": {}, "teleport": {}}  # no "list"
    return res


def commands_page(n):
    registry = list(REGISTRY)
    if NEW_COMMAND:
        registry.append((NEW_COMMAND, "[on|off|status]", "A command added by a Hermes update", []))
    lines = []
    for name, args, desc, aliases in registry:
        head = "`/%s%s`" % (name, (" " + args) if args else "")
        tail = ""
        if aliases:
            tail = " (alias: %s)" % ", ".join("`/%s`" % a for a in aliases)
        lines.append("%s -- %s%s" % (head, desc, tail))
    skill_lines = ["`/%s` — %s" % (n, d) for n, d in SKILLS]
    pages = [lines[i:i + PER_PAGE] for i in range(0, len(lines), PER_PAGE)]
    pages[-1] = pages[-1] + ["", "⚡ **Skill Commands**:"] + skill_lines
    total = len(registry) + len(SKILLS)
    count = len(pages)
    note = ""
    if n > count:
        note = "Requested page %d was out of range.\n\n" % n
        n = count
    n = max(1, n)
    out = "📚 **Commands** (%d total, page %d/%d)\n\n%s" % (total, n, count, note)
    out += "\n".join(pages[n - 1])
    nav = []
    if n > 1:
        nav.append("`/commands %d` ← prev" % (n - 1))
    if n < count:
        nav.append("next → `/commands %d`" % (n + 1))
    if nav:
        out += "\n\n" + " | ".join(nav)
    return out


def slash(sid, text):
    """Answer a registry command the way the gateway's slash.exec does."""
    parts = text[1:].split(None, 1)
    name = parts[0] if parts else ""
    arg = parts[1].strip() if len(parts) > 1 else ""
    known = {r[0] for r in REGISTRY} | {a for r in REGISTRY for a in r[3]} | {s[0] for s in SKILLS}
    if NEW_COMMAND:
        known.add(NEW_COMMAND)
    alias = {r[0]: r[0] for r in REGISTRY}
    for r in REGISTRY:
        for a in r[3]:
            alias[a] = r[0]
    if name not in known:
        return None
    name = alias.get(name, name)
    if name == "commands":
        try:
            return commands_page(int(arg or "1"))
        except ValueError:
            return commands_page(1)
    if name == "goal":
        # Same wording as hermes_cli/goals.py status_line() / goal_command.py.
        def status_line():
            if not goal["text"]:
                return "No active goal. Set one with /goal <text>."
            return "%s Goal (%s, %d/20 turns): %s" % ("⏸" if goal["state"] == "paused" else "⊙",
                                                     goal["state"], goal.get("turns", 0), goal["text"])
        if arg in ("", "status"):
            return status_line()
        if arg == "show":
            return status_line() + "\n(no completion contract — set one with /goal draft <objective>)"
        if arg == "pause":
            if not goal["text"]:
                return "No goal set."
            goal["state"] = "paused"
            return "⏸ Goal paused: %s" % goal["text"]
        if arg == "resume":
            if not goal["text"]:
                return "No goal to resume."
            goal["state"] = "active"
            return "▶ Goal resumed: %s" % goal["text"]
        if arg == "clear":
            had = bool(goal["text"])
            goal.update(text="", state="none")
            return "✓ Goal cleared." if had else "No active goal."
        if arg.startswith("set "):
            text = arg[4:]
        elif arg.startswith("draft "):
            text = arg[6:]
        elif arg.startswith("-- "):
            text = arg[3:]
        else:
            text = arg
        goal.update(text=text, state="active", turns=0)
        return ("⊙ Goal set (20-turn budget): %s\nAfter each turn, a judge model checks if the goal is done. "
                "Hermes keeps working until it is, you pause/clear it, or the budget is exhausted. "
                "Use /goal status, /goal show, /goal pause, /goal resume, /goal clear." % text)
    if name == "subgoal":
        parts = arg.split(None, 1)
        verb = parts[0] if parts else ""
        rest = parts[1] if len(parts) > 1 else ""
        if verb in ("", "list"):
            if not subgoals:
                return "No subgoals."
            return "Subgoals:\n" + "\n".join("  %d. %s" % (i + 1, s) for i, s in enumerate(subgoals))
        if verb in ("add",) or (verb and verb not in ("remove", "rm", "clear")):
            text = rest if verb == "add" else arg
            if not text:
                return "Usage: /subgoal add <text>"
            subgoals.append(text)
            return "✓ Subgoal added: %s" % text
        if verb in ("remove", "rm"):
            try:
                n = int(rest or "0")
            except ValueError:
                return "Usage: /subgoal rm <N>"
            if n < 1 or n > len(subgoals):
                return "No subgoal %s." % rest
            gone = subgoals.pop(n - 1)
            return "✓ Removed subgoal %d: %s" % (n, gone)
        if verb == "clear":
            n = len(subgoals)
            subgoals[:] = []
            return "✓ Cleared %d subgoal(s)." % n
        return "Usage: /subgoal [add <text> | rm N | clear]"
    if name == "heartbeat":
        parts = arg.split(None, 1)
        verb = parts[0] if parts else ""
        rest = parts[1] if len(parts) > 1 else ""
        if verb in ("", "status"):
            if not heartbeat["prompt"]:
                return "No heartbeat set. Use /heartbeat set <interval> <prompt>."
            mark = "♥" if heartbeat["state"] != "paused" else "⏸"
            return "%s Heartbeat (every %s, next in ~60s): %s" % (
                mark, heartbeat["interval"] or "10m", heartbeat["prompt"])
        if verb in ("set", "every"):
            bits = rest.split(None, 1)
            interval = bits[0] if bits else "10m"
            prompt = bits[1] if len(bits) > 1 else ""
            if not prompt:
                return "Usage: /heartbeat set <interval> <prompt>"
            heartbeat.update(interval=interval, prompt=prompt, state="active")
            return "♥ Heartbeat set (every %s): %s" % (interval, prompt)
        if verb == "pause":
            if not heartbeat["prompt"]:
                return "No heartbeat set."
            heartbeat["state"] = "paused"
            return "⏸ Heartbeat paused."
        if verb == "resume":
            if not heartbeat["prompt"]:
                return "No heartbeat set."
            heartbeat["state"] = "active"
            return "♥ Heartbeat resumed."
        if verb == "clear":
            heartbeat.update(interval="", prompt="", state="none")
            return "✓ Heartbeat cleared."
        return "Usage: /heartbeat set <interval> <prompt>"
    if name == "loop":
        parts = arg.split(None, 1)
        verb = parts[0] if parts else ""
        rest = parts[1] if len(parts) > 1 else ""
        if verb in ("", "status"):
            if not loop["prompt"]:
                return "No loop set."
            return "Loop every %s (3 ticks remaining): %s" % (loop["interval"] or "15m", loop["prompt"])
        if verb == "stop":
            loop.update(interval="", prompt="", state="none")
            return "✓ Loop stopped."
        interval, prompt = verb, rest
        if not prompt:
            prompt = arg
            interval = "15m"
        loop.update(interval=interval, prompt=prompt, state="active")
        return "Loop set every %s: %s" % (interval, prompt)
    if name == "queue":
        parts = arg.split(None, 1)
        verb = parts[0] if parts else ""
        rest = parts[1] if len(parts) > 1 else ""
        def list_q():
            if not queue_items:
                return "0 prompts queued"
            out = "%d prompt%s queued\n" % (len(queue_items), "" if len(queue_items) == 1 else "s")
            out += "\n".join("    %d. %s" % (i + 1, q) for i, q in enumerate(queue_items))
            return out
        if verb in ("", "list"):
            return list_q()
        if verb == "add":
            if not rest:
                return "Usage: /queue add <prompt>"
            queue_items.append(rest)
            return "Queued: %s\n" % rest + list_q()
        if verb == "clear":
            n = len(queue_items)
            queue_items[:] = []
            return "✓ Cleared %d queued prompt(s)." % n
        if verb in ("rm", "remove"):
            try:
                n = int(rest)
            except ValueError:
                return "Usage: /queue rm <N>"
            if n < 1 or n > len(queue_items):
                return "No queued prompt %s." % rest
            gone = queue_items.pop(n - 1)
            return "✓ Removed: %s\n" % gone + list_q()
        queue_items.append(arg)
        return "Queued: %s\n" % arg + list_q()
    if name == "agents":
        if not agents:
            return "No active agents."
        return "Agents:\n" + "\n".join("  %d. %s  %s  %s" % (i + 1, a[0], a[1], a[2]) for i, a in enumerate(agents))
    if name == "memory":
        parts = arg.split(None, 1)
        verb = parts[0] if parts else ""
        rest = parts[1] if len(parts) > 1 else ""
        def pending():
            lines = ["memory.write_approval = %s" % memory_gate]
            if not memory_pending:
                lines.append("No pending memory writes.")
            else:
                for mid, summary in memory_pending:
                    lines.append("  %s  %s" % (mid, summary))
            return "\n".join(lines)
        if verb in ("", "pending"):
            return pending()
        if verb == "approve":
            before = len(memory_pending)
            memory_pending[:] = [p for p in memory_pending if p[0] != rest]
            return ("✓ Approved %s.\n" % rest + pending()) if before != len(memory_pending) else "No pending write %s." % rest
        if verb == "reject":
            before = len(memory_pending)
            memory_pending[:] = [p for p in memory_pending if p[0] != rest]
            return ("✓ Rejected %s.\n" % rest + pending()) if before != len(memory_pending) else "No pending write %s." % rest
        if verb == "write-approval":
            if rest in ("on", "off"):
                globals()["memory_gate"] = rest
                return "memory.write_approval = %s" % rest
            return "Usage: /memory write-approval on|off"
        return pending()
    if name == "skills":
        parts = arg.split(None, 1)
        verb = parts[0] if parts else ""
        rest = parts[1] if len(parts) > 1 else ""
        catalog = "Installed skills:\n  1. arxiv  Search arXiv papers by keyword.\n  2. ascii-video  Convert video to ASCII."
        if verb in ("", "browse", "list"):
            lines = [catalog, "skills.write_approval = %s" % skill_gate]
            if skills_pending:
                lines.append("Pending skill writes:")
                for sid_, summary in skills_pending:
                    lines.append("  %s  %s" % (sid_, summary))
            return "\n".join(lines)
        if verb == "search":
            return "Skill search: %s\n  1. arxiv  Search arXiv papers by keyword." % (rest or "*")
        if verb == "inspect":
            return "Skill %s\nName: %s\nDescription: A sample skill from the fake agent." % (rest, rest or "arxiv")
        if verb == "install":
            return "✓ Installed skill: %s (dry run in the test agent)." % (rest or "unknown")
        if verb in ("approve", "reject"):
            skills_pending[:] = [p for p in skills_pending if p[0] != rest]
            return "✓ %s %s." % (verb.title(), rest)
        if verb == "write-approval":
            if rest in ("on", "off"):
                globals()["skill_gate"] = rest
                return "skills.write_approval = %s" % rest
            return "Usage: /skills write-approval on|off"
        return catalog
    if name == "rollback":
        parts = arg.split()
        if not parts or parts[0] in ("list",):
            lines = ["Checkpoints:"]
            for n, hsh, ts, reason in checkpoints:
                lines.append("  %d. %s  %s  %s" % (n, hsh, ts, reason))
            return "\n".join(lines)
        if parts[0] in ("--diff", "diff") or (len(parts) >= 1 and parts[0] == "--diff"):
            n = parts[-1] if parts[-1].lstrip("-").isdigit() or parts[-1] != "--diff" else "1"
            return "Diff against checkpoint %s:\n README.md | 2 +++---\n 1 file changed, 2 insertions(+), 2 deletions(-)" % n
        if parts[0].lstrip("-").isdigit() or parts[0] not in ("--all",):
            n = parts[0]
            extra = " (all files)" if "--all" in parts else ""
            return "✓ Restored checkpoint %s%s." % (n, extra)
        return "Usage: /rollback [N] [--all] [--diff N]"
    if name == "snapshot":
        parts = arg.split(None, 1)
        verb = parts[0] if parts else ""
        rest = parts[1] if len(parts) > 1 else ""
        def slist():
            lines = ["Snapshots:"]
            for i, (sid_, files, size, label) in enumerate(snapshots, 1):
                lines.append("  %d. %s  %s  %s  %s" % (i, sid_, files, size, label))
            if not snapshots:
                lines.append("  (none)")
            return "\n".join(lines)
        if verb in ("", "list"):
            return slist()
        if verb == "restore":
            return "✓ Restored snapshot %s." % (rest or "snap-1")
        if verb == "prune":
            n = len(snapshots)
            snapshots[:] = snapshots[:1]
            return "✓ Pruned snapshots (kept 1 of %d)." % n
        # create
        label = arg if verb != "create" else rest
        sid_ = "snap-%d" % (len(snapshots) + 1)
        snapshots.append((sid_, "2 files", "8K", label or "snapshot"))
        return "✓ Created %s.\n" % sid_ + slist()
    if name == "debug":
        if "--upload" in arg:
            return "✓ Debug report uploaded (test agent, nothing left the machine)."
        return ("Debug report\n------------\nHermes  test-agent %s\nSession  %s\nModel  %s\n"
                "Platform  linux\nThis is a local report only." % (VERSION, sid, config["model"]))
    if name == "insights":
        days = arg.strip() or "7"
        return ("Usage insights (%s days)\n  Prompts   12\n  Tokens    8,432\n  Errors    0\n  Top model   fake/model" % days)
    if name == "diff":
        return ("Diff%s\n README.md | 4 ++++\n src/app.cpp | 12 ++++++------\n 2 files changed, 10 insertions(+), 6 deletions(-)"
                % ((" " + arg) if arg else " --stat"))
    if name == "cron":
        parts = arg.split(None, 1)
        verb = parts[0] if parts else ""
        rest = parts[1] if len(parts) > 1 else ""
        def clist():
            if not cron_jobs:
                return "No scheduled tasks."
            lines = ["Scheduled tasks:"]
            for i, job in enumerate(cron_jobs, 1):
                lines.append("  %d. %s  %s  %s  %s" % (i, job["id"], job["state"], job["when"], job["label"]))
            return "\n".join(lines)
        if verb in ("", "list"):
            return clist()
        job = next((j for j in cron_jobs if j["id"] == rest), None)
        if not job:
            return "No scheduled task %s." % rest
        if verb == "pause":
            job["state"] = "paused"
            return "⏸ Paused %s.\n" % rest + clist()
        if verb == "resume":
            job["state"] = "active"
            return "▶ Resumed %s.\n" % rest + clist()
        if verb == "run":
            return "✓ Triggered %s." % rest
        if verb == "remove":
            cron_jobs[:] = [j for j in cron_jobs if j["id"] != rest]
            return "✓ Removed %s.\n" % rest + clist()
        return clist()
    if name == "suggestions":
        parts = arg.split(None, 1)
        verb = parts[0] if parts else ""
        rest = parts[1] if len(parts) > 1 else ""
        def slist():
            if not suggestions:
                return "No suggestions."
            return "Suggestions:\n" + "\n".join("  %d. %s" % (n, text) for n, text in suggestions)
        if verb in ("", "list"):
            return slist()
        try:
            n = int(rest)
        except ValueError:
            return "Usage: /suggestions accept|dismiss N"
        before = len(suggestions)
        suggestions[:] = [s for s in suggestions if s[0] != n]
        if before == len(suggestions):
            return "No suggestion %s." % rest
        return "✓ %s suggestion %d.\n" % (verb.title(), n) + slist()
    if name == "blueprint":
        if not arg:
            return "Blueprints:\n" + "\n".join("  %d. %s  %s" % (i + 1, n, d) for i, (n, d) in enumerate(blueprints))
        return "✓ Created session from blueprint %s." % arg
    if name == "usage":
        if arg.startswith("reset"):
            if usage["reset_available"]:
                usage["reset_available"] = False
                return "✓ Redeemed a banked Codex usage reset."
            return "No banked Codex reset is available."
        bank = "available" if usage["reset_available"] else "none"
        return "Session usage: %s tokens\nBanked Codex reset: %s" % (format(usage["tokens"], ","), bank)
    if name == "status":
        return ("**Session** `%s`\n\n| | |\n|---|---|\n| Model | `%s` |\n| Reasoning | %s |\n"
                "| Context | 1,204 / 65,536 tokens |" % (sid, config["model"], config["reasoning_effort"]))
    if name == "version":
        return "Hermes Agent v%s" % VERSION
    return "`/%s%s` ran." % (name, (" " + arg) if arg else "")


def config_options():
    def select(cid, name, category, choices, desc=""):
        return {"id": cid, "name": name, "description": desc, "category": category, "type": "select",
                "currentValue": config[cid],
                "options": [{"value": v, "name": n} for v, n in choices]}
    return [
        select("edit_approval_policy", "Edits", "mode",
               [("ask", "Ask every time"), ("workspace_session", "Auto-approve in workspace"),
                ("session", "Auto-approve everything")], "When file edits need approval"),
        select("model", "Model", "model", [("fake/model", "Fake"), ("fake/large", "Fake Large")]),
        select("reasoning_effort", "Reasoning", "thought_level",
               [(v, v.capitalize()) for v in ("none", "low", "medium", "high")]),
    ]


def handle_prompt(rid, sid, text):
    global current_prompt
    current_prompt = (rid, sid, text)
    if text.startswith("/"):
        reply = slash(sid, text)
        if reply is not None:
            chunk(sid, reply)
            end_turn(rid)
            current_prompt = None
            return
    if text.lower().strip() == "hello world":
        if REPLY_DELAY:
            time.sleep(REPLY_DELAY)
        update(sid, {"sessionUpdate": "agent_thought_chunk",
                     "content": {"type": "text", "text": "The user is greeting me. I'll greet them back."}})
        chunk(sid, "Hello, world! 👋 I'm Hermes. How can I help you today?")
        update(sid, {"sessionUpdate": "usage_update", "used": 10840, "size": 65536})
        end_turn(rid)
        current_prompt = None
        return
    if text.lower().startswith("demo"):
        # A richer turn for screenshots: reasoning, a tool call, markdown.
        update(sid, {"sessionUpdate": "agent_thought_chunk",
                     "content": {"type": "text", "text": "I should look at the repository layout first, then summarise the build steps."}})
        update(sid, {"sessionUpdate": "tool_call", "toolCallId": "tc-demo", "title": "ls -la src/",
                     "kind": "execute", "status": "in_progress"})
        update(sid, {"sessionUpdate": "tool_call_update", "toolCallId": "tc-demo", "status": "completed",
                     "content": [{"type": "content", "content": {"type": "text",
                                 "text": "acpclient.cpp\nchatpane.cpp\ncommandcatalog.cpp\nmainwindow.cpp\ntheme.cpp"}}]})
        chunk(sid, "The client lives in `src/`. To build it:\n\n"
                   "```bash\ncmake -S . -B build\ncmake --build build -j\n```\n\n"
                   "| File | Role |\n|---|---|\n| `acpclient.cpp` | ACP transport |\n"
                   "| `commandcatalog.cpp` | Command discovery |\n| `mainwindow.cpp` | Window and menus |\n\n"
                   "Run the tests with **ctest** once it builds.")
        update(sid, {"sessionUpdate": "usage_update", "used": 21840, "size": 65536})
        end_turn(rid)
        current_prompt = None
        return
    if text == "permission":
        # An unsupported client method first, then a permission request whose
        # id deliberately collides with the prompt's own request id.
        send({"jsonrpc": "2.0", "id": "fs-1", "method": "fs/read_text_file",
              "params": {"sessionId": sid, "path": "/etc/hostname"}})
        send({"jsonrpc": "2.0", "id": rid, "method": "session/request_permission",
              "params": {"sessionId": sid,
                         "toolCall": {"toolCallId": "tc-1", "title": "rm -rf build",
                                      "kind": "execute", "status": "pending",
                                      "content": [{"type": "content",
                                                   "content": {"type": "text", "text": "rm -rf build"}}],
                                      "rawInput": {"command": "rm -rf build"}},
                         "options": [{"optionId": "opt-yes-7f3", "name": "Allow once", "kind": "allow_once"},
                                     {"optionId": "opt-always", "name": "Allow always", "kind": "allow_always"},
                                     {"optionId": "opt-no", "name": "Deny", "kind": "reject_once"},
                                     {"optionId": "opt-future", "name": "Ask later", "kind": "defer_v9"}]}})
        return  # turn continues when the permission answer arrives
    if text == "fail":
        send({"jsonrpc": "2.0", "id": rid, "error": {"code": -32603, "message": "provider exploded"}})
        current_prompt = None
        return
    if text == "crash":
        sys.exit(3)
    if text == "slow":
        return  # waits for session/cancel
    if text == "drift":
        send({"jsonrpc": "2.0", "method": "hermes/somethingNew", "params": {"x": 1}})
        update(sid, {"sessionUpdate": "plan_v7", "entries": [{"weird": True}]})
        update(sid, {"sessionUpdate": "usage_update", "used": 5000000000, "size": 9000000000,
                     "newField": "ignored"})
        update(sid, {"sessionUpdate": "agent_message_chunk",
                     "content": {"type": "text", "text": "ok", "annotations": {"z": 1}}, "extra": 1})
        end_turn(rid, "end_turn", newResultField={"a": 1})
        current_prompt = None
        return
    chunk(sid, "echo:" + text + "@" + VERSION)
    end_turn(rid)
    current_prompt = None


def main():
    global session_counter, current_prompt
    if len(sys.argv) < 2 or sys.argv[1] != "acp":
        print("fake hermes: only `acp` is supported", file=sys.stderr)
        return 2
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        msg = json.loads(line)
        log(msg)
        method = msg.get("method")
        rid = msg.get("id")
        params = msg.get("params") or {}
        if method is None:
            # A response from the client to one of our requests.
            if current_prompt and rid == current_prompt[0] and "result" in msg:
                outcome = msg["result"].get("outcome", {})
                prid, psid, _ = current_prompt
                chunk(psid, "permission:%s/%s" % (outcome.get("outcome"), outcome.get("optionId", "")))
                end_turn(prid)
                current_prompt = None
            continue
        if method == "initialize":
            respond(rid, initialize_result())
        elif method == "session/new":
            session_counter += 1
            sid = "S-%d" % session_counter
            respond(rid, {"sessionId": sid,
                          "models": {"availableModels": [{"modelId": "fake/model", "name": "Fake"}],
                                     "currentModelId": "fake/model"},
                          "modes": {"availableModes": [{"id": "default", "name": "Default"},
                                                       {"id": "accept_edits", "name": "Accept edits"}],
                                    "currentModeId": "default"},
                          "configOptions": config_options()})
            if CAPS == "full" and os.environ.get("FAKE_ACP_FULL_COMMANDS"):
                cmds = ADVERTISED
            else:
                cmds = [{"name": "model", "description": "Show model"}]
            update(sid, {"sessionUpdate": "available_commands_update", "availableCommands": cmds})
        elif method == "session/set_config_option":
            cid = params.get("configId")
            if cid not in config:
                send({"jsonrpc": "2.0", "id": rid, "error": {"code": -32602, "message": "unknown config option"}})
            elif params.get("value") == "explode":
                send({"jsonrpc": "2.0", "id": rid, "error": {"code": -32603, "message": "rejected"}})
            else:
                config[cid] = params.get("value")
                respond(rid, {"configOptions": []})  # what Hermes v0.23 really returns
        elif method == "session/set_mode":
            respond(rid, {})
            update(params.get("sessionId"), {"sessionUpdate": "current_mode_update",
                                             "currentModeId": params.get("modeId")})
        elif method == "session/resume":
            respond(rid, {"_meta": {"hermes": {"sessionProvenance": {"acpSessionId": params.get("sessionId")}}}})
        elif method == "session/load":
            chunk(params.get("sessionId"), "replayed")
            respond(rid, {})
        elif method == "session/list":
            mode = os.environ.get("FAKE_ACP_SESSIONS", "")
            cursor = params.get("cursor") or params.get("nextCursor")
            if mode == "pages":
                page1 = [{"sessionId": "S-new", "title": "Current", "cwd": "/now",
                          "updatedAt": "2026-11-20T12:00:00Z"}]
                page2 = [{"session_id": "S-old", "title": "August", "cwd": "/legacy",
                          "updated_at": "2026-08-19T12:00:00Z"}]
                if not cursor:
                    respond(rid, {"sessions": page1, "nextCursor": "S-new"})
                else:
                    respond(rid, {"sessions": page2})
            elif mode == "snake":
                respond(rid, {"sessions": [{"session_id": "S-snake", "title": "Snake",
                                            "cwd": "/x", "updated_at": 1795147779.97}]})
            else:
                sessions = [{"sessionId": "S-old", "title": "Old", "cwd": params.get("cwd", ""),
                             "updatedAt": "2026-11-19T00:00:00Z"}]
                if mode == "many":
                    titles = ["Refactor the ACP transport", "Fix flaky bootstrap test", "Write release notes for 0.1",
                              "Investigate context overflow", "Add dark theme tokens", "Profile startup time"]
                    sessions = [{"sessionId": "S-h%d" % i, "title": t,
                                 "cwd": params.get("cwd", "") or "/home/user/consiglio",
                                 "updatedAt": "2026-11-%02dT1%d:00:00Z" % (19 - i, i)} for i, t in enumerate(titles)]
                respond(rid, {"sessions": sessions})
        elif method == "session/prompt":
            text = "".join(b.get("text", "") for b in params.get("prompt", []))
            handle_prompt(rid, params.get("sessionId"), text)
        elif method == "session/cancel":
            if current_prompt and current_prompt[2] == "slow":
                prid, psid, _ = current_prompt
                chunk(psid, "late chunk from cancelled turn")
                end_turn(prid, "cancelled")
                current_prompt = None
        elif rid is not None:
            send({"jsonrpc": "2.0", "id": rid, "error": {"code": -32601, "message": "Method not found"}})
    return 0


if __name__ == "__main__":
    sys.exit(main())
