import { useEffect, useMemo, useRef, useState } from 'react';

type Provider = CodexProvider;

type NewSessionOptions = {
  repository?: string;
  branch?: string;
  provider?: Provider;
  selectedLanProviderId?: string;
};

interface Props {
  sessions: SessionRecord[];
  selected: string | null;
  onSelect: (id: string) => void;
  onStartSession: (options: NewSessionOptions) => void;
  onReconnect: (sessionId: string) => void;
  onPickRepository: () => Promise<string | null>;
  onCopyPath: (path: string, label: string) => void;
  onOpenPath: (path: string, label: string) => void;
  onRequestNewSession: () => Promise<void>;
  onStopSession: (sessionId: string) => Promise<void>;
  onDeleteSession: (sessionId: string) => Promise<void>;
  settings: {
    defaultProvider: Provider;
    lanProviders: LanProviderConfig[];
  };
  connectionLabel: string;
}

/**
 * Task rail.
 *
 * Selection opens a tab (the App owns tab state); the rail only lists what Hermes
 * and the Codex backend have stored, with the backend name for each task.
 */
export default function SessionList({
  sessions,
  selected,
  onSelect,
  onStartSession,
  onPickRepository,
  onDeleteSession,
  settings,
  connectionLabel,
}: Props) {
  const [showNewTask, setShowNewTask] = useState(false);
  const [repository, setRepository] = useState('');
  const [branch, setBranch] = useState('');
  const [provider, setProvider] = useState<Provider>(settings.defaultProvider);
  const repositoryRef = useRef<HTMLInputElement>(null);

  const sortedSessions = useMemo(
    () => [...sessions].sort((left, right) => right.updated_at - left.updated_at),
    [sessions],
  );

  useEffect(() => {
    const unsubscribe = window.codexApi.onNewSession(() => setShowNewTask(true));
    return () => unsubscribe();
  }, []);

  useEffect(() => {
    if (showNewTask) queueMicrotask(() => repositoryRef.current?.focus());
  }, [showNewTask]);

  useEffect(() => {
    setProvider(settings.defaultProvider);
  }, [settings.defaultProvider]);

  const startTask = () => {
    const workspace = repository.trim();
    onStartSession({
      repository: workspace || undefined,
      branch: branch.trim() || undefined,
      provider,
    });
    setRepository('');
    setBranch('');
    setShowNewTask(false);
  };

  return (
    <aside className="codex-sidebar codex-simple-sidebar">
      <div className="codex-sidebar-top">
        <button className="codex-new-task" onClick={() => startTask()}>
          <span aria-hidden="true">+</span>
          New task
        </button>
        <button className="codex-open-workspace" onClick={() => setShowNewTask(true)}>
          Choose a folder…
        </button>
      </div>

      <div className="codex-session-heading">Tasks</div>
      <div className="codex-list" aria-label="Tasks">
        {sortedSessions.length === 0 && (
          <div className="codex-sidebar-empty">No tasks yet. Start one to talk to Hermes.</div>
        )}
        {sortedSessions.map(session => {
          const label = sessionLabel(session.repository);
          return (
            <button
              key={session.id}
              className={`codex-simple-session${selected === session.id ? ' is-active' : ''}`}
              onClick={() => onSelect(session.id)}
              title={session.repository || label}
            >
              <span className={`codex-session-dot is-${session.status}`} aria-hidden="true" />
              <span className="codex-simple-session-copy">
                <span className="codex-simple-session-title">{label}</span>
                <span className="codex-simple-session-meta">
                  {backendLabel(session)} · {formatUpdatedAt(session.updated_at)}
                </span>
              </span>
              {(session.status === 'completed' || session.status === 'stopped' || session.status === 'failed') && (
                <span
                  role="button"
                  tabIndex={0}
                  className="codex-session-delete"
                  title="Remove this task from Consiglio"
                  onClick={event => {
                    event.stopPropagation();
                    void onDeleteSession(session.id);
                  }}
                  onKeyDown={event => {
                    if (event.key === 'Enter') {
                      event.stopPropagation();
                      void onDeleteSession(session.id);
                    }
                  }}
                >
                  ×
                </span>
              )}
            </button>
          );
        })}
      </div>

      <div className="codex-sidebar-provider" title={connectionLabel}>
        <span className="codex-session-dot is-running" aria-hidden="true" />
        <span className="codex-sidebar-provider-label">{connectionLabel}</span>
      </div>

      {showNewTask && (
        <div className="codex-modal-backdrop" onMouseDown={() => setShowNewTask(false)}>
          <section className="codex-new-task-dialog" onMouseDown={event => event.stopPropagation()} aria-label="New task">
            <div className="codex-dialog-header">
              <div>
                <h2>New task</h2>
                <p>Pick the folder Hermes should work in. The backend is remembered for the next task.</p>
              </div>
              <button className="codex-icon-button" onClick={() => setShowNewTask(false)} aria-label="Close">×</button>
            </div>

            <label className="codex-field-label" htmlFor="workspace-path">Workspace (optional)</label>
            <div className="codex-path-picker">
              <input
                id="workspace-path"
                ref={repositoryRef}
                className="codex-input"
                value={repository}
                onChange={event => setRepository(event.target.value)}
                placeholder="Select a folder"
                onKeyDown={event => {
                  if (event.key === 'Enter') startTask();
                }}
              />
              <button
                className="codex-button codex-button-secondary"
                onClick={async () => {
                  const picked = await onPickRepository();
                  if (picked) setRepository(picked);
                }}
              >
                Browse
              </button>
            </div>

            <div className="codex-task-options">
              <label className="codex-field-label" htmlFor="task-provider">Backend</label>
              <select
                id="task-provider"
                className="codex-select"
                value={provider}
                onChange={event => setProvider(event.target.value as Provider)}
              >
                <option value="hermes">Hermes Agent (models from your Hermes config)</option>
                <option value="codex">Codex CLI</option>
              </select>
              <label className="codex-field-label" htmlFor="task-branch">Branch override</label>
              <input
                id="task-branch"
                className="codex-input"
                value={branch}
                onChange={event => setBranch(event.target.value)}
                placeholder="Use current branch"
              />
            </div>

            <div className="codex-dialog-actions">
              <button className="codex-button codex-button-secondary" onClick={() => setShowNewTask(false)}>Cancel</button>
              <button className="codex-button codex-button-primary" onClick={startTask}>Start task</button>
            </div>
          </section>
        </div>
      )}
    </aside>
  );
}

function sessionLabel(repository?: string) {
  const trimmed = repository?.trim() || '';
  if (!trimmed) return 'Untitled task';
  const parts = trimmed.split(/[\\/]/).filter(Boolean);
  return parts[parts.length - 1] || trimmed;
}

function backendLabel(session: SessionRecord) {
  const isCodex = session.agent === 'codex' || session.provider === 'codex'
    || session.provider === 'remote_llamacpp' || session.provider === 'lan' || session.provider === 'ollama';
  const backend = isCodex ? 'Codex CLI' : 'Hermes';
  const model = session.model ? session.model.split('/').pop() : '';
  return [backend, model].filter(Boolean).join(' · ');
}

function formatUpdatedAt(updatedAt: number) {
  const minutes = Math.max(1, Math.round((Date.now() - updatedAt) / 60_000));
  if (minutes < 60) return `${minutes}m ago`;
  const hours = Math.round(minutes / 60);
  if (hours < 24) return `${hours}h ago`;
  return `${Math.round(hours / 24)}d ago`;
}
