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
    // Keep visual pitch modest; probe noise / segment seams invent "hills".
    static constexpr int32_t kMaxPitchDegX16 = 12 << 16;
    static constexpr int32_t kMaxRollDegX16 = 8 << 16;
    static constexpr int32_t kMaxSuspensionOffsetX16 = static_cast<int32_t>(0x00002000); // ~0.125 short travel
    static constexpr int32_t kSteerFilterShift = 2;  // 1/4
    // Heavier pitch filter: 1/16 per frame (was 1/8 — snaps at segment seams).
    static constexpr int32_t kPitchFilterShift = 4;
    static constexpr int32_t kPitchFilterShiftBrake = 5; // 1/32 while braking / stopped
    static constexpr int32_t kRollFilterShift = 4;   // 1/16
    static constexpr int32_t kSuspFilterShift = 4;   // 1/16 smoother wheel travel
    static constexpr int32_t kSpinDegPerKmhX16 = 2200; // tune visual spin
    // Full wheelbase ≈ 1.70 (2 * kProbeHalfWheelBase) in 16.16.
    static constexpr int32_t kWheelbaseRaw = 0x0001B332;
    // Softer than true atan*57 so small ΔY does not nose-dive the mesh.
    static constexpr int32_t kRadToDegApprox = 28;
    // Ignore front/rear grade below this (probe triangulation noise on flat).
    static constexpr int32_t kPitchDeadzoneRaw = 10 << 16;
    // Below this speed, flatten pitch (stops front bob when parked).
    static constexpr int32_t kPitchHoldSpeedKmh = 12;
    // Low-pass on ΔY before converting to degrees (heavier = smoother seams).
    static constexpr int32_t kDeltaFilterShift = 3; // 1/8 toward sample
    // One-frame |ΔY| jump larger than this is treated as a segment seam spike.
    static constexpr int32_t kMaxDeltaJumpRaw = 18 << 16;
    // Hard cap on visual pitch change per frame (~0.75 deg) after filtering.
    static constexpr int32_t kMaxPitchStepDegX16 = (3 << 16) / 4;

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
