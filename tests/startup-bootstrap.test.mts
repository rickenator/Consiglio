import assert from 'node:assert/strict';
import test from 'node:test';
import path from 'node:path';

import {
  configureManagedAgentEnvironment,
  installMissingAgentFrontends,
  managedCodexExecutable,
  managedCodexNativeCandidates,
  managedOpenInterpreterExecutable,
  managedOpenInterpreterPython,
  resolveNpmCommand,
  runInstallCommand,
  type InstallCommandRequest,
  type InstallCommandResult,
} from '../src/main/startup-bootstrap.ts';
import type { AgentReadiness } from '../src/main/agent-readiness.ts';

function readiness(id: AgentReadiness['id'], installed: boolean): AgentReadiness {
  return {
    id,
    name: id,
    installed,
    authenticated: installed ? null : false,
    configuration: installed ? 'unknown' : 'required',
    selectable: installed,
    state: installed ? 'ready' : 'missing',
    diagnostic: installed ? 'ready' : 'missing',
    supportTier: id === 'codex' ? 'supported' : 'preview',
    checkedAt: 1,
  };
}

function ok(): InstallCommandResult {
  return { exitCode: 0, stdout: 'ok', stderr: '', timedOut: false };
}

test('managed agent paths are platform-specific', () => {
  assert.equal(managedCodexExecutable('/data', 'linux'), path.posix.join('/data', 'agents', 'codex', 'node_modules', '.bin', 'codex'));
  assert.equal(managedCodexExecutable('C:\\data', 'win32'), path.win32.join('C:\\data', 'agents', 'codex', 'node_modules', '.bin', 'codex.cmd'));
  assert.equal(managedOpenInterpreterExecutable('/data', 'linux'), path.posix.join('/data', 'agents', 'open-interpreter', 'bin', 'interpreter'));
  assert.equal(managedOpenInterpreterExecutable('C:\\data', 'win32'), path.win32.join('C:\\data', 'agents', 'open-interpreter', 'Scripts', 'interpreter.exe'));
});

test('managed executables are added to the environment when present', () => {
  const env: NodeJS.ProcessEnv = {};
  const expected = new Set([
    managedCodexExecutable('/data', 'linux'),
    managedOpenInterpreterExecutable('/data', 'linux'),
  ]);
  configureManagedAgentEnvironment('/data', env, 'linux', candidate => expected.has(candidate));
  assert.equal(env.CONSIGLIO_AGENT_HOME, path.posix.join('/data', 'agents'));
  assert.equal(env.CODEX_BIN, managedCodexExecutable('/data', 'linux'));
  assert.equal(env.OI_BIN, managedOpenInterpreterExecutable('/data', 'linux'));
});

test('stale executable overrides are replaced by managed executables', () => {
  const env: NodeJS.ProcessEnv = { CODEX_BIN: '/missing/codex', OI_BIN: '/missing/interpreter' };
  const expected = new Set([
    managedCodexExecutable('/data', 'linux'),
    managedOpenInterpreterExecutable('/data', 'linux'),
  ]);
  configureManagedAgentEnvironment('/data', env, 'linux', candidate => expected.has(candidate));
  assert.equal(env.CODEX_BIN, managedCodexExecutable('/data', 'linux'));
  assert.equal(env.OI_BIN, managedOpenInterpreterExecutable('/data', 'linux'));
});

// Simulates the installers for one platform. The runner records every call
// and "creates" the files npm/pip would, so the test checks the exact argv
// Consiglio spawns on Linux, macOS and Windows from any host.
function simulatedInstall(platform: NodeJS.Platform, userDataPath: string) {
  const p = platform === 'win32' ? path.win32 : path.posix;
  const nodeDir = platform === 'win32' ? 'C:\\Program Files\\nodejs' : '/usr/local/bin';
  const files = new Set<string>(platform === 'win32'
    ? [p.join(nodeDir, 'node.exe'), p.join(nodeDir, 'node_modules', 'npm', 'bin', 'npm-cli.js')]
    : [p.join(nodeDir, 'npm'), p.join(nodeDir, 'python3')]);
  const calls: InstallCommandRequest[] = [];
  const runner = async (request: InstallCommandRequest): Promise<InstallCommandResult> => {
    calls.push(request);
    const args = request.args;
    if (args.includes('--version')) {
      return { exitCode: 0, stdout: /py|python/i.test(request.command) ? 'Python 3.12.4' : '10.9.0', stderr: '', timedOut: false };
    }
    if (args.includes('@openai/codex')) {
      files.add(managedCodexExecutable(userDataPath, platform));
      if (platform === 'win32') files.add(managedCodexNativeCandidates(userDataPath, platform, 'x64')[0]);
    }
    if (args.includes('venv')) files.add(managedOpenInterpreterPython(userDataPath, platform));
    if (args.includes('open-interpreter')) files.add(managedOpenInterpreterExecutable(userDataPath, platform));
    return ok();
  };
  const env: NodeJS.ProcessEnv = platform === 'win32'
    ? { Path: `C:\\Windows\\system32;${nodeDir}` }
    : { PATH: '/usr/bin:/bin', HOME: '/home/someone' };
  return { calls, runner, env, fileExists: (candidate: string) => files.has(candidate), nodeDir };
}

