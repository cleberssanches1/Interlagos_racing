import argparse
import math
import pathlib
import sys


def read_u32_be(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "big", signed=False)


def read_s32_be(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "big", signed=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Measure PATH.NYA polyline lengths and derive world-unit scale.")
    parser.add_argument(
        "--input",
        default=r"c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\cd\data\PATH.NYA",
        help="PATH.NYA file to inspect.")
    parser.add_argument(
        "--real-length-m",
        type=float,
        default=4309.0,
        help="Real-world track length in meters used for calibration.")
    parser.add_argument(
        "--fps",
        type=float,
        default=30.0,
        help="Simulation framerate used for km/h conversion.")
    parser.add_argument(
        "--top-speed-kmh",
        type=float,
        default=320.0,
        help="Desired top speed for deriving kMaxForwardSpeed.")
    return parser.parse_args()


def validate_and_collect_lines(data: bytes):
    if len(data) < 8:
        raise ValueError("file too small for PATH.NYA header")

    version = read_u32_be(data, 0)
    line_count = read_u32_be(data, 4)
    if version != 1:
        raise ValueError(f"unexpected version: {version}")
    if line_count == 0 or line_count > 16:
        raise ValueError(f"unexpected line count: {line_count}")

    header_size = 8 + (line_count * 8)
    if len(data) < header_size:
        raise ValueError("file too small for line descriptors")

    lines = []
    for line_index in range(line_count):
        descriptor_offset = 8 + (line_index * 8)
        point_count = read_u32_be(data, descriptor_offset)
        points_offset = read_u32_be(data, descriptor_offset + 4)

        if point_count == 0:
            lines.append([])
            continue

        expected_end = points_offset + (point_count * 12)
        if points_offset < header_size:
            raise ValueError(
                f"line {line_index}: points_offset={points_offset} overlaps header_size={header_size}")
        if expected_end > len(data):
            raise ValueError(
                f"line {line_index}: point_count={point_count} points_offset={points_offset} "
                f"needs {expected_end} bytes but file has {len(data)}")

        points = []
        for point_index in range(point_count):
            point_offset = points_offset + (point_index * 12)
            points.append((
                read_s32_be(data, point_offset) / 65536.0,
                read_s32_be(data, point_offset + 4) / 65536.0,
                read_s32_be(data, point_offset + 8) / 65536.0,
            ))
        lines.append(points)

    return version, line_count, lines


def measure_line(points):
    if len(points) < 2:
        return {
            "points": len(points),
            "length_xz_open": 0.0,
            "length_3d_open": 0.0,
            "close_gap_xz": 0.0,
            "close_gap_3d": 0.0,
            "length_xz_closed": 0.0,
            "length_3d_closed": 0.0,
        }

    length_xz_open = 0.0
    length_3d_open = 0.0
    for point_a, point_b in zip(points, points[1:]):
        dx = point_b[0] - point_a[0]
        dy = point_b[1] - point_a[1]
        dz = point_b[2] - point_a[2]
        length_xz_open += math.hypot(dx, dz)
        length_3d_open += math.sqrt((dx * dx) + (dy * dy) + (dz * dz))

    close_dx = points[0][0] - points[-1][0]
    close_dy = points[0][1] - points[-1][1]
    close_dz = points[0][2] - points[-1][2]
    close_gap_xz = math.hypot(close_dx, close_dz)
    close_gap_3d = math.sqrt(
        (close_dx * close_dx) + (close_dy * close_dy) + (close_dz * close_dz))

    return {
        "points": len(points),
        "length_xz_open": length_xz_open,
        "length_3d_open": length_3d_open,
        "close_gap_xz": close_gap_xz,
        "close_gap_3d": close_gap_3d,
        "length_xz_closed": length_xz_open + close_gap_xz,
        "length_3d_closed": length_3d_open + close_gap_3d,
    }


def main() -> int:
    args = parse_args()
    input_path = pathlib.Path(args.input)
    if not input_path.exists():
        print(f"PATH.NYA not found: {input_path}", file=sys.stderr)
        return 1

    data = input_path.read_bytes()
    print(f"file: {input_path}")
    print(f"size_bytes: {len(data)}")

    try:
        version, line_count, lines = validate_and_collect_lines(data)
    except ValueError as exc:
        print("status: invalid")
        print(f"reason: {exc}")
        print("expected_format: version(4) lineCount(4) then [pointCount(4), pointsOffset(4)] per line")
        print("next_step: regenerate PATH.NYA with tools/convert_path_obj_to_nya.ps1 before calibrating speed")
        return 2

    print("status: valid")
    print(f"version: {version}")
    print(f"line_count: {line_count}")

    best_line_index = -1
    best_line_length = -1.0
    measurements = []
    for line_index, line_points in enumerate(lines):
        measurement = measure_line(line_points)
        measurements.append(measurement)
        print(
            f"line[{line_index}] points={measurement['points']} "
            f"len_xz_open={measurement['length_xz_open']:.3f} "
            f"gap_xz={measurement['close_gap_xz']:.3f} "
            f"len_xz_closed={measurement['length_xz_closed']:.3f} "
            f"len_3d_closed={measurement['length_3d_closed']:.3f}")
        if measurement["points"] >= 2 and measurement["length_xz_closed"] > best_line_length:
            best_line_index = line_index
            best_line_length = measurement["length_xz_closed"]

    if best_line_index < 0:
        print("no usable lines found")
        return 3

    selected = measurements[best_line_index]
    world_units_per_meter = selected["length_xz_closed"] / max(1e-6, args.real_length_m)
    kmh_per_world_unit_per_frame = (args.fps * 3.6) / max(1e-6, world_units_per_meter)
    max_forward_speed = ((args.top_speed_kmh / 3.6) / args.fps) * world_units_per_meter

    print("")
    print(f"selected_line_index: {best_line_index}")
    print(f"selected_line_reason: longest closed-loop XZ length")
    print(f"track_length_real_m: {args.real_length_m:.3f}")
    print(f"track_length_game_units: {selected['length_xz_closed']:.3f}")
    print(f"world_units_per_meter: {world_units_per_meter:.6f}")
    print(f"kmh_per_world_unit_per_frame@{args.fps:.2f}fps: {kmh_per_world_unit_per_frame:.6f}")
    print(f"kMaxForwardSpeed_for_{args.top_speed_kmh:.1f}_kmh: {max_forward_speed:.6f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
