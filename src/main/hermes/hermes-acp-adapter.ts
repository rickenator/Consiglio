/**
 * Hermes ACP adapter — Consiglio's default backend.
 *
 * Hermes owns the session: `session/new` returns the durable id (local-<hash>)
 * that `~/.hermes/state.db` persists, `session/resume` reattaches to it, and the
 * model catalogue comes from the operator's Hermes config rather than from
 * endpoints typed into this app.
 */

import path from 'path';
import { execFileSync } from 'child_process';
import { app } from 'electron';

import type {
  AgentAdapter,
  AgentApproval,
  AgentInfo,
  AgentModel,
  AgentSession,
  AgentSessionOptions,
  EventEmitters,
  ModelAwareAdapter,
} from '../agent-adapter';
import { agentApprovalRouter } from '../approval-router.ts';
import { AcpClient, AcpUnavailableError, resolveResumedSessionId, spawnHermesAcp, type SessionCreated } from './acp-client.ts';
import {
  mapPermissionRequest,
  mapSessionUpdate,
  optionIdForDecision,
  type AcpPermissionRequest,
  type AcpSessionUpdate,
  type ConsiglioApproval,
} from './acp-events.ts';
import { resolveHermesCommand } from './hermes-command.ts';

/** How long a permission request waits for a human before ACP auto-denies it. */
const PERMISSION_TIMEOUT_MS = 120_000;

interface PendingPermission {
  approval: ConsiglioApproval;
  resolve: (optionId: string | null) => void;
  timer: NodeJS.Timeout;
}

interface HermesSessionState {
  sessionId: string;
  repository: string;
  branch: string;
  model?: string;
  responseBuffer: string;
  active: boolean;
  /** Tool-call notifications repeat per status change; emit each call once. */
  seenToolCalls: Set<string>;
  availableCommands?: unknown;
}

export class HermesAcpAdapter implements AgentAdapter, ModelAwareAdapter {
  private client: AcpClient | null = null;
  private readonly emitters: EventEmitters;
  private readonly sessions = new Map<string, HermesSessionState>();
  private readonly pendingPermissions = new Map<string, PendingPermission>();
  private readonly models: AgentModel[] = [];
  private startupError = '';

  constructor(emitters: EventEmitters) {
    this.emitters = emitters;
  }

  // ─── Detection ───────────────────────────────────────────────────────────────

  detectAvailable(): AgentInfo[] {
    const resolved = resolveHermesCommand();
    let version = '';
    let installed = false;
    let loginMessage = 'Hermes Agent was not found. Install Hermes or set HERMES_BIN.';

    try {
      version = execFileSync(resolved?.command || 'hermes', [...(resolved?.prefixArgs || []), '--version'], {
        encoding: 'utf8',
        stdio: ['ignore', 'pipe', 'pipe'],
      }).trim().split('\n')[0] || '';
      installed = Boolean(version);
      loginMessage = version
        ? `Hermes ${version} is available. Models and providers come from your Hermes configuration.`
        : loginMessage;
    } catch (error) {
      loginMessage = error instanceof Error ? error.message : loginMessage;
    }

    return [{
      id: 'hermes',
      name: 'Hermes Agent',
      installed,
      // Hermes carries its own provider auth; a missing model makes it fail at prompt time, not here.
      authenticated: installed,
      version,
      loginMessage,
    }];
  }

  // ─── Client lifecycle ────────────────────────────────────────────────────────

  private async ensureClient(): Promise<AcpClient> {
    if (this.client?.isRunning) return this.client;

    const logPath = path.join(app.getPath('userData'), 'hermes-acp.log');
    const client = new AcpClient(spawnHermesAcp(logPath));
    client.setNotificationHandler(frame => this.handleNotification(frame));
    client.setRequestHandler(frame => this.handleAgentRequest(frame));

    try {
      await client.initialize();
    } catch (error) {
      this.startupError = error instanceof Error ? error.message : String(error);
      client.dispose();
      throw new AcpUnavailableError(
        `Consiglio could not start Hermes: ${this.startupError}. Run \`hermes acp --check\` in a terminal.`,
      );
    }

    this.client = client;
    this.startupError = '';
    return client;
  }

