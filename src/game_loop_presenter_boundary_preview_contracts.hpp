#ifndef GAME_LOOP_PRESENTER_BOUNDARY_PREVIEW_CONTRACTS_HPP
#define GAME_LOOP_PRESENTER_BOUNDARY_PREVIEW_CONTRACTS_HPP

#include "game_loop_presenter_presence_preview_contracts.hpp"
#include "game_loop_presenter_summary_scheduler_reuse_preview_contracts.hpp"

namespace GameLoopRuntime
{

struct PresenterBoundaryPreviewPacket
{
    bool valid{false};
    PresenterPresencePreviewPacket presence{};
    PresenterSummarySchedulerReusePreviewPacket schedulerReuse{};
};

}

#endif
