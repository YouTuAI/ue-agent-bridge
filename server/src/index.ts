#!/usr/bin/env node

/**
 * UE Agent Bridge — MCP Server
 * 
 * Connects AI agents (via MCP protocol) to Unreal Engine 5 through TCP bridge.
 * Built by YouTu3D Studio.
 * 
 * License: MIT
 */

import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";
import { UEBridge, BridgeError } from "./bridge.js";

const BRIDGE_HOST = process.env.UE_BRIDGE_HOST || "127.0.0.1";
const BRIDGE_PORT = parseInt(process.env.UE_BRIDGE_PORT || "9877", 10);

const server = new Server(
  {
    name: "ue-agent-bridge",
    version: "0.1.0",
  },
  {
    capabilities: {
      tools: {},
    },
  }
);

const bridge = new UEBridge(BRIDGE_HOST, BRIDGE_PORT);

import { ToolRegistry } from "./tool-factory.js";

// Tool registry
const toolRegistry: ToolRegistry = new Map();

// Register built-in tools
import { registerBlueprintTools } from "./tools/blueprint.js";
import { registerEditorTools } from "./tools/editor.js";
import { registerLevelTools } from "./tools/level.js";
import { registerAssetTools } from "./tools/asset.js";
registerBlueprintTools(toolRegistry);
registerEditorTools(toolRegistry);
registerLevelTools(toolRegistry);
registerAssetTools(toolRegistry);

// MCP: List tools
server.setRequestHandler(ListToolsRequestSchema, async () => ({
  tools: Array.from(toolRegistry.values()).map(t => t.schema),
}));

// MCP: Call tool
server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;
  const tool = toolRegistry.get(name);

  if (!tool) {
    return {
      content: [{ type: "text", text: `Unknown tool: ${name}` }],
      isError: true,
    };
  }

  try {
    await bridge.connect();
    const result = await tool.handler(args || {}, bridge);
    return {
      content: [{ type: "text", text: JSON.stringify(result, null, 2) }],
    };
  } catch (err: any) {
    const errorObj = err instanceof BridgeError
      ? { success: false, error: { code: err.code, message: err.message } }
      : { success: false, error: { code: -3, message: err.message || String(err) } };
    return {
      content: [{ type: "text", text: JSON.stringify(errorObj) }],
      isError: true,
    };
  }
});

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error("[UE Agent Bridge] MCP server started");
  console.error(`[UE Agent Bridge] Bridge: tcp://${BRIDGE_HOST}:${BRIDGE_PORT}`);
}

main().catch((err) => {
  console.error("[UE Agent Bridge] Fatal error:", err);
  process.exit(1);
});