  // ─── Session lifecycle ───────────────────────────────────────────────────────

  async launch(options: AgentSessionOptions): Promise<AgentSession> {
    const client = await this.ensureClient();
    const repository = path.resolve(options.repository);

    let created: SessionCreated;
    if (options.resumeSessionId) {
      const requested = options.resumeSessionId;
      try {
        created = await client.resumeSession(requested, repository);
      } catch {
        try {
          created = await client.loadSession(requested, repository);
        } catch {
          // A record written before Hermes became the backend has no Hermes
          // session behind it; start a fresh one instead of failing the IPC call.
          created = await client.newSession({ cwd: repository });
          this.emitters.emitEvent({
            id: `evt_${Date.now()}`,
            type: 'system',
            content: `Hermes has no stored session for ${requested}; started a new one in ${repository}.`,
            timestamp: Date.now(),
            session_id: created.sessionId,
          });
        }
      }
      // resume/load answer with provenance for the SAME session rather than a new
      // id, so the requested id is authoritative unless Hermes reports another.
      created = { ...created, sessionId: resolveResumedSessionId(created, requested) };
    } else {
      created = await client.newSession({ cwd: repository });
    }

    if (!created?.sessionId) {
      throw new Error('Hermes did not return a session id.');
    }

    this.rememberModels(created);

    this.sessions.set(created.sessionId, {
      sessionId: created.sessionId,
      repository,
      branch: options.branch || '',
      model: created.models?.currentModelId,
      responseBuffer: '',
      active: false,
      seenToolCalls: new Set(),
    });

    return {
      sessionId: created.sessionId,
      // ACP has no PTY: the session is a JSON-RPC conversation, not a terminal.
      pty: null,
      repository,
      branch: options.branch || '',
      adapter: this,
      model: created.models?.currentModelId,
    };
  }

  async sendPrompt(sessionId: string, input: string): Promise<string> {
    const prompt = input.trim();
    if (!prompt) return '';

    const session = this.sessions.get(sessionId);
    if (!session) return '';
    if (session.active) return '';

    const client = await this.ensureClient();
    session.responseBuffer = '';
    session.seenToolCalls.clear();
    session.active = true;

    try {
      await client.prompt(sessionId, prompt);
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      this.emitters.emitEvent({
        id: `evt_${Date.now()}`,
        type: 'error',
        content: `Hermes turn failed: ${message}`,
        timestamp: Date.now(),
        session_id: sessionId,
      });
    } finally {
      session.active = false;
    }

    const response = session.responseBuffer.trim();
    session.responseBuffer = '';
    // The turn's answer must reach the UI and the store: without this event the
    // transcript would show a prompt and then nothing at all.
    if (response) {
      this.emitters.emitEvent({
        id: `evt_${Date.now()}`,
        type: 'response',
        content: response,
        timestamp: Date.now(),
        session_id: sessionId,
      });
    }
    return response;
  }

  async stopSession(sessionId: string): Promise<boolean> {
    const session = this.sessions.get(sessionId);
    for (const [approvalId, pending] of this.pendingPermissions) {
      if (pending.approval.sessionId === sessionId) {
        clearTimeout(pending.timer);
        pending.resolve(null);
        this.pendingPermissions.delete(approvalId);
      }
    }
    agentApprovalRouter.clearSession(sessionId);

    if (this.client?.isRunning && session) {
      try {
        await this.client.cancelSession(sessionId);
      } catch {
        // The session may already be finished; forgetting it is still correct.
      }
    }
    this.sessions.delete(sessionId);
    return true;
  }

  async reconnectSession(sessionId: string): Promise<boolean> {
    const state = this.sessions.get(sessionId);
    if (state) return true;
    return false;
  }

  // ─── Approvals ───────────────────────────────────────────────────────────────

