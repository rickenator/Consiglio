import assert from 'node:assert/strict';
import test from 'node:test';

import { createReconnectGate, ensureSessionLive } from '../src/main/session-reconnect.ts';
import { AgentApprovalRouter, resolveThroughRouter } from '../src/main/approval-router.ts';
// @ts-expect-error — .ts import for node's type stripping
import { CodexAdapter } from '../src/main/adapters/codex-adapter.ts';

// ─── C1: reconnect keeps the original id and never leaks agents ──────────────

function emitters() {
  return { emitEvent() {}, emitApproval() {}, emitTerminalOutput() {}, emitThreadId() {} };
}

function launchOptions(sessionId: string, threadId?: string) {
  return {
    sessionId,
    repository: '/tmp',
    agent: 'codex',
    provider: 'default',
    // Any existing executable works: launch() records state but never spawns
    // without a prompt. process.execPath exists on every CI platform.
    codexPath: process.execPath,
    codexThreadId: threadId,
  };
}

test('CodexAdapter.launch reuses the caller-owned session id (reconnect)', async () => {
  const adapter = new (CodexAdapter as any)(emitters());
  const session = await adapter.launch(launchOptions('sess_original', 'thread-123'));
  assert.equal(session.sessionId, 'sess_original');
  const state = (CodexAdapter as any).sessions.get('sess_original');
  assert.ok(state, 'state is stored under the original id');
  assert.equal(state.codexThreadId, 'thread-123');
  assert.equal(session.pty, null, 'launch must not spawn a prompt-less codex exec');
  await adapter.stopSession('sess_original');
});

test('CodexAdapter.launch of an already-tracked id kills the previous Codex process', async () => {
  const adapter = new (CodexAdapter as any)(emitters());
  await adapter.launch(launchOptions('sess_dup'));
  const first = (CodexAdapter as any).sessions.get('sess_dup');
  let killed = 0;
  let resolved: string | undefined;
  first.pty = { kill() { killed += 1; } };
  first.responseResolve = (value: string) => { resolved = value; };

  await adapter.launch(launchOptions('sess_dup', 'thread-9'));
  const second = (CodexAdapter as any).sessions.get('sess_dup');
  assert.notEqual(second, first);
  assert.equal(killed, 1, 'old PTY is killed instead of orphaned');
  assert.equal(first.pty, null);
  assert.equal(resolved, '', 'a pending sendPrompt on the old state is settled');
  assert.equal(second.codexThreadId, 'thread-9');
  await adapter.stopSession('sess_dup');
});

test('createReconnectGate: concurrent reconnects of one id share a single attempt', async () => {
  let calls = 0;
  let release!: (value: boolean) => void;
  const gate = createReconnectGate(async () => {
    calls += 1;
    return new Promise<boolean>(resolve => { release = resolve; });
  });
  const a = gate('sess_1');
  const b = gate('sess_1');
  assert.equal(calls, 1);
  release(true);
  assert.deepEqual(await Promise.all([a, b]), [true, true]);
  // A later reconnect (after the first finished) runs again.
  const c = gate('sess_1');
  assert.equal(calls, 2);
  release(false);
  assert.equal(await c, false);
});

test('createReconnectGate: different ids reconnect independently', async () => {
  const seen: string[] = [];
  const gate = createReconnectGate(async id => { seen.push(id); return true; });
  await Promise.all([gate('a'), gate('b')]);
  assert.deepEqual(seen.sort(), ['a', 'b']);
});

test('createReconnectGate: a failed attempt does not wedge the id', async () => {
  let calls = 0;
  const gate = createReconnectGate(async () => {
    calls += 1;
    if (calls === 1) throw new Error('spawn failed');
    return true;
  });
  await assert.rejects(gate('x'));
  assert.equal(await gate('x'), true);
});

test('ensureSessionLive: reconnects a recorded task before prompting (after restart)', async () => {
  const live = new Set<string>();
  const reconnected: string[] = [];
  const deps = {
    isLive: (id: string) => live.has(id),
    hasRecord: (id: string) => id === 'recorded',
    reconnect: async (id: string) => { reconnected.push(id); live.add(id); return true; },
  };
  assert.equal(await ensureSessionLive('recorded', deps), true);
  assert.deepEqual(reconnected, ['recorded']);
  // Already live: no second reconnect.
  assert.equal(await ensureSessionLive('recorded', deps), true);
  assert.deepEqual(reconnected, ['recorded']);
  // Unknown task: nothing to reconnect.
  assert.equal(await ensureSessionLive('unknown', deps), false);
});

test('ensureSessionLive: a reconnect that fails reports not-live', async () => {
  const deps = {
    isLive: () => false,
    hasRecord: () => true,
    reconnect: async () => false,
  };
  assert.equal(await ensureSessionLive('s', deps), false);
});

// ─── M8: approvals reach the adapter that is waiting on them ─────────────────

test('resolveThroughRouter delivers the decision to the waiting adapter', async () => {
  const router = new AgentApprovalRouter();
  const decisions: Array<[string, string, boolean]> = [];
  router.register(
    { id: 'appr-1', sessionId: 's1', command: 'rm -rf build', workingDir: '/tmp', timestamp: 1 } as any,
    { resolveApproval: async (sessionId, approvalId, approved) => { decisions.push([sessionId, approvalId, approved]); return true; } },
  );
  assert.equal(await resolveThroughRouter(router, 'appr-1', true), true);
  assert.deepEqual(decisions, [['s1', 'appr-1', true]]);
  // Single use.
  assert.equal(await resolveThroughRouter(router, 'appr-1', false), undefined);
});

test('resolveThroughRouter returns undefined for ids the router never saw', async () => {
  const router = new AgentApprovalRouter();
  assert.equal(await resolveThroughRouter(router, 'legacy-only', true), undefined);
});
