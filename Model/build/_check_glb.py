import bpy, json
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=r"D:\Project\SmartBookShelf\models\smartbookshelf_v1.glb")
obs = [o for o in bpy.data.objects]
meshes = [o for o in obs if o.type == "MESH"]
lo = [1e9]*3; hi = [-1e9]*3
dg = bpy.context.evaluated_depsgraph_get()
for o in meshes:
    for c in o.bound_box:
        p = o.matrix_world @ __import__("mathutils").Vector(c)
        for k in range(3):
            lo[k] = min(lo[k], p[k]); hi[k] = max(hi[k], p[k])
acts = sorted(a.name for a in bpy.data.actions)
framed = [o.name for o in obs if o.animation_data and o.animation_data.action]
res = {
  "objects": len(obs), "meshes": len(meshes),
  "materials": len(bpy.data.materials),
  "bbox_size": [round(hi[k]-lo[k], 1) for k in range(3)],
  "actions": acts, "animated_objects": framed,
  "has_push_plate": "Push_Plate" in [o.name for o in obs],
  "has_middle_layer": "Middle_Layer" in [o.name for o in obs],
  "has_3_motors": sum(1 for o in obs if o.name.startswith("Motor0") and o.name.endswith("_42")),
  "has_3_screws": sum(1 for o in obs if "LeadScrew" in o.name),
}
# 采样动画：检查 Book_03 与平台在关键帧的世界坐标
def wloc(n, f):
    o = bpy.data.objects.get(n)
    if not o: return None
    bpy.context.scene.frame_set(f)
    bpy.context.view_layer.update()
    return [round(v, 1) for v in o.matrix_world.translation]
res["book03_f1"] = wloc("Book_03", 1)
res["book03_f270"] = wloc("Book_03", 270)
res["platform_f1"] = wloc("LiftPlatform", 1)
res["platform_f150"] = wloc("LiftPlatform", 150)
res["middle_f1"] = wloc("Middle_Layer", 1)
res["middle_f170"] = wloc("Middle_Layer", 170)
print("GLBCHECK " + json.dumps(res, ensure_ascii=False))
