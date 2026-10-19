import assert from 'node:assert/strict';
import test from 'node:test';

import { encodeFrame, FrameDecoder } from '../src/main/hermes/acp-codec.ts';

test('encodes a request as one newline-terminated JSON line', () => {
  const line = encodeFrame({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { protocolVersion: 1 } });
  assert.equal(line.endsWith('\n'), true);
  assert.deepEqual(JSON.parse(line), {
    jsonrpc: '2.0',
    id: 1,
    method: 'initialize',
    params: { protocolVersion: 1 },
  });
});

test('reassembles a frame split across chunks', () => {
  const decoder = new FrameDecoder();
  assert.deepEqual(decoder.push('{"jsonrpc":"2.0","id":1,"result":{"a":'), []);
  assert.deepEqual(decoder.push('1}}\n'), [{ jsonrpc: '2.0', id: 1, result: { a: 1 } }]);
});

test('ignores blank lines and reports malformed ones as null', () => {
  const decoder = new FrameDecoder();
  assert.deepEqual(decoder.push('\n'), []);
  assert.deepEqual(decoder.push('not json\n'), [null]);
});

test('decodes several frames from one chunk', () => {
  const decoder = new FrameDecoder();
  const frames = decoder.push(
    '{"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1"}}\n'
    + '{"jsonrpc":"2.0","id":7,"result":{"stopReason":"end_turn"}}\n',
  );
  assert.equal(frames.length, 2);
  assert.equal(frames[0]?.method, 'session/update');
  assert.deepEqual(frames[1]?.result, { stopReason: 'end_turn' });
});
