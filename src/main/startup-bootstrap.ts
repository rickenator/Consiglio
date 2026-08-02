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
    child = spawn(request.command, request.args, {
      env: request.env,
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

export function managedCodexExecutable(userDataPath: string, platform: NodeJS.Platform = process.platform) {
  const platformPath = pathForPlatform(platform);
  const base = platformPath.join(managedAgentHome(userDataPath, platform), 'codex', 'node_modules', '.bin');
  return platformPath.join(base, platform === 'win32' ? 'codex.cmd' : 'codex');
}

export function managedOpenInterpreterExecutable(userDataPath: string, platform: NodeJS.Platform = process.platform) {
  const platformPath = pathForPlatform(platform);
  const base = platformPath.join(managedAgentHome(userDataPath, platform), 'open-interpreter', platform === 'win32' ? 'Scripts' : 'bin');
  return platformPath.join(base, platform === 'win32' ? 'interpreter.exe' : 'interpreter');
}

export function configureManagedAgentEnvironment(
  userDataPath: string,
  env: NodeJS.ProcessEnv = process.env,
  platform: NodeJS.Platform = process.platform,
  fileExists: (candidate: string) => boolean = executableExists,
): void {
  const home = managedAgentHome(userDataPath, platform);
  env.CONSIGLIO_AGENT_HOME = home;

  const codexBin = managedCodexExecutable(userDataPath, platform);
  if (fileExists(codexBin)) {
    env.CODEX_BIN = codexBin;
    const binDir = path.dirname(codexBin);
    env.PATH = `${binDir}${path.delimiter}${env.PATH || ''}`;
  }

  const oiBin = managedOpenInterpreterExecutable(userDataPath, platform);
  if (fileExists(oiBin)) {
    env.OI_BIN = oiBin;
    const binDir = path.dirname(oiBin);
    env.PATH = `${binDir}${path.delimiter}${env.PATH || ''}`;
  }

  try {
    fs.mkdirSync(home, { recursive: true });
  } catch { /* best effort */ }
}

function findPython(
  runner: InstallCommandRunner,
  env: NodeJS.ProcessEnv,
  platform: NodeJS.Platform,
): Promise<{ command: string; prefix: string[] } | null> {
  const candidates = platform === 'win32'
    ? ['python', 'python3']
    : ['python3', 'python'];

  for (const candidate of candidates) {
    try {
      const result = runner({
        command: candidate,
        args: ['--version'],
        env,
        timeoutMs: 5_000,
      });
      return result.then(r => r.exitCode === 0 ? { command: candidate, prefix: [] } : null);
    } catch { /* try next */ }
  }
  return Promise.resolve(null);
}

function progress(phase: BootstrapPhase, message: string, completed: number, total: number): BootstrapProgress {
  return { phase, message, active: true, completed, total, updatedAt: Date.now() };
}

async function installCodex(
  userDataPath: string,
  platform: NodeJS.Platform,
  env: NodeJS.ProcessEnv,
  runner: InstallCommandRunner,
  fileExists: (candidate: string) => boolean,
): Promise<AgentInstallResult> {
  configureManagedAgentEnvironment(userDataPath, env, platform, fileExists);

  if (platform === 'win32') {
    const result = await runner({
      command: 'npm',
      args: ['install', '-g', '@openai/codex'],
      env,
      timeoutMs: INSTALL_TIMEOUT_MS,
    });
    if (result.exitCode === 0) {
      const userHome = env.USERPROFILE?.trim() || '';
      const executable = [
        userHome ? path.win32.join(userHome, 'AppData', 'Roaming', 'npm', 'codex.cmd') : '',
        userHome ? path.win32.join(userHome, 'AppData', 'Roaming', 'npm', 'codex') : '',
      ].find(candidate => candidate && fileExists(candidate));
      if (executable) env.CODEX_BIN = executable;
      return {
        id: 'codex', attempted: true, installed: true, executable: executable || undefined,
        diagnostic: executable
          ? 'Codex was installed via npm and the executable was detected.'
          : 'Codex was installed via npm; Consiglio will rescan standard executable locations.',
      };
    }
    return {
      id: 'codex', attempted: true, installed: false,
      diagnostic: `Codex installation with npm failed: ${commandOutput(result) || 'no installer output'}`,
    };
  }

  if (platform === 'darwin') {
    const result = await runner({
      command: 'sh',
      args: ['-lc', 'curl -fsSL https://chatgpt.com/codex/install.sh | sh'],
      env,
      timeoutMs: INSTALL_TIMEOUT_MS,
    });
    if (result.exitCode === 0) {
      const userHome = env.HOME?.trim() || '';
      const executable = [
        userHome ? path.posix.join(userHome, '.local', 'bin', 'codex') : '',
        userHome ? path.posix.join(userHome, 'bin', 'codex') : '',
      ].find(candidate => candidate && fileExists(candidate));
      if (executable) env.CODEX_BIN = executable;
      return {
        id: 'codex', attempted: true, installed: true, executable: executable || undefined,
        diagnostic: executable
          ? 'The official Codex standalone installer completed and the executable was detected.'
          : 'The official Codex standalone installer completed; Consiglio will rescan standard executable locations.',
      };
    }
    return {
      id: 'codex', attempted: true, installed: false,
      diagnostic: `The official Codex installer failed: ${commandOutput(result) || 'no installer output'}`,
    };
  }

  // Linux — try npm first, then the standalone installer
  const npmResult = await runner({
    command: 'npm',
    args: ['install', '-g', '@openai/codex'],
    env,
    timeoutMs: INSTALL_TIMEOUT_MS,
  });
  if (npmResult.exitCode === 0) {
    const userHome = env.HOME?.trim() || '';
    const executable = [
      userHome ? path.posix.join(userHome, '.local', 'bin', 'codex') : '',
      userHome ? path.posix.join(userHome, 'bin', 'codex') : '',
      '/usr/local/bin/codex',
    ].find(candidate => candidate && fileExists(candidate));
    if (executable) env.CODEX_BIN = executable;
    return {
      id: 'codex', attempted: true, installed: true, executable: executable || undefined,
      diagnostic: executable
        ? 'Codex was installed via npm and the executable was detected.'
        : 'Codex was installed via npm; Consiglio will rescan standard executable locations.',
    };
  }

  const result = await runner({
    command: 'sh',
    args: ['-lc', 'curl -fsSL https://chatgpt.com/codex/install.sh | sh'],
    env,
    timeoutMs: INSTALL_TIMEOUT_MS,
  });
  if (result.exitCode === 0) {
    const userHome = env.HOME?.trim() || '';
    const executable = [
      userHome ? path.posix.join(userHome, '.local', 'bin', 'codex') : '',
      userHome ? path.posix.join(userHome, 'bin', 'codex') : '',
    ].find(candidate => candidate && fileExists(candidate));
    if (executable) env.CODEX_BIN = executable;
    return {
      id: 'codex', attempted: true, installed: true, executable: executable || undefined,
      diagnostic: executable
        ? 'The official Codex standalone installer completed and the executable was detected.'
        : 'The official Codex standalone installer completed; Consiglio will rescan standard executable locations.',
    };
  }

  return {
    id: 'codex', attempted: true, installed: false,
    diagnostic: `Codex installation failed: ${commandOutput(result) || 'no installer output'}`,
  };
}

async function installOpenInterpreter(
  userDataPath: string,
  platform: NodeJS.Platform,
  env: NodeJS.ProcessEnv,
  runner: InstallCommandRunner,
  fileExists: (candidate: string) => boolean,
): Promise<AgentInstallResult> {
  configureManagedAgentEnvironment(userDataPath, env, platform, fileExists);

  const result = await runner({
    command: 'npm',
    args: ['install', '-g', 'open-interpreter'],
    env,
    timeoutMs: INSTALL_TIMEOUT_MS,
  });
  if (result.exitCode === 0) {
    const oiBin = managedOpenInterpreterExecutable(userDataPath, platform);
    if (fileExists(oiBin)) {
      env.OI_BIN = oiBin;
      return {
        id: 'open-interpreter', attempted: true, installed: true, executable: oiBin,
        diagnostic: 'Open Interpreter was installed in the managed environment.',
      };
    }
    // Fallback: try to find it in npm global bin
    const userHome = env.HOME || env.USERPROFILE || '';
    const npmBin = platform === 'win32'
      ? path.win32.join(userHome, 'AppData', 'Roaming', 'npm', 'interpreter.exe')
      : path.posix.join(userHome, '.local', 'bin', 'interpreter');
    if (fileExists(npmBin)) {
      env.OI_BIN = npmBin;
      return {
        id: 'open-interpreter', attempted: true, installed: true, executable: npmBin,
        diagnostic: 'Open Interpreter was installed via npm and detected.',
      };
    }
    return {
      id: 'open-interpreter', attempted: true, installed: true,
      diagnostic: 'Open Interpreter was installed via npm; Consiglio will rescan standard executable locations.',
    };
  }
  return {
    id: 'open-interpreter', attempted: true, installed: false,
    diagnostic: `Open Interpreter installation with npm failed: ${commandOutput(result) || 'no installer output'}`,
  };
}

export async function installMissingAgentFrontends(options: InstallMissingAgentOptions): Promise<AgentInstallResult[]> {
  const env = options.env || process.env;
  const platform = options.platform || process.platform;
  const runner = options.runner || runInstallCommand;
  const fileExists = options.fileExists || executableExists;
  const total = 2;
  const results: AgentInstallResult[] = [];

  configureManagedAgentEnvironment(options.userDataPath, env, platform, fileExists);
  
  const codex = options.readiness.find(agent => agent.id === 'codex');
  if (codex?.installed) {
    results.push({ id: 'codex', attempted: false, installed: true, diagnostic: codex.diagnostic });
  } else {
    options.onProgress?.(progress('installing-codex', 'Installing the Codex agent front end…', 1, total));
    results.push(await installCodex(options.userDataPath, platform, env, runner, fileExists));
  }

  configureManagedAgentEnvironment(options.userDataPath, env, platform, fileExists);
  
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
