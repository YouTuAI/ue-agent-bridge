/**
 * Blueprint Tools — compile, read, and batch-operate on Blueprint assets
 */
import { defineTool, ToolRegistry } from "../tool-factory.js";

export function registerBlueprintTools(registry: ToolRegistry): void {
  defineTool(registry, {
    name: "blueprint_compile",
    description: "Compile a Blueprint asset and return results",
    method: "compile_blueprint",
    params: {
      asset_path: { type: "string", description: "Blueprint asset path (e.g., /Game/BP_MyActor)" },
    },
  });

  defineTool(registry, {
    name: "blueprint_compile_all",
    description: "Batch compile multiple Blueprint assets",
    method: "compile_blueprints",
    params: {
      asset_paths: { type: "array", items: { type: "string" }, description: "Blueprint asset paths" },
      save: { type: "boolean", description: "Save after compile", default: true },
    },
  });

  defineTool(registry, {
    name: "blueprint_read",
    description: "Read a Blueprint's structure: parent class, variables, functions, components",
    method: "read_blueprint",
    params: {
      asset_path: { type: "string", description: "Blueprint asset path" },
    },
  });

  defineTool(registry, {
    name: "blueprint_set_property",
    description: "Set a Blueprint variable's default value on the Class Default Object. Supports string, int, float, bool, and text types. Optionally saves after modification.",
    method: "set_blueprint_property",
    params: {
      asset_path: { type: "string", description: "Blueprint asset path" },
      property: { type: "string", description: "Variable name to set" },
      value: { type: "string", description: "New value (auto-converted to match property type)" },
      save: { type: "boolean", description: "Save asset after modification", default: false },
    },
  });
}
