import assert from 'node:assert/strict';
import test from 'node:test';
import { EventEmitter } from 'events';
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

// Mock electron before any adapter imports — adapters use app.getPath('userData') etc.
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const electronMock = {
  app: {
    getPath: (name: string) => {
      const paths: Record<string, string> = {
        'userData': '/tmp/consiglio-test-data',
        'tempDir': '/tmp',
        'homeDir': '/tmp',
        'logsDir': '/tmp/logs',
      };
      return paths[name] || '/tmp';
    },
    on: () => {},
    commandLine: { appendSwitch: () => {} },
  },
  ipcMain: { on: () => {}, handle: () => {} },
  BrowserWindow: class {
    static getAllWindows() { return []; }
    constructor() { this.webContents = { send: () => {} }; }
    isDestroyed() { return false; }
    isMinimized() { return false; }
    restore() {}
    show() {}
    focus() {}
    destroy() {}
  },
  dialog: { showOpenDialog: async () => ({ canceled: true, filePaths: [] }) },
  clipboard: { writeText: () => {} },
  shell: { openExternal: async () => {}, openPath: async () => '' },
  net: { request: () => ({ on: () => {}, destroy: () => {} }) },
  protocol: { registerSchemesAsPrivileged: () => {} },
  safeStorage: { isEncryptionAvailable: () => true, encryptString: () => Buffer.from('encrypted'), decryptString: () => 'decrypted' },
};

// Intercept electron imports by overriding the module loader
const originalResolve = createRequire(import.meta.url).resolve;
const require = createRequire(import.meta.url);

// Store original electron if it exists
let _electronOverride: typeof import('electron') | null = null;

// We need to mock electron at the import level. Since Node's native TS mode
// doesn't support dynamic mocking well, we use a different approach:
// The adapters import 'electron' which resolves to node_modules/electron.
// We'll replace it with our mock by setting up a custom loader hook.

import { register } from 'node:module';
import { pathToFileURL } from 'url';

// Register a custom loader that intercepts electron imports
const loaderPath = path.join(__dirname, 'electron-mock-loader.mjs');
try {
  register(pathToFileURL(loaderPath).href);
} catch { /* loader may already be registered */ }

// Also set up a global mock for any code that accesses electron directly
(globalThis as any).__ELECTRON_MOCK__ = electronMock;

// ─── Shared test helpers ──────────────────────────────────────────────────────

/** Collect events emitted by an adapter during a test. */
function collectEvents(sessionId: string) {
  const events: Array<{ type: string; content: string; session_id: string }> = [];
  const approvals: Array<{ command: string; sessionId: string }> = [];
  const terminalOutputs: string[] = [];

  return {
    emitters: {
      emitEvent(event: { type: string; content: string; session_id: string }) {
        events.push(event);
      },
      emitApproval(approval: { command: string; sessionId: string }) {
        approvals.push(approval);
      },
      emitTerminalOutput(_sessionId: string, data: string) {
        terminalOutputs.push(data);
      },
    },
    events,
    approvals,
    terminalOutputs,
  };
}

/** Create a fake PTY that buffers writes and emits stored data. */
function createFakePty(storedData: string[] = []) {
  const writes: string[] = [];
  let killed = false;
  const emitter = new EventEmitter();

  return {
    pty: {
      write(value: string) { writes.push(value); },
      kill() { killed = true; },
      on(event: string, _cb: (...args: unknown[]) => void) {
        if (event === 'data') {
          for (const chunk of storedData) {
            // Simulate data arriving in chunks
          }
        }
      },
      onData(cb: (data: string) => void) {
        for (const chunk of storedData) {
          cb(chunk);
        }
      },
      onExit(cb: (info: { exitCode: number }) => void) {
        // Don't auto-exit — tests control lifecycle
      },
    } as never,
    writes,
    killed,
    emitter,
  };
}

// ─── CodexAdapter tests ───────────────────────────────────────────────────────

