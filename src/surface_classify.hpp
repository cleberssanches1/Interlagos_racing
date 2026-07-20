#pragma once

#include <cstdint>

// Shared ground vs wall classification from face normals.
// Driveable ramps: surface slope < ~70° from horizontal.
// Walls/curbs: slope ~70–90° (normal nearly horizontal).
//
// World convention: larger Y = lower altitude; "up" ≈ -Y.
// For a unit normal n, slope α from horizontal satisfies |n·up| = cos(α) ≈ |ny|.
//
// IMPORTANT: classification must be overflow-safe. GEO faces often have zero
// stored normals; the fallback cross-product can produce very large components.
// Never do ny² vs k * |n|² with raw int64 multiplies on those values.

namespace Game::SurfaceClassify
{
// Max driveable slope from horizontal (degrees). Faces steeper = walls.
static constexpr int kMaxDriveableSlopeDeg = 70;

// cos(70°) in 16.16 (~0.3420 → 22415). Used only for unit-scale normals.
static constexpr int64_t kMinGroundNyAbsQ16 = 22415;

// tan(70°) ≈ 2.7475 ≈ 11/4 = 2.75. Ground if max(|nx|,|nz|) <= |ny| * tan(70°).
// (Using max instead of hypot is slightly more permissive for ground — preferred.)
static constexpr int64_t kTanMaxSlopeNum = 11;
static constexpr int64_t kTanMaxSlopeDen = 4;

// Returns true if face is GROUND/driveable (slope < ~70°).
// nx,ny,nz are fixed-point normal components (any scale; ratio is used).
inline bool IsGroundFaceNormalRaw(int64_t nx, int64_t ny, int64_t nz)
{
    const int64_t ay = (ny < 0) ? -ny : ny;
    if (ay == 0) return false; // pure vertical wall / missing normal

    const int64_t ax = (nx < 0) ? -nx : nx;
    const int64_t az = (nz < 0) ? -nz : nz;
    const int64_t planar = (ax > az) ? ax : az;

    // Fast path: SGL-style near-unit 16.16 normals (matches old |ny| threshold style).
    if (ax <= (2 << 16) && ay <= (2 << 16) && az <= (2 << 16))
    {
        return ay >= kMinGroundNyAbsQ16;
    }

    // Overflow-safe general path (GEO cross-product normals, any scale):
    // ground iff planar / ay <= tan(70°) ≈ 2.75
    // α <= 45° when ay >= planar — always driveable under the 70° cap.
    if (ay >= planar) return true;

    // Reduce both sides before multiply so large cross-product components
    // cannot overflow int64 (ny² * k * n² was the previous failure mode).
    int64_t p = planar;
    int64_t y = ay;
    while (p > 0x3FFFFFFF || y > 0x3FFFFFFF)
    {
        p >>= 1;
        y >>= 1;
    }
    if (y <= 0) return false;
    return (p * kTanMaxSlopeDen) <= (y * kTanMaxSlopeNum);
}

inline bool IsWallFaceNormalRaw(int64_t nx, int64_t ny, int64_t nz)
{
    const int64_t ax = (nx < 0) ? -nx : nx;
    const int64_t az = (nz < 0) ? -nz : nz;
    const int64_t planar = (ax > az) ? ax : az;
    if (planar <= 0) return false; // pure floor/ceiling — not a side wall
    return !IsGroundFaceNormalRaw(nx, ny, nz);
}

} // namespace Game::SurfaceClassify
