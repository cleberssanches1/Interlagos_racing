import bpy
import json
import os

# Ajuste para o caminho do JSON gerado pelo script PowerShell
RENAME_MAP_JSON = r"C:\Users\clebe\OneDrive\Área de Trabalho\Objetos corrida\Interlagos_2\ARQ_TGA\tga_rename_map.json"


def load_map(path):
    # PowerShell often writes UTF-8 with BOM; utf-8-sig handles both with/without BOM.
    with open(path, "r", encoding="utf-8-sig") as f:
        data = json.load(f)
    items = data.get("items", [])
    rename = {}
    for it in items:
        old_name = it.get("old_name")
        new_name = it.get("new_name")
        if old_name and new_name:
            rename[old_name.lower()] = new_name
    return rename


def update_image(image, rename_map):
    changed = False
    old_fp = image.filepath
    abs_fp = bpy.path.abspath(old_fp) if old_fp else ""
    old_base = os.path.basename(abs_fp or old_fp)
    if not old_base:
        return False

    key = old_base.lower()
    if key not in rename_map:
        return False

    new_base = rename_map[key]
    if abs_fp:
        new_abs = os.path.join(os.path.dirname(abs_fp), new_base)
        image.filepath = bpy.path.relpath(new_abs)
    else:
        image.filepath = new_base
    image.name = new_base
    changed = True
    return changed


def update_material_nodes(mat, rename_map):
    changed = 0
    if not mat.use_nodes or not mat.node_tree:
        return changed
    for node in mat.node_tree.nodes:
        if node.type == "TEX_IMAGE" and node.image:
            if update_image(node.image, rename_map):
                changed += 1
    return changed


def main():
    if not os.path.exists(RENAME_MAP_JSON):
        raise FileNotFoundError(f"Mapa nao encontrado: {RENAME_MAP_JSON}")

    rename_map = load_map(RENAME_MAP_JSON)
    if not rename_map:
        print("Mapa vazio, nada para atualizar.")
        return

    img_changed = 0
    for img in bpy.data.images:
        try:
            if update_image(img, rename_map):
                img_changed += 1
        except Exception as ex:
            print(f"[WARN] imagem '{img.name}': {ex}")

    node_changed = 0
    for mat in bpy.data.materials:
        try:
            node_changed += update_material_nodes(mat, rename_map)
        except Exception as ex:
            print(f"[WARN] material '{mat.name}': {ex}")

    print(f"OK Blender refs atualizadas. images={img_changed} node_refs={node_changed}")


if __name__ == "__main__":
    main()
