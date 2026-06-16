#pragma once

#include <cstdint>

#include "simulation_scheduler_state.hpp"

// Operacoes passivas externas para o scheduler de simulacao.
// Nao devem ser integradas no runtime critico nesta fase.

namespace SimulationSchedulerDomain
{

inline void MarkSimulationCompleted(Game::SimulationRuntimeState& ioState,
                                    uint8_t slot)
{
    ioState.SetJobInFlight(false);
    ioState.SetHasCompleted(true);
    ioState.completedIdx = slot;
}

inline void ClearSimulationCompleted(Game::SimulationRuntimeState& ioState)
{
    ioState.SetHasCompleted(false);
}

inline void MarkSimulationDispatched(Game::SimulationRuntimeState& ioState,
                                     uint8_t slot)
{
    ioState.SetJobInFlight(true);
    ioState.inFlightIdx = slot;
    ioState.writeIdx ^= 1u;
    ++ioState.slaveDispatchCount;
}

template <typename TPrepareState>
inline void MarkPrepareCompleted(TPrepareState& ioState,
                                 uint8_t slot)
{
    ioState.SetJobInFlight(false);
    ioState.SetHasCompleted(true);
    ioState.completedIdx = slot;
}

template <typename TPrepareState>
inline void ClearPrepareCompleted(TPrepareState& ioState)
{
    ioState.SetHasCompleted(false);
}

template <typename TPrepareState>
inline void MarkPrepareDispatched(TPrepareState& ioState,
                                  uint8_t slot)
{
    ioState.SetJobInFlight(true);
    ioState.inFlightIdx = slot;
    ioState.writeIdx ^= 1u;
}

} // namespace SimulationSchedulerDomain
