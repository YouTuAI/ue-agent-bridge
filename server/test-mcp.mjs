#!/usr/bin/env node

/**
 * Quick MCP protocol test — spawns the MCP server and exercises tools via stdio JSON-RPC.
 */

import { spawn } from "child_process";
import { fileURLToPath } from "url";
import { dirname } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));

const MCP_SERVER_CMD = process.execPath;
const MCP_SERVER_ARGS = ["dist/index.js"];

function send(mcp, msg) {
  const data = JSON.stringify(msg) + "\n";
  process.stdout.write(`[>>>] ${data}`);
  mcp.stdin.write(data);
}

function rpc(mcp, id, method, params = {}) {
  mcp._nextId = (mcp._nextId || 0) + 1;
  send(mcp, {
    jsonrpc: "2.0",
    id: mcp._nextId,
    method,
    params,
  });
  return mcp._nextId;
}

async function waitForResponse(mcp, expectedId, timeoutMs = 5000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      mcp._pendingResolves?.delete(expectedId);
      reject(new Error(`Timeout waiting for response id=${expectedId}`));
    }, timeoutMs);

    mcp._pendingResolves = mcp._pendingResolves || new Map();
    mcp._pendingResolves.set(expectedId, (msg) => {
      clearTimeout(timer);
      mcp._pendingResolves.delete(expectedId);
      resolve(msg);
    });
  });
}

