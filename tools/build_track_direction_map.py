#!/usr/bin/env python3
"""Build the compact per-segment route-tangent map used by track streaming.

TDIR.BIN records the direction of increasing logical segment ids at the centre
of the drivable PATH.NYA guide.  Keeping this as an offline artifact prevents
the runtime belt from confusing a tight world-space hairpin with a driver
turning the car around in place.
"""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path


TDR_MAGIC = 0x31524454  # "TDR1"
TDR_VERSION = 1
TDR_RECORD_SIZE = 8
RDR_MAGIC = 0x31524452  # "RDR1"


def wrap_segment_id(segment_id: int, count: int) -> int:
    return ((segment_id - 1) % count) + 1


def read_u32(data: bytes, offset: int, big_endian: bool) -> int:
    return struct.unpack_from(">I" if big_endian else "<I", data, offset)[0]


def parse_path(path: Path) -> list[list[tuple[int, int]]]:
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"PATH.NYA curto: {path}")

    candidates: list[tuple[bool, int, int]] = []
    for big_endian in (True, False):
        version = read_u32(data, 0, big_endian)
        line_count = read_u32(data, 4, big_endian)
        if version == 1 and 0 < line_count <= 16:
            candidates.append((big_endian, version, line_count))
    if not candidates:
        raise ValueError(f"PATH.NYA invalido: {path}")

    best_lines: list[list[tuple[int, int]]] | None = None
    best_points = -1
    for big_endian, _version, line_count in candidates:
        for descriptor_base in (8, 12):
            if descriptor_base + line_count * 8 > len(data):
                continue
            lines: list[list[tuple[int, int]]] = []
            valid = True
            point_total = 0
            for line_index in range(line_count):
                desc = descriptor_base + line_index * 8
                point_count = read_u32(data, desc, big_endian)
                point_offset = read_u32(data, desc + 4, big_endian)
                if point_count == 0:
                    if line_index < 3:
                        lines.append([])
                    continue
                if point_offset == 0 or point_offset + point_count * 12 > len(data):
                    valid = False
                    break
                if line_index >= 3:
                    continue
                points: list[tuple[int, int]] = []
                for point_index in range(point_count):
                    offset = point_offset + point_index * 12
                    x = struct.unpack_from(">i" if big_endian else "<i", data, offset)[0]
                    z = struct.unpack_from(">i" if big_endian else "<i", data, offset + 8)[0]
                    points.append((x, z))
                lines.append(points)
                point_total += len(points)
            if valid and point_total > best_points:
                best_lines = lines
                best_points = point_total
    if best_lines is None or best_points < 2:
        raise ValueError(f"PATH.NYA sem guia utilizavel: {path}")
    return [line for line in best_lines if len(line) >= 2]