test('CodexAdapter: detectAvailable returns correct info when codex is installed', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error — we only need the detectAvailable method
  const { CodexAdapter } = await import('../src/main/adapters/codex-adapter.ts');

  const adapter = new (CodexAdapter as never)(emitters);
  const info = adapter.detectAvailable();

  assert.equal(info.length, 1);
  assert.equal(info[0].id, 'codex');
  assert.equal(info[0].name, 'Codex CLI');
});

test('CodexAdapter: buildProviderArgs for remote_llamacpp includes model and provider config', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error — we only need the internal method
  const { CodexAdapter } = await import('../src/main/adapters/codex-adapter.ts');

  const adapter = new (CodexAdapter as never)(emitters);
  // Access the private method via any cast
  const buildArgs = (adapter as any).buildProviderArgs.bind(adapter);

  const args = buildArgs({
    repository: '/tmp/test',
    agent: 'codex',
    provider: 'remote_llamacpp',
    model: 'qwen2.5:32b',
    baseUrl: 'http://localhost:8081',
  });

  assert.ok(args.some(a => a.includes('model="qwen2.5:32b"')));
  assert.ok(args.some(a => a.includes('model_provider="remote_llamacpp"')));
  assert.ok(args.some(a => a.includes('base_url="http://localhost:8081/v1"')));
});

test('CodexAdapter: buildProviderArgs for ollama includes model and provider config', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error
  const { CodexAdapter } = await import('../src/main/adapters/codex-adapter.ts');

  const adapter = new (CodexAdapter as never)(emitters);
  const buildArgs = (adapter as any).buildProviderArgs.bind(adapter);

  const args = buildArgs({
    repository: '/tmp/test',
    agent: 'codex',
    provider: 'ollama',
    model: 'llama3.1:8b',
    baseUrl: 'http://localhost:11434',
  });

  assert.ok(args.some(a => a.includes('model="llama3.1:8b"')));
  assert.ok(args.some(a => a.includes('model_provider="ollama"')));
});

test('CodexAdapter: buildProviderArgs for gpt56 includes model flag', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error
  const { CodexAdapter } = await import('../src/main/adapters/codex-adapter.ts');

  const adapter = new (CodexAdapter as never)(emitters);
  const buildArgs = (adapter as any).buildProviderArgs.bind(adapter);

  const args = buildArgs({
    repository: '/tmp/test',
    agent: 'codex',
    provider: 'gpt56',
  });

  assert.ok(args.includes('-m'));
  assert.ok(args.includes('gpt-5.6'));
});

test('CodexAdapter: buildProviderArgs for default provider has no provider-specific args', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error
  const { CodexAdapter } = await import('../src/main/adapters/codex-adapter.ts');

  const adapter = new (CodexAdapter as never)(emitters);
  const buildArgs = (adapter as any).buildProviderArgs.bind(adapter);

  const args = buildArgs({
    repository: '/tmp/test',
    agent: 'codex',
    provider: 'default',
  });

  assert.ok(!args.some(a => a.includes('model_provider=')));
});

test('CodexAdapter: buildProviderArgs for lan provider includes multi_agent and web_search config', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error
  const { CodexAdapter } = await import('../src/main/adapters/codex-adapter.ts');

  const adapter = new (CodexAdapter as never)(emitters);
  const buildArgs = (adapter as any).buildProviderArgs.bind(adapter);

  const args = buildArgs({
    repository: '/tmp/test',
    agent: 'codex',
    provider: 'lan',
    localProviderBehavior: { isolateProfile: true, enableWebSearch: false, enableMultiAgent: true },
  });

  assert.ok(args.some(a => a.includes('features.multi_agent=true')));
  assert.ok(args.some(a => a.includes('web_search="disabled"')));
});

