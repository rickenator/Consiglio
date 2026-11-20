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
    if name not in known:
        return None
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
        text = arg[6:] if arg.startswith("draft ") else (arg[3:] if arg.startswith("-- ") else arg)
        goal.update(text=text, state="active", turns=0)
        return ("⊙ Goal set (20-turn budget): %s\nAfter each turn, a judge model checks if the goal is done. "
                "Hermes keeps working until it is, you pause/clear it, or the budget is exhausted. "
                "Use /goal status, /goal show, /goal pause, /goal resume, /goal clear." % text)
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
            sessions = [{"sessionId": "S-old", "title": "Old", "cwd": params.get("cwd", ""),
                         "updatedAt": "2026-11-19T00:00:00Z"}]
            if os.environ.get("FAKE_ACP_SESSIONS") == "many":
                titles = ["Refactor the ACP transport", "Fix flaky bootstrap test", "Write release notes for 0.1",
                          "Investigate context overflow", "Add dark theme tokens", "Profile startup time"]
                sessions = [{"sessionId": "S-h%d" % i, "title": t, "cwd": params.get("cwd", "") or "/home/rick/consiglio",
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
