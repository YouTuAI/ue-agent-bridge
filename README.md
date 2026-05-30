# UE Agent Bridge

MIT-licensed Unreal Engine 5.6 MCP bridge — control UE Editor via AI assistants through standard MCP protocol.

## Architecture

```
WorkBuddy / Claude Desktop
    │  stdio (JSON-RPC)
    ▼
TypeScript MCP Server  ──TCP 9877──▶  UE Editor (C++ Plugin)
  (server/src/)         JSON-line         (plugin/UEAgentBridge/)
```

- **TypeScript side**: MCP stdio server, tool schema definitions, TCP client
- **C++ side**: FRunnable TCP server, Handler registry, game-thread dispatch
- **Protocol**: line-delimited JSON over TCP (one compact JSON object per line)

## Quick Start

### Prerequisites

- Unreal Engine 5.6
- Node.js 22+
- [.NET SDK](https://dotnet.microsoft.com/download) (for C++ build)

### One-Click Install

```bash
# 1. Configure your paths (one-time)
copy build-config.example.bat build-config.bat
notepad build-config.bat    # set UE_ENGINE_PATH

# 2. Run install (pass your .uproject path)
install.bat "D:\MyProject\MyProject.uproject"
```

This single command handles:
1. Copy plugin to your project's `Plugins/`
2. Compile C++ plugin (auto-checks UE Editor is closed)
3. npm install + TypeScript compile
4. Generate `mcp-config.json`

### Configure MCP Client

Copy the generated `mcp-config.json` section into `~/.workbuddy/mcp.json`.

### Launch

1. Open your UE project in the Editor
2. Enable plugin: **Edit → Plugins → "UE Agent Bridge"**
3. Restart your MCP client and trust the connector
4. Test: `ue_ping` should return `pong`

> **Manual setup**: If you prefer step-by-step, see `setup.bat` (TS only) and `build.bat` (C++ only).

## Demo

Quickly set up a demo scene and verify all tools:

```bash
# 1. In UE Editor, open Python console (Window → Developer Tools → Python)
# 2. Execute the setup script:
exec(open("path/to/ue-agent-bridge/demo/setup_demo.py").read())

# 3. Run walkthrough (from your terminal):
node demo/demo-walkthrough.mjs
```

The demo creates a scene with:
- A dark platform
- Cubes tower, sphere array, cylinders, cone
- Floating text labels
- A demo Blueprint (BP_DemoActor)

The walkthrough demonstrates 11 of 15 tools end-to-end.

## Tools (15)

| Tool | Description | Args |
|------|-------------|------|
| `ue_ping` | Connection check | — |
| `ue_execute_command` | Execute UE console command | `command: string` |
| `ue_execute_python` | Execute Python in UE Editor | `code: string` |
| `editor_get_selected` | Get selected actors in viewport | — |
| `level_get_actors` | List actors in current level | `class_filter?: string` |
| `level_spawn_actor` | Spawn a new actor | `class: string`, `location?`, `rotation?`, `scale?` |
| `level_move_actor` | Move an actor to new location | `name: string`, `x?`, `y?`, `z?` |
| `level_rotate_actor` | Set an actor's rotation | `name: string`, `pitch?`, `yaw?`, `roll?` |
| `level_scale_actor` | Set an actor's 3D scale | `name: string`, `x?`, `y?`, `z?` |
| `level_delete_actor` | Delete an actor from level | `name: string` |
| `asset_search` | Search assets by name/class | `query: string`, `class_filter?: string`, `max_results?: number` |
| `blueprint_read` | Read blueprint structure | `asset_path: string` |
| `blueprint_compile` | Compile a single blueprint | `asset_path: string`, `save?: boolean` |
| `blueprint_compile_all` | Batch compile blueprints | `asset_paths: string[]`, `save?: boolean` |
| `blueprint_set_property` | Set a blueprint variable default value | `asset_path: string`, `property_name: string`, `value: any`, `save?: boolean` |

### `blueprint_read` Response Example

```json
{
  "asset_path": "/Game/Blueprints/BP_PopupActor",
  "name": "BP_PopupActor",
  "parent_class": "/Script/Engine.Actor",
  "blueprint_type": "BPTYPE_Normal",
  "variables": [
    { "name": "PopupMessage", "type": "string", "editable": true, "category": "Default" }
  ],
  "variable_count": 1,
  "functions": [
    { "name": "UserConstructionScript" }
  ],
  "function_count": 1,
  "components": [
    { "name": "DefaultSceneRoot", "class": "SceneComponent" }
  ],
  "component_count": 1
}
```

## Error Codes

All errors return structured JSON: `{"error": {"code": <number>, "message": "<string>"}}`

| Code | Name | Meaning |
|------|------|---------|
| -1 | `InvalidParams` | Missing or invalid required parameters |
| -2 | `NotFound` | Asset / actor / resource not found |
| -3 | `Internal` | Unexpected internal error |
| -4 | `NotAvailable` | Required subsystem unavailable (GEditor, World, Python) |

## Project Structure

```
ue-agent-bridge/
├── plugin/UEAgentBridge/           # C++ UE5 plugin
│   ├── UEAgentBridge.uplugin
│   └── Source/UEAgentBridge/
│       ├── UEAgentBridge.Build.cs
│       ├── Public/BridgeServer.h    # TCP server + handler declarations
│       └── Private/
│           ├── UEAgentBridge.cpp    # Module startup
│           └── BridgeServer.cpp     # TCP server, handler registry, all handlers
├── server/                         # TypeScript MCP server
│   ├── src/
│   │   ├── index.ts                # MCP stdio server entry
│   │   ├── bridge.ts               # TCP client + BridgeError
│   │   ├── tool-factory.ts         # `defineTool()` factory
│   │   └── tools/
│   │       ├── editor.ts
│   │       ├── blueprint.ts
│   │       ├── level.ts
│   │       └── asset.ts
│   ├── test-mcp.mjs                # MCP integration test
│   └── test-raw-tcp.mjs            # Raw TCP protocol test
├── demo/                           # Demo scene + walkthrough
│   ├── setup_demo.py               # UE Python script to build scene
│   └── demo-walkthrough.mjs        # MCP tool demo sequence
├── install.bat                     # One-click full install
├── setup.bat                       # npm install + tsc only
├── build.bat                       # C++ build only
├── build-config.example.bat        # Config template
├── build-config.bat                # Your local config (gitignored)
├── mcp-config.json                 # Generated MCP config snippet
└── README.md
```

## Adding a New Tool

### C++ Side

1. **Add a handler** in `BridgeServer.cpp`:

```cpp
class FMyHandler : public FBridgeHandler
{
public:
    virtual bool CanHandle(const FString& Action) const override
    {
        return Action == TEXT("my_method");
    }

    virtual TSharedPtr<FJsonObject> Handle(
        const TSharedPtr<FJsonObject>& Params,
        FString& OutError) const override
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("status"), TEXT("ok"));
        return Result;
    }
};

// Register in Start():
Registry->RegisterHandler(MakeShared<FMyHandler>());
```

2. Rebuild, restart UE Editor.

### TypeScript Side

```typescript
// In tools/my-tool.ts
import { defineTool, ToolRegistry } from "../tool-factory.js";

export function registerMyTools(registry: ToolRegistry) {
  defineTool(registry, {
    name: "my_tool",
    description: "What my tool does",
    method: "my_method",          // defaults to tool name
    params: {
      input: {
        type: "string",
        description: "Input parameter",
      },
    },
  });
}

// In index.ts:
import { registerMyTools } from "./tools/my-tool.js";
registerMyTools(toolRegistry);
```

### Custom Handler Pattern

When a tool needs argument transformation before calling C++ (e.g., packing flat params into nested objects), use the `handler` option:

```typescript
defineTool(registry, {
  name: "level_move_actor",
  description: "Move an existing actor to a new world location.",
  method: "modify_actor",   // all 3 tools share the same C++ method
  params: {
    name: { type: "string", description: "Actor name or label to find", required: true },
    x: { type: "number", description: "New X coordinate" },
    y: { type: "number", description: "New Y coordinate" },
    z: { type: "number", description: "New Z coordinate" },
  },
  handler: (bridge, args) => {
    return bridge.call("modify_actor", {
      name: args.name,
      location: { x: args.x, y: args.y, z: args.z },
    });
  },
});
```

This is used for `level_move_actor`, `level_rotate_actor`, and `level_scale_actor` — all three map to the same C++ `modify_actor` method but with different flat parameter sets.

### Known Limitations

**DeferExecuteTool validation**: Some MCP clients (including WorkBuddy's `DeferExecuteTool`) have a parameter validation layer that ignores JSON Schema `default` fields. Objects with `default: null` are still treated as required. **Workaround**: use flat primitive parameters instead of nested objects.

## Comparison: ue-agent-bridge vs ue-mcp

| Aspect | ue-agent-bridge | ue-mcp |
|--------|:-----------------:|:------:|
| License | **MIT** | BUSL-1.1 |
| Transport | TCP (port 9877) | WebSocket (port 9876) |
| Architecture | Handler registry + factory | Monolithic switch-case |
| Tools | **15** | ~12 |
| New tool cost | ~6 lines (TS) + 15 lines (C++) | ~30 lines (TS) + ~30 lines (C++) |
| Error handling | Structured `{code, message}` | Plain string |
| Error enrichment | "Did you mean:" suggestions | None |
| Auto-reconnect | Exponential backoff (1s→30s) | None |
| File structure | 4 tool files + 1 factory | 1 monolithic file |
| Thread safety | AsyncTask → game thread | Checked but fragile |
| Custom handler | handler option for arg transform | None |

## License

MIT — see LICENSE file.