test('CodexAdapter: consumeExecEvents parses agent_message events', async () => {
  const session = 'sess-1';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { CodexAdapter } = await import('../src/main/adapters/codex-adapter.ts');
  const adapter = new (CodexAdapter as never)(collected.emitters);

  // Create a session state manually to test parsing
  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    provider: 'default' as const,
    status: 'running' as const,
    terminalBuffer: '',
    args: [],
    env: {},
    codexCommand: { executable: 'codex', prefixArgs: [] },
    jsonRemainder: '',
    lastStructuredError: undefined,
    processedItemIds: new Set<string>(),
    activePrompt: undefined,
    retryFreshAfterExit: false,
    protocolRetryUsed: false,
    responseBuffer: '',
  };

  // @ts-expect-error
  CodexAdapter.sessions.set(session, state);

  // Feed JSONL events
  const jsonl = [
    '{"type":"thread.started","thread_id":"thread-abc"}',
    '{"type":"item.completed","item":{"id":"msg-1","type":"agent_message","text":"Hello world"}}',
    '{"type":"item.completed","item":{"id":"cmd-1","type":"command_execution","command":"ls -la","aggregated_output":"/tmp/test/image.png"}}',
  ].join('\n') + '\n';

  // @ts-expect-error
  adapter.consumeExecEvents(state, jsonl);

  assert.equal(collected.events.length, 2);
  assert.equal(collected.events[0].type, 'response');
  assert.equal(collected.events[0].content, 'Hello world');
  assert.equal(collected.events[1].type, 'code');
  assert.equal(collected.events[1].content, 'ls -la');
});

test('CodexAdapter: consumeExecEvents handles JSONL split across chunks', async () => {
  const session = 'sess-split';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { CodexAdapter } = await import('../src/main/adapters/codex-adapter.ts');
  const adapter = new (CodexAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    provider: 'default' as const,
    status: 'running' as const,
    terminalBuffer: '',
    args: [],
    env: {},
    codexCommand: { executable: 'codex', prefixArgs: [] },
    jsonRemainder: '',
    lastStructuredError: undefined,
    processedItemIds: new Set<string>(),
    activePrompt: undefined,
    retryFreshAfterExit: false,
    protocolRetryUsed: false,
    responseBuffer: '',
  };

  // @ts-expect-error
  CodexAdapter.sessions.set(session, state);

  // First chunk: incomplete JSON
  // @ts-expect-error
  adapter.consumeExecEvents(state, '{"type":"item.completed","item":{"id":"msg-split","type":"');

  assert.equal(collected.events.length, 0);
  assert.ok(state.jsonRemainder.length > 0);

  // Second chunk: completes the JSON
  const secondChunk = 'agent_message","text":"Split message"}}\n';
  // @ts-expect-error
  adapter.consumeExecEvents(state, secondChunk);

  assert.equal(collected.events.length, 1);
  assert.equal(collected.events[0].content, 'Split message');
});

test('CodexAdapter: consumeExecEvents deduplicates item.completed by id', async () => {
  const session = 'sess-dedup';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { CodexAdapter } = await import('../src/main/adapters/codex-adapter.ts');
  const adapter = new (CodexAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    provider: 'default' as const,
    status: 'running' as const,
    terminalBuffer: '',
    args: [],
    env: {},
    codexCommand: { executable: 'codex', prefixArgs: [] },
    jsonRemainder: '',
    lastStructuredError: undefined,
    processedItemIds: new Set<string>(),
    activePrompt: undefined,
    retryFreshAfterExit: false,
    protocolRetryUsed: false,
    responseBuffer: '',
  };

  // @ts-expect-error
  CodexAdapter.sessions.set(session, state);

  const jsonl = [
    '{"type":"item.completed","item":{"id":"msg-1","type":"agent_message","text":"First"}}',
    '{"type":"item.completed","item":{"id":"msg-1","type":"agent_message","text":"Duplicate"}}',
  ].join('\n') + '\n';

  // @ts-expect-error
  adapter.consumeExecEvents(state, jsonl);

  assert.equal(collected.events.length, 1);
  assert.equal(collected.events[0].content, 'First');
});

