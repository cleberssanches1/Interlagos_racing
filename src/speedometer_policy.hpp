#pragma once

#include <cstdint>

namespace SpeedometerPolicy
{
    struct Display
    {
        uint16_t speedKmh;
        uint8_t rpmSegments;
        char gear;
    };

    inline Display Resolve(int32_t speedKmh, int32_t engineRpm, char gear)
    {
        if (speedKmh < 0) speedKmh = -speedKmh;
        if (speedKmh > 999) speedKmh = 999;

        // The tachometer art is marked 4..14 x1000. The actual gearbox shifts
        // at 12,500 RPM, so the last three cells form its redline cue.
        int32_t rpmSegments = ((engineRpm - 4000) * 12) / (12500 - 4000);
        if (rpmSegments < 0) rpmSegments = 0;
        if (rpmSegments > 12) rpmSegments = 12;
        if (gear != 'R' && gear != 'N' && (gear < '1' || gear > '8')) gear = 'N';

        return Display{static_cast<uint16_t>(speedKmh),
                       static_cast<uint8_t>(rpmSegments), gear};
    }
}
