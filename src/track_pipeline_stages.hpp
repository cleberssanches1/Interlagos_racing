#pragma once

#define DOXYGEN 1
#include <srl.hpp>
#undef DOXYGEN

class TrackSystem;

namespace TrackPipeline
{
class TrackMaintenanceStage
{
public:
    static bool RunInitial(TrackSystem& system);
    static bool RunPostSlide(TrackSystem& system, bool slidThisFrame);
    static bool RunLegacy(TrackSystem& system, bool windowSlid);
};

class TrackWindowStage
{
public:
    static bool Run(TrackSystem& system,
                    const SRL::Math::Types::Vector3D& carWorldPosition,
                    const SRL::Math::Types::Vector3D& trackOffset);
};

class TrackPrefetchStage
{
public:
    static void RunCompaction(TrackSystem& system, bool windowSlid);
    static void RunPrefetch(TrackSystem& system, bool windowSlid);
};

class TrackLodStage
{
public:
    static bool RunRecovery(TrackSystem& system, bool slidThisFrame);
};

class TrackWorkingSetStage
{
public:
    static bool Run(TrackSystem& system);
};
} // namespace TrackPipeline