test('CodexAdapter: consumeExecEvents records structured errors', async () => {
  const session = 'sess-error';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { CodexAdapter } = await import('../src/main/adapters/codex-adapter.ts');
  const adapter = new (CodexAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    provider: 'default' as const,
    status: 'running' as const,
    terminalBuffer: '',
    args: [],
    env: {},
    codexCommand: { executable: 'codex', prefixArgs: [] },
    jsonRemainder: '',
    lastStructuredError: undefined,
    processedItemIds: new Set<string>(),
    activePrompt: undefined,
    retryFreshAfterExit: false,
    protocolRetryUsed: false,
    responseBuffer: '',
  };

  // @ts-expect-error
  CodexAdapter.sessions.set(session, state);

  const jsonl = '{"type":"error","message":"API key is invalid"}\n';
  // @ts-expect-error
  adapter.consumeExecEvents(state, jsonl);

  assert.equal(collected.events.length, 1);
  assert.equal(collected.events[0].type, 'error');
});

// ─── OpenInterpreterAdapter tests ─────────────────────────────────────────────

test('OpenInterpreterAdapter: detectAvailable returns correct info', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error
  const { OpenInterpreterAdapter } = await import('../src/main/adapters/open-interpreter-adapter.ts');

  const adapter = new (OpenInterpreterAdapter as never)(emitters);
  const info = adapter.detectAvailable();

  assert.equal(info.length, 1);
  assert.equal(info[0].id, 'open-interpreter');
  assert.equal(info[0].name, 'Open Interpreter');
});

test('OpenInterpreterAdapter: parseOutput handles message chunks', async () => {
  const session = 'sess-oi';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { OpenInterpreterAdapter } = await import('../src/main/adapters/open-interpreter-adapter.ts');
  const adapter = new (OpenInterpreterAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    customInstructions: undefined,
    autoRun: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
  };

  // @ts-expect-error
  OpenInterpreterAdapter.sessions.set(session, state);

  const chunks = [
    '{"type":"message","content":"Processing your request"}',
    '{"type":"code","format":"python","content":"print(1)","start":true}',
    '{"type":"code","format":"python","content":"","end":true}',
    '{"type":"console","format":"output","content":"1"}',
  ].join('\n') + '\n';

  // @ts-expect-error
  adapter.parseOutput(state, chunks);

  assert.ok(collected.events.length >= 1);
  const messageEvent = collected.events.find(e => e.type === 'response');
  assert.ok(messageEvent);
  assert.equal(messageEvent!.content, 'Processing your request');
});

test('OpenInterpreterAdapter: parseOutput handles confirmation (approval) chunks', async () => {
  const session = 'sess-oi-approval';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { OpenInterpreterAdapter } = await import('../src/main/adapters/open-interpreter-adapter.ts');
  const adapter = new (OpenInterpreterAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    customInstructions: undefined,
    autoRun: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
  };

  // @ts-expect-error
  OpenInterpreterAdapter.sessions.set(session, state);

  const chunk = '{"type":"confirmation","content":{"format":"shell","content":"rm -rf /tmp/test"}}';
  // @ts-expect-error
  adapter.parseOutput(state, chunk);

  assert.ok(collected.approvals.length >= 1);
});

test('OpenInterpreterAdapter: parseOutput handles image chunks', async () => {
  const session = 'sess-oi-image';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { OpenInterpreterAdapter } = await import('../src/main/adapters/open-interpreter-adapter.ts');
  const adapter = new (OpenInterpreterAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    customInstructions: undefined,
    autoRun: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
  };

  // @ts-expect-error
  OpenInterpreterAdapter.sessions.set(session, state);

  const chunk = '{"type":"image","content":"/tmp/test/output.png"}';
  // @ts-expect-error
  adapter.parseOutput(state, chunk);

  const fileEvent = collected.events.find(e => e.type === 'files');
  assert.ok(fileEvent);
});

// ─── AiderAdapter tests ───────────────────────────────────────────────────────

