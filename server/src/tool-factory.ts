/**
 * Tool Factory — declarative MCP tool definitions
 *
 * Reduces each tool definition from ~20 lines of boilerplate to ~8 lines.
 * Also removes the need for separate registerXxxTools() wrapper functions
 * in each module — tools are defined inline and auto-registered.
 */

import { UEBridge } from "./bridge.js";

// ===== Types =====

export interface ParamDef {
  type: string;
  description: string;
  default?: unknown;
  items?: { type: string };
  properties?: Record<string, ParamDef>;
}

export interface ToolDef {
  name: string;
  description: string;
  /** Bridge method name (e.g. "ping", "get_actors"). Defaults to `name`. */
  method?: string;
  params?: Record<string, ParamDef>;
  required?: string[];
  /** Custom handler — overrides auto-generated handler. Receives (args, bridge). */
  handler?: (args: Record<string, unknown>, bridge: UEBridge) => Promise<unknown>;
}

export interface ToolEntry {
  schema: {
    name: string;
    description: string;
    inputSchema: {
      type: "object";
      properties: Record<string, unknown>;
      required?: string[];
    };
  };
  handler: (args: Record<string, unknown>, bridge: UEBridge) => Promise<unknown>;
}

// ===== Registry type =====

export type ToolRegistry = Map<string, ToolEntry>;

// ===== Factory =====

/** Convert ParamDef to JSON Schema property (recursive) */
function paramToSchema(p: ParamDef): Record<string, unknown> {
  const s: Record<string, unknown> = {
    type: p.type,
    description: p.description,
  };
  if (p.default !== undefined) s.default = p.default;
  if (p.items) s.items = p.items;
  if (p.properties) {
    s.properties = Object.fromEntries(
      Object.entries(p.properties).map(([k, v]) => [k, paramToSchema(v)])
    );
    // Explicitly mark no required sub-properties (avoids strict AJV behavior)
    s.required = [];
  }
  return s;
}

/** Build args payload for bridge.call, applying defaults */
function buildPayload(
  args: Record<string, unknown>,
  params: Record<string, ParamDef> | undefined
): Record<string, unknown> {
  if (!params) return args;
  const payload: Record<string, unknown> = {};
  for (const [key, def] of Object.entries(params)) {
    payload[key] = key in args ? args[key] : def.default;
  }
  // Include any extra args not in params (forward compatibility)
  for (const key of Object.keys(args)) {
    if (!(key in payload)) payload[key] = args[key];
  }
  // Remove undefined and null values
  for (const key of Object.keys(payload)) {
    if (payload[key] === undefined || payload[key] === null) delete payload[key];
  }
  return payload;
}

/**
 * Define and register an MCP tool.
 *
 * @example
 * defineTool(registry, {
 *   name: "level_get_actors",
 *   description: "List actors in the current level",
 *   method: "get_actors",        // defaults to `name` if omitted
 *   params: {
 *     class:      { type: "string", description: "Class filter" },
 *     max_results:{ type: "number", description: "Max results", default: 100 },
 *   },
 * });
 */
export function defineTool(registry: ToolRegistry, def: ToolDef): void {
  const method = def.method ?? def.name;
  const schemaProps: Record<string, unknown> = {};
  const required: string[] = [];

  if (def.params) {
    for (const [key, p] of Object.entries(def.params)) {
      schemaProps[key] = paramToSchema(p);
      // Only auto-mark as required if no explicit `required` list AND no default
      if (def.required === undefined && p.default === undefined) required.push(key);
    }
  }

  // Allow explicit required list to override auto-detection
  const finalRequired = def.required ?? required;

  const entry: ToolEntry = {
    schema: {
      name: def.name,
      description: def.description,
      inputSchema: {
        type: "object",
        properties: schemaProps,
        ...(finalRequired.length > 0 ? { required: finalRequired } : {}),
      },
    },
    handler: def.handler ?? (async (args, bridge) => {
      const payload = buildPayload(args, def.params);
      return bridge.call(method, payload);
    }),
  };

  registry.set(def.name, entry);
}
