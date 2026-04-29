import bpy
import os

OLD_NAME = "F01064.tga"
NEW_PATH = r"C:\Users\clebe\OneDrive\Área de Trabalho\Objetos corrida\Interlagos_2\ARQ_TGA\F06364.TGA"

def find_or_load_new_image():
    new_name = os.path.basename(NEW_PATH)
    for img in bpy.data.images:
        if img.filepath and os.path.normcase(bpy.path.abspath(img.filepath)) == os.path.normcase(NEW_PATH):
            return img
        if img.name.lower() == new_name.lower():
            return img
    if not os.path.isfile(NEW_PATH):
        raise FileNotFoundError(f"Nova textura não encontrada: {NEW_PATH}")
    return bpy.data.images.load(NEW_PATH)

def image_matches_old(img):
    """Return True if this image datablock refers to OLD_NAME."""
    checks = []
    # Blender name (e.g. "F01064.tga" or "F01064.tga.001")
    checks.append(img.name)
    # Basename from filepath (handles // relative Blender paths)
    if img.filepath:
        checks.append(bpy.path.basename(img.filepath))
        checks.append(os.path.basename(img.filepath))
    for c in checks:
        if c.lower().startswith(OLD_NAME.lower()):
            return True
    return False

def replace():
    if not os.path.isfile(NEW_PATH):
        print(f"[ERRO] Arquivo não encontrado: {NEW_PATH}")
        return

    # --- debug: listar todas as imagens carregadas ---
    print("=== Imagens em bpy.data.images ===")
    for img in bpy.data.images:
        print(f"  name='{img.name}'  filepath='{img.filepath}'")
    print("===================================")

    new_img = find_or_load_new_image()
    replaced = 0
    seen_mats = set()

    # Percorre todos os materiais (incluindo os não atribuídos a objetos)
    for mat in bpy.data.materials:
        if mat.name in seen_mats:
            continue
        seen_mats.add(mat.name)

        if not mat.use_nodes or not mat.node_tree:
            continue

        for node in mat.node_tree.nodes:
            if node.type != 'TEX_IMAGE':
                continue
            if not node.image:
                continue
            if image_matches_old(node.image):
                print(f"  [MATCH] Material '{mat.name}' / nó '{node.name}': "
                      f"'{node.image.name}' -> '{new_img.name}'")
                node.image = new_img
                replaced += 1

    for area in bpy.context.screen.areas:
        area.tag_redraw()

    if replaced == 0:
        print(f"\n[AVISO] Nenhum nó com '{OLD_NAME}' encontrado. "
              f"Confira os nomes listados acima.")
    else:
        print(f"\nConcluído: {replaced} nó(s) substituído(s).")

replace()