async function main() {
  console.log("=== UE Agent Bridge — MCP Protocol Test ===\n");

  const mcp = spawn(MCP_SERVER_CMD, MCP_SERVER_ARGS, {
    cwd: __dirname,
    stdio: ["pipe", "pipe", "inherit"],
  });

  // Collect stdout (MCP responses)
  let buffer = "";
  mcp.stdout.on("data", (chunk) => {
    buffer += chunk.toString();
    while (buffer.includes("\n")) {
      const idx = buffer.indexOf("\n");
      const line = buffer.substring(0, idx).trim();
      buffer = buffer.substring(idx + 1);
      if (!line) continue;
      try {
        const msg = JSON.parse(line);
        process.stdout.write(`[<<<] ${JSON.stringify(msg)}\n`);
        if (msg.id && mcp._pendingResolves?.has(msg.id)) {
          mcp._pendingResolves.get(msg.id)(msg);
        }
      } catch {
        // ignore non-JSON (stderr goes to inherit)
      }
    }
  });

  mcp.on("error", (err) => {
    console.error("MCP process error:", err.message);
    process.exit(1);
  });

  mcp.on("close", (code) => {
    console.log(`\nMCP process exited with code ${code}`);
  });

  let passed = 0;
  let failed = 0;

  async function test(name, fn) {
    try {
      await fn();
      console.log(`  ✅ ${name}`);
      passed++;
    } catch (err) {
      console.log(`  ❌ ${name}: ${err.message}`);
      failed++;
    }
  }

  // Wait a moment for the process to start
  await new Promise((r) => setTimeout(r, 500));

  // Test 1: Initialize
  await test("MCP Initialize", async () => {
    const id = rpc(mcp, 1, "initialize", {
      protocolVersion: "2024-11-05",
      capabilities: {},
      clientInfo: { name: "test-client", version: "1.0" },
    });
    const resp = await waitForResponse(mcp, id);
    if (!resp.result || resp.error) throw new Error(JSON.stringify(resp.error || "no result"));
    // Send "initialized" notification (required by MCP protocol)
    send(mcp, { jsonrpc: "2.0", method: "notifications/initialized" });
  });

  // Test 2: List tools
  await test("List Tools", async () => {
    const id = rpc(mcp, 1, "tools/list", {});
    const resp = await waitForResponse(mcp, id);
    if (!resp.result?.tools) throw new Error("No tools returned");
    console.log(`      Tools: ${resp.result.tools.map((t) => t.name).join(", ")}`);
  });

  // Test 3: Ping (ue_ping)
  await test("ue_ping", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "ue_ping",
      arguments: {},
    });
    const resp = await waitForResponse(mcp, id, 10000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    console.log(`      Result: ${JSON.stringify(result)}`);
  });

  // Test 4: execute_command
  await test("ue_execute_command (stat fps)", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "ue_execute_command",
      arguments: { command: "stat fps" },
    });
    const resp = await waitForResponse(mcp, id, 20000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    console.log(`      Result: ${JSON.stringify(result)}`);
  });

  // Test 5: execute_python
  await test("ue_execute_python (hello world)", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "ue_execute_python",
      arguments: { code: "print('Hello from Python!')" },
    });
    const resp = await waitForResponse(mcp, id, 20000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    console.log(`      Result: ${JSON.stringify(result)}`);
  });

  // Test 6: get_actors
  await test("level_get_actors", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "level_get_actors",
      arguments: { class_filter: "" },
    });
    const resp = await waitForResponse(mcp, id, 20000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    console.log(`      Actors: ${result.actors?.length ?? "?"} found`);
  });

  // Test 7: search_assets
  await test("asset_search", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "asset_search",
      arguments: { query: "BP_", max_results: 5 },
    });
    const resp = await waitForResponse(mcp, id, 20000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    console.log(`      Assets: ${result.assets?.length ?? "?"} found`);
  });

  // Test 8: get_selected
  await test("editor_get_selected", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "editor_get_selected",
      arguments: {},
    });
    const resp = await waitForResponse(mcp, id, 10000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    console.log(`      Selected: ${result.selected?.length ?? "?"} actors`);
  });

  // Test 9: read_blueprint — expect structured error for missing asset
  await test("blueprint_read (structured error)", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "blueprint_read",
      arguments: { asset_path: "/Game/Test" },
    });
    const resp = await waitForResponse(mcp, id, 15000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    if (result.success === false) {
      console.log(`      Error: code=${result.error?.code}, message="${result.error?.message}"`);
    } else {
      console.log(`      Success: ${JSON.stringify(result).substring(0, 80)}`);
    }
  });

  // Test 10: blueprint_set_property — set a real blueprint property
  await test("blueprint_set_property", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "blueprint_set_property",
      arguments: { asset_path: "/Game/Blueprints/BP_PopupActor", property: "TotalPopups", value: 42 },
    });
    const resp = await waitForResponse(mcp, id, 15000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    if (result.success) {
      console.log(`      Set "${result.property}" = ${result.value} (${result.type})`);
    } else {
      console.log(`      Error: code=${result.error?.code}, message="${result.error?.message}"`);
    }
  });

  // Test 11: level_move_actor
  await test("level_move_actor", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "level_move_actor",
      arguments: { name: "SM_SkySphere", x: 100, y: 200, z: 50 },
    });
    const resp = await waitForResponse(mcp, id, 10000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    if (result.success) {
      console.log(`      Actor "${result.actor?.name}" moved to (${result.actor?.location?.x}, ${result.actor?.location?.y}, ${result.actor?.location?.z})`);
    } else {
      console.log(`      Error: code=${result.error?.code}, message="${result.error?.message}"`);
    }
  });

  // Test 12: level_rotate_actor
  await test("level_rotate_actor", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "level_rotate_actor",
      arguments: { name: "SM_SkySphere", pitch: 0, yaw: 45, roll: 0 },
    });
    const resp = await waitForResponse(mcp, id, 10000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    if (result.success) {
      console.log(`      Actor "${result.actor?.name}" rotated to yaw=${result.actor?.rotation?.yaw}`);
    } else {
      console.log(`      Error: code=${result.error?.code}, message="${result.error?.message}"`);
    }
  });

  // Test 13: level_scale_actor
  await test("level_scale_actor", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "level_scale_actor",
      arguments: { name: "SM_SkySphere", x: 2, y: 2, z: 2 },
    });
    const resp = await waitForResponse(mcp, id, 10000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    if (result.success) {
      console.log(`      Actor "${result.actor?.name}" scaled to (${result.actor?.scale?.x}, ${result.actor?.scale?.y}, ${result.actor?.scale?.z})`);
    } else {
      console.log(`      Error: code=${result.error?.code}, message="${result.error?.message}"`);
    }
  });

  // Test 14: level_delete_actor (non-existent → expect NotFound)
  await test("level_delete_actor (error for missing)", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "level_delete_actor",
      arguments: { name: "NonExistentActor_12345" },
    });
    const resp = await waitForResponse(mcp, id, 10000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    if (result.success === false) {
      console.log(`      Error: code=${result.error?.code}, message="${result.error?.message}"`);
    } else {
      console.log(`      Unexpected success: ${JSON.stringify(result)}`);
      throw new Error("Expected NotFound error but got success");
    }
  });

  // Test 15: blueprint_compile (with error details)
  await test("blueprint_compile (error list)", async () => {
    const id = rpc(mcp, 1, "tools/call", {
      name: "blueprint_compile",
      arguments: { asset_path: "/Game/Blueprints/BP_PopupActor" },
    });
    const resp = await waitForResponse(mcp, id, 15000);
    const text = resp.result?.content?.[0]?.text;
    if (!text) throw new Error("No response content");
    const result = JSON.parse(text);
    console.log(`      success=${result.success}, errors=${result.errors?.length ?? 0}, warnings=${result.warnings?.length ?? 0}`);
  });

  // Summary
  console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);

  // Cleanup
  mcp.kill();
  process.exit(failed > 0 ? 1 : 0);
}

main().catch((err) => {
  console.error("Fatal:", err);
  process.exit(1);
});
