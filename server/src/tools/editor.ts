/**
 * Editor Tools — control the UE editor and PIE
 */
import { defineTool, ToolRegistry } from "../tool-factory.js";

export function registerEditorTools(registry: ToolRegistry): void {
  defineTool(registry, {
    name: "ue_ping",
    description: "Ping the UE bridge to check connection and get client count",
    method: "ping",
  });

  defineTool(registry, {
    name: "ue_execute_command",
    description: "Execute a console command in the Unreal Editor",
    method: "execute_command",
    params: {
      command: { type: "string", description: "Console command (e.g., 'stat fps')" },
    },
  });

  defineTool(registry, {
    name: "ue_execute_python",
    description: "Execute Python code in the Unreal Editor",
    method: "execute_python",
    params: {
      code: { type: "string", description: "Python code to execute" },
    },
  });

  defineTool(registry, {
    name: "editor_get_selected",
    description: "Get currently selected actors in the Unreal Editor viewport",
    method: "get_selected",
  });
}
