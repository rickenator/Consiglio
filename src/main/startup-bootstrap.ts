import { spawn } from 'child_process';
import fs from 'fs';
import path from 'path';

import type { AgentReadiness } from './agent-readiness.ts';

export type BootstrapPhase =
  | 'idle'
  | 'discovering-local'
  | 'configuring-local'
  | 'installing-codex'
  | 'installing-open-interpreter'
  | 'refreshing'
  | 'complete';

export interface BootstrapProgress {
  phase: BootstrapPhase;
  message: string;
  active: boolean;
  completed: number;
  total: number;
  updatedAt: number;
}

export interface AgentInstallResult {
  id: AgentReadiness['id'];
  attempted: boolean;
  installed: boolean;
  executable?: string;
  diagnostic: string;
}

export interface InstallCommandRequest {
  command: string;
  args: string[];
  env: NodeJS.ProcessEnv;
  timeoutMs: number;
}

export interface InstallCommandResult {
  exitCode: number | null;
  stdout: string;
  stderr: string;
  errorCode?: string;
  timedOut: boolean;
}

export type InstallCommandRunner = (request: InstallCommandRequest) => Promise<InstallCommandResult>;

export interface InstallMissingAgentOptions {
  readiness: AgentReadiness[];
  userDataPath: string;
  env?: NodeJS.ProcessEnv;
  platform?: NodeJS.Platform;
  runner?: InstallCommandRunner;
  fileExists?: (candidate: string) => boolean;
  onProgress?: (progress: BootstrapProgress) => void;
}

const INSTALL_TIMEOUT_MS = 10 * 60 * 1000;
const OUTPUT_LIMIT = 128 * 1024;

function appendLimited(current: string, chunk: Buffer | string) {
  return (current + chunk.toString()).slice(-OUTPUT_LIMIT);
}

export const runInstallCommand: InstallCommandRunner = request => new Promise(resolve => {
  let stdout = '';
  let stderr = '';
  let settled = false;
  let timedOut = false;
  let timer: NodeJS.Timeout | undefined;

  const finish = (result: InstallCommandResult) => {
    if (settled) return;
    settled = true;
    if (timer) clearTimeout(timer);
    resolve(result);
  };

  let child: ReturnType<typeof spawn>;
  try {
    // argv-only: never a shell, so no argument is ever parsed as a command line.
    child = spawn(request.command, request.args, {
      env: request.env,
      shell: false,
      windowsHide: true,
      stdio: ['ignore', 'pipe', 'pipe'],
    });
  } catch (error) {
    const code = error instanceof Error && 'code' in error ? String(error.code) : undefined;
    resolve({ exitCode: null, stdout, stderr, errorCode: code, timedOut: false });
    return;
  }

  timer = setTimeout(() => {
    timedOut = true;
    try { child.kill(); } catch { /* best effort */ }
    finish({ exitCode: null, stdout, stderr, timedOut: true });
  }, request.timeoutMs);

  child.stdout?.on('data', chunk => { stdout = appendLimited(stdout, chunk); });
  child.stderr?.on('data', chunk => { stderr = appendLimited(stderr, chunk); });
  child.on('error', error => {
    const code = 'code' in error ? String(error.code) : undefined;
    finish({ exitCode: null, stdout, stderr, errorCode: code, timedOut });
  });
  child.on('close', exitCode => finish({ exitCode, stdout, stderr, timedOut }));
});

function commandOutput(result: InstallCommandResult) {
  return `${result.stdout}\n${result.stderr}`
    .split(/\r?\n/)
    .map(line => line.trim())
    .filter(Boolean)
    .slice(-6)
    .join(' ')
    .slice(0, 700);
}

function executableExists(candidate: string) {
  try {
    fs.accessSync(candidate, process.platform === 'win32' ? fs.constants.F_OK : fs.constants.X_OK);
    return fs.statSync(candidate).isFile();
  } catch {
    return false;
  }
}

function pathForPlatform(platform: NodeJS.Platform) {
  return platform === 'win32' ? path.win32 : path.posix;
}

export function managedAgentHome(userDataPath: string, platform: NodeJS.Platform = process.platform) {
  return pathForPlatform(platform).join(userDataPath, 'agents');
}

