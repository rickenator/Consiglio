/**
 * ACP client: owns one `hermes acp` child process and speaks JSON-RPC over
 * stdio. The transport is injectable so the protocol layer is unit-testable
 * without a Hermes installation.
 */

import { spawn, type ChildProcessWithoutNullStreams } from 'child_process';
import fs from 'fs';
import path from 'path';

import { FrameDecoder, encodeFrame, type AcpFrame } from './acp-codec.ts';
import { resolveHermesCommand } from './hermes-command.ts';

export interface Transport {
  write(line: string): void;
  onData(cb: (chunk: string) => void): void;
  onExit(cb: (code: number) => void): void;
  kill(): void;
}

export interface InitializeResult {
  protocolVersion: number;
  agentCapabilities: {
    loadSession?: boolean;
    promptCapabilities?: { image?: boolean };
    sessionCapabilities?: { fork?: unknown; list?: unknown; resume?: unknown };
  };
  authMethods?: Array<{ id: string; name?: string }>;
}

export interface SessionCreated {
  sessionId: string;
  models?: {
    availableModels?: Array<{ modelId: string; name?: string; description?: string }>;
    currentModelId?: string;
  };
  modes?: { availableModes?: Array<{ id: string; name?: string; description?: string }>; currentModeId?: string };
  /** Hermes attaches provenance instead of a fresh id when resuming. */
  _meta?: {
    hermes?: {
      sessionProvenance?: {
        acpSessionId?: string;
        currentHermesSessionId?: string;
        rootHermesSessionId?: string;
        parentHermesSessionId?: string | null;
      };
    };
  };
}

export class AcpUnavailableError extends Error {}

/**
 * `session/resume` and `session/load` answer with provenance for the SAME
 * session instead of a fresh id, so the requested id is authoritative unless
 * Hermes reports another one.
 */
export function resolveResumedSessionId(created: SessionCreated | undefined, requested: string): string {
  const provenance = created?._meta?.hermes?.sessionProvenance;
  return created?.sessionId
    ?? provenance?.currentHermesSessionId
    ?? provenance?.acpSessionId
    ?? requested;
}

export function spawnHermesAcp(stderrLogPath: string): Transport {
  const resolved = resolveHermesCommand();
  const child: ChildProcessWithoutNullStreams = spawn(
    resolved?.command || 'hermes',
    [...(resolved?.prefixArgs || []), 'acp'],
    { stdio: ['pipe', 'pipe', 'pipe'] },
  );

  let log: fs.WriteStream | null = null;
  try {
    fs.mkdirSync(path.dirname(stderrLogPath), { recursive: true });
    log = fs.createWriteStream(stderrLogPath, { flags: 'a' });
  } catch {
    log = null;
  }
  if (log) child.stderr.on('data', chunk => log?.write(chunk));
  child.stdin.on('error', () => { /* the exit handler reports the failure */ });

  return {
    write: line => { try { child.stdin.write(line); } catch { /* pipe closed */ } },
    onData: cb => child.stdout.on('data', chunk => cb(chunk.toString())),
    onExit: cb => {
      child.on('exit', code => cb(code ?? 0));
      child.on('error', () => cb(1));
    },
    kill: () => { try { child.kill(); } catch { /* already gone */ } },
  };
}

type Pending = {
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
};

export class AcpClient {
  private readonly decoder = new FrameDecoder();
  private readonly transport: Transport;
  private nextId = 1;
  private readonly pending = new Map<number, Pending>();
  private exited = false;
  private exitReason = 'hermes acp is not running';
  private notificationHandler?: (frame: AcpFrame) => void;
  private requestHandler?: (frame: AcpFrame) => Promise<unknown>;

  constructor(transport: Transport) {
    this.transport = transport;
    transport.onData(chunk => this.consume(chunk));
    transport.onExit(code => {
      this.exited = true;
      this.exitReason = `hermes acp exited with code ${code}`;
      for (const [, waiter] of this.pending) waiter.reject(new Error(this.exitReason));
      this.pending.clear();
    });
  }

