// Real end-to-end check of the first-run agent bootstrap: installs Codex
// (npm, into user data) and Open Interpreter (venv, into user data) exactly as
// Consiglio does on this platform, then runs both. Used by CI on Linux, macOS
// and Windows. Usage: node scripts/bootstrap-smoke.mts [userDataDir]
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import { installMissingAgentFrontends, runInstallCommand } from '../src/main/startup-bootstrap.ts';
import { resolveCodexCommand } from '../src/main/platform.ts';
import type { AgentReadiness } from '../src/main/agent-readiness.ts';

const missing = (id: AgentReadiness['id']): AgentReadiness => ({
  id, name: id, installed: false, authenticated: false, configuration: 'required', selectable: false,
  state: 'missing', diagnostic: 'missing', supportTier: 'preview', checkedAt: Date.now(),
});

const userData = process.argv[2] || fs.mkdtempSync(path.join(os.tmpdir(), 'consiglio smoke '));
const env: NodeJS.ProcessEnv = { ...process.env };
delete env.CODEX_BIN;
delete env.OI_BIN;

console.log(`user data: ${userData}`);
const results = await installMissingAgentFrontends({
  readiness: [missing('codex'), missing('open-interpreter')],
  userDataPath: userData,
  env,
  onProgress: update => console.log(`… ${update.message}`),
});
for (const result of results) console.log(`${result.id}: installed=${result.installed} ${result.executable || ''}\n  ${result.diagnostic}`);
if (!results.every(result => result.installed)) process.exit(1);

const inside = (file: string | undefined) => !!file && path.resolve(file).startsWith(path.resolve(userData));
if (!inside(env.CODEX_BIN) || !inside(env.OI_BIN)) {
  console.error(`executables are not inside user data: CODEX_BIN=${env.CODEX_BIN} OI_BIN=${env.OI_BIN}`);
  process.exit(1);
}

const codex = resolveCodexCommand({ env });
if (!codex) { console.error('Consiglio could not resolve the installed Codex'); process.exit(1); }
for (const [name, command, args] of [
  ['codex', codex.executable, [...codex.prefixArgs, '--version']],
  ['interpreter', env.OI_BIN!, ['--version']],
] as const) {
  const run = await runInstallCommand({ command, args: [...args], env, timeoutMs: 180_000 });
  console.log(`${name} --version → exit ${run.exitCode}: ${(run.stdout + run.stderr).trim().split(/\r?\n/).slice(-2).join(' | ')}`);
  if (run.exitCode !== 0) process.exit(1);
}
console.log('bootstrap smoke test passed');
