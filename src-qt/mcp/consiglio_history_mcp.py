#!/usr/bin/env python3
"""Read-only MCP access to Consiglio's SQLite conversation history."""

from __future__ import annotations

import argparse
import json
import os
import re
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def default_database_path() -> Path:
    data_home = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    return data_home / "Aniviza" / "Consiglio" / "consiglio.sqlite"


class HistoryStore:
    def __init__(self, path: Path):
        uri = f"file:{path.resolve()}?mode=ro"
        self.db = sqlite3.connect(uri, uri=True)
        self.db.row_factory = sqlite3.Row
        self.db.execute("PRAGMA query_only = ON")
        self.db.execute("PRAGMA busy_timeout = 5000")

    @staticmethod
    def _rows(cursor: sqlite3.Cursor) -> list[dict[str, Any]]:
        return [dict(row) for row in cursor.fetchall()]

    def list_projects(self, limit: int) -> list[dict[str, Any]]:
        return self._rows(self.db.execute(
            """SELECT p.id, p.name, p.workspace, p.created_at, p.last_activity,
                      COUNT(s.id) AS session_count
                 FROM projects p LEFT JOIN sessions s ON s.project_id = p.id
                GROUP BY p.id ORDER BY p.last_activity DESC LIMIT ?""", (limit,)))

    def list_sessions(self, project_id: str | None, limit: int) -> list[dict[str, Any]]:
        sql = """SELECT id, project_id, provider, status, repository, branch,
                        permission_mode, preferred_name, started_at, last_activity
                   FROM sessions"""
        params: list[Any] = []
        if project_id:
            sql += " WHERE project_id = ?"
            params.append(project_id)
        sql += " ORDER BY last_activity DESC LIMIT ?"
        params.append(limit)
        return self._rows(self.db.execute(sql, params))

    def conversation(self, session_id: str, limit: int) -> list[dict[str, Any]]:
        rows = self._rows(self.db.execute(
            """SELECT type, content, timestamp, command, working_dir
                 FROM events WHERE session_id = ?
                ORDER BY timestamp DESC, id DESC LIMIT ?""", (session_id, limit)))
        rows.reverse()
        return rows

    def search(self, query: str, project_id: str | None,
               session_id: str | None, limit: int) -> list[dict[str, Any]]:
        tokens = re.findall(r"[\w.-]+", query, flags=re.UNICODE)
        if not tokens:
            return []
        fts_query = " AND ".join(f'"{token.replace(chr(34), chr(34) * 2)}"' for token in tokens)
        sql = """SELECT e.session_id, e.type, e.content, e.timestamp,
                        s.project_id, p.name AS project_name,
                        snippet(events_fts, 0, '[', ']', ' … ', 24) AS snippet
                   FROM events_fts
                   JOIN events e ON e.id = events_fts.rowid
              LEFT JOIN sessions s ON s.id = e.session_id
              LEFT JOIN projects p ON p.id = s.project_id
                  WHERE events_fts MATCH ?"""
        params: list[Any] = [fts_query]
        if project_id:
            sql += " AND s.project_id = ?"
            params.append(project_id)
        if session_id:
            sql += " AND e.session_id = ?"
            params.append(session_id)
        sql += " ORDER BY bm25(events_fts), e.timestamp DESC LIMIT ?"
        params.append(limit)
        try:
            return self._rows(self.db.execute(sql, params))
        except sqlite3.OperationalError:
            fallback = """SELECT e.session_id, e.type, e.content, e.timestamp,
                                  s.project_id, p.name AS project_name, e.content AS snippet
                             FROM events e LEFT JOIN sessions s ON s.id=e.session_id
                             LEFT JOIN projects p ON p.id=s.project_id
                            WHERE e.content LIKE ?"""
            fallback_params: list[Any] = [f"%{query}%"]
            if project_id:
                fallback += " AND s.project_id = ?"
                fallback_params.append(project_id)
            if session_id:
                fallback += " AND e.session_id = ?"
                fallback_params.append(session_id)
            fallback += " ORDER BY e.timestamp DESC LIMIT ?"
            fallback_params.append(limit)
            return self._rows(self.db.execute(fallback, fallback_params))


