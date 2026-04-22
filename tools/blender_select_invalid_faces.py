"""
Select invalid faces in Blender (faces that are not triangles/quads).

How to use:
1) Open Blender.
2) Open Scripting workspace.
3) Load this script and Run.

Notes:
- It checks ALL mesh objects in the current scene.
- Invalid faces are selected in each object.
- First object with issues stays active in Edit Mode.
"""

import bpy
import bmesh


ALLOWED_FACE_SIZES = {3, 4}


def select_invalid_faces_in_object(obj, allowed_sizes):
    if obj.type != "MESH":
        return 0

    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")

    bm = bmesh.from_edit_mesh(obj.data)
    bm.faces.ensure_lookup_table()

    invalid_count = 0
    for f in bm.faces:
        is_invalid = len(f.verts) not in allowed_sizes
        f.select = is_invalid
        if is_invalid:
            invalid_count += 1

    if invalid_count > 0:
        for f in bm.faces:
            if f.select:
                bm.faces.active = f
                bm.select_history.clear()
                bm.select_history.add(f)
                break

    bmesh.update_edit_mesh(obj.data, loop_triangles=False, destructive=False)
    return invalid_count


def main():
    # Start from object mode for reliable switching
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")

    # Deselect all objects first
    bpy.ops.object.select_all(action="DESELECT")

    scene = bpy.context.scene
    mesh_objects = [obj for obj in scene.objects if obj.type == "MESH"]

    if not mesh_objects:
        print("[OBJ VALIDATOR] No mesh objects found in scene.")
        return

    total_invalid = 0
    first_invalid_obj = None

    print(f"[OBJ VALIDATOR] Checking {len(mesh_objects)} mesh object(s)...")
    for obj in mesh_objects:
        invalid_count = select_invalid_faces_in_object(obj, ALLOWED_FACE_SIZES)
        total_invalid += invalid_count
        if invalid_count > 0 and first_invalid_obj is None:
            first_invalid_obj = obj
        print(f"  - {obj.name}: invalid faces = {invalid_count}")

    if first_invalid_obj is not None:
        # Keep first invalid object active in edit mode for user action
        bpy.context.view_layer.objects.active = first_invalid_obj
        first_invalid_obj.select_set(True)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_mode(type="FACE")
        print(f"[OBJ VALIDATOR] Done. Total invalid faces: {total_invalid}")
        print(f"[OBJ VALIDATOR] Active object: {first_invalid_obj.name}")
    else:
        # If none invalid, leave in object mode
        bpy.ops.object.mode_set(mode="OBJECT")
        print("[OBJ VALIDATOR] Done. No invalid faces found (only triangles/quads).")


main()

