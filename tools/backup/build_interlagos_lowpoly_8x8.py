#!/usr/bin/env python3
"""Build a portable 8x8 texture envelope for an Interlagos low-poly OBJ.

The geometry, UVs, normals and face declarations are preserved byte-for-byte at
the logical line level. Only ``mtllib`` and an empty ``usemtl`` assignment are
rewritten. A compact MTL containing every used material is emitted alongside a
portable 8x8 texture directory and a machine-readable audit report.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import shutil
import struct
import subprocess
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


FALLBACK_MATERIAL = "SEM_MATERIAL_8X8"
FALLBACK_RGB = (128, 128, 128)


@dataclass
class MaterialBlock:
    name: str
    lines: list[str]
    kd: tuple[float, float, float] | None
    map_kd: str | None


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tga_dimensions(path: Path) -> tuple[int, int]:
    header = path.read_bytes()[:18]
    if len(header) < 18:
        raise ValueError(f"Invalid TGA header: {path}")
    return struct.unpack("<HH", header[12:16])


def parse_material_blocks(path: Path) -> dict[str, MaterialBlock]:
    blocks: dict[str, MaterialBlock] = {}
    current_name: str | None = None
    current_lines: list[str] = []

    def finish() -> None:
        nonlocal current_name, current_lines
        if current_name is None:
            return
        kd = None
        map_kd = None
        for line in current_lines:
            stripped = line.strip()
            if stripped.startswith("Kd "):
                values = stripped.split()[1:4]
                if len(values) == 3:
                    kd = tuple(float(value) for value in values)
            elif stripped.lower().startswith("map_kd "):
                map_kd = stripped.split(None, 1)[1].strip()
        blocks[current_name] = MaterialBlock(current_name, current_lines[:], kd, map_kd)

    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if raw_line.startswith("newmtl "):
            finish()
            current_name = raw_line[7:].strip()
            current_lines = [raw_line]
        elif current_name is not None:
            current_lines.append(raw_line)
    finish()
    return blocks


def parse_face_vertex(token: str, vertex_count: int) -> int:
    raw = int(token.split("/", 1)[0])
    return raw - 1 if raw > 0 else vertex_count + raw


def polygon_area(points: list[tuple[float, float, float]]) -> float:
    if len(points) < 3:
        return 0.0
    origin = points[0]
    area = 0.0
    for index in range(1, len(points) - 1):
        a = points[index]
        b = points[index + 1]
        u = (a[0] - origin[0], a[1] - origin[1], a[2] - origin[2])
        v = (b[0] - origin[0], b[1] - origin[1], b[2] - origin[2])
        cross = (
            u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0],
        )
        area += 0.5 * math.sqrt(sum(component * component for component in cross))
    return area


def audit_obj(path: Path) -> dict:
    vertices: list[tuple[float, float, float]] = []
    texcoords = 0
    normals = 0
    face_tokens: list[list[str]] = []
    face_materials: list[str] = []
    used_material_order: list[str] = []
    used_material_set: set[str] = set()
    objects_with_faces: Counter[str] = Counter()
    current_material = ""
    current_object = ""
    mtllib = None

    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("mtllib "):
            mtllib = stripped.split(None, 1)[1]
        elif stripped.startswith("o "):
            current_object = stripped.split(None, 1)[1]
        elif stripped.startswith("usemtl"):
            current_material = stripped[6:].strip()
        elif stripped.startswith("v "):
            values = stripped.split()[1:4]
            vertices.append(tuple(float(value) for value in values))
        elif stripped.startswith("vt "):
            texcoords += 1
        elif stripped.startswith("vn "):
            normals += 1
        elif stripped.startswith("f "):
            tokens = stripped.split()[1:]
            face_tokens.append(tokens)
            face_materials.append(current_material)
            objects_with_faces[current_object] += 1
            if current_material not in used_material_set:
                used_material_set.add(current_material)
                used_material_order.append(current_material)

    edge_counts: Counter[tuple[int, int]] = Counter()
    adjacency: dict[int, set[int]] = defaultdict(set)
    repeated_vertex_faces = 0
    invalid_references: list[dict] = []
    areas: list[float] = []
    for face_index, tokens in enumerate(face_tokens, start=1):
        indices = [parse_face_vertex(token, len(vertices)) for token in tokens]
        if len(set(indices)) != len(indices):
            repeated_vertex_faces += 1
        for vertex_index in indices:
            if not 0 <= vertex_index < len(vertices):
                invalid_references.append({"face": face_index, "vertex": vertex_index + 1})
        for first, second in zip(indices, indices[1:] + indices[:1]):
            if first == second:
                continue
            edge = tuple(sorted((first, second)))
            edge_counts[edge] += 1
            adjacency[first].add(second)
            adjacency[second].add(first)
        if all(0 <= vertex_index < len(vertices) for vertex_index in indices):
            areas.append(polygon_area([vertices[vertex_index] for vertex_index in indices]))

    degrees = [len(adjacency[index]) for index in range(len(vertices))]
    face_sizes = Counter(len(tokens) for tokens in face_tokens)
    material_face_counts = Counter(face_materials)
    geometry_lines = [
        line.strip()
        for line in lines
        if line.strip().startswith(("v ", "vt ", "vn ", "f "))
    ]

    return {
        "vertices": len(vertices),
        "texcoords": texcoords,
        "normals": normals,
        "faces": len(face_tokens),
        "faceSizes": dict(sorted(face_sizes.items())),
        "objectsWithFaces": len(objects_with_faces),
        "objectFaceCounts": dict(objects_with_faces),
        "usedMaterialOrder": used_material_order,
        "materialFaceCounts": dict(material_face_counts),
        "mtllib": mtllib,
        "boundsMin": [min(point[axis] for point in vertices) for axis in range(3)],
        "boundsMax": [max(point[axis] for point in vertices) for axis in range(3)],
        "edges": len(edge_counts),
        "boundaryEdges": sum(count == 1 for count in edge_counts.values()),
        "manifoldEdges": sum(count == 2 for count in edge_counts.values()),
        "nonManifoldEdges": sum(count > 2 for count in edge_counts.values()),
        "maxVertexDegree": max(degrees, default=0),
        "verticesOverDegree4": sum(degree > 4 for degree in degrees),
        "degreeDistribution": dict(sorted(Counter(degrees).items())),
        "repeatedVertexFaces": repeated_vertex_faces,
        "zeroAreaFaces": sum(area <= 1.0e-8 for area in areas),
        "minimumFaceArea": min(areas, default=None),
        "invalidReferences": invalid_references,
        "geometryLines": geometry_lines,
    }


def path_basename(raw_path: str) -> str:
    return Path(raw_path.replace("\\", "/")).name


def choose_texture_source(
    basename: str, outer_root: Path, enhanced_texture_dir: Path | None
) -> Path:
    stem = Path(basename).stem
    if enhanced_texture_dir:
        preferred = enhanced_texture_dir / f"ARQ_TGA_{stem}_128.TGA"
        if preferred.is_file():
            return preferred

    direct = outer_root / "ARQ_TGA" / basename
    if direct.is_file():
        direct_area = math.prod(tga_dimensions(direct))
    else:
        direct_area = -1

    matches = [
        candidate
        for candidate in (outer_root / "ARQ_TGA").rglob("*")
        if candidate.is_file() and candidate.name.lower() == basename.lower()
    ]
    if direct.is_file():
        matches.append(direct)
    if not matches:
        raise FileNotFoundError(f"No source texture found for {basename}")
    return max(set(matches), key=lambda candidate: math.prod(tga_dimensions(candidate)))


def run(command: list[str]) -> None:
    completed = subprocess.run(command, text=True, capture_output=True)
    if completed.returncode != 0:
        raise RuntimeError(
            f"Command failed ({completed.returncode}): {' '.join(command)}\n"
            f"{completed.stdout}\n{completed.stderr}"
        )


def rgb_hex(kd: tuple[float, float, float] | None) -> tuple[str, tuple[int, int, int]]:
    if kd is None:
        rgb = FALLBACK_RGB
    else:
        rgb = tuple(max(0, min(255, round(channel * 255))) for channel in kd)
    return "".join(f"{channel:02X}" for channel in rgb), rgb


def material_with_map(block: MaterialBlock, relative_map: str) -> list[str]:
    output: list[str] = []
    map_written = False
    for line in block.lines:
        if line.strip().lower().startswith("map_kd "):
            if not map_written:
                output.append(f"map_Kd {relative_map}")
                map_written = True
        else:
            output.append(line)
    while output and not output[-1].strip():
        output.pop()
    if not map_written:
        output.append(f"map_Kd {relative_map}")
    output.append("")
    return output


def generic_material(name: str, kd: tuple[float, float, float], relative_map: str) -> list[str]:
    return [
        f"newmtl {name}",
        "Ns 0.000000",
        "Ka 1.000000 1.000000 1.000000",
        f"Kd {kd[0]:.6f} {kd[1]:.6f} {kd[2]:.6f}",
        "Ks 0.000000 0.000000 0.000000",
        "d 1.000000",
        "illum 1",
        f"map_Kd {relative_map}",
        "",
    ]


def build(args: argparse.Namespace) -> dict:
    source_obj = args.obj.resolve()
    if not source_obj.is_file():
        raise FileNotFoundError(source_obj)
    source_audit = audit_obj(source_obj)
    source_mtl = (source_obj.parent / source_audit["mtllib"]).resolve()
    if not source_mtl.is_file():
        raise FileNotFoundError(source_mtl)

    output_prefix = args.output_prefix or f"{source_obj.stem}_8x8_enveloped"
    output_obj = source_obj.parent / f"{output_prefix}.obj"
    output_mtl = source_obj.parent / f"{output_prefix}.mtl"
    output_report = source_obj.parent / f"{output_prefix}_report.json"
    texture_dir = source_obj.parent / args.texture_dir_name
    preview = source_obj.parent / f"{output_prefix}_textures_preview.png"

    targets = [output_obj, output_mtl, output_report, preview]
    if not args.force:
        existing = [str(path) for path in targets if path.exists()]
        if texture_dir.exists() and any(texture_dir.iterdir()):
            existing.append(str(texture_dir))
        if existing:
            raise FileExistsError("Output already exists: " + ", ".join(existing))
    texture_dir.mkdir(parents=True, exist_ok=True)

    blocks = parse_material_blocks(source_mtl)
    used_materials: list[str] = source_audit["usedMaterialOrder"]
    nonempty_materials = [name for name in used_materials if name]
    missing_material_blocks = [name for name in nonempty_materials if name not in blocks]

    material_to_relative_map: dict[str, str] = {}
    source_texture_by_basename: dict[str, Path] = {}
    solid_textures: dict[str, tuple[int, int, int]] = {}

    for material_name in nonempty_materials:
        block = blocks.get(material_name)
        if block and block.map_kd:
            basename = path_basename(block.map_kd)
            output_name = f"{Path(basename).stem}_8X8.TGA"
            source_texture_by_basename.setdefault(
                basename,
                choose_texture_source(basename, source_obj.parent.parent, args.enhanced_texture_dir),
            )
        else:
            kd = block.kd if block else None
            color_hex, color_rgb = rgb_hex(kd)
            output_name = f"SOLID_{color_hex}_8X8.TGA"
            solid_textures[color_hex] = color_rgb
        material_to_relative_map[material_name] = f"{args.texture_dir_name}/{output_name}"

    fallback_hex, fallback_rgb = rgb_hex(None)
    fallback_texture_name = f"SOLID_{fallback_hex}_8X8.TGA"
    solid_textures[fallback_hex] = fallback_rgb
    material_to_relative_map[FALLBACK_MATERIAL] = (
        f"{args.texture_dir_name}/{fallback_texture_name}"
    )

    generated_textures: list[dict] = []
    for basename, texture_source in sorted(source_texture_by_basename.items()):
        output_name = f"{Path(basename).stem}_8X8.TGA"
        output_path = texture_dir / output_name
        run(
            [
                str(args.magick),
                str(texture_source),
                "-colorspace",
                "sRGB",
                "-alpha",
                "off",
                "-filter",
                "Lanczos",
                "-resize",
                "8x8!",
                "-unsharp",
                "0x0.55+0.55+0.02",
                "-dither",
                "None",
                "-colors",
                "16",
                "-type",
                "TrueColor",
                "-define",
                "tga:bits-per-pixel=24",
                str(output_path),
            ]
        )
        generated_textures.append(
            {
                "output": output_name,
                "source": str(texture_source),
                "sourceSha256": sha256(texture_source),
                "outputSha256": sha256(output_path),
                "dimensions": list(tga_dimensions(output_path)),
                "kind": "downsampled",
            }
        )

    for color_hex, color_rgb in sorted(solid_textures.items()):
        output_name = f"SOLID_{color_hex}_8X8.TGA"
        output_path = texture_dir / output_name
        run(
            [
                str(args.magick),
                "-size",
                "8x8",
                f"xc:#{color_hex}",
                "-type",
                "TrueColor",
                "-define",
                "tga:bits-per-pixel=24",
                str(output_path),
            ]
        )
        generated_textures.append(
            {
                "output": output_name,
                "source": None,
                "rgb": list(color_rgb),
                "outputSha256": sha256(output_path),
                "dimensions": list(tga_dimensions(output_path)),
                "kind": "solid",
            }
        )

    source_lines = source_obj.read_text(encoding="utf-8", errors="replace").splitlines()
    output_obj_lines: list[str] = []
    mtllib_replaced = False
    for line in source_lines:
        stripped = line.strip()
        if stripped.startswith("mtllib "):
            output_obj_lines.append(f"mtllib {output_mtl.name}")
            mtllib_replaced = True
        elif re.fullmatch(r"usemtl\s*", stripped):
            output_obj_lines.append(f"usemtl {FALLBACK_MATERIAL}")
        else:
            output_obj_lines.append(line)
    if not mtllib_replaced:
        output_obj_lines.insert(2, f"mtllib {output_mtl.name}")
    output_obj.write_text("\n".join(output_obj_lines) + "\n", encoding="utf-8", newline="\n")

    output_mtl_lines = [
        "# Portable 8x8 material envelope generated by build_interlagos_lowpoly_8x8.py",
        "# Source geometry and UV assignments are unchanged.",
        "",
    ]
    for material_name in nonempty_materials:
        relative_map = material_to_relative_map[material_name]
        block = blocks.get(material_name)
        if block:
            output_mtl_lines.extend(material_with_map(block, relative_map))
        else:
            output_mtl_lines.extend(generic_material(material_name, (0.5, 0.5, 0.5), relative_map))
    fallback_relative_map = material_to_relative_map[FALLBACK_MATERIAL]
    output_mtl_lines.extend(
        generic_material(FALLBACK_MATERIAL, (0.5, 0.5, 0.5), fallback_relative_map)
    )
    output_mtl.write_text("\n".join(output_mtl_lines), encoding="utf-8", newline="\n")

    texture_paths = sorted(texture_dir.glob("*.TGA"))
    if texture_paths:
        run(
            [
                str(args.magick),
                "montage",
                *[str(path) for path in texture_paths],
                "-filter",
                "point",
                "-thumbnail",
                "128x128!",
                "-tile",
                "4x",
                "-geometry",
                "+8+8",
                "-background",
                "#202020",
                str(preview),
            ]
        )

    output_audit = audit_obj(output_obj)
    geometry_identical = source_audit["geometryLines"] == output_audit["geometryLines"]
    all_8x8 = all(tga_dimensions(path) == (8, 8) for path in texture_paths)

    mapped_output_materials: set[str] = set()
    current_material = None
    for line in output_mtl.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("newmtl "):
            current_material = stripped[7:]
        elif stripped.lower().startswith("map_kd ") and current_material:
            mapped_output_materials.add(current_material)
    expected_output_materials = set(nonempty_materials) | {FALLBACK_MATERIAL}
    all_used_materials_mapped = expected_output_materials <= mapped_output_materials

    unique_texture_count = len(texture_paths)
    report = {
        "generator": "tools/build_interlagos_lowpoly_8x8.py",
        "sourceObj": str(source_obj),
        "sourceMtl": str(source_mtl),
        "sourceObjSha256": sha256(source_obj),
        "sourceMtlSha256": sha256(source_mtl),
        "outputObj": str(output_obj),
        "outputMtl": str(output_mtl),
        "outputObjSha256": sha256(output_obj),
        "outputMtlSha256": sha256(output_mtl),
        "sourceAudit": {key: value for key, value in source_audit.items() if key != "geometryLines"},
        "outputAudit": {key: value for key, value in output_audit.items() if key != "geometryLines"},
        "materialEnvelope": {
            "sourceUsedMaterialSlots": len(used_materials),
            "emptySourceMaterialFaces": source_audit["materialFaceCounts"].get("", 0),
            "fallbackMaterial": FALLBACK_MATERIAL,
            "missingMaterialBlocks": missing_material_blocks,
            "uniqueSourceMaps": len(source_texture_by_basename),
            "solidTextureCount": len(solid_textures),
            "uniqueOutputTextures": unique_texture_count,
            "materialToMap": material_to_relative_map,
        },
        "textures": generated_textures,
        "estimatedRawTextureBytes": {
            "4bppWithoutClut": unique_texture_count * 8 * 8 // 2,
            "8bppWithoutClut": unique_texture_count * 8 * 8,
            "24bppSourceTgaPayload": unique_texture_count * 8 * 8 * 3,
        },
        "validation": {
            "geometryUvNormalAndFaceLinesIdentical": geometry_identical,
            "vertexCountIdentical": source_audit["vertices"] == output_audit["vertices"],
            "texcoordCountIdentical": source_audit["texcoords"] == output_audit["texcoords"],
            "normalCountIdentical": source_audit["normals"] == output_audit["normals"],
            "faceCountIdentical": source_audit["faces"] == output_audit["faces"],
            "allOutputTextures8x8": all_8x8,
            "allUsedMaterialsMapped": all_used_materials_mapped,
            "absoluteTexturePathsRemaining": bool(
                re.search(r"(?im)^map_kd\s+[a-z]:[/\\]", output_mtl.read_text(encoding="utf-8"))
            ),
        },
        "preview": str(preview),
    }
    output_report.write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    failed = [key for key, value in report["validation"].items() if key != "absoluteTexturePathsRemaining" and not value]
    if report["validation"]["absoluteTexturePathsRemaining"]:
        failed.append("absoluteTexturePathsRemaining")
    if failed:
        raise RuntimeError("Validation failed: " + ", ".join(failed))
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("obj", type=Path, help="Source OBJ")
    parser.add_argument("--output-prefix")
    parser.add_argument("--texture-dir-name", default="ARQ_TGA_8X8_ENVELOPE")
    parser.add_argument("--enhanced-texture-dir", type=Path)
    parser.add_argument(
        "--magick",
        type=Path,
        default=Path(shutil.which("magick") or "magick"),
        help="ImageMagick executable",
    )
    parser.add_argument("--force", action="store_true")
    return parser.parse_args()


if __name__ == "__main__":
    result = build(parse_args())
    print(json.dumps(result, indent=2, ensure_ascii=False))
