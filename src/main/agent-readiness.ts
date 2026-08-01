import { spawn } from 'child_process';

export type AgentId = 'codex' | 'open-interpreter' | 'aider' | 'claude-code' | 'gemini' | 'copilot' | 'amazon-q';
export type AgentSupportTier = 'supported' | 'preview' | 'detected-only';
export type AgentReadinessState = 'ready' | 'configuration-required' | 'missing' | 'timeout' | 'error';
export type AgentConfigurationState = 'ready' | 'required' | 'not-required' | 'unknown';

export interface AgentReadiness {
  id: AgentId;
  name: string;
  installed: boolean;
  authenticated: boolean | null;
  configuration: AgentConfigurationState;
  selectable: boolean;
  state: AgentReadinessState;
  version?: string;
  diagnostic: string;
  supportTier: AgentSupportTier;
  checkedAt: number;
}

export interface CommandProbeRequest {
  command: string;
  args: string[];
  timeoutMs: number;
  env: NodeJS.ProcessEnv;
}

export interface CommandProbeResult {
  exitCode: number | null;
  stdout: string;
  stderr: string;
  errorCode?: string;
  timedOut: boolean;
}

export interface ResolvedProbeCommand {
  command: string;
  prefixArgs: string[];
}

export type CommandRunner = (request: CommandProbeRequest) => Promise<CommandProbeResult>;
export type AgentCommandResolver = (
  agentId: AgentId,
  env: NodeJS.ProcessEnv,
) => ResolvedProbeCommand | null;

export interface AgentReadinessOptions {
  timeoutMs?: number;
  env?: NodeJS.ProcessEnv;
  runner?: CommandRunner;
  commandResolver?: AgentCommandResolver;
  now?: () => number;
}

interface AgentSpec {
  id: AgentId;
  name: string;
  commandEnv: string;
  command: string;
  versionArgs: string[];
  supportTier: AgentSupportTier;
  installDiagnostic: string;
  authentication: 'codex' | 'not-required' | 'unknown' | 'open-interpreter' | 'claude-code' | 'gemini' | 'copilot' | 'amazon-q';
}

const DEFAULT_TIMEOUT_MS = 4_000;
const OUTPUT_LIMIT = 64 * 1024;

const AGENT_SPECS: AgentSpec[] = [
  {
    id: 'codex',
    name: 'Codex',
    commandEnv: 'CODEX_BIN',
    command: 'codex',
    versionArgs: ['--version'],
    supportTier: 'supported',
    installDiagnostic: 'Codex CLI was not found. Consiglio will attempt a user-level installation; CODEX_BIN may override it.',
    authentication: 'codex',
  },
  {
    id: 'open-interpreter',
    name: 'Open Interpreter',
    commandEnv: 'OI_BIN',
    command: 'interpreter',
    versionArgs: ['--version'],
    supportTier: 'preview',
    installDiagnostic: 'Open Interpreter was not found. Consiglio will attempt a user-level installation; OI_BIN may override it.',
    authentication: 'not-required',
  },
  {
    id: 'aider',
    name: 'Aider',
    commandEnv: 'AIDER_BIN',
    command: 'aider',
    versionArgs: ['--version'],
    supportTier: 'preview',
    installDiagnostic: 'Aider was not found. Install via pip or your package manager; AIDER_BIN may override it.',
    authentication: 'not-required',
  },
  {
    id: 'claude-code',
    name: 'Claude Code',
    commandEnv: 'CLAUDE_BIN',
    command: 'claude',
    versionArgs: ['--version'],
    supportTier: 'preview',
    installDiagnostic: 'Claude Code was not found. Install via npm; CLAUDE_BIN may override it.',
    authentication: 'claude-code',
  },
  {
    id: 'gemini',
    name: 'Gemini CLI',
    commandEnv: 'GEMINI_BIN',
    command: 'gemini',
    versionArgs: ['--version'],
    supportTier: 'preview',
    installDiagnostic: 'Gemini CLI was not found. Install via npm; GEMINI_BIN may override it.',
    authentication: 'not-required',
  },
  {
    id: 'copilot',
    name: 'GitHub Copilot',
    commandEnv: 'COPILOT_BIN',
    command: 'copilot',
    versionArgs: ['--version'],
    supportTier: 'preview',
    installDiagnostic: 'GitHub Copilot CLI was not found. Install via npm; COPILOT_BIN may override it.',
    authentication: 'not-required',
  },
  {
    id: 'amazon-q',
    name: 'Amazon Q',
    commandEnv: 'Q_BIN',
    command: 'q',
    versionArgs: ['--version'],
    supportTier: 'preview',
    installDiagnostic: 'Amazon Q was not found. Install via npm; Q_BIN may override it.',
    authentication: 'not-required',
  },
];

function appendLimited(current: string, chunk: Buffer | string): string {
  if (current.length >= OUTPUT_LIMIT) return current;
  return (current + chunk.toString()).slice(0, OUTPUT_LIMIT);
}

