/**
 * Hermes update detection.
 *
 * `hermes update --check` is read-only and safe to run on a live fleet; the
 * installer itself restarts the Hermes gateway and kills running agents, so the
 * caller decides when (and whether) to apply it.
 */

import { resolveHermesCommand } from './hermes-command.ts';
import { runHermesCli, type CommandRunner } from './hermes-config.ts';

export type HermesUpdateState = 'up-to-date' | 'available' | 'unknown';

export interface HermesUpdateStatus {
  state: HermesUpdateState;
  channel: string;
  currentVersion: string;
  availableVersion?: string;
  summary: string;
  checkedAt: number;
}

export function parseUpdateCheck(output: string, currentVersion = ''): HermesUpdateStatus {
  const text = output || '';
  const channel = /Update channel:\s*([^\s,;]+)/i.exec(text)?.[1] ?? 'stable';
  const versionMatch = /Hermes Agent v([0-9][^\s·]*)/i.exec(text)
    ?? /(?:^|\s)v(\d+\.\d+\.\d+)/i.exec(text);
  const resolvedCurrent = currentVersion || versionMatch?.[1] || '';

  const upToDate = /no newer release|up to date|already up to date/i.test(text);
  const available = /update (?:is )?available|newer release (?:is )?available|behind|ahead by|would update/i.test(text);

  if (available && !upToDate) {
    // Versions are normalised without the leading "v" so callers can compare
    // them against `hermes --version` output directly.
    const offered = /(?:available|offered|update to)[^\d]*(v?\d+\.\d+\.\d+[^\s,;)]*)/i.exec(text)?.[1]?.replace(/^v/i, '');
    return {
      state: 'available',
      channel,
      currentVersion: resolvedCurrent,
      availableVersion: offered,
      summary: text.split('\n').map(line => line.trim()).filter(Boolean).slice(-1)[0] || 'A Hermes update is available.',
      checkedAt: Date.now(),
    };
  }

  if (upToDate) {
    return {
      state: 'up-to-date',
      channel,
      currentVersion: resolvedCurrent,
      summary: text.split('\n').map(line => line.trim()).filter(Boolean).slice(-1)[0] || 'Hermes is up to date.',
      checkedAt: Date.now(),
    };
  }

  return {
    state: 'unknown',
    channel,
    currentVersion: resolvedCurrent,
    summary: text.split('\n').map(line => line.trim()).filter(Boolean).slice(-1)[0] || 'Hermes did not report an update status.',
    checkedAt: Date.now(),
  };
}

export function readHermesVersion(run: CommandRunner = runHermesCli): string {
  const { command, prefixArgs } = resolveHermesCommand();
  try {
    return run(command ?? 'hermes', [...(prefixArgs || []), '--version']).split('\n')[0] ?? '';
  } catch {
    return '';
  }
}

export function checkForHermesUpdate(run: CommandRunner = runHermesCli): HermesUpdateStatus {
  const { command, prefixArgs } = resolveHermesCommand();
  const version = readHermesVersion(run);
  const current = /v([0-9][^\s·]*)/.exec(version)?.[1] ?? '';
  try {
    const output = run(command ?? 'hermes', [...(prefixArgs || []), 'update', '--check']);
    return parseUpdateCheck(output, current);
  } catch (error) {
    return {
      state: 'unknown',
      channel: 'stable',
      currentVersion: current,
      summary: error instanceof Error ? error.message : 'Could not check for Hermes updates.',
      checkedAt: Date.now(),
    };
  }
}

/**
 * Apply the update. `restartGateway: false` leaves running agents and the
 * gateway process alone, which is what an idle-only auto-update wants.
 */
export function applyHermesUpdate(
  options: { restartGateway?: boolean; run?: CommandRunner } = {},
): { ok: boolean; output: string } {
  const run = options.run ?? runHermesCli;
  const { command, prefixArgs } = resolveHermesCommand();
  const args = [...(prefixArgs || []), 'update', '--yes'];
  if (options.restartGateway === false) args.push('--no-gateway-restart');
  try {
    return { ok: true, output: run(command ?? 'hermes', args) };
  } catch (error) {
    return { ok: false, output: error instanceof Error ? error.message : String(error) };
  }
}
