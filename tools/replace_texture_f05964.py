import bpy
import os

OLD_NAME = "F05964.tga"
NEW_PATH = r"C:\Users\clebe\OneDrive\Área de Trabalho\Objetos corrida\Interlagos_2\ARQ_TGA\F06164.TGA"

def find_or_load_new_image():
    """Return the new image, loading it from disk if not already in bpy.data.images."""
    new_name = os.path.basename(NEW_PATH)
    # Check if already loaded (case-insensitive)
    for img in bpy.data.images:
        if img.filepath and os.path.normcase(img.filepath) == os.path.normcase(NEW_PATH):
            return img
        if img.name.lower() == new_name.lower():
            return img
    # Load from disk
    if not os.path.isfile(NEW_PATH):
        raise FileNotFoundError(f"Nova textura não encontrada: {NEW_PATH}")
    return bpy.data.images.load(NEW_PATH)

def replace():
    if not os.path.isfile(NEW_PATH):
        print(f"[ERRO] Arquivo não encontrado: {NEW_PATH}")
        return

    new_img = find_or_load_new_image()
    replaced = 0

    for mat in bpy.data.materials:
        if not mat.use_nodes or not mat.node_tree:
            continue
        for node in mat.node_tree.nodes:
            if node.type != 'TEX_IMAGE':
                continue
            if not node.image:
                continue
            img_basename = os.path.basename(node.image.filepath or node.image.name)
            if img_basename.lower() == OLD_NAME.lower():
                node.image = new_img
                replaced += 1
                print(f"  Material '{mat.name}': substituído por {new_img.name}")

    # Force viewport refresh
    for area in bpy.context.screen.areas:
        area.tag_redraw()

    print(f"\nConcluído: {replaced} nó(s) substituído(s).")

replace()
