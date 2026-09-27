# -*- coding: utf-8 -*-
"""速定位书格 — 一键构建 / 渲染 / 校验 / 导出

用法（在仓库根目录）：
  & "D:\\Application\\3D\\Blender\\blender.exe" --background --factory-startup `
      --python "build\\run_all.py" -- build anim stage render:VIEW1_外观 fast
"""
import os
import sys
import json
import time
import traceback

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
if BASE not in sys.path:
    sys.path.insert(0, BASE)

import bpy  # noqa: E402
import sbs_params as SP       # noqa: E402
import sbs_lib as L           # noqa: E402
import sbs_build              # noqa: E402
import sbs_anim               # noqa: E402
import sbs_validate           # noqa: E402
import sbs_stage              # noqa: E402
import sbs_explode            # noqa: E402
import sbs_flow               # noqa: E402
import sbs_docs               # noqa: E402

MODELS = os.path.join(ROOT, "models")
RENDERS = os.path.join(ROOT, "renders")
DOCS = os.path.join(ROOT, "docs")
for p in (MODELS, RENDERS, DOCS, os.path.join(RENDERS, "diag")):
    os.makedirs(p, exist_ok=True)

BLEND = os.path.join(MODELS, "smartbookshelf_v1.blend")
GLB = os.path.join(MODELS, "smartbookshelf_v1.glb")
REPORT = os.path.join(DOCS, "validation_report.json")


def argv():
    a = sys.argv
    return a[a.index("--") + 1:] if "--" in a else []


def log(tag, payload):
    print("[SBS] %-14s %s" % (tag, json.dumps(payload, ensure_ascii=False)))


def fix_viewport_clip():
    n = 0
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                for space in area.spaces:
                    if space.type == "VIEW_3D":
                        space.clip_start = 1.0
                        space.clip_end = 400000.0
                        n += 1
    for cam in bpy.data.cameras:
        cam.clip_start = 1.0
        cam.clip_end = 400000.0
    return n


def do_save(path=None):
    path = path or BLEND
    n = fix_viewport_clip()
    bpy.ops.wm.save_as_mainfile(filepath=path)
    log("save", {"path": path, "bytes": os.path.getsize(path), "viewports_fixed": n})
    return path


