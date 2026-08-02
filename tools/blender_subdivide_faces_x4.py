"""
Blender Edit Mode — fatiar SOMENTE as faces selecionadas no eixo Y.

Planos Y = constante → faixas [|][|]... sem cruz [+].
Nao recorta faces nao selecionadas do objeto.

Uso: Edit Mode, selecione so as faces do asfalto, Run Script.
"""

import bpy
import bmesh
from mathutils import Vector

# 4 faixas finais → 3 planos de corte
PARTS = 4
EPS = 1e-6

# Eixo de fatiamento: Y mundial
AXIS = Vector((0.0, 1.0, 0.0))
AXIS_INDEX = 1  # 0=X, 1=Y, 2=Z

LAYER_NAME = "subdiv_axis_job"


def face_axis_range(face):
    coords = [v.co[AXIS_INDEX] for v in face.verts]
    return min(coords), max(coords)


def get_or_new_int_layer(bm, name):
    layer = bm.faces.layers.int.get(name)
    if layer is None:
        layer = bm.faces.layers.int.new(name)
    return layer


def faces_of_job_crossing(bm, layer, job_id, cut_value):
    """Apenas faces marcadas com job_id que cruzam o plano de corte."""
    out = []
    bm.faces.ensure_lookup_table()
    for f in bm.faces:
        if f.hide:
            continue
        if f[layer] != job_id:
            continue
        f0, f1 = face_axis_range(f)
        if f0 < cut_value - EPS and f1 > cut_value + EPS:
            out.append(f)
    return out


def bisect_face(bm, face, cut_value, layer, job_id):
    verts = list(face.verts)
    edges = list(face.edges)
    geom = verts + edges + [face]
    plane_co = Vector((0.0, 0.0, 0.0))
    plane_co[AXIS_INDEX] = cut_value
    result = bmesh.ops.bisect_plane(
        bm,
        geom=geom,
        dist=EPS,
        plane_co=plane_co,
        plane_no=AXIS.copy(),
        use_snap_center=False,
        clear_inner=False,
        clear_outer=False,
    )
    # Herdar job_id nas faces novas do bisect (para os proximos cortes)
    for key in ("geom", "geom_cut"):
        for ele in result.get(key, []):
            if isinstance(ele, bmesh.types.BMFace):
                ele[layer] = job_id


obj = bpy.context.edit_object
if not obj or obj.type != "MESH":
    raise RuntimeError("Entre no Edit Mode de um Mesh.")

if PARTS < 2:
    raise RuntimeError("PARTS deve ser >= 2 (ex.: 4).")

me = obj.data
bm = bmesh.from_edit_mesh(me)
bm.faces.ensure_lookup_table()

layer = get_or_new_int_layer(bm, LAYER_NAME)

# Zera marca em TODAS as faces; so as selecionadas recebem job_id > 0
for f in bm.faces:
    f[layer] = 0

sel_faces = [f for f in bm.faces if f.select and not f.hide]
if not sel_faces:
    raise RuntimeError("Selecione faces do asfalto (somente as que devem ser cortadas).")

jobs = []  # (job_id, a0, a1)
job_id = 0
for f in sel_faces:
    a0, a1 = face_axis_range(f)
    if (a1 - a0) <= EPS * 10.0:
        continue
    job_id += 1
    f[layer] = job_id
    jobs.append((job_id, a0, a1))

if not jobs:
    raise RuntimeError(
        "Nenhuma face selecionada tem extensao no eixo Y global."
    )

cut_ts = [i / float(PARTS) for i in range(1, PARTS)]
total_cuts = 0

for jid, a0, a1 in jobs:
    span = a1 - a0
    for t in cut_ts:
        cut_value = a0 + span * t
        # SOMENTE faces deste job (selecionada + filhas do bisect)
        targets = faces_of_job_crossing(bm, layer, jid, cut_value)
        for f in list(targets):
            try:
                if not f.is_valid:
                    continue
            except ReferenceError:
                continue
            if f[layer] != jid:
                continue
            f0, f1 = face_axis_range(f)
            if not (f0 < cut_value - EPS and f1 > cut_value + EPS):
                continue
            bisect_face(bm, f, cut_value, layer, jid)
            total_cuts += 1
            bm.faces.ensure_lookup_table()
            bm.edges.ensure_lookup_table()
            bm.verts.ensure_lookup_table()

# Limpa marca interna (nao deixa lixo no mesh)
for f in bm.faces:
    f[layer] = 0

bmesh.update_edit_mesh(me)
bpy.ops.mesh.select_mode(type="FACE")

print(
    f"OK: so faces selecionadas ({len(jobs)}), eixo=Y, PARTS={PARTS}, "
    f"bisects={total_cuts}."
)
