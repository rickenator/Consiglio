import assert from 'node:assert/strict';
import test from 'node:test';

import {
  mapPermissionRequest,
  mapSessionUpdate,
  optionIdForDecision,
} from '../src/main/hermes/acp-events.ts';

test('maps an agent message chunk to a response event', () => {
  const event = mapSessionUpdate('sess-1', {
    sessionUpdate: 'agent_message_chunk',
    content: { type: 'text', text: 'PONG' },
  });
  assert.equal(event?.type, 'response');
  assert.equal(event?.content, 'PONG');
  assert.equal(event?.session_id, 'sess-1');
});

test('ignores empty chunks and non-transcript updates', () => {
  assert.equal(mapSessionUpdate('sess-1', { sessionUpdate: 'agent_message_chunk', content: { text: '  ' } }), null);
  assert.equal(mapSessionUpdate('sess-1', { sessionUpdate: 'usage_update' }), null);
  assert.equal(mapSessionUpdate('sess-1', { sessionUpdate: 'available_commands_update' }), null);
  // session_info_update carries the session title, which is derived from the
  // prompt already in the transcript.
  assert.equal(mapSessionUpdate('sess-1', { sessionUpdate: 'session_info_update', title: 'Reply with PONG' }), null);
});

test('maps a running tool call to tool_call and a failed one to error', () => {
  const running = mapSessionUpdate('sess-1', { sessionUpdate: 'tool_call', title: 'ls -la', status: 'in_progress' });
  assert.equal(running?.type, 'tool_call');
  assert.equal(running?.content, 'ls -la');

  const failed = mapSessionUpdate('sess-1', { sessionUpdate: 'tool_call_update', title: 'rm -rf build', status: 'failed' });
  assert.equal(failed?.type, 'error');
});

test('maps a permission request to a pending approval carrying ACP option ids', () => {
  const approval = mapPermissionRequest({
    sessionId: 'sess-1',
    toolCall: { title: 'rm -rf build', kind: 'execute' },
    options: [
      { optionId: 'allow_once', name: 'Allow once' },
      { optionId: 'allow_always', name: 'Always allow' },
      { optionId: 'reject_once', name: 'Reject' },
    ],
  }, 'fallback');

  assert.equal(approval.status, 'pending');
  assert.equal(approval.sessionId, 'sess-1');
  assert.equal(approval.command, 'rm -rf build');
  assert.deepEqual(approval.optionIds, ['allow_once', 'allow_always', 'reject_once']);
});

test('chooses the ACP option id matching the decision', () => {
  const options = ['allow_once', 'allow_always', 'reject_once', 'reject_always'];
  assert.equal(optionIdForDecision(options, true), 'allow_once');
  assert.equal(optionIdForDecision(options, false), 'reject_once');
  assert.equal(optionIdForDecision([], true), null);
  assert.equal(optionIdForDecision(['weird_a', 'weird_b'], false), 'weird_b');
});
