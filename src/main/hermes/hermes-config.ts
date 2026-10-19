/**
 * Hermes settings configurator backend.
 *
 * All reads and writes go through `hermes config`, never by editing
 * `~/.hermes/config.yaml` directly — a stray indent corrupts the file and breaks
 * the live gateway. Writes are limited to a whitelist of settings that are safe
 * to change from a shell, and values are validated before any process spawns so
 * a value containing `\n` cannot inject YAML into the config file.
 */

import { execFileSync } from 'node:child_process';

import { resolveHermesCommand } from './hermes-command.ts';

export type ConfigValueKind = 'string' | 'integer' | 'ratio' | 'boolean';

export interface WritableConfigKey {
  key: string;
  label: string;
  kind: ConfigValueKind;
  description: string;
  /** Changing this affects sessions that are already running. */
  requiresRestart?: boolean;
}

/**
 * Whitelist: the settings an operator (or the agent, with approval) may change
 * from Consiglio. Everything else — including every secret — stays out.
 */
export const WRITABLE_CONFIG: WritableConfigKey[] = [
  { key: 'model.default', label: 'Model', kind: 'string', description: 'The model Hermes runs for new turns.' },
  { key: 'model.provider', label: 'Provider', kind: 'string', description: 'Provider entry that serves that model.' },
  { key: 'model.base_url', label: 'Endpoint', kind: 'string', description: 'OpenAI-compatible base URL when the provider is custom.' },
  {
    key: 'model.context_length',
    label: 'Context window',
    kind: 'integer',
    description: 'Token window Hermes assumes for this model. Too large and the provider truncates, too small and compaction fires early.',
    requiresRestart: true,
  },
  {
    key: 'compression.threshold',
    label: 'Compaction threshold',
    kind: 'ratio',
    description: 'Fraction of the context window at which Hermes compacts (0–1).',
    requiresRestart: true,
  },
  {
    key: 'compression.threshold_tokens',
    label: 'Compaction token cap',
    kind: 'integer',
    description: 'Absolute cap on the compaction trigger, overriding the ratio. Leave unset for a 1M-window model.',
    requiresRestart: true,
  },
  {
    key: 'compression.enabled',
    label: 'Context compression',
    kind: 'boolean',
    description: 'Whether Hermes compacts long conversations automatically.',
  },
  { key: 'auxiliary.compression.model', label: 'Compaction model', kind: 'string', description: 'Model used to summarise during compaction.' },
  { key: 'auxiliary.compression.provider', label: 'Compaction provider', kind: 'string', description: 'Provider entry for that summariser.' },
  { key: 'agent.max_turns', label: 'Max turns', kind: 'integer', description: 'Upper bound on tool-calling turns per request.' },
  { key: 'display.skin', label: 'Skin', kind: 'string', description: 'Active Hermes skin.' },
  { key: 'display.interface', label: 'Interface', kind: 'string', description: 'Default surface: cli or tui.' },
];

const FORBIDDEN_KEY_PATTERN = /(api[_-]?key|secret|token|password|credential)/i;

export function findWritableKey(key: string): WritableConfigKey | null {
  return WRITABLE_CONFIG.find(entry => entry.key === key) ?? null;
}

/**
 * Validate a candidate value. Returns an error string, or null when the value
 * may be written. Secrets and unknown keys are always refused.
 */
