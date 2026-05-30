/**
 * UE Agent Bridge — Demo Walkthrough
 * 演示脚本：展示 15 个 MCP 工具的核心能力
 *
 * 前提：
 *   1. 已在 UE Editor 中执行 setup_demo.py 生成场景
 *   2. MCP 连接器已启用 ue-agent-bridge
 *   3. UE Editor 运行中，UEAgentBridge 插件已启用
 *
 * 用法：node demo/demo-walkthrough.mjs
 */

import { spawn } from "child_process";
import { createInterface } from "readline";

// MCP stdio 客户端
import { fileURLToPath } from "url";
import { dirname, join } from "path";
const __dirname = dirname(fileURLToPath(import.meta.url));
const SERVER_PATH = join(__dirname, "../server/dist/index.js");
let requestId = 0;
const pending = new Map();

// ============================================
// MCP Client
// ============================================

const proc = spawn("node", [SERVER_PATH], {
    stdio: ["pipe", "pipe", "pipe"],
});

const rl = createInterface({ input: proc.stdout });

rl.on("line", (line) => {
    try {
        const msg = JSON.parse(line);
        if (msg.id && pending.has(msg.id)) {
            pending.get(msg.id)(msg);
            pending.delete(msg.id);
        }
    } catch (e) {
        // 非 JSON 行忽略
    }
});

proc.stderr.on("data", (d) => process.stderr.write(d));

function call(method, params = {}) {
    return new Promise((resolve, reject) => {
        const id = ++requestId;
        const req = { jsonrpc: "2.0", id, method, params };
        pending.set(id, resolve);
        proc.stdin.write(JSON.stringify(req) + "\n");

        setTimeout(() => {
            if (pending.has(id)) {
                pending.delete(id);
                reject(new Error(`Timeout: ${method}`));
            }
        }, 30000);
    });
}

// 初始化
async function init() {
    const result = await call("initialize", {
        protocolVersion: "2024-11-05",
        capabilities: {},
        clientInfo: { name: "demo-walkthrough", version: "1.0.0" },
    });
    console.log("[INIT] OK");
    return result;
}

// ============================================
// 演示步骤
// ============================================

