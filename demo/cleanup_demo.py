"""Clean up demo scene before re-running setup."""
import unreal

# 1. Delete demo actors by label
actors = unreal.EditorLevelLibrary.get_all_level_actors()
count = 0
for a in actors:
    label = a.get_actor_label() or ""
    cls_name = a.get_class().get_name()
    
    # Match demo labels
    if label.startswith(("Demo_", "Cube_Tower_", "Sphere_", "Cyl_", "Cone_", "Label_", "Platform")):
        try:
            a.destroy()
            count += 1
        except:
            pass

print(f"Deleted {count} demo actors")

# 2. Delete DemoMaterials folder if exists
mat_dir = "/Game/DemoMaterials"
if unreal.EditorAssetLibrary.does_directory_exist(mat_dir):
    unreal.EditorAssetLibrary.delete_directory(mat_dir)
    print(f"Deleted {mat_dir}")

# 3. Delete BP_DemoActor if exists  
bp_path = "/Game/DemoBlueprints/BP_DemoActor"
if unreal.EditorAssetLibrary.does_asset_exist(bp_path):
    unreal.EditorAssetLibrary.delete_asset(bp_path)
    print(f"Deleted {bp_path}")

# 4. Delete DemoBlueprints folder if empty
bp_dir = "/Game/DemoBlueprints"
if unreal.EditorAssetLibrary.does_directory_exist(bp_dir):
    unreal.EditorAssetLibrary.delete_directory(bp_dir)
    print(f"Deleted {bp_dir}")

# 5. Save the level
unreal.EditorLevelLibrary.save_current_level()
print("Level saved")
print("CLEANUP COMPLETE")
