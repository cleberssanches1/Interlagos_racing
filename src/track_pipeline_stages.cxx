#include "track_pipeline_stages.hpp"

#include "track_system.hpp"

namespace TrackPipeline
{
bool TrackMaintenanceStage::RunInitial(TrackSystem& system)
{
    return system.RunInitialMaintenanceStage();
}

bool TrackMaintenanceStage::RunPostSlide(TrackSystem& system, bool slidThisFrame)
{
    return system.RunPostSlideMaintenanceStage(slidThisFrame);
}

bool TrackMaintenanceStage::RunLegacy(TrackSystem& system, bool windowSlid)
{
    return system.RunLegacyMaintenanceStage(windowSlid);
}

bool TrackWindowStage::Run(TrackSystem& system,
                           const SRL::Math::Types::Vector3D& carWorldPosition,
                           const SRL::Math::Types::Vector3D& trackOffset)
{
    return system.RunWindowStage(carWorldPosition, trackOffset);
}

void TrackPrefetchStage::RunCompaction(TrackSystem& system, bool windowSlid)
{
    system.RunTextureCompactionStage(windowSlid);
}

void TrackPrefetchStage::RunPrefetch(TrackSystem& system, bool windowSlid)
{
    system.RunPrefetchStage(windowSlid);
}

bool TrackLodStage::RunRecovery(TrackSystem& system, bool slidThisFrame)
{
    return system.RunPendingLodRecoveryStage(slidThisFrame);
}

bool TrackWorkingSetStage::Run(TrackSystem& system)
{
    return system.RunWorkingSetStage();
}
} // namespace TrackPipeline