async function run() {
    console.log("=".repeat(60));
    console.log("  UE Agent Bridge — Demo Walkthrough");
    console.log("=".repeat(60));
    console.log();

    await init();

    // 1. 连接测试
    console.log("─".repeat(40));
    console.log("Step 1: Connection Check");
    console.log("─".repeat(40));
    let r = await call("tools/call", {
        name: "ue_ping",
        arguments: {},
    });
    console.log(" ue_ping →", JSON.stringify(r.result?.content?.[0]?.text));
    console.log();

    // 2. 列表全部 Actor
    console.log("─".repeat(40));
    console.log("Step 2: List All Actors in Level");
    console.log("─".repeat(40));
    r = await call("tools/call", {
        name: "level_get_actors",
        arguments: {},
    });
    const text = typeof r.result?.content?.[0]?.text === "string"
        ? r.result.content[0].text
        : JSON.stringify(r.result?.content?.[0]?.text);
    try {
        const data = JSON.parse(text);
        const actorNames = data.map(a => a.name).slice(0, 10);
        console.log(` level_get_actors → ${data.length} actors`);
        console.log(`   First 10: ${actorNames.join(", ")}`);
    } catch {
        console.log(" level_get_actors →", text.substring(0, 200));
    }
    console.log();

    // 3. 查找特定 Actor
    console.log("─".repeat(40));
    console.log("Step 3: Filter Actors by Class (StaticMeshActor)");
    console.log("─".repeat(40));
    r = await call("tools/call", {
        name: "level_get_actors",
        arguments: { class_filter: "StaticMeshActor" },
    });
    try {
        const data = JSON.parse(r.result.content[0].text);
        console.log(` level_get_actors(class_filter=StaticMeshActor) → ${data.length} actors`);
        data.forEach(a => console.log(`   - ${a.name}`));
    } catch {
        console.log(" level_get_actors →", r.result?.content?.[0]?.text?.substring(0, 200));
    }
    console.log();

    // 4. 获取选中物体
    console.log("─".repeat(40));
    console.log("Step 4: Get Selected Actors");
    console.log("─".repeat(40));
    r = await call("tools/call", {
        name: "editor_get_selected",
        arguments: {},
    });
    console.log(" editor_get_selected →", JSON.stringify(r.result?.content?.[0]?.text));
    console.log();

    // 5. 移动 Actor
    console.log("─".repeat(40));
    console.log("Step 5: Move an Actor");
    console.log("─".repeat(40));
    r = await call("tools/call", {
        name: "level_move_actor",
        arguments: { name: "Ball_1", x: 2.0, y: -1.0, z: 1.5 },
    });
    console.log(" level_move_actor Ball_1 (2, -1, 1.5) →", JSON.stringify(r.result?.content?.[0]?.text));
    console.log();

    // 6. 旋转 Actor
    console.log("─".repeat(40));
    console.log("Step 6: Rotate an Actor");
    console.log("─".repeat(40));
    r = await call("tools/call", {
        name: "level_rotate_actor",
        arguments: { name: "Cone_01", yaw: 45 },
    });
    console.log(" level_rotate_actor Cone_01 yaw=45 →", JSON.stringify(r.result?.content?.[0]?.text));
    console.log();

    // 7. 缩放 Actor
    console.log("─".repeat(40));
    console.log("Step 7: Scale an Actor");
    console.log("─".repeat(40));
    r = await call("tools/call", {
        name: "level_scale_actor",
        arguments: { name: "Pillar_01", x: 0.5, y: 0.5, z: 2.0 },
    });
    console.log(" level_scale_actor Pillar_01 (0.5, 0.5, 2.0) →", JSON.stringify(r.result?.content?.[0]?.text));
    console.log();

    // 8. 搜索资源
    console.log("─".repeat(40));
    console.log("Step 8: Search Assets");
    console.log("─".repeat(40));
    r = await call("tools/call", {
        name: "asset_search",
        arguments: { query: "BP_Demo", max_results: 5 },
    });
    try {
        const data = JSON.parse(r.result.content[0].text);
        console.log(` asset_search "BP_Demo" → ${data.length} results`);
        data.forEach(a => console.log(`   - ${a.asset_path} (${a.asset_class})`));
    } catch {
        console.log(" asset_search →", r.result?.content?.[0]?.text?.substring(0, 200));
    }
    console.log();

    // 9. 读取 Blueprint 结构
    console.log("─".repeat(40));
    console.log("Step 9: Read Blueprint Structure");
    console.log("─".repeat(40));
    r = await call("tools/call", {
        name: "blueprint_read",
        arguments: { asset_path: "/Game/DemoMaterials/BP_DemoActor" },
    });
    console.log(" blueprint_read BP_DemoActor →", r.result?.content?.[0]?.text?.substring(0, 300));
    console.log();

    // 10. 执行控制台命令
    console.log("─".repeat(40));
    console.log("Step 10: Execute Console Command");
    console.log("─".repeat(40));
    r = await call("tools/call", {
        name: "ue_execute_command",
        arguments: { command: "stat fps" },
    });
    console.log(" ue_execute_command 'stat fps' →", JSON.stringify(r.result?.content?.[0]?.text));
    console.log();

    // 11. 执行 Python
    console.log("─".repeat(40));
    console.log("Step 11: Execute Python in Editor");
    console.log("─".repeat(40));
    r = await call("tools/call", {
        name: "ue_execute_python",
        arguments: { code: "print(f'Demo scene has {len(unreal.EditorLevelLibrary.get_all_level_actors())} actors')" },
    });
    console.log(" ue_execute_python →", r.result?.content?.[0]?.text?.substring(0, 200));
    console.log();

    // 12. 错误处理演示
    console.log("─".repeat(40));
    console.log("Step 12: Error Handling (Not Found)");
    console.log("─".repeat(40));
    r = await call("tools/call", {
        name: "level_delete_actor",
        arguments: { name: "NonExistentActor" },
    });
    console.log(" level_delete_actor NonExistentActor →", JSON.stringify(r.result?.content?.[0]?.text));
    console.log();

    // 完成
    console.log("=".repeat(60));
    console.log("  Demo Complete — 11 tools demonstrated");
    console.log("=".repeat(60));
    console.log();
    console.log("Not shown (depends on project content):");
    console.log("  - blueprint_compile");
    console.log("  - blueprint_compile_all");
    console.log("  - blueprint_set_property");
    console.log("  - level_spawn_actor");
    console.log("  - level_delete_actor (success case)");

    proc.kill();
}

run().catch((err) => {
    console.error("DEMO ERROR:", err.message);
    proc.kill();
    process.exit(1);
});