TOOLS = [
    {"name": "list_projects", "description": "List Consiglio projects and session counts.",
     "inputSchema": {"type": "object", "properties": {
         "limit": {"type": "integer", "minimum": 1, "maximum": 200, "default": 50}}}},
    {"name": "list_sessions", "description": "List recorded sessions, optionally within a project.",
     "inputSchema": {"type": "object", "properties": {
         "project_id": {"type": "string"},
         "limit": {"type": "integer", "minimum": 1, "maximum": 500, "default": 100}}}},
    {"name": "get_conversation", "description": "Read a session's chronological conversation and tool output.",
     "inputSchema": {"type": "object", "required": ["session_id"], "properties": {
         "session_id": {"type": "string"},
         "limit": {"type": "integer", "minimum": 1, "maximum": 5000, "default": 1000}}}},
    {"name": "search_conversations", "description": "Full-text retrieval over retained Consiglio conversations.",
     "inputSchema": {"type": "object", "required": ["query"], "properties": {
         "query": {"type": "string"}, "project_id": {"type": "string"},
         "session_id": {"type": "string"},
         "limit": {"type": "integer", "minimum": 1, "maximum": 200, "default": 20}}}},
]


def bounded(value: Any, default: int, maximum: int) -> int:
    try:
        return max(1, min(int(value), maximum))
    except (TypeError, ValueError):
        return default


def call_tool(store: HistoryStore, name: str, args: dict[str, Any]) -> Any:
    if name == "list_projects":
        return store.list_projects(bounded(args.get("limit"), 50, 200))
    if name == "list_sessions":
        return store.list_sessions(args.get("project_id"), bounded(args.get("limit"), 100, 500))
    if name == "get_conversation":
        return store.conversation(str(args["session_id"]), bounded(args.get("limit"), 1000, 5000))
    if name == "search_conversations":
        return store.search(str(args["query"]), args.get("project_id"),
                            args.get("session_id"), bounded(args.get("limit"), 20, 200))
    raise ValueError(f"Unknown tool: {name}")


def response(request_id: Any, result: Any = None, error: dict[str, Any] | None = None) -> None:
    payload: dict[str, Any] = {"jsonrpc": "2.0", "id": request_id}
    payload["error" if error else "result"] = error if error else result
    sys.stdout.write(json.dumps(payload, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def serve(store: HistoryStore) -> None:
    for line in sys.stdin:
        message: dict[str, Any] = {}
        try:
            message = json.loads(line)
            request_id = message.get("id")
            method = message.get("method")
            if request_id is None:
                continue
            if method == "initialize":
                requested = message.get("params", {}).get("protocolVersion", "2025-06-18")
                response(request_id, {"protocolVersion": requested,
                    "capabilities": {"tools": {"listChanged": False}},
                    "serverInfo": {"name": "consiglio-history", "version": "0.1.0"}})
            elif method == "ping":
                response(request_id, {})
            elif method == "tools/list":
                response(request_id, {"tools": TOOLS})
            elif method == "tools/call":
                params = message.get("params", {})
                result = call_tool(store, params.get("name", ""), params.get("arguments", {}))
                text = json.dumps(result, indent=2, ensure_ascii=False)
                response(request_id, {"content": [{"type": "text", "text": text}],
                                      "structuredContent": {"results": result}, "isError": False})
            else:
                response(request_id, error={"code": -32601, "message": "Method not found"})
        except Exception as exc:  # MCP must return errors without corrupting stdout.
            response(message.get("id") if isinstance(message, dict) else None,
                     error={"code": -32603, "message": str(exc)})


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", type=Path, default=default_database_path())
    args = parser.parse_args()
    if not args.database.exists():
        print(f"Consiglio database not found: {args.database}", file=sys.stderr)
        return 2
    serve(HistoryStore(args.database))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
