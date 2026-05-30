import * as net from "net";

const HOST = "127.0.0.1";
const PORT = 9877;
const TIMEOUT = 5000;

function rawTest(label, message) {
  return new Promise((resolve) => {
    const socket = new net.Socket();
    let buf = "";
    let result = null;

    const timer = setTimeout(() => {
      result = { label, status: "TIMEOUT", received: buf || "(nothing)" };
      socket.destroy();
      resolve(result);
    }, TIMEOUT);

    socket.connect(PORT, HOST, () => {
      console.log(`[${label}] Connected`);
      socket.write(JSON.stringify(message) + "\n");
      console.log(`[${label}] Sent: ${JSON.stringify(message)}`);
    });

    socket.on("data", (data) => {
      const chunk = data.toString("utf-8");
      const chunkHex = [...data].map(b => b.toString(16).padStart(2,'0')).join(' ');
      console.log(`  [DATA chunk ${buf.length}+${chunk.length}B] HEX: ${chunkHex}`);
      buf += chunk;
      // Check if we have a complete line (newline-terminated JSON)
      const idx = buf.indexOf("\n");
      if (idx !== -1) {
        const line = buf.substring(0, idx);
        const rest = buf.substring(idx + 1);
        console.log(`  [LINE] "${line}" (rest: ${rest.length}B)`);
        const fullHex = [...Buffer.from(buf, "utf-8")].map(b => b.toString(16).padStart(2,'0')).join(' ');
        console.log(`  [FULL BUF HEX] ${fullHex}`);
        clearTimeout(timer);
        if (line.trim()) {
          try {
            const parsed = JSON.parse(line.trim());
            result = { label, status: "OK", parsed };
          } catch (e) {
            result = { label, status: "PARSE_ERROR", raw: line.trim(), hex: [...Buffer.from(line.trim(), "utf-8")].map(b => b.toString(16).padStart(2,'0')).join(' '), parseError: e.message };
          }
        } else {
          result = { label, status: "EMPTY_LINE" };
        }
        socket.destroy();
        resolve(result);
      }
    });

    socket.on("error", (err) => {
      clearTimeout(timer);
      if (!result) {
        result = { label, status: "ERROR", error: err.message, received: buf || "(nothing)" };
        resolve(result);
      }
    });

    socket.on("close", () => {
      if (result) return;
      clearTimeout(timer);
      // On close, try to parse whatever we have (might be complete JSON without trailing \n)
      if (buf.trim()) {
        try {
          const parsed = JSON.parse(buf.trim());
          result = { label, status: "OK (close)", parsed };
        } catch (e) {
          const hex = [...Buffer.from(buf.trim(), "utf-8")].map(b => b.toString(16).padStart(2,'0')).join(' ');
          result = { label, status: "CLOSE_PARSE_ERR", raw: buf.trim(), hex, parseError: e.message };
        }
      } else {
        result = { label, status: "CLOSED_NO_DATA", received: "(empty)" };
      }
      resolve(result);
    });
  });
}

async function main() {
  console.log("=== Raw TCP Test ===\n");

  function showResult(r) {
    const data = r.parsed || r.raw || r.error || r.received || "(none)";
    console.log(`  ${r.status}: ${typeof data === 'string' ? data : JSON.stringify(data)}`);
    if (r.hex) console.log(`  HEX: ${r.hex}`);
    if (r.parseError) console.log(`  PARSE_ERR: ${r.parseError}`);
    console.log();
  }

  // Test 1: Ping
  showResult(await rawTest("PING", { id: 1, method: "ping", params: {} }));
  await new Promise(r => setTimeout(r, 500));

  // Test 2: execute_command
  showResult(await rawTest("EXEC_CMD", { id: 2, method: "execute_command", params: { command: "stat fps" } }));
  await new Promise(r => setTimeout(r, 500));

  // Test 3: execute_python
  showResult(await rawTest("EXEC_PY", { id: 3, method: "execute_python", params: { code: "print('Hello from Python!')" } }));

  console.log("=== Done ===");
}

main().catch(console.error);
