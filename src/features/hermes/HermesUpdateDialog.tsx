import React, { useState } from 'react';

interface Props {
  status: HermesUpdateStatus | null;
  autoUpdate: boolean;
  busy: boolean;
  onToggleAutoUpdate: (next: boolean) => void;
  onUpdate: () => void;
  onClose: () => void;
}

/**
 * Hermes update prompt shown at startup when a newer release exists.
 *
 * `hermes update` restarts the Hermes gateway and kills running agents, so the
 * auto-update switch only fires when no task is mid-response; otherwise the
 * dialog says why it deferred.
 */
export default function HermesUpdateDialog({
  status,
  autoUpdate,
  busy,
  onToggleAutoUpdate,
  onUpdate,
  onClose,
}: Props) {
  const [deferredReason, setDeferredReason] = useState<string | null>(null);

  if (!status || status.state !== 'available') return null;

  return (
    <div className="codex-modal-backdrop" onMouseDown={onClose}>
      <section className="codex-update-dialog" onMouseDown={event => event.stopPropagation()} aria-label="Hermes update">
        <div className="codex-dialog-header">
          <div>
            <h2>Hermes update available</h2>
            <p>{status.summary}</p>
          </div>
          <button className="codex-icon-button" onClick={onClose} aria-label="Dismiss">×</button>
        </div>

        <div className="codex-update-versions">
          <div>
            <span className="codex-update-label">Installed</span>
            <code>{status.currentVersion || 'unknown'}</code>
          </div>
          <div>
            <span className="codex-update-label">Available</span>
            <code>{status.availableVersion || 'newer release'}</code>
          </div>
          <div>
            <span className="codex-update-label">Channel</span>
            <code>{status.channel}</code>
          </div>
        </div>

        <label className="codex-update-checkbox">
          <input
            type="checkbox"
            checked={autoUpdate}
            onChange={event => {
              onToggleAutoUpdate(event.target.checked);
              if (event.target.checked) {
                setDeferredReason('Auto-update runs at startup only when no task is mid-response, because updating restarts the Hermes gateway.');
              }
            }}
          />
          <span>
            Update Hermes automatically on startup
            <span className="codex-help">
              Deferred while a task is answering — updating restarts the gateway and would kill it.
            </span>
          </span>
        </label>

        {deferredReason && <div className="codex-update-note">{deferredReason}</div>}

        <div className="codex-dialog-actions">
          <button className="codex-button codex-button-secondary" onClick={onClose} disabled={busy}>
            Later
          </button>
          <button className="codex-button codex-button-primary" onClick={onUpdate} disabled={busy}>
            {busy ? 'Updating…' : 'Update now'}
          </button>
        </div>
      </section>
    </div>
  );
}
