/**
 * Quick ping test for UE Agent Bridge
 * Connects to tcp://127.0.0.1:9877 and sends a JSON-RPC ping
 */
const net = require("net");

const HOST = "127.0.0.1";
const PORT = 9877;

console.log(`Connecting to tcp://${HOST}:${PORT}...`);

const socket = new net.Socket();
let buffer = "";

socket.connect(PORT, HOST, () => {
  console.log("Connected! Sending ping...");
  const message = JSON.stringify({ id: 1, method: "ping", params: {} }) + "\n";
  socket.write(message);
});

socket.on("data", (data) => {
  buffer += data.toString("utf-8");
  const idx = buffer.indexOf("\n");
  if (idx !== -1) {
    const line = buffer.substring(0, idx).trim();
    console.log("Response:", line);
    try {
      const parsed = JSON.parse(line);
      if (parsed.result && parsed.result.status === "pong") {
        console.log("\n=== PING SUCCESS: Bridge is working! ===");
      }
    } catch (e) {
      console.log("(Response is not valid JSON)");
    }
    socket.end();
    process.exit(0);
  }
});

socket.on("error", (err) => {
  console.error("Connection error:", err.message);
  process.exit(1);
});

socket.on("close", () => {
  console.log("Connection closed");
});

setTimeout(() => {
  console.error("Timeout: no response in 5 seconds");
  process.exit(1);
}, 5000);