export function managedCodexPrefix(userDataPath: string, platform: NodeJS.Platform = process.platform) {
  return pathForPlatform(platform).join(managedAgentHome(userDataPath, platform), 'codex');
}

export function managedCodexExecutable(userDataPath: string, platform: NodeJS.Platform = process.platform) {
  const platformPath = pathForPlatform(platform);
  const base = platformPath.join(managedCodexPrefix(userDataPath, platform), 'node_modules', '.bin');
  return platformPath.join(base, platform === 'win32' ? 'codex.cmd' : 'codex');
}

// On Windows, npm's codex.cmd shim can only run through cmd.exe. The package's
// native codex.exe (an optional per-platform dependency) runs directly, so
// prefer it: prompts are then passed argv-only, never through a shell.
export function managedCodexNativeCandidates(
  userDataPath: string,
  platform: NodeJS.Platform = process.platform,
  arch: string = process.arch,
) {
  if (platform !== 'win32') return [];
  const win = path.win32;
  const modules = win.join(managedCodexPrefix(userDataPath, platform), 'node_modules', '@openai');
  const targets = [
    { pkg: 'codex-win32-x64', triple: 'x86_64-pc-windows-msvc', arch: 'x64' },
    { pkg: 'codex-win32-arm64', triple: 'aarch64-pc-windows-msvc', arch: 'arm64' },
  ].sort((a, b) => Number(b.arch === arch) - Number(a.arch === arch));
  return targets.flatMap(target => [
    win.join(modules, target.pkg, 'vendor', target.triple, 'bin', 'codex.exe'),
    win.join(modules, 'codex', 'vendor', target.triple, 'bin', 'codex.exe'),
  ]);
}

function managedCodexLaunchExecutable(
  userDataPath: string,
  platform: NodeJS.Platform,
  fileExists: (candidate: string) => boolean,
) {
  return managedCodexNativeCandidates(userDataPath, platform).find(fileExists) || managedCodexExecutable(userDataPath, platform);
}

export function managedOpenInterpreterVenv(userDataPath: string, platform: NodeJS.Platform = process.platform) {
  return pathForPlatform(platform).join(managedAgentHome(userDataPath, platform), 'open-interpreter');
}

export function managedOpenInterpreterPython(userDataPath: string, platform: NodeJS.Platform = process.platform) {
  const platformPath = pathForPlatform(platform);
  const venv = managedOpenInterpreterVenv(userDataPath, platform);
  return platform === 'win32'
    ? platformPath.join(venv, 'Scripts', 'python.exe')
    : platformPath.join(venv, 'bin', 'python');
}

export function managedOpenInterpreterExecutable(userDataPath: string, platform: NodeJS.Platform = process.platform) {
  const platformPath = pathForPlatform(platform);
  const base = platformPath.join(managedOpenInterpreterVenv(userDataPath, platform), platform === 'win32' ? 'Scripts' : 'bin');
  return platformPath.join(base, platform === 'win32' ? 'interpreter.exe' : 'interpreter');
}

function prependPath(env: NodeJS.ProcessEnv, directory: string, platform: NodeJS.Platform) {
  const delimiter = pathForPlatform(platform).delimiter;
  // Windows environment keys are case-insensitive; keep whichever spelling is present.
  const key = platform === 'win32' ? (Object.keys(env).find(name => name.toLowerCase() === 'path') || 'Path') : 'PATH';
  const current = (env[key] || '').split(delimiter).filter(Boolean);
  const same = (a: string, b: string) => (platform === 'win32' ? a.toLowerCase() === b.toLowerCase() : a === b);
  if (current.some(entry => same(entry, directory))) return;
  env[key] = [directory, ...current].join(delimiter);
}

