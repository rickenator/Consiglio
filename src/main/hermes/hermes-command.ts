import fs from 'fs';
import os from 'os';
import path from 'path';

export interface ResolvedCommand {
  command: string;
  prefixArgs: string[];
  displayPath: string;
}

function candidatePaths(env: NodeJS.ProcessEnv, platform: NodeJS.Platform): string[] {
  const home = env.HOME || env.USERPROFILE || os.homedir();
  const names = platform === 'win32'
    ? ['hermes.exe', 'hermes.cmd', 'hermes.bat']
    : ['hermes'];

  const directories = platform === 'win32'
    ? [
        env.APPDATA ? path.join(env.APPDATA, 'npm') : '',
        path.join(home, '.local', 'bin'),
        path.join(home, '.hermes', 'hermes-agent', 'venv', 'Scripts'),
      ]
    : [
        path.join(home, '.local', 'bin'),
        path.join(home, '.hermes', 'hermes-agent', 'venv', 'bin'),
        '/usr/local/bin',
        '/usr/bin',
        '/opt/homebrew/bin',
        path.join(home, 'bin'),
      ];

  const candidates: string[] = [];
  for (const directory of directories) {
    if (!directory) continue;
    for (const name of names) candidates.push(path.join(directory, name));
  }
  return candidates;
}

/**
 * Locate the Hermes executable.
 *
 * A GUI-launched Electron app inherits a much smaller PATH than an interactive
 * shell, so a bare `hermes` is not enough: check HERMES_BIN, then the standard
 * install locations, then fall back to PATH resolution. Never returns null: the
 * final fallback is the bare command name, which the OS resolves.
 */
export function resolveHermesCommand(
  options: {
    requested?: string;
    platform?: NodeJS.Platform;
    env?: NodeJS.ProcessEnv;
  } = {},
): ResolvedCommand {
  const platform = options.platform || process.platform;
  const env = options.env || process.env;

  const requested = options.requested?.trim() || env.HERMES_BIN?.trim();
  if (requested && fs.existsSync(requested)) {
    return { command: requested, prefixArgs: [], displayPath: requested };
  }

  for (const candidate of candidatePaths(env, platform)) {
    if (fs.existsSync(candidate)) {
      return { command: candidate, prefixArgs: [], displayPath: candidate };
    }
  }

  // Last resort: let the OS resolve it from PATH.
  return { command: 'hermes', prefixArgs: [], displayPath: 'hermes' };
}

/** True when a runnable Hermes was found rather than the blind PATH fallback. */
export function hermesCommandIsVerified(resolved: ResolvedCommand): boolean {
  return resolved.displayPath !== 'hermes' || resolved.command !== 'hermes';
}