test('AiderAdapter: detectAvailable returns correct info', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error
  const { AiderAdapter } = await import('../src/main/adapters/aider-adapter.ts');

  const adapter = new (AiderAdapter as never)(emitters);
  const info = adapter.detectAvailable();

  assert.equal(info.length, 1);
  assert.equal(info[0].id, 'aider');
  assert.equal(info[0].name, 'Aider');
});

test('AiderAdapter: parseOutput handles code blocks', async () => {
  const session = 'sess-aider';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { AiderAdapter } = await import('../src/main/adapters/aider-adapter.ts');
  const adapter = new (AiderAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    provider: undefined,
    autoApprove: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
    currentCodeBlock: undefined,
    currentDiff: undefined,
    inCodeBlock: false,
    inDiff: false,
    diffPath: '',
  };

  // @ts-expect-error
  AiderAdapter.sessions.set(session, state);

  const output = 'Here is the code:\n```python\nprint("hello")\n```\nDone.';
  // @ts-expect-error
  adapter.parseOutput(state, output);

  const codeEvent = collected.events.find(e => e.type === 'code');
  assert.ok(codeEvent);
  assert.ok(codeEvent!.content.includes('print("hello")'));
});

test('AiderAdapter: parseOutput handles diff blocks', async () => {
  const session = 'sess-aider-diff';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { AiderAdapter } = await import('../src/main/adapters/aider-adapter.ts');
  const adapter = new (AiderAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    provider: undefined,
    autoApprove: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
    currentCodeBlock: undefined,
    currentDiff: undefined,
    inCodeBlock: false,
    inDiff: false,
    diffPath: '',
  };

  // @ts-expect-error
  AiderAdapter.sessions.set(session, state);

  const output = 'Editing file:\n--- a/src/main.py\n+++ b/src/main.py\n@@ -1 +1 @@\n-print("old")\n+print("new")';
  // @ts-expect-error
  adapter.parseOutput(state, output);

  const diffEvent = collected.events.find(e => e.type === 'code');
  assert.ok(diffEvent);
});

test('AiderAdapter: parseOutput handles confirmation prompts', async () => {
  const session = 'sess-aider-confirm';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { AiderAdapter } = await import('../src/main/adapters/aider-adapter.ts');
  const adapter = new (AiderAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    provider: undefined,
    autoApprove: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
    currentCodeBlock: undefined,
    currentDiff: undefined,
    inCodeBlock: false,
    inDiff: false,
    diffPath: '',
  };

  // @ts-expect-error
  AiderAdapter.sessions.set(session, state);

  const output = 'Apply these changes? (Y/n)';
  // @ts-expect-error
  adapter.parseOutput(state, output);

  assert.ok(collected.approvals.length >= 1);
});

// ─── ClaudeCodeAdapter tests ──────────────────────────────────────────────────

test('ClaudeCodeAdapter: detectAvailable returns correct info', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error
  const { ClaudeCodeAdapter } = await import('../src/main/adapters/claude-code-adapter.ts');

  const adapter = new (ClaudeCodeAdapter as never)(emitters);
  const info = adapter.detectAvailable();

  assert.equal(info.length, 1);
  assert.equal(info[0].id, 'claude-code');
  assert.equal(info[0].name, 'Claude Code');
});

test('ClaudeCodeAdapter: parseOutput handles tool call blocks', async () => {
  const session = 'sess-claude';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { ClaudeCodeAdapter } = await import('../src/main/adapters/claude-code-adapter.ts');
  const adapter = new (ClaudeCodeAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    provider: undefined,
    apiKey: undefined,
    autoApprove: false,
    activePrompt: undefined,
    pendingApproval: undefined,
    currentToolCall: undefined,
    inToolBlock: false,
    toolBlockContent: '',
  };

  // @ts-expect-error
  ClaudeCodeAdapter.sessions.set(session, state);

  const output = 'I will read the file:\n<tool_use>\n<name>read_file</name>\n<path>src/main.py</path>\n</tool_use>';
  // @ts-expect-error
  adapter.parseOutput(state, output);

  const codeEvent = collected.events.find(e => e.type === 'code');
  assert.ok(codeEvent);
});