for (const platform of ['linux', 'darwin', 'win32'] as NodeJS.Platform[]) {
  test(`missing Codex and Open Interpreter are installed in user data (${platform})`, async () => {
    const userDataPath = platform === 'win32' ? 'C:\\Users\\Someone\\AppData\\Roaming\\Consiglio' : '/home/someone/.config/Consiglio';
    const sim = simulatedInstall(platform, userDataPath);
    const progress: string[] = [];
    const result = await installMissingAgentFrontends({
      readiness: [readiness('codex', false), readiness('open-interpreter', false)],
      userDataPath,
      env: sim.env,
      platform,
      runner: sim.runner,
      fileExists: sim.fileExists,
      onProgress: update => progress.push(update.phase),
    });

    assert.deepEqual(result.map(entry => [entry.id, entry.installed]), [['codex', true], ['open-interpreter', true]]);
    assert.deepEqual(progress, ['installing-codex', 'installing-open-interpreter']);

    // Nothing is ever spawned through a shell or as a .cmd/.bat shim.
    for (const call of sim.calls) {
      assert.ok(!/(^|[\\/])(sh|bash|cmd(\.exe)?)$/i.test(call.command), `shell spawned: ${call.command}`);
      assert.ok(!/\.(cmd|bat)$/i.test(call.command), `shim spawned: ${call.command}`);
      assert.ok(!call.args.includes('-g') && !call.args.includes('--global'), 'no global install');
      assert.ok(!call.args.some(arg => /curl|\| *sh/.test(arg)), 'no piped installer');
    }

    // Codex: a local npm install into <userData>/agents/codex on every platform.
    const codexInstall = sim.calls.find(call => call.args.includes('@openai/codex'));
    assert.ok(codexInstall);
    const p = platform === 'win32' ? path.win32 : path.posix;
    if (platform === 'win32') {
      assert.equal(codexInstall.command, p.join(sim.nodeDir, 'node.exe'));
      assert.equal(codexInstall.args[0], p.join(sim.nodeDir, 'node_modules', 'npm', 'bin', 'npm-cli.js'));
    } else {
      assert.equal(codexInstall.command, '/usr/local/bin/npm');
      // npm's `#!/usr/bin/env node` must resolve even from a Finder-launched app.
      assert.ok(String(codexInstall.env.PATH).split(':').includes('/usr/local/bin'));
    }
    const prefixAt = codexInstall.args.indexOf('--prefix');
    assert.equal(codexInstall.args[prefixAt + 1], p.join(userDataPath, 'agents', 'codex'));

    // Open Interpreter: a venv in <userData>/agents/open-interpreter, then pip inside it.
    const venv = sim.calls.find(call => call.args.includes('venv'));
    assert.ok(venv);
    assert.equal(venv.args[venv.args.length - 1], p.join(userDataPath, 'agents', 'open-interpreter'));
    if (platform === 'win32') assert.deepEqual([venv.command, venv.args[0]], ['py', '-3.12']);
    const pip = sim.calls.find(call => call.args.includes('open-interpreter'));
    assert.ok(pip);
    assert.equal(pip.command, managedOpenInterpreterPython(userDataPath, platform));
    assert.deepEqual(pip.args.slice(0, 3), ['-m', 'pip', 'install']);

    // Windows runs the native codex.exe (argv-only), not the codex.cmd shim.
    assert.equal(sim.env.CODEX_BIN, platform === 'win32'
      ? managedCodexNativeCandidates(userDataPath, platform, 'x64')[0]
      : managedCodexExecutable(userDataPath, platform));
    assert.equal(sim.env.OI_BIN, managedOpenInterpreterExecutable(userDataPath, platform));
  });
}