export function configureManagedAgentEnvironment(
  userDataPath: string,
  env: NodeJS.ProcessEnv = process.env,
  platform: NodeJS.Platform = process.platform,
  fileExists: (candidate: string) => boolean = executableExists,
) {
  const home = managedAgentHome(userDataPath, platform);
  env.CONSIGLIO_AGENT_HOME = home;
  const platformPath = pathForPlatform(platform);
  const codex = managedCodexExecutable(userDataPath, platform);
  const interpreter = managedOpenInterpreterExecutable(userDataPath, platform);
  // An explicit override that still points at a real file wins; a stale one
  // is replaced by the managed install.
  const configuredCodex = env.CODEX_BIN?.trim();
  const configuredInterpreter = env.OI_BIN?.trim();
  if (fileExists(codex)) {
    if (!configuredCodex || !fileExists(configuredCodex)) env.CODEX_BIN = managedCodexLaunchExecutable(userDataPath, platform, fileExists);
    prependPath(env, platformPath.dirname(codex), platform);
  }
  if (fileExists(interpreter)) {
    if (!configuredInterpreter || !fileExists(configuredInterpreter)) env.OI_BIN = interpreter;
    prependPath(env, platformPath.dirname(interpreter), platform);
  }
  return { home, codex, interpreter };
}

function progress(phase: BootstrapPhase, message: string, completed: number, total: number): BootstrapProgress {
  return { phase, message, active: true, completed, total, updatedAt: Date.now() };
}

function pathEntries(env: NodeJS.ProcessEnv, platform: NodeJS.Platform) {
  const key = platform === 'win32' ? Object.keys(env).find(name => name.toLowerCase() === 'path') : 'PATH';
  return ((key && env[key]) || '').split(pathForPlatform(platform).delimiter).map(entry => entry.trim().replace(/^"(.*)"$/, '$1')).filter(Boolean);
}

export interface ResolvedCommand {
  command: string;
  prefix: string[];
}

// How to run npm without a shell.
//
// POSIX: `npm` is an executable script, so it is spawned directly.
// Windows: `npm` is `npm.cmd`, and Node refuses to spawn .cmd/.bat files
// without a shell (CVE-2024-27980). Rather than going through cmd.exe, run
// npm's own entry point with the node.exe that ships next to it:
//   node.exe <dir>\node_modules\npm\bin\npm-cli.js …
// Every argument stays a separate argv entry; nothing is parsed by a shell.
export function resolveNpmCommand(
  env: NodeJS.ProcessEnv,
  platform: NodeJS.Platform,
  fileExists: (candidate: string) => boolean = executableExists,
): ResolvedCommand | null {
  if (platform !== 'win32') {
    const found = posixToolDirectories(env, platform).map(directory => path.posix.join(directory, 'npm')).find(fileExists);
    return { command: found || 'npm', prefix: [] };
  }
  const win = path.win32;
  for (const directory of pathEntries(env, platform)) {
    const node = win.join(directory, 'node.exe');
    const npmCli = win.join(directory, 'node_modules', 'npm', 'bin', 'npm-cli.js');
    if (fileExists(node) && fileExists(npmCli)) return { command: node, prefix: [npmCli] };
  }
  return null;
}

// Apps started from the macOS Finder (or a Linux desktop launcher) get a
// minimal PATH without Homebrew or /usr/local, so look there as well.
function posixToolDirectories(env: NodeJS.ProcessEnv, platform: NodeJS.Platform) {
  const extra = platform === 'darwin'
    ? ['/opt/homebrew/bin', '/usr/local/bin', '/opt/local/bin', '/usr/bin']
    : ['/usr/local/bin', '/usr/bin'];
  const home = env.HOME?.trim();
  if (home) extra.push(path.posix.join(home, '.volta', 'bin'), path.posix.join(home, '.local', 'bin'));
  return [...new Set([...pathEntries(env, platform), ...extra])];
}

// The child's environment: the tool's own directory first on PATH, so
// `#!/usr/bin/env node` in npm (and npm's node for lifecycle scripts) resolve.
function toolEnv(env: NodeJS.ProcessEnv, command: string, platform: NodeJS.Platform) {
  const platformPath = pathForPlatform(platform);
  if (!platformPath.isAbsolute(command)) return env;
  const child = { ...env };
  prependPath(child, platformPath.dirname(command), platform);
  return child;
}

async function probe(runner: InstallCommandRunner, command: ResolvedCommand, args: string[], env: NodeJS.ProcessEnv) {
  return runner({ command: command.command, args: [...command.prefix, ...args], env, timeoutMs: 15_000 });
}

