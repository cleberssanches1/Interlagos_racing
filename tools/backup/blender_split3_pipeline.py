import bpy
import bmesh
from mathutils import Vector

# ------------------------------------------------------------
# Split selected mesh objects into three parts along local X
# in one pass.
# ------------------------------------------------------------

KEEP_ORIGINAL = False
SUFFIXES = ("_A", "_B", "_C")


def get_local_bounds_x(mesh):
    if mesh is None or len(mesh.vertices) == 0:
        return None
    min_x = min(v.co.x for v in mesh.vertices)
    max_x = max(v.co.x for v in mesh.vertices)
    return min_x, max_x


def compute_cuts(min_x, max_x):
    span = max_x - min_x
    if span <= 1e-8:
        return None
    return min_x + span / 3.0, min_x + 2.0 * span / 3.0


def bisect_on_x(bm, x_local):
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


def classify_faces(bm, cut1, cut2):
    a, b, c = [], [], []
    for f in bm.faces:
        cx = f.calc_center_median().x
        if cx <= cut1:
            a.append(f)
        elif cx <= cut2:
            b.append(f)
        else:
            c.append(f)
    return a, b, c


def build_mesh_from_faces(src_bm, src_faces, mesh_name):
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
    if src_obj.users_collection:
        for col in src_obj.users_collection:
            col.objects.link(new_obj)
    else:
        bpy.context.scene.collection.objects.link(new_obj)


def split_object(obj):
    if obj.type != 'MESH':
        return False, f"skip {obj.name}: not mesh"

    bx = get_local_bounds_x(obj.data)
    if bx is None:
        return False, f"skip {obj.name}: empty mesh"

    cuts = compute_cuts(bx[0], bx[1])
    if cuts is None:
        return False, f"skip {obj.name}: zero span"
    cut1, cut2 = cuts

    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bisect_on_x(bm, cut1)
    bisect_on_x(bm, cut2)

    fa, fb, fc = classify_faces(bm, cut1, cut2)
    buckets = (fa, fb, fc)

    made = 0
    for i, faces in enumerate(buckets):
        new_mesh = build_mesh_from_faces(bm, faces, f"{obj.name}{SUFFIXES[i]}_mesh")
        if new_mesh is None:
            continue

        new_obj = bpy.data.objects.new(f"{obj.name}{SUFFIXES[i]}", new_mesh)
        new_obj.matrix_world = obj.matrix_world.copy()

        for mat in obj.data.materials:
            new_obj.data.materials.append(mat)

        link_like_source(new_obj, obj)
        made += 1

    bm.free()

    if made > 0 and not KEEP_ORIGINAL:
        bpy.data.objects.remove(obj, do_unlink=True)

    return made > 0, f"ok {obj.name}: parts={made} cut1={cut1:.3f} cut2={cut2:.3f}"


def main():
    if bpy.context.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')

    sel = [o for o in bpy.context.selected_objects if o.type == 'MESH']
    if not sel:
        print("No mesh objects selected")
        return

    ok = 0
    for obj in list(sel):
        done, msg = split_object(obj)
        print(msg)
        if done:
            ok += 1

    print(f"Done. Split: {ok}/{len(sel)}")


if __name__ == "__main__":
    main()