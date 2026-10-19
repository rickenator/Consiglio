import React, { useEffect, useMemo, useState } from 'react';

interface Props {
  open: boolean;
  onClose: () => void;
  onNotice: (kind: 'info' | 'success' | 'error', message: string) => void;
}

/**
 * Hermes settings configurator.
 *
 * Reads and writes go through `hermes config`, never by editing config.yaml.
 * Only whitelisted keys are offered, and every write reports a real before/after
 * read back from Hermes. Keys marked "needs restart" affect sessions that are
 * already running until those sessions are restarted.
 */
export default function HermesSettings({ open, onClose, onNotice }: Props) {
  const [entries, setEntries] = useState<HermesConfigEntry[]>([]);
  const [configPath, setConfigPath] = useState('');
  const [draft, setDraft] = useState<Record<string, string>>({});
  const [busyKey, setBusyKey] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const load = async () => {
    try {
      const [loaded, path] = await Promise.all([
        window.codexApi.listHermesConfig(),
        window.codexApi.getHermesConfigPath(),
      ]);
      setEntries(loaded);
      setConfigPath(path);
      setDraft({});
      setError(null);
    } catch (e) {
      setError((e as Error).message);
    }
  };

  useEffect(() => {
    if (open) void load();
  }, [open]);

  const groups = useMemo(() => {
    const map = new Map<string, HermesConfigEntry[]>();
    for (const entry of entries) {
      const group = entry.key.split('.')[0];
      map.set(group, [...(map.get(group) || []), entry]);
    }
    return [...map.entries()];
  }, [entries]);

  if (!open) return null;

  const apply = async (entry: HermesConfigEntry) => {
    const value = draft[entry.key] ?? entry.value ?? '';
    setBusyKey(entry.key);
    try {
      const result = await window.codexApi.setHermesConfig(entry.key, value);
      onNotice('success', `${entry.key}: ${result.before ?? '(unset)'} → ${result.after ?? '(unset)'}`);
      await load();
    } catch (e) {
      onNotice('error', `Could not write ${entry.key}: ${(e as Error).message}`);
    } finally {
      setBusyKey(null);
    }
  };

  const clear = async (entry: HermesConfigEntry) => {
    setBusyKey(entry.key);
    try {
      setEntries(await window.codexApi.unsetHermesConfig(entry.key));
      onNotice('info', `${entry.key} unset.`);
    } catch (e) {
      onNotice('error', `Could not unset ${entry.key}: ${(e as Error).message}`);
    } finally {
      setBusyKey(null);
    }
  };

  return (
    <div className="codex-modal-backdrop" onMouseDown={onClose}>
      <section className="codex-hermes-panel" onMouseDown={event => event.stopPropagation()} aria-label="Hermes settings">
        <div className="codex-dialog-header">
          <div>
            <h2>Hermes settings</h2>
            <p>
              Everything here is written with <code>hermes config set</code>. Secrets stay in <code>~/.hermes/.env</code> and are never editable from Consiglio.
            </p>
          </div>
          <button className="codex-icon-button" onClick={onClose} aria-label="Close">×</button>
        </div>

        {configPath && (
          <div className="codex-hermes-path">
            <span>Config file</span>
            <code>{configPath}</code>
            <button
              className="codex-button codex-button-secondary"
              onClick={() => void window.codexApi.openHermesConfig()}
            >
              Open file
            </button>
            <button className="codex-button codex-button-secondary" onClick={() => void load()}>
              Reload
            </button>
          </div>
        )}

        {error && <div className="codex-hermes-error">{error}</div>}

        <div className="codex-hermes-groups">
          {groups.map(([group, groupEntries]) => (
            <div className="codex-hermes-group" key={group}>
              <h3>{group}</h3>
              {groupEntries.map(entry => {
                const value = draft[entry.key] ?? entry.value ?? '';
                const dirty = draft[entry.key] !== undefined && draft[entry.key] !== (entry.value ?? '');
                return (
                  <div className="codex-hermes-row" key={entry.key}>
                    <div className="codex-hermes-row-head">
                      <span className="codex-hermes-label">{entry.label}</span>
                      <code className="codex-hermes-key">{entry.key}</code>
                      {entry.requiresRestart && <span className="codex-hermes-badge">restart sessions</span>}
                    </div>
                    <p className="codex-hermes-help">{entry.description}</p>
                    <div className="codex-hermes-controls">
                      <input
                        className="codex-input"
                        value={value}
                        placeholder={entry.value === null ? '(unset)' : ''}
                        onChange={event => setDraft(current => ({ ...current, [entry.key]: event.target.value }))}
                      />
                      <button
                        className="codex-button codex-button-primary"
                        disabled={busyKey === entry.key}
                        onClick={() => void apply(entry)}
                      >
                        {busyKey === entry.key ? 'Writing…' : dirty ? 'Apply' : 'Re-write'}
                      </button>
                      <button
                        className="codex-button codex-button-secondary"
                        disabled={busyKey === entry.key || entry.value === null}
                        onClick={() => void clear(entry)}
                      >
                        Unset
                      </button>
                    </div>
                    <div className="codex-hermes-current">
                      current: <code>{entry.value ?? '(unset)'}</code>
                      {entry.error && <span className="codex-hermes-error-inline"> · {entry.error}</span>}
                    </div>
                  </div>
                );
              })}
            </div>
          ))}
        </div>

        <div className="codex-dialog-actions">
          <span className="codex-hermes-footnote">
            The agent can change the same keys itself through its terminal tool; Consiglio shows the result here.
          </span>
          <button className="codex-button codex-button-secondary" onClick={onClose}>Close</button>
        </div>
      </section>
    </div>
  );
}
