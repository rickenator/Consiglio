#!/usr/bin/env python3
"""A scripted stand-in for `hermes acp`, used by test_acpclient.

It speaks newline-delimited JSON-RPC over stdio like the real ACP adapter and
picks its behaviour from the prompt text, so tests can drive permission
requests, errors, crashes, cancellation and protocol drift deterministically.

Environment:
  FAKE_ACP_LOG      append every message received from the client (JSON lines)
  FAKE_ACP_VERSION  agentInfo.version to report (default 1.0.0)
  FAKE_ACP_CAPS     full | loadonly | legacy | drift   (default full)
"""
import json
import os
import sys

LOG = os.environ.get("FAKE_ACP_LOG")
VERSION = os.environ.get("FAKE_ACP_VERSION", "1.0.0")
CAPS = os.environ.get("FAKE_ACP_CAPS", "full")
session_counter = 0
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


def handle_prompt(rid, sid, text):
    global current_prompt
    current_prompt = (rid, sid, text)
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
                                     "currentModelId": "fake/model"}})
            update(sid, {"sessionUpdate": "available_commands_update",
                         "availableCommands": [{"name": "model", "description": "Show model"}]})
        elif method == "session/resume":
            respond(rid, {"_meta": {"hermes": {"sessionProvenance": {"acpSessionId": params.get("sessionId")}}}})
        elif method == "session/load":
            chunk(params.get("sessionId"), "replayed")
            respond(rid, {})
        elif method == "session/list":
            respond(rid, {"sessions": [{"sessionId": "S-old", "title": "Old", "cwd": params.get("cwd", ""),
                                        "updatedAt": "2026-11-19T00:00:00Z"}]})
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
