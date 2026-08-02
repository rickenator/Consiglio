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
}

// ─── Event Parser Helper ──────────────────────────────────────────────────────

export interface EventParser {
  parse(raw: string, sessionId: string): AgentEvent[];
  parseApproval?(raw: string, sessionId: string): AgentApproval | null;
}
