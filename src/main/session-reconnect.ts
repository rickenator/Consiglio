/**
 * Reconnect bookkeeping for main-process sessions (audit C1).
 *
 * A stored task is reconnected under its ORIGINAL session id (the adapter
 * accepts a caller-owned id), so the record, events and renderer/mobile
 * subscriptions stay attached. These helpers close the remaining gaps:
 *  - concurrent reconnects of the same id (desktop + mobile, double clicks)
 *    share one attempt instead of each spawning an agent;
 *  - a prompt sent to a recorded-but-not-live task (e.g. right after an app
 *    restart) reconnects it first instead of silently returning false.
 */

export type ReconnectFn = (sessionId: string) => Promise<boolean>;

export function createReconnectGate(reconnect: ReconnectFn): ReconnectFn {
  const inflight = new Map<string, Promise<boolean>>();
  return (sessionId: string) => {
    const existing = inflight.get(sessionId);
    if (existing) return existing;
    const attempt = (async () => {
      try {
        return await reconnect(sessionId);
      } finally {
        inflight.delete(sessionId);
      }
    })();
    inflight.set(sessionId, attempt);
    return attempt;
  };
}

export interface EnsureLiveDeps {
  isLive: (sessionId: string) => boolean;
  hasRecord: (sessionId: string) => boolean;
  reconnect: ReconnectFn;
}

/** True when the session is live, reconnecting a recorded session if needed. */
export async function ensureSessionLive(sessionId: string, deps: EnsureLiveDeps): Promise<boolean> {
  if (deps.isLive(sessionId)) return true;
  if (!deps.hasRecord(sessionId)) return false;
  const ok = await deps.reconnect(sessionId);
  return ok && deps.isLive(sessionId);
}
