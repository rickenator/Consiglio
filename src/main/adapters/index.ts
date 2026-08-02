/**
 * Agent Adapter Registry
 *
 * Factory function that returns the Codex adapter. Consiglio uses Codex as
 * the agent runtime and connects it to local or online providers.
 */

// Safe electron imports — may be undefined in test environments
let _BrowserWindow: typeof import('electron').BrowserWindow | null = null;
let _ipcMain: typeof import('electron').ipcMain | null = null;
let _Electron: typeof import('electron') | null = null;

try {
  const electron = require('electron') as typeof import('electron');
  _BrowserWindow = electron.BrowserWindow;
  _ipcMain = electron.ipcMain;
  _Electron = electron;
} catch {
  // Electron not available (e.g., in Node test runner)
}

function getBrowserWindow() { return _BrowserWindow; }
function getIpcMain() { return _ipcMain; }
function getElectron() { return _Electron; }

import { isTrustedRendererUrl } from '../app-protocol.ts';
import { ApprovalAwareAdapter, type AdapterCore } from '../approval-aware-adapter.ts';
import { agentApprovalRouter } from '../approval-router.ts';
import { detectAgentReadiness } from '../agent-readiness.ts';
import type { AgentAdapter, AgentApproval, EventEmitters } from '../agent-adapter';
import { resolveCodexCommand } from '../platform.ts';
import { CodexAdapter } from './codex-adapter.ts';

export const AGENT_READINESS_CHANNEL = 'agents:readiness';
export const AGENT_APPROVAL_RESOLVE_CHANNEL = 'agents:resolve-approval';
export const AGENT_APPROVAL_PENDING_CHANNEL = 'agents:pending-approvals';

type AgentId = import('../agent-readiness.ts').AgentId;

function assertTrustedRenderer(event: import('electron').IpcMainInvokeEvent): void {
  const senderUrl = event.senderFrame?.url || event.sender.getURL();
  const isMainFrame = event.senderFrame === event.sender.mainFrame;
  if (!isMainFrame || !isTrustedRendererUrl(senderUrl)) {
    throw new Error('Rejected agent request from an untrusted renderer');
  }
}

function approvalRecord(approval: AgentApproval) {
  return {
    ...approval,
    sandboxPolicy: approval.sandboxPolicy || 'agent-controlled',
    affectedPaths: approval.affectedPaths || [],
  };
}

function broadcast(channel: string, payload: unknown): void {
  const BW = getBrowserWindow();
  if (!BW) return;
  for (const win of BW.getAllWindows()) {
    if (!win.isDestroyed()) win.webContents.send(channel, payload);
  }
}

function registerAgentHandlers(): void {
  const ipc = getIpcMain();
  if (!ipc) return;
  ipc.removeHandler(AGENT_READINESS_CHANNEL);
  ipc.handle(AGENT_READINESS_CHANNEL, event => {
    assertTrustedRenderer(event);
    return detectAgentReadiness({
      commandResolver: (agentId, env) => {
        if (agentId !== 'codex') return null;
        const command = resolveCodexCommand({ env });
        return command
          ? { command: command.executable, prefixArgs: command.prefixArgs }
          : null;
      },
    });
  });

  ipc.removeHandler(AGENT_APPROVAL_RESOLVE_CHANNEL);
  ipc.handle(AGENT_APPROVAL_RESOLVE_CHANNEL, async (event, input: {
    approvalId?: unknown;
    approved?: unknown;
    sessionId?: unknown;
  }) => {
    assertTrustedRenderer(event);
    if (!input || typeof input.approvalId !== 'string' || typeof input.approved !== 'boolean') {
      throw new Error('Invalid approval resolution request');
    }
    const expectedSessionId = typeof input.sessionId === 'string' ? input.sessionId : undefined;
    const result = await agentApprovalRouter.resolve(input.approvalId, input.approved, expectedSessionId);
    if (result.ok) {
      broadcast('codex:approval-processed', { id: input.approvalId, approved: input.approved });
    }
    return result;
  });

  ipc.removeHandler(AGENT_APPROVAL_PENDING_CHANNEL);
  ipc.handle(AGENT_APPROVAL_PENDING_CHANNEL, (event, sessionId?: unknown) => {
    assertTrustedRenderer(event);
    return agentApprovalRouter
      .pendingApprovals(typeof sessionId === 'string' ? sessionId : undefined)
      .map(approvalRecord);
  });
}

registerAgentHandlers();

function createConcreteAdapter(_agent: AgentId, emitters: EventEmitters): AdapterCore {
  return new CodexAdapter(emitters);
}

const KNOWN_AGENTS = ['codex'] as const;

export function getAdapter(agent: AgentId, emitters: EventEmitters): AgentAdapter {
  if (!KNOWN_AGENTS.includes(agent as never)) {
    throw new Error(`Unknown agent: ${agent}`);
  }
  let decorated: ApprovalAwareAdapter | undefined;
  const routedEmitters: EventEmitters = {
    ...emitters,
    emitApproval: (approval: AgentApproval) => {
      if (!decorated?.trackApproval(approval)) return;
      broadcast('codex:approval-request', approvalRecord(approval));
    },
  };

  const concrete = createConcreteAdapter(agent, routedEmitters);
  decorated = new ApprovalAwareAdapter(agent, concrete);
  return decorated;
}

export { CodexAdapter } from './codex-adapter.ts';
