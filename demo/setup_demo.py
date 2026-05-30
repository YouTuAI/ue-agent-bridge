"""
UE Agent Bridge — Demo Scene Setup
通过 ue_execute_python 工具一键执行，自动生成演示场景。
"""

import unreal

# ============================================
# 工具函数
# ============================================

def spawn_mesh(mesh_path, location, rotation=(0,0,0), scale=(1,1,1), label=None):
    """生成一个 StaticMesh Actor"""
    actor = unreal.EditorLevelLibrary.spawn_actor_from_object(
        unreal.load_object(None, mesh_path),
        unreal.Vector(*location)
    )
    if actor:
        actor.set_actor_rotation(unreal.Rotator(*rotation), False)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        if label:
            actor.set_actor_label(label)
        print(f"  [OK] {label or mesh_path.split('/')[-1]} at {location}")
    return actor

def create_material(name, color):
    """创建基础颜色材质 (UE 5.6 compatible)"""
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, "/Game/DemoMaterials/",
        unreal.Material, unreal.MaterialFactoryNew()
    )
    if material:
        # UE 5.6: Expressions is protected, use MaterialEditingLibrary
        color_node = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionConstant3Vector, -300, 0
        )
        if not color_node:
            raise Exception(f"Failed to create expression for {name}")
        color_node.set_editor_property("constant", unreal.LinearColor(color[0], color[1], color[2], 1.0))
        unreal.MaterialEditingLibrary.connect_material_property(
            color_node, "", unreal.MaterialProperty.MP_BASE_COLOR
        )
        unreal.MaterialEditingLibrary.layout_material_expressions(material)
        unreal.MaterialEditingLibrary.recompile_material(material)
        print(f"  [OK] Material M_{name} created")
        return material
    return None

def make_cube(loc, size, color, label):
    """快速创建彩色立方体"""
    return spawn_mesh("/Engine/BasicShapes/Cube.Cube", loc, scale=(size, size, size), label=label)

def make_sphere(loc, radius, label):
    """快速创建球体"""
    return spawn_mesh("/Engine/BasicShapes/Sphere.Sphere", loc, scale=(radius, radius, radius), label=label)

def make_cylinder(loc, radius, height, label):
    """快速创建圆柱体"""
    return spawn_mesh("/Engine/BasicShapes/Cylinder.Cylinder", loc, scale=(radius, radius, height), label=label)

def make_cone(loc, radius, height, label):
    """快速创建圆锥体"""
    return spawn_mesh("/Engine/BasicShapes/Cone.Cone", loc, scale=(radius, radius, height), label=label)


# ============================================
# 主流程
# ============================================

print("=" * 50)
print("UE Agent Bridge — Demo Scene Setup")
print("=" * 50)

# 1. 确保有 DemoMaterials 文件夹
if not unreal.EditorAssetLibrary.does_directory_exist("/Game/DemoMaterials"):
    unreal.EditorAssetLibrary.make_directory("/Game/DemoMaterials")
    print("[OK] Created /Game/DemoMaterials/")

# 2. 创建彩色材质
print("\n[1] Creating materials...")
materials = {}
colors = {
    "M_Red":     (1.0, 0.15, 0.15, 1.0),
    "M_Green":   (0.15, 0.85, 0.25, 1.0),
    "M_Blue":    (0.15, 0.40, 1.0, 1.0),
    "M_Yellow":  (1.0, 0.85, 0.10, 1.0),
    "M_Purple":  (0.60, 0.20, 1.0, 1.0),
    "M_Orange":  (1.0, 0.50, 0.05, 1.0),
    "M_White":   (0.95, 0.95, 0.95, 1.0),
    "M_Dark":    (0.10, 0.10, 0.15, 1.0),
}
for name, color in colors.items():
    mat = create_material(name, color)
    if mat:
        materials[name] = mat

