#pragma once

#include "game_loop_track_reuse_preview_presenter_ops.hpp"
#include "game_loop_track_reuse_runtime_bridge_assembler.hpp"

namespace GameLoopRuntime
{

inline void PresentTrackReuseRuntimePreviewPacket(
    const FrameReuseDomain::FrameReuseRuntimeOwnerPacket& runtimeOwnerPacket)
{
    PresentTrackReusePreviewPacket(
        BuildTrackReuseRuntimePreviewPacket(runtimeOwnerPacket));
}

} // namespace GameLoopRuntime
