import struct
import pathlib
import math

d = pathlib.Path("cd/data/TCOL.BIN").read_bytes()
sc = struct.unpack_from("<H", d, 20)[0]
dir_off = struct.unpack_from("<I", d, 30)[0]


def read_seg(i: int):
    o = dir_off + i * 56
    # Match track_collision_map.hpp ReadSegmentAt:
    # +0 sid,+2 vc,+4 gc,+6 wc,+8 gdim, +12 minX,+16 maxX,+20 minZ,+24 maxZ,
    # +28 voff,+32 goff,+36 woff,+40 gcell,+48 wcell
    sid, vc, gc, wc, gdim = struct.unpack_from("<HHHHH", d, o)
    minx, maxx, minz, maxz = struct.unpack_from("<iiii", d, o + 12)
    voff, goff, woff = struct.unpack_from("<III", d, o + 28)
    return {
        "sid": sid,
        "vc": vc,
        "gc": gc,
        "wc": wc,
        "minx": minx,
        "maxx": maxx,
        "minz": minz,
        "maxz": maxz,
        "woff": woff,
    }


def walls_for(seg):
    out = []
    for wi in range(seg["wc"]):
        o = seg["woff"] + wi * 36
        ax, az, bx, bz, miny, maxy, nx, nz, fam, src = struct.unpack_from(
            "<iiiiiiiiHH", d, o
        )
        out.append((ax, az, bx, bz, miny, maxy, nx, nz, fam, src))
    return out


near_center = 0
for i in range(min(12, sc)):
    seg = read_seg(i)
    cx = (seg["minx"] + seg["maxx"]) * 0.5
    cz = (seg["minz"] + seg["maxz"]) * 0.5
    print(
        f"SEG {seg['sid']} bounds=({seg['minx']},{seg['maxx']},{seg['minz']},{seg['maxz']}) "
        f"center=({cx:.0f},{cz:.0f}) walls={seg['wc']}"
    )
    for wi, (ax, az, bx, bz, miny, maxy, nx, nz, fam, src) in enumerate(walls_for(seg)):
        length = math.hypot(bx - ax, bz - az)
        abx, abz = bx - ax, bz - az
        apx, apz = cx - ax, cz - az
        ab2 = abx * abx + abz * abz
        t = 0.0 if ab2 == 0 else max(0.0, min(1.0, (apx * abx + apz * abz) / ab2))
        qx, qz = ax + t * abx, az + t * abz
        closest = math.hypot(cx - qx, cz - qz)
        crosses_lane = closest < 120.0
        if crosses_lane:
            near_center += 1
        mark = " <== NEAR CENTER" if crosses_lane else ""
        print(
            f"  w{wi} fam={fam} len={length:.0f} closestToCtr={closest:.0f} "
            f"y=[{miny},{maxy}] A=({ax},{az}) B=({bx},{bz}){mark}"
        )

print(f"walls near center in first 12 segs: {near_center}")
