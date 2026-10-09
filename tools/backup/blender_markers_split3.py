import bpy
from mathutils import Vector

# ------------------------------------------------------------
# Split Markers For Track Segments
#
# What it does
# - For each selected mesh object, finds local bbox on X.
# - Creates 2 marker empties at 1/3 and 2/3 of local X span.
# - Stores cut metadata on object custom properties for later split.
#
# Notes
# - Based on PistaTeste2.obj analysis: all segments are X-dominant.
# - Markers are parented to each segment so they move together.
# ------------------------------------------------------------

MARKER_COLLECTION_NAME = "CUT_MARKERS"
MARKER_PREFIX = "MK3_"
MARKER_SIZE = 0.12
USE_EVALUATED_MESH = False


def ensure_collection(name: str):
    # Ensure a dedicated collection exists for marker empties.
    col = bpy.data.collections.get(name)
    if col is None:
        col = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(col)
    return col


def remove_old_markers(obj_name: str):
    # Remove old markers generated for this object to keep script idempotent.
    for ob in list(bpy.data.objects):
        if ob.name.startswith(f"{MARKER_PREFIX}{obj_name}_"):
            bpy.data.objects.remove(ob, do_unlink=True)


def get_local_bounds_from_vertices(obj):
    # Read bounds directly from mesh vertices in local space.
    mesh = obj.data
    if not mesh or len(mesh.vertices) == 0:
        return None

    min_x = min(v.co.x for v in mesh.vertices)
    max_x = max(v.co.x for v in mesh.vertices)
    min_y = min(v.co.y for v in mesh.vertices)
    max_y = max(v.co.y for v in mesh.vertices)
    min_z = min(v.co.z for v in mesh.vertices)
    max_z = max(v.co.z for v in mesh.vertices)

    return (min_x, max_x, min_y, max_y, min_z, max_z)


def get_local_bounds_from_evaluated(obj, depsgraph):
    # Optional path if modifiers must be considered for bounds.
    obj_eval = obj.evaluated_get(depsgraph)
    mesh = obj_eval.to_mesh()
    try:
        if not mesh or len(mesh.vertices) == 0:
            return None

        min_x = min(v.co.x for v in mesh.vertices)
        max_x = max(v.co.x for v in mesh.vertices)
        min_y = min(v.co.y for v in mesh.vertices)
        max_y = max(v.co.y for v in mesh.vertices)
        min_z = min(v.co.z for v in mesh.vertices)
        max_z = max(v.co.z for v in mesh.vertices)
        return (min_x, max_x, min_y, max_y, min_z, max_z)
    finally:
        obj_eval.to_mesh_clear()


def create_marker(col, parent_obj, marker_name, world_position):
    # Create one empty marker in world space and parent it to segment object.
    mk = bpy.data.objects.new(marker_name, None)
    mk.empty_display_type = 'PLAIN_AXES'
    mk.empty_display_size = MARKER_SIZE
    mk.location = world_position
    col.objects.link(mk)

    # Parent and preserve world transform.
    mk.parent = parent_obj
    mk.matrix_parent_inverse = parent_obj.matrix_world.inverted()

    return mk


def process_object(obj, marker_collection, depsgraph):
    # Create two 1/3 and 2/3 markers for one object.
    if obj.type != 'MESH':
        return False, f"skip {obj.name}: not mesh"

    remove_old_markers(obj.name)

    if USE_EVALUATED_MESH:
        bounds = get_local_bounds_from_evaluated(obj, depsgraph)
    else:
        bounds = get_local_bounds_from_vertices(obj)

    if bounds is None:
        return False, f"skip {obj.name}: no vertices"

    min_x, max_x, min_y, max_y, min_z, max_z = bounds
    span_x = max_x - min_x
    if span_x <= 1e-8:
        return False, f"skip {obj.name}: zero X span"

    cut1_x = min_x + (span_x / 3.0)
    cut2_x = min_x + (2.0 * span_x / 3.0)

    center_y = (min_y + max_y) * 0.5
    center_z = (min_z + max_z) * 0.5

    # Local positions.
    local_p1 = Vector((cut1_x, center_y, center_z))
    local_p2 = Vector((cut2_x, center_y, center_z))

    # Convert to world positions.
    world_p1 = obj.matrix_world @ local_p1
    world_p2 = obj.matrix_world @ local_p2

    mk1_name = f"{MARKER_PREFIX}{obj.name}_01"
    mk2_name = f"{MARKER_PREFIX}{obj.name}_02"

    create_marker(marker_collection, obj, mk1_name, world_p1)
    create_marker(marker_collection, obj, mk2_name, world_p2)

    # Save metadata for next split script.
    obj["split3_axis"] = "X"
    obj["split3_cut1_local"] = float(cut1_x)
    obj["split3_cut2_local"] = float(cut2_x)

    return True, f"ok {obj.name}: X cuts at {cut1_x:.4f}, {cut2_x:.4f}"


def main():
    # Entry point: process selected meshes.
    selected = [o for o in bpy.context.selected_objects if o.type == 'MESH']
    if not selected:
        print("No mesh objects selected.")
        return

    depsgraph = bpy.context.evaluated_depsgraph_get()
    marker_collection = ensure_collection(MARKER_COLLECTION_NAME)

    ok_count = 0
    for obj in selected:
        ok, msg = process_object(obj, marker_collection, depsgraph)
        print(msg)
        if ok:
            ok_count += 1

    print(f"Done. Objects processed: {ok_count}/{len(selected)}")


if __name__ == "__main__":
    main()