test('ClaudeCodeAdapter: parseOutput handles terminal tool calls', async () => {
  const session = 'sess-claude-term';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { ClaudeCodeAdapter } = await import('../src/main/adapters/claude-code-adapter.ts');
  const adapter = new (ClaudeCodeAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    provider: undefined,
    apiKey: undefined,
    autoApprove: false,
    activePrompt: undefined,
    pendingApproval: undefined,
    currentToolCall: undefined,
    inToolBlock: false,
    toolBlockContent: '',
  };

  // @ts-expect-error
  ClaudeCodeAdapter.sessions.set(session, state);

  const output = '<tool_use>\n<name>run_in_terminal</name>\n<command>ls -la</command>\n</tool_use>';
  // @ts-expect-error
  adapter.parseOutput(state, output);

  assert.ok(collected.events.length >= 1);
});

// ─── GeminiAdapter tests ──────────────────────────────────────────────────────

test('GeminiAdapter: detectAvailable returns correct info', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error
  const { GeminiAdapter } = await import('../src/main/adapters/gemini-adapter.ts');

  const adapter = new (GeminiAdapter as never)(emitters);
  const info = adapter.detectAvailable();

  assert.equal(info.length, 1);
  assert.equal(info[0].id, 'gemini');
  assert.equal(info[0].name, 'Gemini CLI');
});

test('GeminiAdapter: parseOutput handles code blocks', async () => {
  const session = 'sess-gemini';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { GeminiAdapter } = await import('../src/main/adapters/gemini-adapter.ts');
  const adapter = new (GeminiAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    apiKey: undefined,
    autoApprove: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
    currentCodeBlock: undefined,
    inCodeBlock: false,
  };

  // @ts-expect-error
  GeminiAdapter.sessions.set(session, state);

  const output = 'Here is the solution:\n```javascript\nconst x = 42;\n```\nThat should work.';
  // @ts-expect-error
  adapter.parseOutput(state, output);

  const codeEvent = collected.events.find(e => e.type === 'code');
  assert.ok(codeEvent);
  assert.ok(codeEvent!.content.includes('const x = 42'));
});

test('GeminiAdapter: parseOutput handles confirmation prompts', async () => {
  const session = 'sess-gemini-confirm';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { GeminiAdapter } = await import('../src/main/adapters/gemini-adapter.ts');
  const adapter = new (GeminiAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    apiKey: undefined,
    autoApprove: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
    currentCodeBlock: undefined,
    inCodeBlock: false,
  };

  // @ts-expect-error
  GeminiAdapter.sessions.set(session, state);

  const output = 'Do you want to proceed? (y/n)';
  // @ts-expect-error
  adapter.parseOutput(state, output);

  assert.ok(collected.approvals.length >= 1);
});

// ─── CopilotAdapter tests ─────────────────────────────────────────────────────

test('CopilotAdapter: detectAvailable returns correct info', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error
  const { CopilotAdapter } = await import('../src/main/adapters/copilot-adapter.ts');

  const adapter = new (CopilotAdapter as never)(emitters);
  const info = adapter.detectAvailable();

  assert.equal(info.length, 1);
  assert.equal(info[0].id, 'copilot');
  assert.equal(info[0].name, 'GitHub Copilot CLI');
});

test('CopilotAdapter: parseOutput handles code blocks', async () => {
  const session = 'sess-copilot';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { CopilotAdapter } = await import('../src/main/adapters/copilot-adapter.ts');
  const adapter = new (CopilotAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    apiKey: undefined,
    autoApprove: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
    currentCodeBlock: undefined,
    inCodeBlock: false,
  };

  // @ts-expect-error
  CopilotAdapter.sessions.set(session, state);

  const output = 'I can help with that:\n```python\ndef hello():\n    print("world")\n```';
  // @ts-expect-error
  adapter.parseOutput(state, output);

  const codeEvent = collected.events.find(e => e.type === 'code');
  assert.ok(codeEvent);
  assert.ok(codeEvent!.content.includes('def hello'));
});