export function validateConfigValue(key: string, value: string): string | null {
  // Credential-shaped keys are refused before anything else, so a future
  // whitelist entry can never quietly expose a secret.
  if (FORBIDDEN_KEY_PATTERN.test(key)) return `${key} looks like a credential; secrets live in ~/.hermes/.env.`;

  const entry = findWritableKey(key);
  if (!entry) return `${key} is not a setting Consiglio may change.`;

  const trimmed = value.trim();
  if (!trimmed) return 'A value is required.';
  // Control characters (including newlines) would inject YAML into config.yaml.
  if (/[\u0000-\u001f\u007f]/.test(trimmed)) return 'Control characters are not allowed in a configuration value.';
  if (trimmed.length > 300) return 'Value is too long (300 characters max).';

  if (entry.kind === 'integer') {
    if (!/^\d+$/.test(trimmed)) return 'Expected a whole number.';
    const parsed = Number.parseInt(trimmed, 10);
    if (entry.key === 'model.context_length' && parsed < 1_000) return 'Context window must be at least 1000 tokens.';
    if (parsed < 0) return 'Expected a non-negative number.';
    return null;
  }

  if (entry.kind === 'ratio') {
    if (!/^\d*\.?\d+$/.test(trimmed)) return 'Expected a number between 0 and 1.';
    const parsed = Number.parseFloat(trimmed);
    if (!(parsed > 0 && parsed <= 1)) return 'Threshold must be greater than 0 and at most 1.';
    return null;
  }

  if (entry.kind === 'boolean') {
    if (trimmed !== 'true' && trimmed !== 'false') return 'Expected true or false.';
    return null;
  }

  return null;
}

export type CommandRunner = (command: string, args: string[]) => string;

export const runHermesCli: CommandRunner = (command, args) => execFileSync(command, args, {
  encoding: 'utf8',
  stdio: ['ignore', 'pipe', 'pipe'],
  timeout: 30_000,
}).trim();

function hermesInvocation(): { command: string; prefixArgs: string[] } {
  const resolved = resolveHermesCommand();
  return { command: resolved?.command || 'hermes', prefixArgs: resolved?.prefixArgs || [] };
}

export interface ConfigEntry {
  key: string;
  label: string;
  kind: ConfigValueKind;
  description: string;
  requiresRestart?: boolean;
  value: string | null;
  error?: string;
}

export function readConfig(run: CommandRunner = runHermesCli): ConfigEntry[] {
  const { command, prefixArgs } = hermesInvocation();
  return WRITABLE_CONFIG.map(entry => {
    try {
      const raw = run(command, [...prefixArgs, 'config', 'get', entry.key]);
      const value = raw.includes('Config key not set') ? null : raw.split('\n')[0].trim();
      return { ...entry, value: value === '' ? null : value };
    } catch (error) {
      return { ...entry, value: null, error: error instanceof Error ? error.message : String(error) };
    }
  });
}

export interface ConfigWriteResult {
  key: string;
  before: string | null;
  after: string | null;
}

/**
 * Write one whitelisted setting through `hermes config set` and read it back, so
 * the UI can show a real before/after rather than an optimistic guess.
 */
export function writeConfig(key: string, value: string, run: CommandRunner = runHermesCli): ConfigWriteResult {
  const invalid = validateConfigValue(key, value);
  if (invalid) throw new Error(invalid);

  const { command, prefixArgs } = hermesInvocation();

  let before: string | null = null;
  try {
    const raw = run(command, [...prefixArgs, 'config', 'get', key]);
    before = raw.includes('Config key not set') ? null : raw.split('\n')[0].trim();
  } catch {
    before = null;
  }

  run(command, [...prefixArgs, 'config', 'set', key, value.trim()]);

  let after: string | null = null;
  try {
    const raw = run(command, [...prefixArgs, 'config', 'get', key]);
    after = raw.includes('Config key not set') ? null : raw.split('\n')[0].trim();
  } catch {
    after = null;
  }

  return { key, before, after };
}

export function unsetConfig(key: string, run: CommandRunner = runHermesCli): void {
  const entry = findWritableKey(key);
  if (!entry) throw new Error(`${key} is not a setting Consiglio may change.`);
  if (FORBIDDEN_KEY_PATTERN.test(key)) throw new Error(`${key} looks like a credential; secrets live in ~/.hermes/.env.`);
  const { command, prefixArgs } = hermesInvocation();
  run(command, [...prefixArgs, 'config', 'unset', key]);
}

export function configFilePath(run: CommandRunner = runHermesCli): string {
  const { command, prefixArgs } = hermesInvocation();
  try {
    return run(command, [...prefixArgs, 'config', 'path']);
  } catch {
    return '';
  }
}
