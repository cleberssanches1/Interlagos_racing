#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <srl.hpp>

#include "mesh_renderer.hpp"
#include "modelObject.hpp"

class CarWheelRig
{
public:
    struct Input
    {
        int16_t speedKmh = 0;
        int16_t steering = 0;
        int16_t yawStepDeg = 0;
        int16_t groundRearY = 0;
        int16_t groundFrontY = 0;
        int32_t groundRearYRaw = 0;
        int32_t groundFrontYRaw = 0;
        uint8_t groundMask = 0;
        uint8_t braking = 0;
    };

    bool Initialize(ModelObject& model,
                    bool isSmoothMesh,
                    const SRL::Math::Types::Vector3D* meshCenters,
                    size_t meshCenterCount);

    void Update(const Input& input);
    void Apply(MeshRenderer& renderer) const;
    bool Ready() const { return wheelCount_ == 4u; }

    const std::array<size_t, 4>& WheelMeshIds() const { return wheelMeshIds_; }
    // Visual chassis attitude (16.16 degrees) for camera terrain follow.
    int32_t BodyPitchDegX16() const { return bodyPitchDegX16_; }
    int32_t BodyRollDegX16() const { return bodyRollDegX16_; }

private:
    struct WheelSlot
    {
        size_t meshId = SIZE_MAX;
        SRL::Math::Types::Vector3D center{0.0, 0.0, 0.0};
        bool front = false;
    };

    static constexpr int32_t kMaxSteerDegX16 = 12 << 16;
    // Allow clearer nose-up/down on real grades (Senna S, climbs).
    static constexpr int32_t kMaxPitchDegX16 = 18 << 16;
    static constexpr int32_t kMaxRollDegX16 = 8 << 16;
    static constexpr int32_t kMaxSuspensionOffsetX16 = static_cast<int32_t>(0x00004000); // ~0.25 short travel
    static constexpr int32_t kSteerFilterShift = 2;  // 1/4
    // Responsive pitch: 1/4 per frame (was 1/16 — car never leaned on grades).
    static constexpr int32_t kPitchFilterShift = 2;
    static constexpr int32_t kPitchFilterShiftBrake = 3; // 1/8 while braking / stopped
    static constexpr int32_t kRollFilterShift = 4;   // 1/16
    static constexpr int32_t kSuspFilterShift = 3;   // 1/8
    static constexpr int32_t kSpinDegPerKmhX16 = 2200; // tune visual spin
    // Full wheelbase ≈ 1.70 (2 * kProbeHalfWheelBase) in 16.16.
    static constexpr int32_t kWheelbaseRaw = 0x0001B332;
    // Closer to atan*57 so grade ΔY maps to visible chassis pitch.
    static constexpr int32_t kRadToDegApprox = 45;
    // Only ignore tiny probe noise on flat (was 10 — hid real mild slopes).
    static constexpr int32_t kPitchDeadzoneRaw = 2 << 16;
    // Flatten pitch only when nearly stopped (was 12 — killed pitch while rolling).
    static constexpr int32_t kPitchHoldSpeedKmh = 4;
    // Low-pass on ΔY (1/4) — still smooth seams, reacts to grades.
    static constexpr int32_t kDeltaFilterShift = 2; // 1/4 toward sample
    // Allow larger one-frame grade change (declines/climbs across faces).
    static constexpr int32_t kMaxDeltaJumpRaw = 40 << 16;
    // Hard cap on visual pitch change per frame (~2.5 deg).
    static constexpr int32_t kMaxPitchStepDegX16 = (5 << 16) / 2;

    bool DetectWheelIdsFromMeshtex(size_t meshCount, std::array<size_t, 4>& outIds, size_t& outCount) const;
    bool DetectWheelIdsFromMeshStats(ModelObject& model,
                                     bool isSmoothMesh,
                                     const SRL::Math::Types::Vector3D* meshCenters,
                                     size_t meshCenterCount,
                                     std::array<size_t, 4>& outIds,
                                     size_t& outCount) const;
    void ClassifyWheels(const std::array<size_t, 4>& meshIds,
                        ModelObject& model,
                        bool isSmoothMesh,
                        const SRL::Math::Types::Vector3D* meshCenters,
                        bool preferMeshtexOrderFrontAxle);
    void ResetState();

    static int32_t ClampInt(int32_t value, int32_t minValue, int32_t maxValue);
    static int32_t StepToward(int32_t current, int32_t target, int32_t shift);

    std::array<size_t, 4> wheelMeshIds_{SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
    std::array<WheelSlot, 4> wheelSlots_{};
    size_t wheelCount_ = 0u;
    int32_t steerDegX16_ = 0;
    int32_t bodyPitchDegX16_ = 0;
    int32_t bodyRollDegX16_ = 0;
    int32_t filteredDeltaYRaw_ = 0;
    bool deltaFilterInit_ = false;
    std::array<int32_t, 4> wheelSpinDegX16_{0, 0, 0, 0};
    std::array<int32_t, 4> wheelSuspensionOffsetX16_{0, 0, 0, 0};
};
