#include <cstdio>
#include <cstddef>

#define private public
#define protected public
#include "car_physics_shared.hpp"
#include "car_system.hpp"
#include "track_system.hpp"
#include "game_loop_system.hpp"
#undef protected
#undef private

#ifdef TYPE_SIZE_PROBE_EMIT_SYMBOLS
#define DECLARE_SIZE_SYMBOL(symbol, type) unsigned char symbol[sizeof(type)] = {}
#else
namespace
{
template <typename T>
void PrintSize(const char* name)
{
    std::printf("%-44s %6zu\n", name, sizeof(T));
}
}
#endif

#ifdef TYPE_SIZE_PROBE_EMIT_SYMBOLS
DECLARE_SIZE_SYMBOL(size_fxp, SRL::Math::Types::Fxp);
DECLARE_SIZE_SYMBOL(size_vector3d, SRL::Math::Types::Vector3D);
DECLARE_SIZE_SYMBOL(size_carphysics_dynamicsstate, Game::CarPhysics::DynamicsState);
DECLARE_SIZE_SYMBOL(size_carphysics_groundstate, Game::CarPhysics::GroundState);
DECLARE_SIZE_SYMBOL(size_gameplayframestate, Game::GameplayFrameState);
DECLARE_SIZE_SYMBOL(size_surfacecontact, Game::SurfaceContact);
DECLARE_SIZE_SYMBOL(size_carsystem_gameplayinputsnapshot, Game::CarSystem::GameplayInputSnapshot);
DECLARE_SIZE_SYMBOL(size_carsystem_runtimedebugsnapshot, Game::CarSystem::RuntimeDebugSnapshot);
DECLARE_SIZE_SYMBOL(size_carsystem_drivetraindebugsnapshot, Game::CarSystem::DrivetrainDebugSnapshot);
DECLARE_SIZE_SYMBOL(size_carsystem_commandsnapshot_inputlatchstate, Game::CarSystem::CommandSnapshot::InputLatchState);
DECLARE_SIZE_SYMBOL(size_carsystem_commandsnapshot, Game::CarSystem::CommandSnapshot);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_segmentoverlaysnapshot, GameLoopSystem::SegmentOverlaySnapshot);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_overlaydiagnosticssnapshot, GameLoopSystem::OverlayDiagnosticsSnapshot);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_sh2splittelemetrysnapshot, GameLoopSystem::Sh2SplitTelemetrySnapshot);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_framepresentationsnapshot, GameLoopSystem::FramePresentationSnapshot);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_autolaproutestate, GameLoopSystem::AutoLapRouteState);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_camerapathruntimestate, GameLoopSystem::CameraPathRuntimeState);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_realtimefpsstate, GameLoopSystem::RealtimeFpsState);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_simulationruntimestate, GameLoopSystem::SimulationRuntimeState);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_carprepareruntimestate, GameLoopSystem::CarPrepareRuntimeState);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_lowworkoverlaystate, GameLoopSystem::LowWorkOverlayState);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_hwrstagetrace, GameLoopSystem::HwrStageTrace);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_lwrstagetrace, GameLoopSystem::LwrStageTrace);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_simulationpayload, GameLoopSystem::SimulationPayload);
DECLARE_SIZE_SYMBOL(size_gameloopsystem_carrenderframestate, GameLoopSystem::CarRenderFrameState);
DECLARE_SIZE_SYMBOL(size_tracksystem_trackframesnapshot_segmentmeta, TrackSystem::TrackFrameSnapshot::SegmentMeta);
DECLARE_SIZE_SYMBOL(size_tracksystem_trackframesnapshot, TrackSystem::TrackFrameSnapshot);
DECLARE_SIZE_SYMBOL(size_tracksystem_trackframeplan, TrackSystem::TrackFramePlan);
DECLARE_SIZE_SYMBOL(size_tracksystem_sh2perfbucket, TrackSystem::Sh2PerfBucket);
DECLARE_SIZE_SYMBOL(size_tracksystem_runtimediagnosticsstate, TrackSystem::RuntimeDiagnosticsState);
DECLARE_SIZE_SYMBOL(size_tracksystem, TrackSystem);
#endif