  /** Called by the adapter registry when Hermes asks for permission. */
  trackApproval(approval: AgentApproval): boolean {
    const pending = this.pendingPermissions.get(approval.id);
    if (!pending) return false;
    return agentApprovalRouter.register(approval, this);
  }

  async resolveApproval(sessionId: string, approvalId: string, approved: boolean): Promise<boolean> {
    const pending = this.pendingPermissions.get(approvalId);
    if (!pending || pending.approval.sessionId !== sessionId) return false;

    clearTimeout(pending.timer);
    this.pendingPermissions.delete(approvalId);
    pending.resolve(optionIdForDecision(pending.approval.optionIds, approved));
    return true;
  }

  // ─── Models ──────────────────────────────────────────────────────────────────

  availableModels(): AgentModel[] {
    return [...this.models];
  }

  async setSessionModel(sessionId: string, modelId: string): Promise<boolean> {
    if (!this.sessions.has(sessionId)) return false;
    const client = await this.ensureClient();
    try {
      await client.setSessionModel(sessionId, modelId);
      const session = this.sessions.get(sessionId);
      if (session) session.model = modelId;
      return true;
    } catch {
      return false;
    }
  }

  private rememberModels(created: SessionCreated): void {
    const catalogue = created.models?.availableModels || [];
    for (const model of catalogue) {
      if (!model?.modelId) continue;
      if (this.models.some(existing => existing.id === model.modelId)) continue;
      this.models.push({ id: model.modelId, name: model.name || model.modelId, description: model.description });
    }
    const session = this.sessions.get(created.sessionId);
    if (session && created.models?.currentModelId) session.model = created.models.currentModelId;
  }

  // ─── Frame handling ──────────────────────────────────────────────────────────

  private handleNotification(frame: { method?: string; params?: unknown }): void {
    const params = frame.params as { sessionId?: string; update?: AcpSessionUpdate } | undefined;
    const sessionId = params?.sessionId;
    const update = params?.update;
    if (!sessionId || !update) return;

    const session = this.sessions.get(sessionId);

    if (update.sessionUpdate === 'agent_message_chunk') {
      const chunk = update.content?.text || '';
      if (session) session.responseBuffer += chunk;
      // A session this app no longer tracks still streams to the UI.
      if (!session && chunk.trim()) {
        this.emitters.emitEvent({
          id: `acp_${Date.now()}`,
          type: 'response',
          content: chunk,
          timestamp: Date.now(),
          session_id: sessionId,
        });
      }
      return;
    }

    if (update.sessionUpdate === 'available_commands_update') {
      if (session) session.availableCommands = update;
      return;
    }

    if (update.sessionUpdate === 'usage_update') return;

    const event = mapSessionUpdate(sessionId, update);
    if (!event) return;

    if (session && (event.type === 'tool_call' || event.type === 'error')) {
      const key = `${update.toolCallId || event.content}:${update.status || ''}`;
      if (session.seenToolCalls.has(key)) return;
      session.seenToolCalls.add(key);
    }

    this.emitters.emitEvent(event);
  }

  private async handleAgentRequest(frame: { id?: number | string; method?: string; params?: unknown }) {
    if (frame.method !== 'session/request_permission') {
      return { outcome: { outcome: 'cancelled' } };
    }

    const params = (frame.params || {}) as AcpPermissionRequest;
    const fallbackSession = [...this.sessions.keys()][0] || '';
    const approval = mapPermissionRequest(params, fallbackSession);

    const decided = new Promise<string | null>(resolve => {
      const timer = setTimeout(() => {
        this.pendingPermissions.delete(approval.id);
        resolve(null);
      }, PERMISSION_TIMEOUT_MS);
      this.pendingPermissions.set(approval.id, { approval, resolve, timer });
    });

    // The registry routes this through the approval router and the UI banner.
    this.emitters.emitApproval(approval);

    const optionId = await decided;
    if (!optionId) return { outcome: { outcome: 'cancelled' } };
    return { outcome: { outcome: 'selected', optionId } };
  }
}
