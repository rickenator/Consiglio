/**
 * Agent Adapter Interface
 *
 * Defines the contract for pluggable AI agent adapters. Each adapter wraps a
 * different CLI-based AI agent (Codex, Open Interpreter, Aider, etc.) and
 * normalizes its output into a unified event stream that the UI understands.
 *
 * The rest of Consiglio talks to this interface — never to a concrete agent.
 */

import type { IPty } from 'node-pty';

// ─── Unified Event Types ──────────────────────────────────────────────────────

/**
 * All agents emit events through this unified type. The UI renders based on
 * `type`, not on which agent produced the event.
 */
export interface AgentEvent {
  id: string;
  type: 'prompt' | 'response' | 'code' | 'console' | 'error' | 'approval_request'
       | 'system' | 'files' | 'interrupted' | 'tool_call';
  content: string;
  metadata?: Record<string, unknown>;
  timestamp: number;
  session_id: string;
}

// ─── Unified Approval Request ─────────────────────────────────────────────────

/**
 * All agents use the same approval flow. The UI shows one consistent dialog
 * regardless of whether the code came from Codex (shell command) or OI (Python).
 */
export interface AgentApproval {
  id: string;
  sessionId: string;
  command: string;
  code?: string;
  language?: string;
  workingDir: string;
  sandboxPolicy?: string;
  affectedPaths?: string[];
  timestamp: number;
  status: 'pending' | 'approved' | 'rejected';
}

// ─── Session Handle ───────────────────────────────────────────────────────────

/**
 * A running session — PTY + metadata. The adapter owns the PTY lifecycle.
 */
export interface AgentSession {
  sessionId: string;
  pty: IPty | null;
  repository: string;
  branch: string;
  adapter: AgentAdapter;
  /** Model the agent has bound to this session, when it owns the catalogue. */
  model?: string;
}

// ─── Session Options ──────────────────────────────────────────────────────────

/**
 * What the app passes when creating a session. The adapter interprets these
 * options according to its own agent's requirements.
 */
export interface AgentSessionOptions {
  repository: string;
  branch?: string;
  agent: 'hermes' | 'codex' | 'open-interpreter' | 'aider' | 'claude-code';
  model?: string;
  baseUrl?: string;
  apiKey?: string;
  customArgs?: string[];
  /** Reattach to an agent-owned session instead of creating a new one. */
  resumeSessionId?: string;
  /**
   * Fully resolved endpoint for the session's provider, computed by the caller
   * (which owns app settings and LAN discovery). Adapters must NOT read
   * provider settings themselves — a bare `options.model`/`options.baseUrl` is
   * absent for every local/LAN session and silently yields an empty model.
   */
  providerConfig?: {
    baseUrl?: string;
    model?: string;
    apiKey?: string;
    lanProvider?: { id: string; name: string; host: string; port: number; model: string; apiKey?: string };
  };
  /** Environment for the agent process, with secrets and provider URLs applied. */
  env?: NodeJS.ProcessEnv;
  /** Session identity owned by the caller, so reconnect can reuse the record id. */
  sessionId?: string;
  codexThreadId?: string;
  codexPath?: string;
  localProviderBehavior?: {
    isolateProfile?: boolean;
    enableWebSearch?: boolean;
    enableMultiAgent?: boolean;
  };
  // Agent-specific config (OI profiles, Codex providers, etc.)
  [key: string]: unknown;
}

// ─── Agent Detection ──────────────────────────────────────────────────────────

export interface AgentInfo {
  id: 'hermes' | 'codex' | 'open-interpreter' | 'aider' | 'claude-code';
  name: string;
  installed: boolean;
  authenticated: boolean;
  version?: string;
  loginMessage?: string;
}

// ─── The Adapter Interface ────────────────────────────────────────────────────

/**
 * Core adapter interface. Each agent gets its own implementation.
 *
 * The adapter owns:
 * - PTY lifecycle (spawn, kill)
 * - Event parsing (normalize agent output → AgentEvent[])
 * - Approval handling (emit and resolve AgentApproval requests)
 * - Session persistence (save/restore session state)
 */
export interface AgentAdapter {
  launch(options: AgentSessionOptions): Promise<AgentSession>;

  sendPrompt(sessionId: string, input: string): Promise<string>;

  /**
   * Send one single-use approve/reject decision to the exact blocked session
   * that emitted the approval ID. Legacy concrete adapters may omit this while
   * the registry wraps them in ApprovalAwareAdapter.
   */
  resolveApproval?(sessionId: string, approvalId: string, approved: boolean): Promise<boolean>;

  stopSession(sessionId: string): Promise<boolean>;

  reconnectSession(sessionId: string): Promise<boolean>;
}

// ─── Agent Detection ──────────────────────────────────────────────────────────

export function detectAgents(emitters: EventEmitters): AgentInfo[] {
  const results: AgentInfo[] = [];

  try {
    const codexAdapter = new (require('./adapters/codex-adapter').CodexAdapter)(emitters);
    results.push(...(codexAdapter as any).detectAvailable());
  } catch {
    results.push({ id: 'codex', name: 'Codex CLI', installed: false, authenticated: false });
  }

  try {
    const oiAdapter = new (require('./adapters/open-interpreter-adapter').OpenInterpreterAdapter)(emitters);
    results.push(...(oiAdapter as any).detectAvailable());
  } catch {
    results.push({ id: 'open-interpreter', name: 'Open Interpreter', installed: false, authenticated: false });
  }

  try {
    const aiderAdapter = new (require('./adapters/aider-adapter').AiderAdapter)(emitters);
    results.push(...(aiderAdapter as any).detectAvailable());
  } catch {
    results.push({ id: 'aider', name: 'Aider', installed: false, authenticated: false });
  }

  try {
    const claudeAdapter = new (require('./adapters/claude-code-adapter').ClaudeCodeAdapter)(emitters);
    results.push(...(claudeAdapter as any).detectAvailable());
  } catch {
    results.push({ id: 'claude-code', name: 'Claude Code', installed: false, authenticated: false });
  }

  return results;
}

// ─── Event Emitter Helper ─────────────────────────────────────────────────────

export interface EventEmitters {
  emitEvent(event: AgentEvent): void;

  emitApproval(approval: AgentApproval): void;

  emitTerminalOutput(sessionId: string, data: string): void;

  /**
   * Emit the agent's resumable thread/chat identifier when it becomes known.
   * Optional: agents without resumable threads never call it.
   */
  emitThreadId?(sessionId: string, threadId: string): void;
}

// ─── Event Parser Helper ──────────────────────────────────────────────────────

export interface EventParser {
  parse(raw: string, sessionId: string): AgentEvent[];

  parseApproval?(raw: string, sessionId: string): AgentApproval | null;
}

// ─── Model Discovery (agent-owned model catalogues) ───────────────────────────

/**
 * Agents that own provider/model configuration — Hermes resolves models from the
 * operator's own config — expose their catalogue so the shell can offer a picker
 * instead of asking the user to type endpoints.
 */
export interface ModelAwareAdapter {
  availableModels(): AgentModel[];
  setSessionModel(sessionId: string, modelId: string): Promise<boolean>;
}

export interface AgentModel {
  id: string;
  name: string;
  description?: string;
}

export function isModelAware(adapter: unknown): adapter is ModelAwareAdapter {
  const candidate = adapter as Partial<ModelAwareAdapter> | null;
  return Boolean(candidate && typeof candidate.availableModels === 'function' && typeof candidate.setSessionModel === 'function');
}
