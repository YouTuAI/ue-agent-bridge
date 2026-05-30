// Quick TCP test for UEAgentBridge
import * as net from "net";

const HOST = "127.0.0.1";
const PORT = 9877;

const socket = new net.Socket();

socket.on("connect", () => {
  console.log("✅ Connected!");
  // Send a ping
  const ping = JSON.stringify({ id: 1, method: "ping", params: {} }) + "\n";
  socket.write(ping);
  console.log(`→ Sent: ${ping.trim()}`);
});

socket.on("data", (data) => {
  console.log(`← Received: ${data.toString().trim()}`);
  socket.end();
});

socket.on("close", () => {
  console.log("🔌 Connection closed");
  process.exit(0);
});

socket.on("error", (err) => {
  console.error(`❌ Error: ${err.message}`);
  process.exit(1);
});

setTimeout(() => {
  console.error("❌ Timeout (5s)");
  process.exit(1);
}, 5000);

socket.connect(PORT, HOST);
