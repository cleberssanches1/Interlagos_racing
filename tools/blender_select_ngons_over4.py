"""
Blender — localizar faces com mais de 4 vertices (invalidas no export Nya/GEO).

O exportador Saturn so aceita tris (3) e quads (4). N-gons com 5+ verts falham, ex.:
  seg_165.obj  (5 verts)
  seg_179.obj  (9 verts)
  seg_187.obj  (5 verts)
  seg_196.obj  (5 verts)
  seg_229.obj  (7 verts)

Uso rapido (objeto ja aberto / selecionado):
  1. Selecione o mesh do segmento (ou deixe ativo)
  2. Text Editor → colar este script → Run Script
  3. Entra em Edit Mode com as faces problemáticas selecionadas

Uso por nome (opcional):
  TARGET_NAMES = ["seg_165", "seg_179", ...]  # vazio = so o objeto ativo

Corrigir no Blender:
  - Face → Triangulate Faces (Ctrl+T)  ou
  - Knife / Connect Vertex Path ate ficar tri/quad
"""

import bpy
import bmesh
import re

# Se vazio: usa o objeto mesh ativo (ou selecionados).
# Se preenchido: procura objetos cujo nome contenha esses tokens.
TARGET_NAMES = [
    "seg_165",
    "seg_179",
    "seg_187",
    "seg_196",
    "seg_229",
]

# True = so faces com > 4 verts; False = tambem reporta tris/quads ok
ONLY_SELECT_BAD = True

# Entra em Edit Mode no primeiro mesh com problemas
ENTER_EDIT_MODE = True


def face_vert_count(face):
    return len(face.vertices)


def mesh_ngon_report(obj):
    """Return list of (face_index, vert_count) for faces with > 4 verts."""
    if obj is None or obj.type != "MESH":
        return []
    me = obj.data
    bad = []
    for fi, poly in enumerate(me.polygons):
        n = len(poly.vertices)
        if n > 4:
            bad.append((fi, n))
    return bad


def select_bad_faces_edit_mode(obj, bad_face_indices):
    """Enter edit mode and select only the given face indices."""
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)

    # Object mode first to clear other selections
    if bpy.context.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")

    me = obj.data
    # Deselect all in mesh
    for p in me.polygons:
        p.select = False
    for e in me.edges:
        e.select = False
    for v in me.vertices:
        v.select = False

    bad_set = set(bad_face_indices)
    for fi, poly in enumerate(me.polygons):
        if fi in bad_set:
            poly.select = True

    if ENTER_EDIT_MODE:
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_mode(type="FACE")
        # Make sure bmesh sees selection
        bm = bmesh.from_edit_mesh(me)
        bm.faces.ensure_lookup_table()
        for f in bm.faces:
            f.select = f.index in bad_set
        bmesh.update_edit_mesh(me, loop_triangles=False, destructive=False)


def collect_target_objects():
    objs = []
    if TARGET_NAMES:
        tokens = [t.lower() for t in TARGET_NAMES if t]
        for obj in bpy.data.objects:
            if obj.type != "MESH":
                continue
            name = obj.name.lower()
            # match seg_165, SEG_165, pista_seg.165, etc.
            for tok in tokens:
                if tok in name:
                    objs.append(obj)
                    break
                # numeric only "165" → seg_165 / .165
                m = re.match(r"seg[_\.]?(\d+)$", tok)
                if m:
                    num = m.group(1).lstrip("0") or "0"
                    if re.search(r"(?:seg[_\.]?|pista_seg\.)0*" + re.escape(num) + r"(?:\D|$)", name):
                        objs.append(obj)
                        break
        # unique
        seen = set()
        unique = []
        for o in objs:
            if o.name not in seen:
                seen.add(o.name)
                unique.append(o)
        return unique

    # Fallback: active + selected meshes
    for obj in bpy.context.selected_objects:
        if obj.type == "MESH":
            objs.append(obj)
    act = bpy.context.active_object
    if act and act.type == "MESH" and act not in objs:
        objs.append(act)
    return objs


def main():
    targets = collect_target_objects()
    if not targets:
        raise RuntimeError(
            "Nenhum mesh alvo. Selecione um objeto ou preencha TARGET_NAMES "
            "(ex.: seg_165, seg_179)."
        )

    print("=" * 60)
    print("N-gons com mais de 4 vertices (invalidos no export)")
    print("=" * 60)

    total_bad = 0
    first_bad_obj = None
    first_bad_faces = []

    for obj in targets:
        bad = mesh_ngon_report(obj)
        if not bad:
            print(f"OK  {obj.name}: nenhuma face com >4 verts")
            continue
        total_bad += len(bad)
        print(f"FAIL {obj.name}: {len(bad)} face(s) com >4 verts")
        for fi, n in bad[:40]:
            # Centro da face para achar no viewport
            poly = obj.data.polygons[fi]
            cx = sum(obj.data.vertices[i].co.x for i in poly.vertices) / n
            cy = sum(obj.data.vertices[i].co.y for i in poly.vertices) / n
            cz = sum(obj.data.vertices[i].co.z for i in poly.vertices) / n
            print(f"  face_index={fi}  verts={n}  center=({cx:.3f}, {cy:.3f}, {cz:.3f})")
        if len(bad) > 40:
            print(f"  ... +{len(bad) - 40} faces")
        if first_bad_obj is None:
            first_bad_obj = obj
            first_bad_faces = [fi for fi, _ in bad]

    print("=" * 60)
    print(f"Total faces invalidas: {total_bad}")

    if first_bad_obj is not None and ONLY_SELECT_BAD:
        # Deselect other objects
        if bpy.context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        bpy.ops.object.select_all(action="DESELECT")
        select_bad_faces_edit_mode(first_bad_obj, first_bad_faces)
        print(
            f"Selecionado: {first_bad_obj.name} — {len(first_bad_faces)} face(s) em Edit Mode. "
            f"Use View → Frame Selected (Numpad .) para focar."
        )
        print("Correcao tipica: Face menu → Triangulate Faces (Ctrl+T).")
    elif total_bad == 0:
        print("Nada a selecionar.")


if __name__ == "__main__":
    main()