  private consume(chunk: string): void {
    for (const frame of this.decoder.push(chunk)) {
      if (!frame) continue;

      if (frame.id !== undefined && frame.method === undefined) {
        const waiter = this.pending.get(frame.id as number);
        if (!waiter) continue;
        this.pending.delete(frame.id as number);
        if (frame.error) waiter.reject(new Error(frame.error.message));
        else waiter.resolve(frame.result);
        continue;
      }

      if (frame.method && frame.id !== undefined) {
        // A request from the agent (e.g. session/request_permission).
        void this.handleAgentRequest(frame);
        continue;
      }

      this.notificationHandler?.(frame);
    }
  }

  private async handleAgentRequest(frame: AcpFrame): Promise<void> {
    if (!this.requestHandler) {
      this.reply(frame.id!, { outcome: { outcome: 'cancelled' } });
      return;
    }
    try {
      const result = await this.requestHandler(frame);
      this.reply(frame.id!, result ?? { outcome: { outcome: 'cancelled' } });
    } catch {
      this.reply(frame.id!, { outcome: { outcome: 'cancelled' } });
    }
  }

  private reply(id: number | string, result: unknown): void {
    this.transport.write(encodeFrame({ jsonrpc: '2.0', id, result }));
  }

  setNotificationHandler(handler: (frame: AcpFrame) => void): void {
    this.notificationHandler = handler;
  }

  setRequestHandler(handler: (frame: AcpFrame) => Promise<unknown>): void {
    this.requestHandler = handler;
  }

  get isRunning(): boolean {
    return !this.exited;
  }

  request<T>(method: string, params: unknown): Promise<T> {
    if (this.exited) return Promise.reject(new AcpUnavailableError(this.exitReason));
    const id = this.nextId++;
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve: resolve as (value: unknown) => void, reject });
      this.transport.write(encodeFrame({ jsonrpc: '2.0', id, method, params }));
    });
  }

  async initialize(): Promise<InitializeResult> {
    const result = await this.request<InitializeResult>('initialize', {
      protocolVersion: 1,
      clientCapabilities: { fs: { readTextFile: false, writeTextFile: false }, terminal: false },
    });
    if (result?.protocolVersion !== 1) {
      throw new AcpUnavailableError(
        `Unsupported ACP protocol version ${String(result?.protocolVersion)}; Consiglio speaks version 1.`,
      );
    }
    return result;
  }

  newSession(input: { cwd: string; mcpServers?: unknown[] }): Promise<SessionCreated> {
    return this.request<SessionCreated>('session/new', {
      cwd: input.cwd,
      mcpServers: input.mcpServers ?? [],
    });
  }

  loadSession(sessionId: string, cwd: string): Promise<SessionCreated> {
    return this.request<SessionCreated>('session/load', { sessionId, cwd, mcpServers: [] });
  }

  resumeSession(sessionId: string, cwd: string): Promise<SessionCreated> {
    return this.request<SessionCreated>('session/resume', { sessionId, cwd });
  }

  forkSession(sessionId: string, cwd: string): Promise<SessionCreated> {
    return this.request<SessionCreated>('session/fork', { sessionId, cwd });
  }

  listSessions(cwd?: string): Promise<{ sessions?: Array<{ sessionId: string; cwd?: string; title?: string }> }> {
    return this.request<{ sessions?: Array<{ sessionId: string; cwd?: string; title?: string }> }>(
      'session/list',
      cwd ? { cwd } : {},
    );
  }

  cancelSession(sessionId: string): Promise<{ stopReason?: string }> {
    return this.request<{ stopReason?: string }>('session/cancel', { sessionId });
  }

  prompt(sessionId: string, text: string): Promise<{ stopReason?: string; usage?: unknown }> {
    return this.request<{ stopReason?: string; usage?: unknown }>('session/prompt', {
      sessionId,
      prompt: [{ type: 'text', text }],
    });
  }

  setSessionModel(sessionId: string, modelId: string): Promise<unknown> {
    return this.request('session/set_model', { sessionId, modelId });
  }

  dispose(): void {
    this.transport.kill();
  }
}
