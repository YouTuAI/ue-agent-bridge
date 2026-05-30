/**
 * UE Agent Bridge — TCP Socket Client
 * 
 * Connects to the UE C++ bridge server via raw TCP.
 * Protocol: line-delimited JSON-RPC (one JSON object per line, \n terminated).
 * Auto-reconnects with exponential backoff when UE restarts.
 */

import * as net from "net";

// ===== Error Codes (mirrors C++ EUEAgentError) =====

export const ErrorCode = {
  InvalidParams:  -1,
  NotFound:       -2,
  Internal:       -3,
  NotAvailable:   -4,
} as const;

export class BridgeError extends Error {
  code: number;
  constructor(code: number, message: string) {
    super(message);
    this.code = code;
    this.name = "BridgeError";
  }
}

interface PendingRequest {
  resolve: (value: any) => void;
  reject: (error: Error) => void;
  timer: NodeJS.Timeout;
}

type ConnectionState = "disconnected" | "connecting" | "connected";

const INITIAL_RECONNECT_DELAY = 1000;
const MAX_RECONNECT_DELAY = 30000;
const RECONNECT_BACKOFF = 2;

export class UEBridge {
  private host: string;
  private port: number;
  private socket: net.Socket | null = null;
  private requestId = 0;
  private pending = new Map<number, PendingRequest>();
  private timeoutMs: number;
  private buffer = "";
  private connectPromise: Promise<void> | null = null;
  private state: ConnectionState = "disconnected";
  private reconnectTimer: NodeJS.Timeout | null = null;
  private currentReconnectDelay = INITIAL_RECONNECT_DELAY;
  private shuttingDown = false;

  constructor(host: string = "127.0.0.1", port: number = 9877, timeoutMs: number = 30000) {
    this.host = host;
    this.port = port;
    this.timeoutMs = timeoutMs;
  }

  /** Shut down the bridge permanently — stops reconnect attempts */
  shutdown(): void {
    this.shuttingDown = true;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.socket && !this.socket.destroyed) {
      this.socket.destroy();
    }
    this.rejectAllPending(new Error("Bridge shut down"));
  }

  get connectionState(): ConnectionState {
    return this.state;
  }

  async connect(): Promise<void> {
    if (this.shuttingDown) throw new Error("Bridge is shut down");

    // Retry loop: keep trying until connected or shutting down
    while (this.state !== "connected") {
      if (this.shuttingDown) throw new Error("Bridge is shut down");

      if (this.state !== "connecting" || !this.connectPromise) {
        this.state = "connecting";
        this.connectPromise = this.doConnect();
      }

      try {
        await this.connectPromise;
        this.currentReconnectDelay = INITIAL_RECONNECT_DELAY;
        return; // connected
      } catch (err: any) {
        this.connectPromise = null;
        this.state = "disconnected";
        if (this.shuttingDown) throw err;

        const delay = this.currentReconnectDelay;
        console.error(`[Bridge] Connection failed: ${err.message}. Retrying in ${delay / 1000}s...`);
        await new Promise(r => setTimeout(r, delay));
        this.currentReconnectDelay = Math.min(
          this.currentReconnectDelay * RECONNECT_BACKOFF,
          MAX_RECONNECT_DELAY,
        );
      }
    }
  }

  private doConnect(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.socket = new net.Socket();

      this.socket.connect(this.port, this.host, () => {
        this.state = "connected";
        this.currentReconnectDelay = INITIAL_RECONNECT_DELAY;
        console.error(`[Bridge] Connected to tcp://${this.host}:${this.port}`);
        resolve();
      });

      this.socket.on("data", (data: Buffer) => {
        this.buffer += data.toString("utf-8");

        let newlineIdx: number;
        while ((newlineIdx = this.buffer.indexOf("\n")) !== -1) {
          const line = this.buffer.substring(0, newlineIdx).trim();
          this.buffer = this.buffer.substring(newlineIdx + 1);

          if (!line) continue;

          try {
            const msg = JSON.parse(line);
            const pending = this.pending.get(msg.id);
            if (pending) {
              clearTimeout(pending.timer);
              this.pending.delete(msg.id);

              if (msg.error) {
                if (typeof msg.error === "object" && msg.error.message) {
                  pending.reject(new BridgeError(
                    msg.error.code ?? ErrorCode.Internal,
                    msg.error.message
                  ));
                } else {
                  pending.reject(new BridgeError(ErrorCode.Internal, String(msg.error)));
                }
              } else {
                pending.resolve(msg.result);
              }
            }
          } catch {
            // Non-JSON line, ignore
          }
        }
      });

      this.socket.on("error", (err: Error) => {
        if (this.state === "connecting") {
          this.state = "disconnected";
          reject(err);
        } else {
          console.error(`[Bridge] Error: ${err.message}`);
        }
      });

      this.socket.on("close", () => {
        this.state = "disconnected";
        console.error("[Bridge] Disconnected");
        this.rejectAllPending(new Error("Connection closed"));

        // Auto-reconnect unless shutting down
        if (!this.shuttingDown) {
          this.scheduleReconnect();
        }
      });
    });
  }

  private scheduleReconnect(): void {
    if (this.shuttingDown || this.reconnectTimer) return;

    const delay = this.currentReconnectDelay;
    console.error(
      `[Bridge] Reconnecting in ${delay / 1000}s...`
    );

    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.doReconnect();
    }, delay);

    // Exponential backoff
    this.currentReconnectDelay = Math.min(
      this.currentReconnectDelay * RECONNECT_BACKOFF,
      MAX_RECONNECT_DELAY
    );
  }

  private async doReconnect(): Promise<void> {
    if (this.shuttingDown) return;

    try {
      await this.connect();
    } catch (err: any) {
      console.error(`[Bridge] Reconnect failed: ${err.message}`);
      if (!this.shuttingDown) {
        this.scheduleReconnect();
      }
    }
  }

  async call(method: string, params: Record<string, any> = {}): Promise<any> {
    // If not connected, wait for connection (or reconnect)
    if (this.state !== "connected") {
      await this.connect();
    }

    const id = ++this.requestId;

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Timeout: ${method} (${this.timeoutMs}ms)`));
      }, this.timeoutMs);

      this.pending.set(id, { resolve, reject, timer });

      const message = JSON.stringify({ id, method, params }) + "\n";

      try {
        this.socket!.write(message);
      } catch (err: any) {
        clearTimeout(timer);
        this.pending.delete(id);
        reject(new Error(`Send failed: ${err.message}`));
      }
    });
  }

  private rejectAllPending(error: Error): void {
    for (const [, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(error);
    }
    this.pending.clear();
  }
}