test('Windows npm runs as node.exe + npm-cli.js, or not at all', () => {
  const exists = (files: string[]) => (candidate: string) => files.includes(candidate);
  const dir = 'C:\\Program Files\\nodejs';
  const node = path.win32.join(dir, 'node.exe');
  const cli = path.win32.join(dir, 'node_modules', 'npm', 'bin', 'npm-cli.js');
  assert.deepEqual(resolveNpmCommand({ PATH: `"${dir}";C:\\Windows` }, 'win32', exists([node, cli])), { command: node, prefix: [cli] });
  // Only npm.cmd available: refuse rather than go through cmd.exe.
  assert.equal(resolveNpmCommand({ PATH: dir }, 'win32', exists([path.win32.join(dir, 'npm.cmd')])), null);
});

test('missing npm and Python are reported, not worked around', async () => {
  const calls: InstallCommandRequest[] = [];
  const result = await installMissingAgentFrontends({
    readiness: [readiness('codex', false), readiness('open-interpreter', false)],
    userDataPath: 'C:\\data',
    env: { Path: 'C:\\Windows' },
    platform: 'win32',
    runner: async request => { calls.push(request); return { exitCode: 9009, stdout: '', stderr: '', timedOut: false }; },
    fileExists: () => false,
  });
  assert.deepEqual(result.map(entry => entry.installed), [false, false]);
  assert.match(result[0].diagnostic, /npm is unavailable/);
  assert.match(result[1].diagnostic, /Python 3 is unavailable/);
  assert.ok(calls.every(call => !call.args.includes('install')));
});

test('a Python that Open Interpreter supports is preferred over a newer one', async () => {
  const versions: Record<string, string> = { '/usr/bin/python3': 'Python 3.13.5', '/usr/bin/python3.11': 'Python 3.11.9' };
  const calls: InstallCommandRequest[] = [];
  await installMissingAgentFrontends({
    readiness: [readiness('codex', true), readiness('open-interpreter', false)],
    userDataPath: '/data',
    env: { PATH: '/usr/bin' },
    platform: 'darwin',
    runner: async request => {
      calls.push(request);
      if (request.args.includes('--version')) {
        const out = versions[request.command];
        return { exitCode: out ? 0 : 127, stdout: out || '', stderr: '', timedOut: false };
      }
      return ok();
    },
    fileExists: candidate => candidate in versions,
  });
  const venv = calls.find(call => call.args.includes('venv'));
  assert.equal(venv?.command, '/usr/bin/python3.11');
});

test('a valid explicit CODEX_BIN is kept and PATH is not duplicated', () => {
  const managed = managedCodexExecutable('/data', 'linux');
  const env: NodeJS.ProcessEnv = { CODEX_BIN: '/opt/codex/bin/codex', PATH: '/usr/bin' };
  const files = new Set([managed, '/opt/codex/bin/codex']);
  configureManagedAgentEnvironment('/data', env, 'linux', candidate => files.has(candidate));
  configureManagedAgentEnvironment('/data', env, 'linux', candidate => files.has(candidate));
  assert.equal(env.CODEX_BIN, '/opt/codex/bin/codex');
  assert.equal(env.PATH, `${path.posix.dirname(managed)}:/usr/bin`);
});

test('install commands are spawned argv-only (no shell parsing)', async () => {
  const hostile = 'a b & echo INJECTED | "quoted" $(whoami) %PATH% ;';
  const result = await runInstallCommand({
    command: process.execPath,
    args: ['-e', 'process.stdout.write(JSON.stringify(process.argv.slice(1)))', hostile],
    env: process.env,
    timeoutMs: 30_000,
  });
  assert.equal(result.exitCode, 0);
  assert.deepEqual(JSON.parse(result.stdout), [hostile]);
});

test('installed front ends are not reinstalled', async () => {
  let calls = 0;
  const result = await installMissingAgentFrontends({
    readiness: [readiness('codex', true), readiness('open-interpreter', true)],
    userDataPath: '/data',
    platform: 'linux',
    runner: async () => { calls += 1; return ok(); },
    fileExists: () => false,
  });
  assert.equal(calls, 0);
  assert.ok(result.every(entry => entry.installed && !entry.attempted));
});
