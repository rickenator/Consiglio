import assert from 'node:assert/strict';
import test from 'node:test';

import {
  findWritableKey,
  readConfig,
  unsetConfig,
  validateConfigValue,
  writeConfig,
  WRITABLE_CONFIG,
  type CommandRunner,
} from '../src/main/hermes/hermes-config.ts';

test('refuses keys outside the whitelist', () => {
  assert.match(String(validateConfigValue('tools.enabled', 'true')), /not a setting/);
  assert.match(String(validateConfigValue('security.approval_mode', 'auto')), /not a setting/);
  assert.equal(findWritableKey('model.default')?.kind, 'string');
});

test('refuses credential-shaped keys before anything else', () => {
  // Even a key that is not on the whitelist gets the credential message, so a
  // secret can never be written by accident or by a future whitelist entry.
  assert.match(String(validateConfigValue('model.api_key', 'sk-live-123')), /credential/);
  assert.match(String(validateConfigValue('messaging.telegram.token', 'abc')), /credential/);
});

test('rejects control characters so a value cannot inject YAML', () => {
  assert.match(String(validateConfigValue('model.default', 'gpt-5.5\ncompression.enabled: false')), /Control characters/);
  assert.match(String(validateConfigValue('model.default', 'model\tname')), /Control characters/);
});

test('validates integers, ratios and booleans per key kind', () => {
  assert.equal(validateConfigValue('model.context_length', '600000'), null);
  assert.match(String(validateConfigValue('model.context_length', '512')), /at least 1000/);
  assert.match(String(validateConfigValue('model.context_length', '600k')), /whole number/);
  assert.equal(validateConfigValue('compression.threshold', '0.85'), null);
  assert.match(String(validateConfigValue('compression.threshold', '1.4')), /at most 1/);
  assert.equal(validateConfigValue('compression.enabled', 'false'), null);
  assert.match(String(validateConfigValue('compression.enabled', 'yes')), /true or false/);
  assert.match(String(validateConfigValue('model.default', '   ')), /value is required/i);
});

test('reads current values through the injected runner', () => {
  const run: CommandRunner = (_command, args) => {
    if (args[args.length - 1] === 'model.context_length') return '600000';
    return 'Config key not set: x';
  };
  const entries = readConfig(run);
  const contextLength = entries.find(entry => entry.key === 'model.context_length');
  assert.equal(contextLength?.value, '600000');
  const maxTurns = entries.find(entry => entry.key === 'agent.max_turns');
  assert.equal(maxTurns?.value, null);
  assert.equal(entries.length, WRITABLE_CONFIG.length);
});

test('writes through the CLI and reports a real before/after', () => {
  const calls: string[][] = [];
  let stored = '0.5';
  const run: CommandRunner = (_command, args) => {
    calls.push(args);
    const verb = args.includes('set') ? 'set' : 'get';
    if (verb === 'get') return stored;
    if (verb === 'set') { stored = args[args.length - 1]; return ''; }
    return '';
  };

  const result = writeConfig('compression.threshold', '0.75', run);
  assert.equal(result.before, '0.5');
  assert.equal(result.after, '0.75');
  assert.ok(calls.some(args => args.includes('set')), 'expected a `hermes config set` call');
});

test('a refused value never reaches the CLI', () => {
  const calls: string[][] = [];
  const run: CommandRunner = (_command, args) => { calls.push(args); return ''; };
  assert.throws(() => writeConfig('model.default', 'bad\nvalue', run), /Control characters/);
  assert.deepEqual(calls, []);
});

test('unset refuses non-whitelisted keys without calling the CLI', () => {
  const calls: string[][] = [];
  const run: CommandRunner = (_command, args) => { calls.push(args); return ''; };
  assert.throws(() => unsetConfig('tools.enabled', run), /not a setting/);
  assert.deepEqual(calls, []);
});
