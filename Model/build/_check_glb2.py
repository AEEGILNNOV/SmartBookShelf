import bpy, json
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=r"D:\Project\SmartBookShelf\models\smartbookshelf_v1.glb")
bound = {}
for o in bpy.data.objects:
    ad = o.animation_data
    if not ad: continue
    strips = []
    for t in ad.nla_tracks:
        for s in t.strips:
            strips.append(s.action.name if s.action else "?")
    if ad.action: strips.append("ACT:" + ad.action.name)
    if strips: bound[o.name] = strips
# 采样：把 action 挂上再取世界坐标
def sample(name, act, f):
    o = bpy.data.objects.get(name)
    if not o: return None
    if not o.animation_data: o.animation_data_create()
    a = bpy.data.actions.get(act)
    if a: o.animation_data.action = a
    bpy.context.scene.frame_set(f); bpy.context.view_layer.update()
    return [round(v,1) for v in o.matrix_world.translation]
res = {"bound": bound,
       "platform_f1": sample("LiftPlatform","LiftPlatformAction",1),
       "platform_f120": sample("LiftPlatform","LiftPlatformAction",120),
       "platform_f200": sample("LiftPlatform","LiftPlatformAction",200),
       "book3_f1": sample("Book_03","Book_03Action",1),
       "book3_f270": sample("Book_03","Book_03Action",270),
       "carriage_f200": sample("Push_Carriage","Push_CarriageAction",200),
       "carriage_f250": sample("Push_Carriage","Push_CarriageAction",250)}
print("GLBANIM " + json.dumps(res, ensure_ascii=False))
