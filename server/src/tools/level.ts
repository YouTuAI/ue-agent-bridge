/**
 * Level Tools — interact with actors in the current level
 */
import { defineTool, ToolRegistry } from "../tool-factory.js";

export function registerLevelTools(registry: ToolRegistry): void {
  defineTool(registry, {
    name: "level_get_actors",
    description: "List actors in the current level, optionally filtered by class name",
    method: "get_actors",
    params: {
      class: { type: "string", description: "Class name filter (e.g., 'StaticMeshActor')" },
      max_results: { type: "number", description: "Maximum actors to return", default: 100 },
    },
  });

  defineTool(registry, {
    name: "level_spawn_actor",
    description: "Spawn a new actor in the current level. Supports StaticMeshActor with optional static_mesh and material paths.",
    method: "spawn_actor",
    params: {
      class: { type: "string", description: "Actor class (e.g., 'StaticMeshActor', '/Script/Engine.PointLight')" },
      location: {
        type: "object",
        description: "Spawn location { x, y, z }",
        properties: {
          x: { type: "number", description: "X coordinate" },
          y: { type: "number", description: "Y coordinate" },
          z: { type: "number", description: "Z coordinate" },
        },
      },
      rotation: {
        type: "object",
        description: "Spawn rotation { pitch, yaw, roll }",
        properties: {
          pitch: { type: "number", description: "Pitch (degrees)" },
          yaw: { type: "number", description: "Yaw (degrees)" },
          roll: { type: "number", description: "Roll (degrees)" },
        },
      },
      scale: {
        type: "object",
        description: "Spawn scale { x, y, z }",
        default: { x: 1, y: 1, z: 1 },
        properties: {
          x: { type: "number", description: "X scale" },
          y: { type: "number", description: "Y scale" },
          z: { type: "number", description: "Z scale" },
        },
      },
      label: { type: "string", description: "Actor label/name" },
      static_mesh: { type: "string", description: "Static mesh asset path (e.g. /Game/Meshes/MyMesh.MyMesh)" },
      material: { type: "string", description: "Material asset path to apply to all slots (e.g. /Game/Materials/MyMat.MyMat)" },
    },
  });

  // Flat-param alternatives to the old level_modify_actor (bypasses DeferExecuteTool object-required bug)
  defineTool(registry, {
    name: "level_move_actor",
    description: "Move an existing actor to a new world location. Only changes location, keeps rotation and scale unchanged.",
    method: "modify_actor",
    required: ["name"],
    params: {
      name: { type: "string", description: "Actor name or label to find" },
      x: { type: "number", description: "New X coordinate" },
      y: { type: "number", description: "New Y coordinate" },
      z: { type: "number", description: "New Z coordinate" },
    },
    handler: async (args, bridge) => {
      const payload: Record<string, unknown> = { name: args.name };
      if (args.x !== undefined || args.y !== undefined || args.z !== undefined) {
        payload.location = { x: args.x ?? 0, y: args.y ?? 0, z: args.z ?? 0 };
      }
      return bridge.call("modify_actor", payload);
    },
  });

  defineTool(registry, {
    name: "level_rotate_actor",
    description: "Set an existing actor's rotation. Only changes rotation, keeps location and scale unchanged.",
    method: "modify_actor",
    required: ["name"],
    params: {
      name: { type: "string", description: "Actor name or label to find" },
      pitch: { type: "number", description: "Pitch in degrees" },
      yaw: { type: "number", description: "Yaw in degrees" },
      roll: { type: "number", description: "Roll in degrees" },
    },
    handler: async (args, bridge) => {
      const payload: Record<string, unknown> = { name: args.name };
      if (args.pitch !== undefined || args.yaw !== undefined || args.roll !== undefined) {
        payload.rotation = { pitch: args.pitch ?? 0, yaw: args.yaw ?? 0, roll: args.roll ?? 0 };
      }
      return bridge.call("modify_actor", payload);
    },
  });

  defineTool(registry, {
    name: "level_scale_actor",
    description: "Set an existing actor's 3D scale. Only changes scale, keeps location and rotation unchanged.",
    method: "modify_actor",
    required: ["name"],
    params: {
      name: { type: "string", description: "Actor name or label to find" },
      x: { type: "number", description: "New X scale" },
      y: { type: "number", description: "New Y scale" },
      z: { type: "number", description: "New Z scale" },
    },
    handler: async (args, bridge) => {
      const payload: Record<string, unknown> = { name: args.name };
      if (args.x !== undefined || args.y !== undefined || args.z !== undefined) {
        payload.scale = { x: args.x ?? 0, y: args.y ?? 0, z: args.z ?? 0 };
      }
      return bridge.call("modify_actor", payload);
    },
  });

  defineTool(registry, {
    name: "level_delete_actor",
    description: "Delete an actor from the current level by name or label",
    method: "delete_actor",
    params: {
      name: { type: "string", description: "Actor name or label to delete" },
    },
  });
}
