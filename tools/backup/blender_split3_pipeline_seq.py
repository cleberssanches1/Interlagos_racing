import bpy
import bmesh
from mathutils import Vector

# ------------------------------------------------------------
# Split selected mesh objects into four parts along local Y
# and rename outputs in strict numeric sequence.
# ------------------------------------------------------------

KEEP_ORIGINAL = False

# Sequential naming config
RENAME_SEQUENTIAL = True
SEQUENCE_PREFIX = "seg_"
SEQUENCE_START = 1
SEQUENCE_PAD = 3


def get_local_bounds_y(mesh):
    # Return local min and max on Y axis.
    if mesh is None or len(mesh.vertices) == 0:
        return None
    min_y = min(v.co.y for v in mesh.vertices)
    max_y = max(v.co.y for v in mesh.vertices)
    return min_y, max_y


def compute_cuts(min_y, max_y):
    # Compute 1/4, 2/4 and 3/4 split points.
    span = max_y - min_y
    if span <= 1e-8:
        return None
    return (
        min_y + span * 0.25,
        min_y + span * 0.50,
        min_y + span * 0.75,
    )


def bisect_on_y(bm, y_local):
    # Bisect geometry with a local Y plane.
    geom = list(bm.verts) + list(bm.edges) + list(bm.faces)
    if not geom:
        return
    bmesh.ops.bisect_plane(
        bm,
        geom=geom,
        plane_co=Vector((0.0, y_local, 0.0)),
        plane_no=Vector((0.0, 1.0, 0.0)),
        clear_outer=False,
        clear_inner=False,
    )


def classify_faces(bm, cut1, cut2, cut3):
    # Classify by face center Y after bisect.
    a, b, c, d = [], [], [], []
    for f in bm.faces:
        cy = f.calc_center_median().y
        if cy <= cut1:
            a.append(f)
        elif cy <= cut2:
            b.append(f)
        elif cy <= cut3:
            c.append(f)
        else:
            d.append(f)
    return a, b, c, d


def build_mesh_from_faces(src_bm, src_faces, mesh_name):
    # Build a new mesh bucket preserving UV and materials.
    if not src_faces:
        return None

    bm_new = bmesh.new()

    src_uv_layers = list(src_bm.loops.layers.uv)
    uv_layers = []
    for src_uv in src_uv_layers:
        uv_layers.append((src_uv, bm_new.loops.layers.uv.new(src_uv.name)))

    vmap = {}
    for sf in src_faces:
        nverts = []
        for sv in sf.verts:
            nv = vmap.get(sv)
            if nv is None:
                nv = bm_new.verts.new(sv.co.copy())
                vmap[sv] = nv
            nverts.append(nv)

        try:
            nf = bm_new.faces.new(nverts)
        except ValueError:
            continue

        nf.material_index = sf.material_index
        nf.smooth = sf.smooth

        for sl, dl in zip(sf.loops, nf.loops):
            for src_uv, dst_uv in uv_layers:
                dl[dst_uv].uv = sl[src_uv].uv.copy()

    bm_new.normal_update()
    new_mesh = bpy.data.meshes.new(mesh_name)
    bm_new.to_mesh(new_mesh)
    bm_new.free()
    return new_mesh


def link_like_source(new_obj, src_obj):
    # Link output object into same collections as source object.
    if src_obj.users_collection:
        for col in src_obj.users_collection:
            col.objects.link(new_obj)
    else:
        bpy.context.scene.collection.objects.link(new_obj)


def make_seq_name(seq_index):
    # Build final deterministic object name.
    return f"{SEQUENCE_PREFIX}{seq_index:0{SEQUENCE_PAD}d}"


def split_object(obj, seq_index):
    # Split one object into four parts and return next sequence index.
    src_name = obj.name

    if obj.type != 'MESH':
        return False, False, seq_index, f"skip {src_name}: not mesh"

    by = get_local_bounds_y(obj.data)
    if by is None:
        return False, False, seq_index, f"skip {src_name}: empty mesh"

    cuts = compute_cuts(by[0], by[1])
    if cuts is None:
        return False, False, seq_index, f"skip {src_name}: zero span on Y"
    cut1, cut2, cut3 = cuts

    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bisect_on_y(bm, cut1)
    bisect_on_y(bm, cut2)
    bisect_on_y(bm, cut3)

    fa, fb, fc, fd = classify_faces(bm, cut1, cut2, cut3)
    # Name order must follow the expected segment progression.
    # Previous order (fa, fb, fc) produced reversed naming on your track.
    buckets = (fd, fc, fb, fa)

    made = 0
    next_seq = seq_index
    for i, faces in enumerate(buckets):
        out_name = make_seq_name(next_seq) if RENAME_SEQUENTIAL else f"{src_name}_{i+1}"
        new_mesh = build_mesh_from_faces(bm, faces, f"{out_name}_mesh")
        if new_mesh is None:
            continue

        new_obj = bpy.data.objects.new(out_name, new_mesh)
        new_obj.matrix_world = obj.matrix_world.copy()

        for mat in obj.data.materials:
            new_obj.data.materials.append(mat)

        link_like_source(new_obj, obj)
        made += 1
        next_seq += 1

    bm.free()

    if made == 0:
        return False, False, seq_index, f"skip {src_name}: no parts created"

    will_remove = (not KEEP_ORIGINAL)
    return True, will_remove, next_seq, f"ok {src_name}: parts={made} seq:{seq_index}..{next_seq-1} Y cuts={cut1:.3f},{cut2:.3f},{cut3:.3f}"


def main():
    # Split all selected meshes. Remove originals only after loop.
    if bpy.context.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')

    sel = sorted(
        [o for o in bpy.context.selected_objects if o.type == 'MESH'],
        key=lambda o: o.name
    )
    if not sel:
        print("No mesh objects selected")
        return

    ok = 0
    seq = SEQUENCE_START
    to_remove = []

    for obj in list(sel):
        done, remove_flag, next_seq, msg = split_object(obj, seq)
        print(msg)
        if done:
            ok += 1
            seq = next_seq
            if remove_flag:
                to_remove.append(obj)

    for obj in to_remove:
        if obj.name in bpy.data.objects:
            bpy.data.objects.remove(obj, do_unlink=True)

    print(f"Done. Split: {ok}/{len(sel)}")


if __name__ == "__main__":
    main()
