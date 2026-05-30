/**
 * Asset Tools — search and inspect assets in the content browser
 */
import { defineTool, ToolRegistry } from "../tool-factory.js";

export function registerAssetTools(registry: ToolRegistry): void {
  defineTool(registry, {
    name: "asset_search",
    description: "Search assets in the content browser by name, class, or directory",
    method: "search_assets",
    params: {
      query: { type: "string", description: "Name or path substring to search" },
      class: { type: "string", description: "Asset class filter (e.g., '/Script/Engine.StaticMesh')" },
      directory: { type: "string", description: "Content directory to search", default: "/Game" },
      max_results: { type: "number", description: "Maximum results to return", default: 50 },
    },
  });
}
