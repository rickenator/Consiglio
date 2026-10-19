/**
 * ACP (Agent Client Protocol) frame codec.
 *
 * `hermes acp` speaks line-delimited JSON-RPC over stdio: one JSON object per
 * line, stdout reserved for transport, logs on stderr. This module is
 * transport-free so it can be unit-tested without spawning anything.
 */

export interface AcpFrame {
  jsonrpc: '2.0';
  id?: number | string;
  method?: string;
  params?: unknown;
  result?: unknown;
  error?: { code: number; message: string; data?: unknown };
}

export function encodeFrame(frame: AcpFrame): string {
  return `${JSON.stringify(frame)}\n`;
}

export class FrameDecoder {
  private remainder = '';

  /**
   * Feed a raw chunk; returns every complete frame it contained. A malformed
   * line yields `null` rather than throwing, so one bad frame cannot kill the
   * session's event stream.
   */
  push(chunk: string): Array<AcpFrame | null> {
    const combined = this.remainder + chunk;
    const lines = combined.split('\n');
    this.remainder = lines.pop() ?? '';
    return lines.filter(line => line.trim()).map(line => {
      try {
        return JSON.parse(line) as AcpFrame;
      } catch {
        return null;
      }
    });
  }
}