export const runCommandProbe: CommandRunner = request => new Promise(resolve => {
  let settled = false;
  let stdout = '';
  let stderr = '';
  let timedOut = false;
  let timer: NodeJS.Timeout | undefined;

  const finish = (result: CommandProbeResult) => {
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

function firstLine(output: string): string | undefined {
  return output.split(/\r?\n/).find(line => line.trim());
}

function probeOutput(result: CommandProbeResult): string {
  return (result.stdout || result.stderr || '').trim();
}

function resolvedCommand(
  spec: AgentSpec,
  env: NodeJS.ProcessEnv,
  commandResolver?: AgentCommandResolver,
): ResolvedProbeCommand {
  if (commandResolver) {
    const resolved = commandResolver(spec.id, env);
    if (resolved) return resolved;
  }

  const explicitBin = env[spec.commandEnv];
  if (explicitBin) {
    return { command: explicitBin, prefixArgs: [] };
  }

  return { command: spec.command, prefixArgs: [] };
}

function unavailable(
  spec: AgentSpec,
  state: 'missing' | 'timeout' | 'error',
  diagnostic: string,
  checkedAt: number,
  installed = false,
): AgentReadiness {
  return {
    id: spec.id,
    name: spec.name,
    installed,
    authenticated: false,
    configuration: 'required',
    selectable: false,
    state,
    diagnostic,
    supportTier: spec.supportTier,
    checkedAt,
  };
}

async function detectOne(
  spec: AgentSpec,
  runner: CommandRunner,
  env: NodeJS.ProcessEnv,
  timeoutMs: number,
  checkedAt: number,
  commandResolver?: AgentCommandResolver,
): Promise<AgentReadiness> {
  const command = resolvedCommand(spec, env, commandResolver);
  let versionResult: CommandProbeResult;

  try {
    versionResult = await runner({
      command: command.command,
      args: [...command.prefixArgs, ...spec.versionArgs],
      timeoutMs,
      env,
    });
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return unavailable(spec, 'error', `${spec.name} readiness check failed: ${message}`, checkedAt);
  }

  if (versionResult.timedOut) {
    return unavailable(spec, 'timeout', `${spec.name} did not answer its version check within ${timeoutMs} ms.`, checkedAt, true);
  }
  if (versionResult.errorCode === 'ENOENT') {
    return unavailable(spec, 'missing', spec.installDiagnostic, checkedAt);
  }
  if (versionResult.exitCode !== 0) {
    const detail = firstLine(probeOutput(versionResult)) || `exit code ${versionResult.exitCode ?? 'unknown'}`;
    return unavailable(spec, 'error', `${spec.name} was found, but its version check failed: ${detail}`, checkedAt, true);
  }

  const version = firstLine(probeOutput(versionResult)) || 'installed';

  // Only agents that require authentication get an auth check
  const requiresAuth = spec.authentication !== 'not-required';

  if (!requiresAuth) {
    return {
      id: spec.id,
      name: spec.name,
      installed: true,
      authenticated: null,
      configuration: 'unknown',
      selectable: true,
      state: 'ready',
      version,
      diagnostic: `${spec.name} ${version} is installed.`,
      supportTier: spec.supportTier,
      checkedAt,
    };
  }

  let authResult: CommandProbeResult;
  try {
    authResult = await runner({
      command: command.command,
      args: [...command.prefixArgs, 'login', 'status'],
      timeoutMs,
      env,
    });
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    // If the auth command doesn't exist, treat as not requiring auth
    if (message.includes('ENOENT') || message.includes('not found')) {
      return {
        id: spec.id,
        name: spec.name,
        installed: true,
        authenticated: null,
        configuration: 'unknown',
        selectable: true,
        state: 'ready',
        version,
        diagnostic: `${spec.name} ${version} is installed.`,
        supportTier: spec.supportTier,
        checkedAt,
      };
    }
    return {
      id: spec.id,
      name: spec.name,
      installed: true,
      authenticated: false,
      configuration: 'required',
      selectable: false,
      state: 'error',
      version,
      diagnostic: `${spec.name} authentication check failed: ${message}`,
      supportTier: spec.supportTier,
      checkedAt,
    };
  }

  if (authResult.timedOut) {
    return {
      id: spec.id,
      name: spec.name,
      installed: true,
      authenticated: false,
      configuration: 'required',
      selectable: false,
      state: 'timeout',
      version,
      diagnostic: `${spec.name} did not answer \`login status\` within ${timeoutMs} ms.`,
      supportTier: spec.supportTier,
      checkedAt,
    };
  }

  // If the auth command returned ENOENT, treat as not requiring auth
  if (authResult.errorCode === 'ENOENT') {
    return {
      id: spec.id,
      name: spec.name,
      installed: true,
      authenticated: null,
      configuration: 'unknown',
      selectable: true,
      state: 'ready',
      version,
      diagnostic: `${spec.name} ${version} is installed.`,
      supportTier: spec.supportTier,
      checkedAt,
    };
  }

  const authOutput = probeOutput(authResult);
  const explicitlyUnauthenticated = /\bnot logged in\b|\bnot authenticated\b|\bunauthenticated\b/i.test(authOutput);
  const positivelyAuthenticated = /\blogged in\b|\bauthenticated\b/i.test(authOutput);
  const authenticated = authResult.exitCode === 0 && !explicitlyUnauthenticated && positivelyAuthenticated;

  return {
    id: spec.id,
    name: spec.name,
    installed: true,
    authenticated,
    configuration: authenticated ? 'ready' : 'required',
    selectable: authenticated,
    state: authenticated ? 'ready' : 'configuration-required',
    version,
    diagnostic: authenticated
      ? authOutput || `${spec.name} ${version} is installed and authenticated.`
      : authOutput || `${spec.name} is installed but not signed in.`,
    supportTier: spec.supportTier,
    checkedAt,
  };
}

export async function detectAgentReadiness(options: AgentReadinessOptions = {}): Promise<AgentReadiness[]> {
  const runner = options.runner || runCommandProbe;
  const env = options.env || process.env;
  const timeoutMs = Math.max(100, options.timeoutMs || DEFAULT_TIMEOUT_MS);
  const checkedAt = (options.now || Date.now)();

  return Promise.all(AGENT_SPECS.map(spec => detectOne(
    spec,
    runner,
    env,
    timeoutMs,
    checkedAt,
    options.commandResolver,
  )));
}
