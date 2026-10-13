#!/usr/bin/env python3
"""Bake the map artwork atlas and LOD0-segment-to-map lookup for Interlagos.

The similarity registration is deliberately an offline build step. It matches
the ordered LOD0 asphalt centers in TCOL.BIN to the dark road outline in
mpint.tga; no geometry scan or floating-point fitting runs on the Saturn.
Requires Python 3 and NumPy.
"""
from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path

import numpy as np


TCOL_MAGIC = 0x314C4354
TCOL_VERSION = 1
TCOL_HEADER_SIZE = 34
TCOL_DIRECTORY_SIZE = 56
TCOL_VERTEX_SIZE = 12
TCOL_GROUND_SIZE = 16
LUT_MAGIC = b"MPL3"
LUT_VERSION = 3
# Segment 1 starts on the upper-left straight of the authored map. The art is
# mirrored relative to TCOL's X/Z coordinate frame.
START_SEGMENT_ID = 1
START_MAP_POINT = np.array([34.0, 50.0])


def read_tga(path: Path) -> tuple[bytes, np.ndarray, np.ndarray]:
    data = path.read_bytes()
    if len(data) < 18:
        raise ValueError(f"TGA truncado: {path}")
    ident, cmap_type, image_type = data[0], data[1], data[2]
    cmap_first, cmap_count = struct.unpack_from("<HH", data, 3)
    cmap_depth = data[7]
    width, height = struct.unpack_from("<HH", data, 12)
    pixel_depth, descriptor = data[16], data[17]
    pixel_offset = 18 + ident + cmap_count * ((cmap_depth + 7) // 8)
    if (cmap_type != 1 or image_type != 1 or cmap_first != 0 or
            cmap_depth != 32 or pixel_depth != 8 or cmap_count < 2 or
            pixel_offset + width * height > len(data)):
        raise ValueError("mpint.tga deve ser TGA paletizado, não comprimido, 8bpp/32bpp-paleta")
    palette = np.frombuffer(data, dtype=np.uint8, count=cmap_count * 4,
                            offset=18 + ident).copy().reshape(cmap_count, 4)
    pixels = np.frombuffer(data, dtype=np.uint8, count=width * height,
                           offset=pixel_offset).copy().reshape(height, width)
    if not descriptor & 0x20:
        pixels = pixels[::-1].copy()
    if descriptor & 0x10:
        pixels = pixels[:, ::-1].copy()
    return data, palette, pixels


def read_tcol(path: Path) -> tuple[int, dict[int, np.ndarray]]:
    data = path.read_bytes()
    if (len(data) < TCOL_HEADER_SIZE or
            struct.unpack_from("<IHHHHHHHH", data, 0)[:3] !=
            (TCOL_MAGIC, TCOL_VERSION, TCOL_HEADER_SIZE)):
        raise ValueError(f"TCOL header incompatível: {path}")
    segment_count = struct.unpack_from("<H", data, 20)[0]
    directory_offset = struct.unpack_from("<I", data, 30)[0]
    centers: dict[int, np.ndarray] = {}
    for index in range(segment_count):
        entry = directory_offset + index * TCOL_DIRECTORY_SIZE
        sid, vertex_count, ground_count = struct.unpack_from("<3H", data, entry)
        vertices_offset, ground_offset = struct.unpack_from("<II", data, entry + 28)
        if not ground_count or not vertex_count:
            continue
        vertices = np.frombuffer(data, dtype="<i4", count=vertex_count * 3,
                                 offset=vertices_offset).reshape(vertex_count, 3)
        face_points: list[np.ndarray] = []
        for face_index in range(ground_count):
            face = ground_offset + face_index * TCOL_GROUND_SIZE
            ids = struct.unpack_from("<4H", data, face)
            surface_type = data[face + 10]
            vertex_in_face = data[face + 11]
            if surface_type != 1 or vertex_in_face < 3:
                continue
            if any(vertex_id >= vertex_count for vertex_id in ids[:vertex_in_face]):
                raise ValueError(f"índice de vértice inválido no segmento {sid}")
            # TCOL positions are signed 16.16 world units. Only X/Z matter.
            face_points.extend(vertices[vertex_id, [0, 2]].astype(np.float64) / 65536.0
                               for vertex_id in ids[:vertex_in_face])
        if face_points:
            centers[sid] = np.mean(face_points, axis=0)
    if len(centers) < segment_count * 0.75:
        raise ValueError(f"poucas faces de asfalto em TCOL: {len(centers)}/{segment_count}")
    return segment_count, centers


def fit_similarity(world: np.ndarray, outline: np.ndarray,
                   start: np.ndarray) -> tuple[float, np.ndarray, np.ndarray, float]:
    """Anchor-aware ICP; allow either handedness, including the mirrored map."""
    source_mean = world.mean(axis=0)
    target_mean = outline.mean(axis=0)
    source = world - source_mean
    target = outline - target_mean
    source_values, source_vectors = np.linalg.eigh(source.T @ source)
    target_values, target_vectors = np.linalg.eigh(target.T @ target)
    del source_values, target_values
    base_scale = math.sqrt(float(np.sum(target * target) / np.sum(source * source)))
    best: tuple[float, float, np.ndarray, np.ndarray] | None = None

    for sign_a in (-1.0, 1.0):
        for sign_b in (-1.0, 1.0):
            rotation0 = source_vectors @ np.diag([sign_a, sign_b]) @ target_vectors.T
            handedness = np.linalg.det(rotation0)
            for scale0 in (base_scale * 0.8, base_scale, base_scale * 1.2):
                scale = scale0
                rotation = rotation0.copy()
                translation = START_MAP_POINT - scale * (start @ rotation)
                for _ in range(40):
                    projected = scale * (world @ rotation) + translation
                    distance_sq = ((projected[:, None, :] - outline[None, :, :]) ** 2).sum(axis=2)
                    nearest = distance_sq.argmin(axis=1)
                    distances = np.sqrt(distance_sq[np.arange(len(world)), nearest])
                    keep = distances <= np.percentile(distances, 85)
                    # Nearest-outline error alone chose the wrong side of the
                    # map. Keep the known start correspondence in every step.
                    src = np.vstack((world[keep], np.repeat(start[None, :], 20, axis=0)))
                    dst = np.vstack((outline[nearest[keep]],
                                     np.repeat(START_MAP_POINT[None, :], 20, axis=0)))
                    src_mean = src.mean(axis=0)
                    dst_mean = dst.mean(axis=0)
                    covariance = (src - src_mean).T @ (dst - dst_mean)
                    u, _, vt = np.linalg.svd(covariance)
                    rotation = u @ vt
                    if np.linalg.det(rotation) * handedness < 0:
                        vt[-1, :] *= -1
                        rotation = u @ vt
                    scale = float(np.sum((src - src_mean) @ rotation * (dst - dst_mean)) /
                                  np.sum((src - src_mean) ** 2))
                    translation = dst_mean - scale * (src_mean @ rotation)
                projected = scale * (world @ rotation) + translation
                anchor_error = float(np.linalg.norm(scale * (start @ rotation) +
                                                    translation - START_MAP_POINT))
                if (anchor_error > 4.0 or scale <= 0 or
                        projected.min() < -0.5 or projected.max() > 127.5):
                    continue
                distance_sq = ((projected[:, None, :] - outline[None, :, :]) ** 2).sum(axis=2)
                error = np.sqrt(distance_sq.min(axis=1))
                score = float(np.mean(np.minimum(error, 8.0) ** 2) + anchor_error ** 2)
                if best is None or score < best[0]:
                    best = (score, scale, rotation.copy(), translation.copy())
    if best is None:
        raise ValueError("não foi possível registrar LOD0 no contorno do mapa")
    score, scale, rotation, translation = best
    projected = scale * (world @ rotation) + translation
    distance_sq = ((projected[:, None, :] - outline[None, :, :]) ** 2).sum(axis=2)
    rms = float(np.sqrt(distance_sq.min(axis=1)).mean())
    if rms > 4.5:
        raise ValueError(f"registro do mapa impreciso: erro médio {rms:.2f}px (limite 4.5px)")
    return scale, rotation, translation, rms


def interpolate_missing(points: dict[int, tuple[int, int]], count: int) -> list[tuple[int, int]]:
    valid = sorted(points)
    if not valid:
        raise ValueError("tabela de segmentos vazia")
    result: list[tuple[int, int]] = []
    for sid in range(1, count + 1):
        if sid in points:
            result.append(points[sid])
            continue
        before = max((item for item in valid if item < sid), default=valid[-1] - count)
        after = min((item for item in valid if item > sid), default=valid[0] + count)
        p0 = points[((before - 1) % count) + 1]
        p1 = points[((after - 1) % count) + 1]
        t = (sid - before) / (after - before)
        result.append((round(p0[0] + (p1[0] - p0[0]) * t),
                       round(p0[1] + (p1[1] - p0[1]) * t)))
    return result


def write_hud_tga(path: Path, palette: np.ndarray, source_pixels: np.ndarray) -> None:
    atlas = np.full((240, 256), 16, dtype=np.uint8)
    atlas[:128, :128] = source_pixels
    # Eight 16x16 red/white arrow patterns, pointing north and then clockwise
    # every 45 degrees. Each sprite occupies a 2x2 block of VDP2 cells.
    center = 7.5
    for direction in range(8):
        angle = direction * math.pi / 4.0
        dx, dy = math.sin(angle), -math.cos(angle)
        nx, ny = -dy, dx
        for y in range(16):
            for x in range(16):
                px, py = x - center, y - center
                along = px * dx + py * dy
                across = abs(px * nx + py * ny)
                shaft = -6.8 <= along <= 2.2 and across <= 2.2
                head_width = min(4.4, max(0.8, (7.4 - along) * 0.82))
                head = 0.5 <= along <= 7.4 and across <= head_width
                if not (shaft or head):
                    continue
                inner_shaft = -5.9 <= along <= 1.3 and across <= 1.0
                inner_head = 1.4 <= along <= 6.2 and across <= max(0.0, (6.3 - along) * 0.46)
                atlas[y, 128 + direction * 16 + x] = 14 if inner_shaft or inner_head else 3
    header = bytearray(18)
    header[1] = 1
    header[2] = 1
    struct.pack_into("<HH", header, 3, 0, len(palette))
    header[7] = 32
    struct.pack_into("<HH", header, 12, 256, 240)
    header[16] = 8
    header[17] = 0  # bottom-left origin, matching the original mpint asset
    path.write_bytes(bytes(header) + palette.astype(np.uint8).tobytes() + atlas[::-1].tobytes())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", type=Path, default=Path("cd/data"))
    args = parser.parse_args()
    data_dir = args.data_dir
    map_path = data_dir / "mpint.tga"
    tcol_path = data_dir / "TCOL.BIN"
    _, palette, map_pixels = read_tga(map_path)
    width = struct.unpack_from("<H", map_path.read_bytes(), 12)[0]
    height = struct.unpack_from("<H", map_path.read_bytes(), 14)[0]
    if (width, height) != (128, 128) or len(palette) != 17:
        raise ValueError("mpint.tga esperado como 128x128, paleta com 17 entradas")
    count, centers = read_tcol(tcol_path)
    dark = ((palette[map_pixels, 3] > 0) & (palette[map_pixels, :3].max(axis=2) < 40))
    y, x = np.where(dark)
    outline = np.stack((x, y), axis=1).astype(np.float64)
    if len(outline) < 100:
        raise ValueError("contorno escuro insuficiente em mpint.tga")
    ids = np.array(sorted(centers), dtype=np.int32)
    world = np.stack([centers[int(sid)] for sid in ids])
    if START_SEGMENT_ID not in centers:
        raise ValueError(f"segmento inicial {START_SEGMENT_ID} ausente em TCOL")
    scale, rotation, translation, rms = fit_similarity(world, outline,
                                                       centers[START_SEGMENT_ID])
    pixels = scale * (world @ rotation) + translation
    if (pixels[:, 0].min() < -0.5 or pixels[:, 1].min() < -0.5 or
            pixels[:, 0].max() > 127.5 or pixels[:, 1].max() > 127.5):
        raise ValueError("o registro posiciona parte do asfalto fora dos 128x128 pixels do mapa")
    points: dict[int, tuple[int, int]] = {}
    for sid, point in zip(ids, pixels):
        u = int(np.clip(round(float(point[0])), 0, 127))
        v = int(np.clip(round(float(point[1])), 0, 127))
        points[int(sid)] = (u, v)
    lookup = interpolate_missing(points, count)
    # Car yaw 0 points along world -Z. Convert that heading to map pixels and
    # store the clockwise map-space yaw offset for an integer octant lookup.
    north_map_dx = -float(rotation[1, 0])
    north_map_dy = -float(rotation[1, 1])
    yaw_offset = math.degrees(math.atan2(north_map_dx, -north_map_dy))
    yaw_offset_centideg = int(round(yaw_offset * 100.0))
    # Reflection reverses the direction in which heading rotates on the map.
    yaw_sense = -1 if np.linalg.det(rotation) < 0 else 1
    lut = bytearray(struct.pack("<4sHHhbb", LUT_MAGIC, LUT_VERSION, count,
                                yaw_offset_centideg, yaw_sense, 0))
    for u, v in lookup:
        lut.extend((u, v))
    (data_dir / "MPILUT.BIN").write_bytes(lut)
    write_hud_tga(data_dir / "MPHUD.TGA", palette, map_pixels)
    angle = math.degrees(math.atan2(rotation[0, 1], rotation[0, 0]))
    print(f"minimap assets OK: segments={count} asphalt={len(centers)} rms={rms:.2f}px scale={scale:.7f} angle={angle:.2f}deg yaw_offset={yaw_offset:.2f}deg yaw_sense={yaw_sense} start={lookup[START_SEGMENT_ID - 1]}")
    print(f"generated: {data_dir / 'MPHUD.TGA'} ({(data_dir / 'MPHUD.TGA').stat().st_size} bytes)")
    print(f"generated: {data_dir / 'MPILUT.BIN'} ({len(lut)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