// Open Interpreter's pinned dependencies (tiktoken, …) ship wheels for
// CPython 3.9–3.12; on newer Pythons pip falls back to building them from
// source, which needs a Rust toolchain. Prefer a supported interpreter when
// several are installed, and use any Python 3 otherwise.
const PREFERRED_PYTHON = ['3.12', '3.11', '3.10'];

function pythonVersion(result: InstallCommandResult) {
  const match = /Python (\d+)\.(\d+)/.exec(`${result.stdout}${result.stderr}`);
  return match ? { major: Number(match[1]), minor: Number(match[2]) } : null;
}

async function findPython(
  runner: InstallCommandRunner,
  env: NodeJS.ProcessEnv,
  platform: NodeJS.Platform,
  fileExists: (candidate: string) => boolean,
) {
  // `py -3.x` is the Windows Python launcher; `python3.x`/`python3` elsewhere.
  // All are real executables (no .cmd shims), so they spawn without a shell.
  let candidates: ResolvedCommand[];
  if (platform === 'win32') {
    candidates = [
      ...PREFERRED_PYTHON.map(version => ({ command: 'py', prefix: [`-${version}`] })),
      { command: 'py', prefix: ['-3'] }, { command: 'python', prefix: [] }, { command: 'python3', prefix: [] },
    ];
  } else {
    const names = [...PREFERRED_PYTHON.map(version => `python${version}`), 'python3'];
    const located = names.flatMap(name => posixToolDirectories(env, platform)
      .map(directory => path.posix.join(directory, name))
      .filter(candidate => fileExists(candidate)));
    candidates = [
      ...located.map(command => ({ command, prefix: [] })),
      ...PREFERRED_PYTHON.map(version => ({ command: `python${version}`, prefix: [] })),
      { command: 'python3', prefix: [] },
      { command: 'python', prefix: [] },
    ];
  }
  const seen = new Set<string>();
  let fallback: ResolvedCommand | null = null;
  for (const candidate of candidates) {
    const key = [candidate.command, ...candidate.prefix].join(' ');
    if (seen.has(key)) continue;
    seen.add(key);
    const result = await probe(runner, candidate, ['--version'], toolEnv(env, candidate.command, platform));
    if (result.timedOut || result.exitCode !== 0) continue;
    // Python 2 prints "Python 2.x"; the Microsoft Store alias stub exits non-zero.
    const version = pythonVersion(result);
    if (!version || version.major !== 3 || version.minor < 9) continue;
    if (version.minor <= 12) return candidate;
    fallback ??= candidate;
  }
  return fallback;
}

async function installCodex(
  userDataPath: string,
  platform: NodeJS.Platform,
  env: NodeJS.ProcessEnv,
  runner: InstallCommandRunner,
  fileExists: (candidate: string) => boolean,
): Promise<AgentInstallResult> {
  // Same on every platform: a local npm install into Consiglio's user data
  // (agents/codex), never a global -g install or a piped shell installer.
  const npm = resolveNpmCommand(env, platform, fileExists);
  const npmEnv = npm ? toolEnv(env, npm.command, platform) : env;
  const npmProbe = npm ? await probe(runner, npm, ['--version'], npmEnv) : null;
  if (!npm || !npmProbe || npmProbe.exitCode !== 0 || npmProbe.timedOut) {
    return {
      id: 'codex', attempted: true, installed: false,
      diagnostic: 'Codex was not found and npm is unavailable. Install Node.js (which includes npm) or Codex, then refresh.',
    };
  }
  const prefix = managedCodexPrefix(userDataPath, platform);
  const result = await runner({
    command: npm.command,
    args: [...npm.prefix, 'install', '--no-audit', '--no-fund', '--prefix', prefix, '@openai/codex'],
    env: npmEnv,
    timeoutMs: INSTALL_TIMEOUT_MS,
  });
  const executable = managedCodexExecutable(userDataPath, platform);
  if (result.exitCode === 0 && fileExists(executable)) {
    const launch = managedCodexLaunchExecutable(userDataPath, platform, fileExists);
    env.CODEX_BIN = launch;
    configureManagedAgentEnvironment(userDataPath, env, platform, fileExists);
    return { id: 'codex', attempted: true, installed: true, executable: launch, diagnostic: 'Codex was installed in Consiglio application data.' };
  }
  return {
    id: 'codex', attempted: true, installed: false,
    diagnostic: result.exitCode === 0
      ? 'npm finished but the Codex executable was not created in Consiglio application data.'
      : `Codex installation with npm failed: ${commandOutput(result) || 'no installer output'}`,
  };
}

