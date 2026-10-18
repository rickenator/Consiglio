/**
 * Agent Adapter Interface
 *
 * Defines the contract for the Codex agent adapter. Consiglio wraps the Codex
 * CLI and normalizes its output into a unified event stream that the UI understands.
 */

import type { IPty } from 'node-pty';
import type { AgentId } from './agent-readiness.ts';

// ─── Unified Event Types ──────────────────────────────────────────────────────

export interface AgentEvent {
  id: string;
  type: 'prompt' | 'response' | 'code' | 'console' | 'error' | 'approval_request'
       | 'system' | 'files' | 'interrupted';
  content: string;
  metadata?: Record<string, unknown>;
  timestamp: number;
  session_id: string;
}

// ─── Unified Approval Request ─────────────────────────────────────────────────

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

export interface AgentSession {
  sessionId: string;
  pty: IPty | null;
  repository: string;
  branch: string;
  adapter: AgentAdapter;
}

// ─── Session Options ──────────────────────────────────────────────────────────

export interface AgentSessionOptions {
  repository: string;
  branch?: string;
  agent: AgentId;
  model?: string;
  baseUrl?: string;
  apiKey?: string;
  customArgs?: string[];
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
  id: AgentId;
  name: string;
  installed: boolean;
  authenticated: boolean;
  version?: string;
  loginMessage?: string;
}

// ─── The Adapter Interface ────────────────────────────────────────────────────

export interface AgentAdapter {
  launch(options: AgentSessionOptions): Promise<AgentSession>;
  sendPrompt(sessionId: string, input: string): Promise<string>;
  resolveApproval?(sessionId: string, approvalId: string, approved: boolean): Promise<boolean>;
  stopSession(sessionId: string): Promise<boolean>;
  reconnectSession(sessionId: string): Promise<boolean>;
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
