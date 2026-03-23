#pragma once

extern "C" {
#include <sega_tim.h>
}

#include <cstdint>

namespace Sh2FrtProfiler
{
    // FRT is local to each SH2. Use only local deltas; do not compare
    // absolute timestamps across master/slave.
    inline void EnsureInitialized()
    {
        TIM_FRT_INIT(TIM_CKS_128);
    }

    inline uint16_t Now()
    {
        return static_cast<uint16_t>(TIM_FRT_GET_16());
    }

    inline uint16_t Elapsed(uint16_t start, uint16_t end)
    {
        return static_cast<uint16_t>(end - start);
    }
}