async function installOpenInterpreter(
  userDataPath: string,
  platform: NodeJS.Platform,
  env: NodeJS.ProcessEnv,
  runner: InstallCommandRunner,
  fileExists: (candidate: string) => boolean,
): Promise<AgentInstallResult> {
  // Open Interpreter is a Python package: an isolated venv in Consiglio's
  // user data (agents/open-interpreter) on every platform.
  const python = await findPython(runner, env, platform, fileExists);
  if (!python) {
    return {
      id: 'open-interpreter', attempted: true, installed: false,
      diagnostic: 'Open Interpreter was not found and Python 3 is unavailable. Install Python 3, then refresh.',
    };
  }
  const venv = managedOpenInterpreterVenv(userDataPath, platform);
  const create = await runner({
    command: python.command,
    args: [...python.prefix, '-m', 'venv', venv],
    env: toolEnv(env, python.command, platform),
    timeoutMs: INSTALL_TIMEOUT_MS,
  });
  if (create.exitCode !== 0) {
    return {
      id: 'open-interpreter', attempted: true, installed: false,
      diagnostic: `Could not create the Open Interpreter environment: ${commandOutput(create) || 'no installer output'}`,
    };
  }
  const install = await runner({
    command: managedOpenInterpreterPython(userDataPath, platform),
    args: ['-m', 'pip', 'install', '--disable-pip-version-check', '--upgrade', 'open-interpreter'],
    env,
    timeoutMs: INSTALL_TIMEOUT_MS,
  });
  const executable = managedOpenInterpreterExecutable(userDataPath, platform);
  if (install.exitCode === 0 && fileExists(executable)) {
    env.OI_BIN = executable;
    configureManagedAgentEnvironment(userDataPath, env, platform, fileExists);
    return {
      id: 'open-interpreter', attempted: true, installed: true, executable,
      diagnostic: 'Open Interpreter was installed in an isolated Consiglio-managed Python environment.',
    };
  }
  return {
    id: 'open-interpreter', attempted: true, installed: false,
    diagnostic: `Open Interpreter installation failed: ${commandOutput(install) || 'no installer output'}`,
  };
}

export async function installMissingAgentFrontends(options: InstallMissingAgentOptions): Promise<AgentInstallResult[]> {
  const env = options.env || process.env;
  const platform = options.platform || process.platform;
  const runner = options.runner || runInstallCommand;
  const fileExists = options.fileExists || executableExists;
  const total = 2;
  const results: AgentInstallResult[] = [];

  const { home } = configureManagedAgentEnvironment(options.userDataPath, env, platform, fileExists);
  if (platform === process.platform) {
    try { fs.mkdirSync(home, { recursive: true }); } catch { /* npm/venv create what they need */ }
  }

  const codex = options.readiness.find(agent => agent.id === 'codex');
  if (codex?.installed) {
    results.push({ id: 'codex', attempted: false, installed: true, diagnostic: codex.diagnostic });
  } else {
    options.onProgress?.(progress('installing-codex', 'Installing the Codex agent front end…', 1, total));
    results.push(await installCodex(options.userDataPath, platform, env, runner, fileExists));
  }

  const oi = options.readiness.find(agent => agent.id === 'open-interpreter');
  if (oi?.installed) {
    results.push({ id: 'open-interpreter', attempted: false, installed: true, diagnostic: oi.diagnostic });
  } else {
    options.onProgress?.(progress('installing-open-interpreter', 'Installing Open Interpreter…', 2, total));
    results.push(await installOpenInterpreter(options.userDataPath, platform, env, runner, fileExists));
  }

  configureManagedAgentEnvironment(options.userDataPath, env, platform, fileExists);
  return results;
}