def read_rdr_center(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    if len(data) < 80:
        raise ValueError(f"RDR curto: {path}")
    magic, version, header_size, segment_id = struct.unpack_from("<IHHH", data, 0)
    if magic != RDR_MAGIC or version != 1 or header_size < 80:
        raise ValueError(f"RDR invalido: {path}")
    x, z = struct.unpack_from("<i4xi", data, 20)
    return segment_id, x, z


def manhattan(point: tuple[int, int], center: tuple[int, int]) -> int:
    return abs(point[0] - center[0]) + abs(point[1] - center[1])


def map_line_to_segments(
    line: list[tuple[int, int]], centers: dict[int, tuple[int, int]], count: int
) -> list[int]:
    ids = sorted(centers)
    mapped: list[int] = []
    previous = -1
    for point in line:
        if previous > 0:
            local = [
                wrap_segment_id(previous + delta, count)
                for delta in range(-3, 4)
                if wrap_segment_id(previous + delta, count) in centers
            ]
            candidate_ids = local or ids
        else:
            candidate_ids = ids
        best = min(candidate_ids, key=lambda sid: manhattan(point, centers[sid]))
        # A guide crossing can leave the local group; only then permit a global
        # reseed.  This keeps a hairpin from hopping to its nearby parallel lane.
        if previous > 0:
            global_best = min(ids, key=lambda sid: manhattan(point, centers[sid]))
            if manhattan(point, centers[global_best]) * 3 < manhattan(point, centers[best]):
                best = global_best
        mapped.append(best)
        previous = best
    return mapped


def orient_to_increasing_ids(mapped_lines: list[list[int]], count: int) -> bool:
    score = 0
    for mapped in mapped_lines:
        for index, current in enumerate(mapped):
            nxt = mapped[(index + 1) % len(mapped)]
            delta = (nxt - current) % count
            if delta == 0:
                continue
            score += 1 if delta <= count // 2 else -1
    return score < 0


def build_records(
    lines: list[list[tuple[int, int]]], mapped_lines: list[list[int]], count: int
) -> tuple[list[tuple[int, int, int]], int]:
    sums = [(0, 0, 0) for _ in range(count + 1)]
    for line, mapped in zip(lines, mapped_lines):
        for index, segment_id in enumerate(mapped):
            before = line[(index - 1) % len(line)]
            after = line[(index + 1) % len(line)]
            dx = after[0] - before[0]
            dz = after[1] - before[1]
            if dx == 0 and dz == 0:
                continue
            sx, sz, samples = sums[segment_id]
            sums[segment_id] = (sx + dx, sz + dz, samples + 1)

    records: list[tuple[int, int, int]] = [(0, 0, 0)] * (count + 1)
    direct_count = 0
    for segment_id in range(1, count + 1):
        sx, sz, samples = sums[segment_id]
        length = math.hypot(sx, sz)
        if length <= 0.0:
            continue
        x = int(round((sx / length) * 32767.0))
        z = int(round((sz / length) * 32767.0))
        confidence = min(255, samples * 64)
        records[segment_id] = (x, z, confidence)
        direct_count += 1
    return records, direct_count


def fill_short_gaps(
    records: list[tuple[int, int, int]], count: int, maximum_gap: int = 4
) -> int:
    """Interpolate only short unobserved runs between route samples.

    PATH.NYA is intentionally sparser than the render segmentation.  A short
    gap is still on the same local road arc, while a long scenery-only run is
    left invalid so runtime can use its conservative legacy fallback.
    """
    filled = 0
    for segment_id in range(1, count + 1):
        if records[segment_id][2] != 0:
            continue
        previous = 0
        following = 0
        for delta in range(1, maximum_gap + 1):
            candidate = wrap_segment_id(segment_id - delta, count)
            if records[candidate][2] != 0:
                previous = candidate
                break
        for delta in range(1, maximum_gap + 1):
            candidate = wrap_segment_id(segment_id + delta, count)
            if records[candidate][2] != 0:
                following = candidate
                break
        if previous == 0 or following == 0:
            continue
        px, pz, _ = records[previous]
        nx, nz, _ = records[following]
        sx = px + nx
        sz = pz + nz
        length = math.hypot(sx, sz)
        if length <= 1.0:
            # A nearly opposite pair indicates a local U-shaped path.  Use the
            # closest known tangent rather than inventing a diagonal direction.
            prev_distance = (segment_id - previous) % count
            next_distance = (following - segment_id) % count
            x, z = (px, pz) if prev_distance <= next_distance else (nx, nz)
        else:
            x = int(round((sx / length) * 32767.0))
            z = int(round((sz / length) * 32767.0))
        records[segment_id] = (x, z, 32)
        filled += 1
    return filled


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--path", type=Path, required=True)
    parser.add_argument("--rdr-dir", type=Path, required=True)
    parser.add_argument("--segment-count", type=int, required=True)
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.segment_count <= 0 or args.segment_count > 65535:
        raise ValueError("segment-count invalido")
    centers: dict[int, tuple[int, int]] = {}
    for segment_id in range(1, args.segment_count + 1):
        path = args.rdr_dir / f"S{segment_id:03d}.RDR"
        sid, x, z = read_rdr_center(path)
        if sid != segment_id:
            raise ValueError(f"RDR com id interno divergente: {path}")
        centers[segment_id] = (x, z)

    lines = parse_path(args.path)
    mapped_lines = [map_line_to_segments(line, centers, args.segment_count) for line in lines]
    reverse = orient_to_increasing_ids(mapped_lines, args.segment_count)
    if reverse:
        lines = [list(reversed(line)) for line in lines]
        mapped_lines = [list(reversed(mapped)) for mapped in mapped_lines]
    records, direct_count = build_records(lines, mapped_lines, args.segment_count)
    if direct_count < max(8, args.segment_count // 4):
        raise ValueError(
            f"cobertura de tangentes insuficiente: {direct_count}/{args.segment_count}"
        )
    interpolated_count = fill_short_gaps(records, args.segment_count)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("wb") as stream:
        stream.write(struct.pack("<IHHI", TDR_MAGIC, TDR_VERSION, TDR_RECORD_SIZE, args.segment_count))
        for segment_id in range(1, args.segment_count + 1):
            x, z, confidence = records[segment_id]
            stream.write(struct.pack("<HhhBB", segment_id, x, z, confidence, 0))

    print(
        f"track direction map ok: segments={args.segment_count} direct={direct_count} interpolated={interpolated_count} "
        f"lines={len(lines)} reversed={int(reverse)} out={args.out}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
