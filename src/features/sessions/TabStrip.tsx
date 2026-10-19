import React from 'react';

interface Props {
  tabs: string[];
  active: string | null;
  sessions: SessionRecord[];
  onSelect: (sessionId: string) => void;
  onClose: (sessionId: string) => void;
  onNewTask: () => void;
}

function tabLabel(session: SessionRecord | undefined): string {
  if (!session) return 'Task';
  const trimmed = (session.repository || '').trim();
  if (!trimmed) return 'Task';
  const parts = trimmed.split(/[\\/]/).filter(Boolean);
  return parts[parts.length - 1] || trimmed;
}

function statusClass(session: SessionRecord | undefined): string {
  if (!session) return 'stopped';
  return session.status || 'stopped';
}

/**
 * Open-task tabs.
 *
 * Tabs exist so switching tasks never rebuilds a conversation: App keeps every
 * open session's timeline mounted and only hides the inactive ones.
 */
export default function TabStrip({ tabs, active, sessions, onSelect, onClose, onNewTask }: Props) {
  const byId = new Map(sessions.map(session => [session.id, session]));

  return (
    <div className="codex-tabstrip" role="tablist">
      {tabs.map(sessionId => {
        const session = byId.get(sessionId);
        const isActive = sessionId === active;
        return (
          <div
            key={sessionId}
            role="tab"
            aria-selected={isActive}
            tabIndex={0}
            className={`codex-tab${isActive ? ' is-active' : ''}`}
            onClick={() => onSelect(sessionId)}
            onKeyDown={event => {
              if (event.key === 'Enter' || event.key === ' ') onSelect(sessionId);
            }}
            title={session?.repository || sessionId}
          >
            <span className={`codex-tab-dot is-${statusClass(session)}`} aria-hidden="true" />
            <span className="codex-tab-label">{tabLabel(session)}</span>
            {session?.model && <span className="codex-tab-model">{session.model}</span>}
            <button
              type="button"
              className="codex-tab-close"
              aria-label={`Close ${tabLabel(session)}`}
              onClick={event => {
                event.stopPropagation();
                onClose(sessionId);
              }}
            >
              ×
            </button>
          </div>
        );
      })}
      <button type="button" className="codex-tab-new" onClick={onNewTask} title="New task (Ctrl+N)">
        + New task
      </button>
    </div>
  );
}