test('CopilotAdapter: parseOutput handles confirmation prompts', async () => {
  const session = 'sess-copilot-confirm';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { CopilotAdapter } = await import('../src/main/adapters/copilot-adapter.ts');
  const adapter = new (CopilotAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    apiKey: undefined,
    autoApprove: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
    currentCodeBlock: undefined,
    inCodeBlock: false,
  };

  // @ts-expect-error
  CopilotAdapter.sessions.set(session, state);

  const output = 'Apply this change? (Y/n)';
  // @ts-expect-error
  adapter.parseOutput(state, output);

  assert.ok(collected.approvals.length >= 1);
});

// ─── AmazonQAdapter tests ─────────────────────────────────────────────────────

test('AmazonQAdapter: detectAvailable returns correct info', async () => {
  const { emitters } = collectEvents('sess-test');
  // @ts-expect-error
  const { AmazonQAdapter } = await import('../src/main/adapters/amazon-q-adapter.ts');

  const adapter = new (AmazonQAdapter as never)(emitters);
  const info = adapter.detectAvailable();

  assert.equal(info.length, 1);
  assert.equal(info[0].id, 'amazon-q');
  assert.equal(info[0].name, 'Amazon Q Developer CLI');
});

test('AmazonQAdapter: parseOutput handles code blocks', async () => {
  const session = 'sess-awsq';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { AmazonQAdapter } = await import('../src/main/adapters/amazon-q-adapter.ts');
  const adapter = new (AmazonQAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    apiKey: undefined,
    autoApprove: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
    currentCodeBlock: undefined,
    inCodeBlock: false,
  };

  // @ts-expect-error
  AmazonQAdapter.sessions.set(session, state);

  const output = 'Here is the implementation:\n```go\nfunc Main() {\n    fmt.Println("hello")\n}\n```';
  // @ts-expect-error
  adapter.parseOutput(state, output);

  const codeEvent = collected.events.find(e => e.type === 'code');
  assert.ok(codeEvent);
  assert.ok(codeEvent!.content.includes('func Main'));
});

test('AmazonQAdapter: parseOutput handles confirmation prompts', async () => {
  const session = 'sess-awsq-confirm';
  const collected = collectEvents(session);

  // @ts-expect-error
  const { AmazonQAdapter } = await import('../src/main/adapters/amazon-q-adapter.ts');
  const adapter = new (AmazonQAdapter as never)(collected.emitters);

  const state = {
    id: session,
    pty: null,
    repository: '/tmp/test',
    branch: '',
    model: undefined,
    apiKey: undefined,
    autoApprove: false,
    jsonRemainder: '',
    activePrompt: undefined,
    pendingApproval: undefined,
    currentCodeBlock: undefined,
    inCodeBlock: false,
  };

  // @ts-expect-error
  AmazonQAdapter.sessions.set(session, state);

  const output = 'Proceed with changes? (y/n)';
  // @ts-expect-error
  adapter.parseOutput(state, output);

  assert.ok(collected.approvals.length >= 1);
});

// ─── Adapter registry tests ───────────────────────────────────────────────────

test('getAdapter: returns CodexAdapter for codex agent', async () => {
  const collected = collectEvents('sess-test');
  // @ts-expect-error
  const { getAdapter } = await import('../src/main/adapters/index.ts');

  const adapter = getAdapter('codex', collected.emitters);
  assert.ok(adapter !== undefined);
});

test('getAdapter: throws for unknown agent', async () => {
  const collected = collectEvents('sess-test');
  // @ts-expect-error
  const { getAdapter } = await import('../src/main/adapters/index.ts');

  assert.throws(
    () => getAdapter('unknown-agent' as never, collected.emitters),
    /Unknown agent/,
  );
});
