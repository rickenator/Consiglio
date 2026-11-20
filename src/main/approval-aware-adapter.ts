import type {
  AgentAdapter,
  AgentApproval,
  AgentSession,
} from './agent-adapter.ts';
import { agentApprovalRouter } from './approval-router.ts';

export type ApprovalAwareAgentId = import('./agent-readiness.ts').AgentId;
export type AdapterCore = Omit<AgentAdapter, 'resolveApproval'> & Partial<Pick<AgentAdapter, 'resolveApproval'>>;

interface PendingProtocolApproval {
  approval: AgentApproval;
  approveInput: string;
  rejectInput: string;
}

/**
 * Owns session handles and pending approvals for the Codex adapter.
 * The decorator is returned as AgentSession.adapter, so approval IDs resolve to
 * the same session/PTY that emitted them.
 */
export class ApprovalAwareAdapter implements AgentAdapter {
  private readonly sessions = new Map<string, AgentSession>();
  private readonly pendingApprovals = new Map<string, PendingProtocolApproval>();
  private readonly agentId: ApprovalAwareAgentId;
  private readonly inner: AdapterCore;

  constructor(agentId: ApprovalAwareAgentId, inner: AdapterCore) {
    this.agentId = agentId;
    this.inner = inner;
  }

  trackApproval(approval: AgentApproval): boolean {
    if (approval.status !== 'pending') return false;
    if (this.pendingApprovals.has(approval.id)) return false;

    this.pendingApprovals.set(approval.id, {
      approval,
      approveInput: 'y\n',
      rejectInput: 'n\n',
    });

    if (!agentApprovalRouter.register(approval, this)) {
      this.pendingApprovals.delete(approval.id);
      return false;
    }
    return true;
  }

  async launch(options: import('./agent-adapter').AgentSessionOptions): Promise<AgentSession> {
    const session = await this.inner.launch(options);
    const ownedSession: AgentSession = { ...session, adapter: this };
    this.sessions.set(session.sessionId, ownedSession);
    return ownedSession;
  }

  sendPrompt(sessionId: string, input: string): Promise<string> {
    return this.inner.sendPrompt(sessionId, input);
  }

  async resolveApproval(sessionId: string, approvalId: string, approved: boolean): Promise<boolean> {
    const pending = this.pendingApprovals.get(approvalId);
    if (!pending || pending.approval.sessionId !== sessionId) return false;

    const session = this.sessions.get(sessionId);
    if (!session?.pty) return false;

    this.pendingApprovals.delete(approvalId);
    try {
      session.pty.write(approved ? pending.approveInput : pending.rejectInput);
      return true;
    } catch {
      this.pendingApprovals.set(approvalId, pending);
      return false;
    }
  }

  async stopSession(sessionId: string): Promise<boolean> {
    for (const [approvalId, pending] of this.pendingApprovals) {
      if (pending.approval.sessionId === sessionId) {
        this.pendingApprovals.delete(approvalId);
      }
    }
    agentApprovalRouter.clearSession(sessionId);
    this.sessions.delete(sessionId);
    return this.inner.stopSession(sessionId);
  }

  reconnectSession(sessionId: string): Promise<boolean> {
    return this.inner.reconnectSession(sessionId);
  }
}

// ─── Approval argument sanitization ────────────────────────────────────────────
// Removes global auto-approval flags that would bypass the approval flow,
// and adds agent-specific overrides to prevent accidental auto-execution.

export function sanitizeApprovalArgs(
  agentId: string,
  args: string[],
): string[] {
  let filtered = args.filter(arg => arg !== '--yes');

  if (agentId === 'open-interpreter') {
    filtered = filtered.filter(arg => arg !== '--auto_run');
    if (!filtered.includes('--no_auto_run')) {
      filtered.push('--no_auto_run');
    }
  }

  return filtered;
}