# 3. 创建地面平台
print("\n[2] Creating platform...")
platform = make_cube((0, 0, 0), 6.0, (0.15, 0.20, 0.25, 1.0), "Demo_Platform")
if platform:
    platform.set_actor_scale3d(unreal.Vector(8.0, 8.0, 0.2))
    try:
        platform.static_mesh_component.set_material(0, materials.get("M_Dark"))
    except: pass

# 4. 布置演示物体 — 按形状分区域
print("\n[3] Placing demo objects...")

# 区域 A: 立方体塔
make_cube((-3, -2, 0.3), 0.5, None, "Tower_Base")
make_cube((-3, -2, 0.9), 0.4, None, "Tower_Mid")
make_cube((-3, -2, 1.4), 0.3, None, "Tower_Top")

# 区域 B: 球体阵列（前排）
for i in range(3):
    make_sphere((-1.5 + i * 1.5, -2.5, 0.3), 0.3, f"Ball_{i+1}")

# 区域 C: 圆柱体
make_cylinder((0, -2, 0.0), 0.3, 1.5, "Pillar_01")
make_cylinder((1.5, -2, 0.0), 0.25, 1.2, "Pillar_02")

# 区域 D: 锥体
make_cone((3, -2, 0.0), 0.4, 1.8, "Cone_01")

# 5. 文字标注
print("\n[4] Creating labels...")
text_actors = []
label_positions = [
    ("Cube Tower",  (-3, -1, 1.8)),
    ("Spheres",     (0,  -1, 0.8)),
    ("Cylinders",   (0.8, -1, 1.8)),
    ("Cone",        (3,  -1, 2.0)),
]

for text, pos in label_positions:
    text_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.TextRenderActor, unreal.Vector(*pos)
    )
    if text_actor:
        comp = text_actor.get_component_by_class(unreal.TextRenderComponent)
        if comp:
            comp.set_text(text)
            comp.set_world_size(38)
            comp.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
            comp.set_text_render_color(unreal.Color(255, 255, 255))
            # UE 5.6: bAlwaysRenderAsOverlay removed; skip
        text_actors.append(text_actor)
        print(f"  [OK] Label '{text}' at {pos}")

# 6. 创建演示用 Blueprint
print("\n[5] Creating demo Blueprint...")
bp_path = "/Game/DemoMaterials/BP_DemoActor"
if not unreal.EditorAssetLibrary.does_asset_exist(bp_path):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "BP_DemoActor", "/Game/DemoMaterials/",
        unreal.Blueprint, factory
    )
    if bp:
        unreal.EditorAssetLibrary.save_asset(bp_path)
        print(f"  [OK] Blueprint created: {bp_path}")
        
        # 添加变量
        generated_class = bp.generated_class()
        if generated_class:
            cdo = unreal.get_default_object(generated_class)
            # 用 PythonScriptPlugin 添加变量不太方便，
            # 这里先创建 BP 骨架，后续用 blueprint_set_property 演示
        else:
            print("  [WARN] Could not get generated class")
    else:
        print("  [WARN] Could not create blueprint")
else:
    print(f"  [INFO] Blueprint already exists: {bp_path}")

# 7. 保存关卡
print("\n[6] Saving level...")
unreal.EditorLevelLibrary.save_current_level()
print("  [OK] Level saved")

# ============================================
# 完成
# ============================================
print("\n" + "=" * 50)
print("Demo scene setup complete!")
print("=" * 50)
print("\nScene contents:")
print("  - 1  Platform (dark gray, 8x8)")
print("  - 3  Cubes (tower)")
print("  - 3  Spheres (array)")
print("  - 2  Cylinders")
print("  - 1  Cone")
print("  - 4  Text labels (floating)")
print("  - 1  Blueprint (BP_DemoActor)")
print("\nTry MCP tools:")
print("  level_get_actors    → list all actors")
print("  asset_search query:BP_Demo → find blueprint")
print("  blueprint_read /Game/DemoMaterials/BP_DemoActor")
print("  level_move_actor Ball_1 x:5")
print()
