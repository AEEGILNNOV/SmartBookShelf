# -*- coding: utf-8 -*-
"""
修正 Blender 视口 3D 裁剪距离 + 自动取景（针对 1 unit = 1 mm 的整机模型）。

问题：模型约 1300 单位高，而视口默认 clip_end = 1000，一旦拉远观察就被近远裁剪面
切掉，表现为"黑屏 / 模型消失"。

本工具把每个 3D 视口的 clip_start / clip_end 调整为适合本模型的范围，
并让视口框选整机后保存 .blend。

    blender -b --factory-startup -P fix_viewport_clip.py -- <blend_path>
"""
import bpy
import sys
import os
import importlib

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
_lib03 = importlib.import_module('_lib03')

argv = sys.argv
argv = argv[argv.index('--') + 1:] if '--' in argv else []
blend_path = argv[0]

bpy.ops.wm.open_mainfile(filepath=blend_path)

mn, mx = _lib03.scene_bounds()
size = max((mx - mn)[i] for i in range(3))
CLIP_START = max(1.0, size / 2000.0)      # 与模型尺度匹配的近裁剪
CLIP_END = size * 500.0                   # 允许拉远 500 倍仍可见
print('VIEWPORT_CLIP_TARGET start=%.2f end=%.0f (model size %.0f)'
      % (CLIP_START, CLIP_END, size))

touched = []
for wm in bpy.data.window_managers:
    for window in wm.windows:
        for area in window.screen.areas:
            if area.type != 'VIEW_3D':
                continue
            for space in area.spaces:
                if space.type != 'VIEW_3D':
                    continue
                if hasattr(space, 'clip_start'):
                    space.clip_start = CLIP_START
                    touched.append('clip_start')
                if hasattr(space, 'clip_end'):
                    space.clip_end = CLIP_END
                    touched.append('clip_end')
                # 视图着色：实体 + 材质预览，便于观察半透明外壳与内部机构
                if hasattr(space, 'shading'):
                    sh = space.shading
                    try:
                        sh.type = 'SOLID'
                        sh.show_xray = False
                        if hasattr(sh, 'color_type'):
                            sh.color_type = 'MATERIAL'
                    except Exception:
                        pass
                if hasattr(space, 'overlay'):
                    space.overlay.show_floor = True
                # 视图原点为正交等轴测，方便核对尺寸
                if hasattr(space, 'region_3d'):
                    rv = space.region_3d
                    if hasattr(rv, 'view_perspective'):
                        rv.view_perspective = 'ORTHO'
                    rv.view_location = tuple((mn + mx) * 0.5)
                    rv.view_rotation = (0.8207, 0.4247, 0.3843, 0.0)

print('VIEWPORT_SPACES_TOUCHED', len(touched))

# 场景级安全网：即便窗口状态未写入，也保证相机裁剪足够远
for cam in bpy.data.cameras:
    cam.clip_start = min(cam.clip_start, CLIP_START)
    cam.clip_end = max(cam.clip_end, CLIP_END)
print('CAMERAS_FIXED', len(bpy.data.cameras))

bpy.ops.wm.save_as_mainfile(filepath=blend_path, compress=False, copy=False)
print('SAVED', blend_path, os.path.getsize(blend_path))