int main()
{
#ifndef TYPE_SIZE_PROBE_EMIT_SYMBOLS
    std::puts("TYPE SIZE AUDIT");
    std::puts("----------------------------------------------");
    PrintSize<SRL::Math::Types::Fxp>("Fxp");
    PrintSize<SRL::Math::Types::Vector3D>("Vector3D");
    PrintSize<Game::CarPhysics::DynamicsState>("CarPhysics::DynamicsState");
    PrintSize<Game::CarPhysics::GroundState>("CarPhysics::GroundState");
    PrintSize<Game::GameplayFrameState>("GameplayFrameState");
    PrintSize<Game::SurfaceContact>("SurfaceContact");
    PrintSize<Game::CarSystem::GameplayInputSnapshot>("CarSystem::GameplayInputSnapshot");
    PrintSize<Game::CarSystem::RuntimeDebugSnapshot>("CarSystem::RuntimeDebugSnapshot");
    PrintSize<Game::CarSystem::DrivetrainDebugSnapshot>("CarSystem::DrivetrainDebugSnapshot");
    PrintSize<Game::CarSystem::CommandSnapshot::InputLatchState>("CarSystem::CommandSnapshot::InputLatchState");
    PrintSize<Game::CarSystem::CommandSnapshot>("CarSystem::CommandSnapshot");
    PrintSize<GameLoopSystem::SegmentOverlaySnapshot>("GameLoopSystem::SegmentOverlaySnapshot");
    PrintSize<GameLoopSystem::OverlayDiagnosticsSnapshot>("GameLoopSystem::OverlayDiagnosticsSnapshot");
    PrintSize<GameLoopSystem::Sh2SplitTelemetrySnapshot>("GameLoopSystem::Sh2SplitTelemetrySnapshot");
    PrintSize<GameLoopSystem::FramePresentationSnapshot>("GameLoopSystem::FramePresentationSnapshot");
    PrintSize<GameLoopSystem::AutoLapRouteState>("GameLoopSystem::AutoLapRouteState");
    PrintSize<GameLoopSystem::CameraPathRuntimeState>("GameLoopSystem::CameraPathRuntimeState");
    PrintSize<GameLoopSystem::RealtimeFpsState>("GameLoopSystem::RealtimeFpsState");
    PrintSize<GameLoopSystem::SimulationRuntimeState>("GameLoopSystem::SimulationRuntimeState");
    PrintSize<GameLoopSystem::CarPrepareRuntimeState>("GameLoopSystem::CarPrepareRuntimeState");
    PrintSize<GameLoopSystem::LowWorkOverlayState>("GameLoopSystem::LowWorkOverlayState");
    PrintSize<GameLoopSystem::HwrStageTrace>("GameLoopSystem::HwrStageTrace");
    PrintSize<GameLoopSystem::LwrStageTrace>("GameLoopSystem::LwrStageTrace");
    PrintSize<GameLoopSystem::SimulationPayload>("GameLoopSystem::SimulationPayload");
    PrintSize<GameLoopSystem::CarRenderFrameState>("GameLoopSystem::CarRenderFrameState");
    PrintSize<TrackSystem::TrackFrameSnapshot::SegmentMeta>("TrackSystem::TrackFrameSnapshot::SegmentMeta");
    PrintSize<TrackSystem::TrackFrameSnapshot>("TrackSystem::TrackFrameSnapshot");
    PrintSize<TrackSystem::TrackFramePlan>("TrackSystem::TrackFramePlan");
    PrintSize<TrackSystem::Sh2PerfBucket>("TrackSystem::Sh2PerfBucket");
    PrintSize<TrackSystem::RuntimeDiagnosticsState>("TrackSystem::RuntimeDiagnosticsState");
    PrintSize<TrackSystem>("TrackSystem");
#endif
    return 0;
}
