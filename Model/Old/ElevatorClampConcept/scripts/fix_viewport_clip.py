"""Repair the viewport clipping range of an existing .blend.

Why: this project uses 1 Blender unit = 1 mm, so the machine is ~1214 units
tall while Blender's factory viewport clip_end is 1000.  Zooming out clips the
whole model away and the viewport looks black.

This script raises both the 3D viewport clipping range and every camera's
clipping range, saves the file, then re-opens it and prints the values it read
back as proof.

Usage:
    blender --background --factory-startup --python fix_viewport_clip.py -- <file.blend>
"""

import os
import shutil
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import bpy  # noqa: E402

import scene_render  # noqa: E402


def clip_report():
    out = []
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type != 'VIEW_3D':
                continue
            for space in area.spaces:
                if space.type == 'VIEW_3D':
                    out.append('%s/%s: clip_start=%.1f clip_end=%.0f'
                               % (screen.name, area.type, space.clip_start,
                                  space.clip_end))
    for cam in bpy.data.cameras:
        out.append('camera %s: clip_start=%.2f clip_end=%.0f'
                   % (cam.name, cam.clip_start, cam.clip_end))
    return out


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if not argv:
        print('usage: ... --python fix_viewport_clip.py -- <file.blend>')
        sys.exit(1)
    path = os.path.abspath(argv[0])
    if not os.path.exists(path):
        print('missing file:', path)
        sys.exit(1)

    bpy.ops.wm.open_mainfile(filepath=path)
    print('BEFORE PATCH')
    for line in clip_report():
        print('   ', line)

    backup = path + '.preclip.bak'
    if not os.path.exists(backup):
        shutil.copy2(path, backup)
        print('backup written:', backup)

    patched = scene_render.fix_viewport_clip()
    bpy.ops.wm.save_as_mainfile(filepath=path)
    print('patched %d 3D viewport(s), file re-saved' % patched)

    bpy.ops.wm.open_mainfile(filepath=path)
    print('AFTER RE-OPEN (persisted values)')
    for line in clip_report():
        print('   ', line)
    print('CLIP FIX DONE')


main()
