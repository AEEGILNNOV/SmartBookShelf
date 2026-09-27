# -*- coding: utf-8 -*-
"""速定位书格 — VIEW 9 工作流程图：把 6 个工序帧拼成一张带中文图注的 3×2 版面。

不依赖 OpenCV / Pillow：直接把渲染好的 PNG 作为自发光贴图放到 Blender 场景里的平面上，
再用正交相机一次性渲染成版面图（也顺带完成了帧间视觉一致性的人工核查素材）。
"""
import os
import bpy

import sbs_params as SP
import sbs_lib as L

D = SP.D
COL = "12_ANIMATION"

STEPS = [
    ("step_f001.png", "STEP 1  初始状态：一摞书水平平躺在升降平台上"),
    ("step_f096.png", "STEP 2-3  电机3 启动，升降平台带着整摞书上下移动"),
    ("step_f144.png", "STEP 4  目标书（Book_03）到达固定推出位并停止"),
    ("step_f180.png", "STEP 5  电机2 上下移动中间夹层，调整推出孔高度"),
    ("step_f232.png", "STEP 6-7  电机1 驱动推书板，把目标书从推出孔推出"),
    ("step_f300.png", "STEP 8  推书板退回，完成取书（书留在柜外）"),
]

TILE_W, TILE_H = 640.0, 360.0
GAP, CAP_H = 30.0, 62.0


def _emission_image(name, image_path, col, M):
    mat = bpy.data.materials.new("MAT_Flow_" + name)
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    tex = nt.nodes.new("ShaderNodeTexImage")
    img = bpy.data.images.load(image_path, check_existing=True)
    img.colorspace_settings.name = "sRGB"
    tex.image = img
    tex.interpolation = "Cubic"
    em.inputs["Strength"].default_value = 1.0
    nt.links.new(tex.outputs["Color"], em.inputs["Color"])
    nt.links.new(em.outputs["Emission"], out.inputs["Surface"])
    return mat


def build_flowchart(frame_dir, out_image, w=1920, h=1080, samples=16):
    col = L.get_collection(COL)
    for ob in list(col.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    M = L.build_materials()

    n = 0
    for i, (fname, caption) in enumerate(STEPS):
        path = os.path.join(frame_dir, fname)
        if not os.path.exists(path):
            continue
        c, r = i % 3, i // 3
        cx = (1 - c) * (TILE_W + GAP)      # 相机右方对应世界 −X，故列序取反
        cz = (0.5 - r) * (TILE_H + CAP_H + GAP)
        hw, hh = TILE_W / 2.0, TILE_H / 2.0
        verts = [(cx - hw, 0, cz - hh), (cx + hw, 0, cz - hh),
                 (cx + hw, 0, cz + hh), (cx - hw, 0, cz + hh)]
        mat = _emission_image("T%d" % (i + 1), path, col, M)
        ob = L.mesh_obj("FLOW_Tile_%d" % (i + 1), verts, [(0, 3, 2, 1)], col, mat,
                        "flow", caption, smooth=[False])
        uvl = ob.data.uv_layers.new(name="UVMap")
        for k, uv in enumerate([(0.0, 0.0), (0.0, 1.0), (1.0, 1.0), (1.0, 0.0)]):
            uvl.data[k].uv = uv
        ob.hide_render = False
        L.text_label("FLOW_Cap_%d" % (i + 1), caption, (cx, 1.0, cz - hh - 26.0),
                     26.0, col, M, rot=(1.5707963, 0.0, 3.1415927),
                     grp="flow", material=M["label_w"]).hide_render = False
        n += 1

    # 正交相机正对版面
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE" if hasattr(sc, "eevee") else sc.render.engine
    sbs_stage_settings(sc, w, h, samples)
    cd = bpy.data.cameras.new("CAM_FLOW")
    cd.type = "ORTHO"
    cd.ortho_scale = 2200.0
    cd.clip_start = 1.0
    cd.clip_end = 100000.0
    cam = bpy.data.objects.new("CAM_FLOW", cd)
    cam.location = (0.0, 2600.0, 0.0)
    cam.rotation_euler = (1.5707963, 0.0, 3.1415927)   # 朝 −Y 看且 +Z 朝上，正对版面不翻转
    col.objects.link(cam)
    sc.camera = cam
    sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0.035, 0.04, 0.05, 1)
    sc.render.filepath = out_image
    bpy.ops.render.render(write_still=True)
    return {"tiles": n, "path": out_image,
            "bytes": os.path.getsize(out_image) if os.path.exists(out_image) else 0}


def sbs_stage_settings(sc, w, h, samples):
    sc.render.resolution_x = w
    sc.render.resolution_y = h
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGB"
    ee = getattr(sc, "eevee", None)
    if ee is not None:
        for a, v in (("taa_render_samples", samples), ("use_raytracing", False),
                     ("use_shadows", False), ("use_gtao", False)):
            if hasattr(ee, a):
                try:
                    setattr(ee, a, v)
                except Exception:
                    pass
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.view_settings.exposure = 0.0
