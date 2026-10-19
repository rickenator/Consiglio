import assert from 'node:assert/strict';
import test from 'node:test';

import { AcpClient, AcpUnavailableError, resolveResumedSessionId, type Transport } from '../src/main/hermes/acp-client.ts';

class FakeTransport implements Transport {
  writes: string[] = [];
  private dataCb?: (chunk: string) => void;
  private exitCb?: (code: number) => void;

  write(line: string): void { this.writes.push(line); }
  onData(cb: (chunk: string) => void): void { this.dataCb = cb; }
  onExit(cb: (code: number) => void): void { this.exitCb = cb; }
  kill(): void { this.exitCb?.(0); }
  emit(frame: unknown): void { this.dataCb?.(`${JSON.stringify(frame)}\n`); }
  lastRequest(): { id: number; method: string; params: unknown } {
    return JSON.parse(this.writes[this.writes.length - 1]);
  }
}

test('initialize resolves with the negotiated protocol version', async () => {
  const transport = new FakeTransport();
  const client = new AcpClient(transport);

  const initializing = client.initialize();
  const sent = transport.lastRequest();
  assert.equal(sent.method, 'initialize');
  assert.equal((sent.params as { protocolVersion: number }).protocolVersion, 1);

  transport.emit({ jsonrpc: '2.0', id: sent.id, result: { protocolVersion: 1, agentCapabilities: { loadSession: true } } });
  const result = await initializing;
  assert.equal(result.protocolVersion, 1);
  assert.equal(result.agentCapabilities.loadSession, true);
});

test('initialize rejects loudly on a protocol version mismatch', async () => {
  const transport = new FakeTransport();
  const client = new AcpClient(transport);
  const initializing = client.initialize();
  transport.emit({ jsonrpc: '2.0', id: transport.lastRequest().id, result: { protocolVersion: 9 } });
  await assert.rejects(initializing, /Unsupported ACP protocol version 9/);
});

test('a JSON-RPC error rejects the pending request', async () => {
  const transport = new FakeTransport();
  const client = new AcpClient(transport);
  const creating = client.newSession({ cwd: '/tmp' });
  transport.emit({ jsonrpc: '2.0', id: transport.lastRequest().id, error: { code: -32000, message: 'no sessions left' } });
  await assert.rejects(creating, /no sessions left/);
});

test('initialize rejects when the child exits first', async () => {
  const transport = new FakeTransport();
  const client = new AcpClient(transport);
  const initializing = client.initialize();
  transport.kill();
  await assert.rejects(initializing, /exited with code 0/);
});

test('session/new sends the workspace and returns the Hermes session id', async () => {
  const transport = new FakeTransport();
  const client = new AcpClient(transport);

  const created = client.newSession({ cwd: '/home/rick/Projects/Consiglio' });
  const sent = transport.lastRequest();
  assert.equal(sent.method, 'session/new');
  assert.deepEqual(sent.params, { cwd: '/home/rick/Projects/Consiglio', mcpServers: [] });

  transport.emit({
    jsonrpc: '2.0',
    id: sent.id,
    result: {
      sessionId: 'local-abc123',
      models: { availableModels: [{ modelId: 'custom:lefty:deepseek' }], currentModelId: 'custom:lefty:deepseek' },
    },
  });
  const result = await created;
  assert.equal(result.sessionId, 'local-abc123');
  assert.equal(result.models?.currentModelId, 'custom:lefty:deepseek');
});

test('prompt sends a text content block and resolves on the turn result', async () => {
  const transport = new FakeTransport();
  const client = new AcpClient(transport);

  const turn = client.prompt('local-abc123', 'Reply with PONG');
  const sent = transport.lastRequest();
  assert.equal(sent.method, 'session/prompt');
  assert.deepEqual(sent.params, {
    sessionId: 'local-abc123',
    prompt: [{ type: 'text', text: 'Reply with PONG' }],
  });

  transport.emit({ jsonrpc: '2.0', id: sent.id, result: { stopReason: 'end_turn', usage: { outputTokens: 4 } } });
  const result = await turn;
  assert.equal(result.stopReason, 'end_turn');
});

test('notifications reach the notification handler', () => {
  const transport = new FakeTransport();
  const client = new AcpClient(transport);
  const seen: string[] = [];
  client.setNotificationHandler(frame => seen.push(String(frame.method)));

  transport.emit({ jsonrpc: '2.0', method: 'session/update', params: { sessionId: 's1', update: { sessionUpdate: 'agent_message_chunk' } } });
  assert.deepEqual(seen, ['session/update']);
});

test('an agent request is answered through the request handler', async () => {
  const transport = new FakeTransport();
  const client = new AcpClient(transport);
  client.setRequestHandler(async frame => {
    assert.equal(frame.method, 'session/request_permission');
    return { outcome: { outcome: 'selected', optionId: 'allow_once' } };
  });

  transport.emit({ jsonrpc: '2.0', id: 77, method: 'session/request_permission', params: { sessionId: 's1' } });
  await new Promise(resolve => setTimeout(resolve, 5));
  const reply = JSON.parse(transport.writes[transport.writes.length - 1]);
  assert.deepEqual(reply, { jsonrpc: '2.0', id: 77, result: { outcome: { outcome: 'selected', optionId: 'allow_once' } } });
});

test('a failing request handler denies instead of hanging the agent', async () => {
  const transport = new FakeTransport();
  const client = new AcpClient(transport);
  client.setRequestHandler(async () => { throw new Error('boom'); });

  transport.emit({ jsonrpc: '2.0', id: 78, method: 'session/request_permission', params: {} });
  await new Promise(resolve => setTimeout(resolve, 5));
  const reply = JSON.parse(transport.writes[transport.writes.length - 1]);
  assert.deepEqual(reply.result, { outcome: { outcome: 'cancelled' } });
});

test('requests after exit fail fast with a typed error', async () => {
  const transport = new FakeTransport();
  const client = new AcpClient(transport);
  transport.kill();
  await assert.rejects(client.listSessions(), AcpUnavailableError);
});

test('a resumed session keeps the requested id when Hermes returns provenance', () => {
  // `session/resume` answers with provenance for the same session, not a new id.
  const resumed = {
    _meta: { hermes: { sessionProvenance: { acpSessionId: 'local-abc', currentHermesSessionId: 'local-abc' } } },
  };
  assert.equal(resolveResumedSessionId(resumed as never, 'local-abc'), 'local-abc');

  // A fork reports a different current session for the same requested root.
  const forked = { _meta: { hermes: { sessionProvenance: { currentHermesSessionId: 'local-fork' } } } };
  assert.equal(resolveResumedSessionId(forked as never, 'local-root'), 'local-fork');

  // Nothing reported at all: fall back to what the caller asked for.
  assert.equal(resolveResumedSessionId(undefined, 'local-requested'), 'local-requested');
  assert.equal(resolveResumedSessionId({ sessionId: 'local-new' } as never, 'local-requested'), 'local-new');
});