def main():
    args = argv()
    cmds = [a for a in args if ":" not in a and a not in ("fast", "cycles")]
    views = [a.split(":", 1)[1] for a in args if a.startswith("render:")]
    fast = "fast" in args
    do_render = "render" in args or views
    do_export = "export" in args
    t0 = time.time()
    out = {}

    # render / export / validate / 动画 都需要完整场景：未显式构建时自动补齐前置阶段
    if not cmds:
        cmds = ["build", "anim", "explode", "stage", "validate"]
    elif ("build" not in cmds) and (do_render or do_export or "validate" in cmds or
                                    "animframes" in cmds or "animvideo" in cmds):
        cmds = ["build", "anim", "explode", "stage"] + [c for c in cmds if c != "stage"]

    if "build" in cmds or not cmds:
        out["build"] = sbs_build.build_all()
        log("build", out["build"])
    if "anim" in cmds:
        out["anim"] = sbs_anim.build_animation()
        log("anim", out["anim"])
    if "explode" in cmds:
        out["explode"] = sbs_explode.build_exploded()
        log("explode", out["explode"])
    if "stage" in cmds:
        sbs_stage.build_stage()
        sbs_stage.build_cameras()
        sbs_stage.apply_render_settings(1280 if fast else 1920, 720 if fast else 1080)
        log("stage", {"cameras": len(sbs_stage.VIEWS)})
        sbs_explode.build_labels()

    if "validate" in cmds:
        v = sbs_validate.run(do_clash=True)
        out["validate"] = {
            "failed": {a["id"]: a["detail"] for a in v["assertions"] if not a["ok"]},
            "passed": sum(1 for a in v["assertions"] if a["ok"]),
            "total": len(v["assertions"]),
            "clash_free": v["clash_free"],
            "clash": [[c["frame"], c["count"]] for c in v["clash_frames"]],
            "min_clearance_mm": v["min_clearance_mm"],
        }
        out["_assertions"] = v["assertions"]
        log("validate", out["validate"])

    if "save" in cmds or not cmds:
        do_save()

    if "docs" in cmds:
        v = sbs_validate.run(do_clash=True)
        r = sbs_docs.write_docs(os.path.join(DOCS, "结构说明与参数表.txt"), v)
        out["docs"] = r
        log("docs", r)

    if do_render:
        sbs_stage.apply_render_settings(1280 if fast else 1920, 720 if fast else 1080)
        targets = views or list(sbs_stage.VIEWS.keys())
        done = []
        for v in targets:
            path = os.path.join(RENDERS, "%s.png" % v)
            r = sbs_stage.render_view(v, path, samples=32 if fast else 96,
                                      w=1280 if fast else 1920, h=720 if fast else 1080)
            done.append(r)
            log("render", r)
        out["renders"] = done

    if "animframes" in cmds:
        af = os.path.join(RENDERS, "anim")
        os.makedirs(af, exist_ok=True)
        sbs_stage.set_visibility("VIEW9_流程")
        sbs_stage.apply_render_settings(1280, 720, 40)
        sc = bpy.context.scene
        sc.camera = bpy.data.objects["CAM_VIEW9_流程"]
        made = []
        for f in (1, 96, 144, 180, 232, 300):
            sc.frame_set(f)
            p = os.path.join(af, "step_f%03d.png" % f)
            sc.render.filepath = p
            bpy.ops.render.render(write_still=True)
            made.append(p)
            log("frame", {"f": f, "path": p, "bytes": os.path.getsize(p)})
        out["animframes"] = made
        sc.frame_set(1)

    if "flowchart" in cmds:
        r = sbs_flow.build_flowchart(os.path.join(RENDERS, "anim"),
                                     os.path.join(RENDERS, "VIEW9_流程.png"),
                                     w=1920, h=1080, samples=24)
        out["flowchart"] = r
        log("flowchart", r)

    if "animvideo" in cmds:
        sbs_stage.set_visibility("VIEW9_流程")
        sc = bpy.context.scene
        sc.camera = bpy.data.objects["CAM_VIEW9_流程"]
        w, h, sp, rt = (960, 540, 16, False) if fast else (1280, 720, 32, True)
        sbs_stage.apply_render_settings(w, h, sp, raytracing=rt)
        ims = sc.render.image_settings
        if hasattr(ims, "media_type"):        # Blender 5.x 必须切到 VIDEO 才允许 FFMPEG
            ims.media_type = "VIDEO"
        ims.file_format = "FFMPEG"
        sc.render.ffmpeg.format = "MPEG4"
        sc.render.ffmpeg.codec = "H264"
        sc.render.ffmpeg.constant_rate_factor = "HIGH"
        sc.render.ffmpeg.gopsize = 12
        sc.render.fps = SP.D["FPS"]
        video = os.path.join(RENDERS, "smartbookshelf_workflow.mp4")
        sc.render.filepath = video
        bpy.ops.render.render(animation=True)
        log("video", {"path": video,
                      "bytes": os.path.getsize(video) if os.path.exists(video) else 0})
        sbs_stage.apply_render_settings(w, h, sp, raytracing=rt)

    if do_export:
        k = fix_viewport_clip()

        def select_only(prefixes, exclude=()):
            n = 0
            first = None
            for ob in bpy.data.objects:
                ok = (ob.type in ("MESH", "EMPTY")
                      and any(ob.name.startswith(p) for p in prefixes)
                      and not any(ob.name.startswith(p) for p in exclude))
                try:
                    ob.select_set(ok)
                except Exception:
                    ok = False
                if ok:
                    n += 1
                    first = first or ob
            if first is not None:
                bpy.context.view_layer.objects.active = first
            return n

        def gltf(path):
            bpy.ops.export_scene.gltf(filepath=path, export_format="GLB",
                                      use_selection=True, export_apply=True,
                                      export_yup=True, export_animations=True,
                                      export_frame_range=False,
                                      export_cameras=False, export_lights=False)
            return {"path": path, "bytes": os.path.getsize(path)}

        n1 = select_only(("",), exclude=("STAGE_", "EXP_", "LBL_", "FLOW_"))
        out["export"] = gltf(GLB)
        out["export"]["selected"] = n1
        log("export", out["export"])
        n2 = select_only(("EXP_",))
        glb2 = os.path.join(MODELS, "smartbookshelf_v1_exploded.glb")
        out["export_exploded"] = gltf(glb2)
        out["export_exploded"]["selected"] = n2
        log("export", out["export_exploded"])
        out["clip_fixed"] = k

    out["seconds"] = round(time.time() - t0, 1)
    with open(REPORT, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1, default=str)
    log("done", {"seconds": out["seconds"]})


try:
    main()
except Exception as exc:  # noqa: BLE001
    print("[SBS] FATAL", repr(exc))
    traceback.print_exc()
    sys.exit(1)
