/**
 * Bridge ACP notifications into Consiglio's event/approval vocabulary.
 *
 * Every visual surface in Consiglio maps to an inspectsable event, so this is
 * the single place where ACP's vocabulary becomes Consiglio's.
 */

import type { AgentApproval, AgentEvent } from '../agent-adapter';

export interface AcpSessionUpdate {
  sessionUpdate?: string;
  content?: { type?: string; text?: string };
  title?: string;
  kind?: string;
  toolCallId?: string;
  status?: string;
  [key: string]: unknown;
}

export interface AcpPermissionRequest {
  sessionId?: string;
  toolCall?: { title?: string; kind?: string; toolCallId?: string };
  options?: Array<{ optionId?: string; name?: string; kind?: string }>;
  [key: string]: unknown;
}

export interface ConsiglioApproval extends AgentApproval {
  optionIds: string[];
}

function text(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

/**
 * Map one `session/update` notification. Returns null for updates Consiglio
 * tracks but does not render inline (usage metering, command hints).
 */
export function mapSessionUpdate(sessionId: string, update: AcpSessionUpdate): AgentEvent | null {
  const timestamp = Date.now();
  const kind = update.sessionUpdate;

  if (kind === 'agent_message_chunk') {
    const chunk = text(update.content?.text);
    if (!chunk.trim()) return null;
    return { id: `acp_${timestamp}`, type: 'response', content: chunk, timestamp, session_id: sessionId };
  }

  if (kind === 'tool_call' || kind === 'tool_call_update') {
    const label = text(update.title) || text(update.kind) || 'Tool call';
    if (update.status === 'failed') {
      return { id: `acp_${timestamp}`, type: 'error', content: label, timestamp, session_id: sessionId };
    }
    return { id: `acp_${timestamp}`, type: 'tool_call', content: label, timestamp, session_id: sessionId };
  }

  // session_info_update carries the session title (derived from the first prompt)
  // and usage_update carries metering — neither belongs in the transcript.
  return null;
}

/**
 * Map an ACP `session/request_permission` request into a Consiglio approval.
 * ACP's option ids are the reply protocol, so they travel with the approval —
 * approving is "select allow_once", not "write y".
 */
export function mapPermissionRequest(params: AcpPermissionRequest, fallbackSessionId: string): ConsiglioApproval {
  const options = (params.options || [])
    .map(option => text(option.optionId))
    .filter(Boolean);
  const toolCall = params.toolCall || {};
  const command = text(toolCall.title) || text(toolCall.kind) || 'Agent requested permission';

  return {
    id: `appr_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`,
    sessionId: text(params.sessionId) || fallbackSessionId,
    command,
    workingDir: '',
    timestamp: Date.now(),
    status: 'pending',
    optionIds: options,
  } as ConsiglioApproval;
}

/** Pick the ACP option id that answers an approve/reject decision. */
export function optionIdForDecision(optionIds: string[], approved: boolean): string | null {
  if (optionIds.length === 0) return null;
  const wants = approved
    ? ['allow_once', 'allow_always', 'allow', 'approve', 'yes']
    : ['reject_once', 'reject_always', 'reject', 'deny', 'no'];
  const match = optionIds.find(id => wants.includes(id.toLowerCase()));
  if (match) return match;
  // Fall back to position: ACP lists allow options before reject options.
  return approved ? optionIds[0] : optionIds[optionIds.length - 1];
}
