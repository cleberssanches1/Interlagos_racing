import bpy
import bmesh
from mathutils import Vector

# ------------------------------------------------------------
# Split Selected Track Segments Into 3 Objects
#
# What it does
# - Reads split metadata from each selected object:
#   split3_cut1_local, split3_cut2_local
# - If metadata is missing, computes cuts from local bbox on X.
# - Bisects geometry at cut1 and cut2 in local space.
# - Rebuilds 3 new meshes by face center region:
#   A: x <= cut1, B: cut1 < x <= cut2, C: x > cut2
# - Preserves UV layers and face material index.
# - Optionally removes original object.
# ------------------------------------------------------------

KEEP_ORIGINAL = False
SUFFIXES = ("_A", "_B", "_C")


def get_local_cut_positions(obj):
    # Get cut positions from custom props or bbox fallback.
    if "split3_cut1_local" in obj and "split3_cut2_local" in obj:
        cut1 = float(obj["split3_cut1_local"])
        cut2 = float(obj["split3_cut2_local"])
        if cut1 > cut2:
            cut1, cut2 = cut2, cut1
        return cut1, cut2

    mesh = obj.data
    if not mesh or len(mesh.vertices) == 0:
        return None, None

    min_x = min(v.co.x for v in mesh.vertices)
    max_x = max(v.co.x for v in mesh.vertices)
    span_x = max_x - min_x
    if span_x <= 1e-8:
        return None, None

    cut1 = min_x + (span_x / 3.0)
    cut2 = min_x + (2.0 * span_x / 3.0)
    return cut1, cut2


def bisect_on_x(bm, x_local):
    # Bisect full geometry at one local X plane.
    geom = list(bm.verts) + list(bm.edges) + list(bm.faces)
    if not geom:
        return
    bmesh.ops.bisect_plane(
        bm,
        geom=geom,
        plane_co=Vector((x_local, 0.0, 0.0)),
        plane_no=Vector((1.0, 0.0, 0.0)),
        clear_outer=False,
        clear_inner=False,
    )


def classify_faces_by_center_x(bm, cut1, cut2):
    # Bucket faces by center X after bisect.
    faces_a = []
    faces_b = []
    faces_c = []

    for f in bm.faces:
        cx = f.calc_center_median().x
        if cx <= cut1:
            faces_a.append(f)
        elif cx <= cut2:
            faces_b.append(f)
        else:
            faces_c.append(f)

    return faces_a, faces_b, faces_c


def build_mesh_from_faces(src_bm, src_faces, new_mesh_name):
    # Build a new mesh from a face list and copy UVs/material index.
    if not src_faces:
        return None

    bm_new = bmesh.new()

    src_uv_layers = list(src_bm.loops.layers.uv)
    uv_layer_pairs = []
    for src_uv in src_uv_layers:
        dst_uv = bm_new.loops.layers.uv.new(src_uv.name)
        uv_layer_pairs.append((src_uv, dst_uv))

    vmap = {}

    for sf in src_faces:
        new_face_verts = []
        for sv in sf.verts:
            nv = vmap.get(sv)
            if nv is None:
                nv = bm_new.verts.new(sv.co.copy())
                vmap[sv] = nv
            new_face_verts.append(nv)

        try:
            nf = bm_new.faces.new(new_face_verts)
        except ValueError:
            # Face already exists. Skip duplicate.
            continue

        nf.material_index = sf.material_index
        nf.smooth = sf.smooth

        if uv_layer_pairs:
            for sl, dl in zip(sf.loops, nf.loops):
                for src_uv, dst_uv in uv_layer_pairs:
                    dl[dst_uv].uv = sl[src_uv].uv.copy()

    bm_new.normal_update()

    new_mesh = bpy.data.meshes.new(new_mesh_name)
    bm_new.to_mesh(new_mesh)
    bm_new.free()

    return new_mesh


def link_object_like_source(new_obj, src_obj):
    # Link new object to same collections as source.
    if src_obj.users_collection:
        for col in src_obj.users_collection:
            col.objects.link(new_obj)
    else:
        bpy.context.scene.collection.objects.link(new_obj)


def split_object_into_three(obj):
    # Split one object into three parts based on local X cuts.
    cut1, cut2 = get_local_cut_positions(obj)
    if cut1 is None or cut2 is None:
        return False, f"skip {obj.name}: cannot compute cuts"

    bm = bmesh.new()
    bm.from_mesh(obj.data)

    bisect_on_x(bm, cut1)
    bisect_on_x(bm, cut2)

    faces_a, faces_b, faces_c = classify_faces_by_center_x(bm, cut1, cut2)

    buckets = (faces_a, faces_b, faces_c)
    created = 0

    for idx, faces in enumerate(buckets):
        part_name = f"{obj.name}{SUFFIXES[idx]}"
        new_mesh = build_mesh_from_faces(bm, faces, part_name + "_mesh")
        if new_mesh is None:
            continue

        new_obj = bpy.data.objects.new(part_name, new_mesh)
        new_obj.matrix_world = obj.matrix_world.copy()

        # Copy material slots references.
        for mat in obj.data.materials:
            new_obj.data.materials.append(mat)

        link_object_like_source(new_obj, obj)
        created += 1

    bm.free()

    if created == 0:
        return False, f"skip {obj.name}: no parts created"

    if not KEEP_ORIGINAL:
        bpy.data.objects.remove(obj, do_unlink=True)
        return True, f"ok {obj.name}: created {created} parts, original removed"

    return True, f"ok {obj.name}: created {created} parts, original kept"


def main():
    # Entry point for selected mesh objects.
    if bpy.context.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')

    selected = [o for o in bpy.context.selected_objects if o.type == 'MESH']
    if not selected:
        print("No mesh objects selected.")
        return

    ok_count = 0
    for obj in list(selected):
        ok, msg = split_object_into_three(obj)
        print(msg)
        if ok:
            ok_count += 1

    print(f"Done. Split objects: {ok_count}/{len(selected)}")


if __name__ == "__main__":
    main()
