#!/usr/bin/env python3

import json
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SERVER_PATH = Path(sys.argv[1])


class TestConsiglioHistoryMcp(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.database = Path(self.temp.name) / "consiglio.sqlite"
        db = sqlite3.connect(self.database)
        db.executescript("""
            CREATE TABLE projects(id TEXT PRIMARY KEY, name TEXT, workspace TEXT,
                created_at INTEGER, last_activity INTEGER);
            CREATE TABLE sessions(id TEXT PRIMARY KEY, project_id TEXT, provider TEXT,
                status TEXT, repository TEXT, branch TEXT, permission_mode TEXT,
                preferred_name TEXT, started_at INTEGER, last_activity INTEGER);
            CREATE TABLE events(id INTEGER PRIMARY KEY AUTOINCREMENT, session_id TEXT,
                type INTEGER, content TEXT, timestamp INTEGER, command TEXT, working_dir TEXT);
            CREATE VIRTUAL TABLE events_fts USING fts5(content, command,
                content='events', content_rowid='id');
            INSERT INTO projects VALUES('p1', 'Consiglio', '/tmp/Consiglio', 10, 30);
            INSERT INTO sessions VALUES('s1', 'p1', 'codex', 'stopped', '/tmp/Consiglio',
                '', 'workspace-write', 'Dude', 10, 30);
            INSERT INTO events(session_id, type, content, timestamp, command, working_dir)
                VALUES('s1', 1, 'How should conversations be retained?', 20, '', ''),
                      ('s1', 2, 'Store conversations in SQLite with full-text retrieval.', 30, '', '');
            INSERT INTO events_fts(events_fts) VALUES('rebuild');
        """)
        db.commit()
        db.close()
        self.process = subprocess.Popen(
            [sys.executable, str(SERVER_PATH), "--database", str(self.database)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, bufsize=1)

    def tearDown(self):
        self.process.terminate()
        self.process.communicate(timeout=5)
        self.temp.cleanup()

    def request(self, request_id, method, params=None):
        payload = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            payload["params"] = params
        self.process.stdin.write(json.dumps(payload) + "\n")
        self.process.stdin.flush()
        return json.loads(self.process.stdout.readline())

    def test_tools_and_retrieval(self):
        initialized = self.request(1, "initialize", {"protocolVersion": "2025-06-18"})
        self.assertEqual(initialized["result"]["serverInfo"]["name"], "consiglio-history")
        tools = self.request(2, "tools/list")["result"]["tools"]
        self.assertIn("search_conversations", {tool["name"] for tool in tools})

        result = self.request(3, "tools/call", {"name": "search_conversations",
            "arguments": {"query": "full-text retrieval"}})["result"]
        self.assertFalse(result["isError"])
        self.assertEqual(result["structuredContent"]["results"][0]["session_id"], "s1")

        conversation = self.request(4, "tools/call", {"name": "get_conversation",
            "arguments": {"session_id": "s1"}})["result"]["structuredContent"]["results"]
        self.assertEqual(len(conversation), 2)
        self.assertTrue(conversation[0]["content"].startswith("How should"))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
