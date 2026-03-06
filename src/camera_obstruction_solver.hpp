#pragma once

#include <srl.hpp>

#include "track_system.hpp"

// Camera obstruction solver that keeps camera out of nearby track volumes.
// This is an approximate solver based on nearest segment center.
class CameraObstructionSolver
{
public:
    using Vector3D = SRL::Math::Types::Vector3D;
    using Fxp = SRL::Math::Types::Fxp;

    struct Config
    {
        // Horizontal range around a segment center considered obstructed.
        Fxp obstructionRangeXZ{};
        // Vertical range around a segment center considered obstructed.
        Fxp obstructionRangeY{};
        // Pull camera this fraction toward target when obstructed.
        // 16/256 ~= 6.25% per iteration.
        int32_t pullFactor256 = 0;
        // Max correction iterations.
        int32_t maxIterations = 0;
    };

    static Config DefaultConfig()
    {
        Config cfg{};
        cfg.obstructionRangeXZ = Fxp::BuildRaw(52 << 16);
        cfg.obstructionRangeY = Fxp::BuildRaw(24 << 16);
        cfg.pullFactor256 = 16;
        cfg.maxIterations = 8;
        return cfg;
    }

    CameraObstructionSolver()
        : cfg_(DefaultConfig())
    {}

    explicit CameraObstructionSolver(const Config& cfg)
        : cfg_(cfg)
    {}

    // Resolve desired camera position against track obstruction.
    Vector3D Resolve(const TrackSystem* trackSystem,
                     const Vector3D& trackOffset,
                     const Vector3D& targetPos,
                     const Vector3D& desiredCameraPos) const
    {
        if (!trackSystem || !trackSystem->Ready()) return desiredCameraPos;

        Vector3D cam = desiredCameraPos;
        for (int32_t i = 0; i < cfg_.maxIterations; ++i)
        {
            int32_t segId = -1;
            Vector3D center{};
            if (!trackSystem->FindNearestSegment(cam, trackOffset, segId, center))
            {
                break;
            }

            const Fxp dx = (cam.X - center.X).Abs();
            const Fxp dy = (cam.Y - center.Y).Abs();
            const Fxp dz = (cam.Z - center.Z).Abs();
            const bool obstructed =
                (dx <= cfg_.obstructionRangeXZ) &&
                (dz <= cfg_.obstructionRangeXZ) &&
                (dy <= cfg_.obstructionRangeY);
            if (!obstructed) break;

            // Pull camera closer to target while preserving direction.
            // NewCam = Cam + (Target - Cam) * alpha
            const Vector3D toTarget = targetPos - cam;
            const Fxp alpha = Fxp::BuildRaw((cfg_.pullFactor256 << 16) / 256);
            cam = cam + (toTarget * alpha);
        }

        return cam;
    }

private:
    Config cfg_{};
};
