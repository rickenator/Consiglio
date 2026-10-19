import assert from 'node:assert/strict';
import test from 'node:test';

import { applyHermesUpdate, parseUpdateCheck } from '../src/main/hermes/hermes-update.ts';

// Captured verbatim from this machine: `hermes update --check`.
const UP_TO_DATE_OUTPUT = [
  '→ Update channel: stable',
  '✓ No newer release: stable v0.22.2; this checkout is newer and waits for the next release (`hermes update --set-channel main` follows every commit).',
].join('\n');

test('parses the real up-to-date output', () => {
  const status = parseUpdateCheck(UP_TO_DATE_OUTPUT, '0.22.2+1868.g5480918');
  assert.equal(status.state, 'up-to-date');
  assert.equal(status.channel, 'stable');
  assert.equal(status.currentVersion, '0.22.2+1868.g5480918');
  assert.match(status.summary, /No newer release/);
});

test('detects an offered update and normalises its version', () => {
  const status = parseUpdateCheck('→ Update channel: stable\nAn update is available: v0.23.0 (installed v0.22.2).', '0.22.2');
  assert.equal(status.state, 'available');
  assert.equal(status.availableVersion, '0.23.0');
});

test('reports unknown rather than guessing when the output is unrecognised', () => {
  const status = parseUpdateCheck('something entirely unexpected', '0.22.2');
  assert.equal(status.state, 'unknown');
  assert.equal(status.currentVersion, '0.22.2');
});

test('empty output is unknown, not up-to-date', () => {
  assert.equal(parseUpdateCheck('', '0.22.2').state, 'unknown');
});

test('applyHermesUpdate forwards the gateway-restart decision to the CLI', () => {
  const calls: string[][] = [];
  const run = (_command: string, args: string[]) => { calls.push(args); return 'done'; };

  const idle = applyHermesUpdate({ restartGateway: false, run });
  assert.equal(idle.ok, true);
  assert.ok(calls[0].includes('--no-gateway-restart'), `expected --no-gateway-restart in ${JSON.stringify(calls[0])}`);

  const restarted = applyHermesUpdate({ restartGateway: true, run });
  assert.equal(restarted.ok, true);
  assert.equal(calls[1].includes('--no-gateway-restart'), false);
});

test('applyHermesUpdate reports failure instead of throwing', () => {
  const run = () => { throw new Error('git pull failed'); };
  const result = applyHermesUpdate({ run });
  assert.equal(result.ok, false);
  assert.match(result.output, /git pull failed/);
});
