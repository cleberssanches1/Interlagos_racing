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
        // Wheel-plane surface heights (world Y). Larger Y = lower altitude.
        int16_t groundRearY = 0;
        int16_t groundFrontY = 0;
        int16_t groundLeftY = 0;
        int16_t groundRightY = 0;
        int32_t groundRearYRaw = 0;
        int32_t groundFrontYRaw = 0;
        int32_t groundLeftYRaw = 0;
        int32_t groundRightYRaw = 0;
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
        bool left = false;
    };

    static constexpr int32_t kMaxSteerDegX16 = 12 << 16;
    static constexpr int32_t kMaxPitchDegX16 = 32 << 16;
    static constexpr int32_t kMaxRollDegX16 = 18 << 16;
    static constexpr int32_t kMaxSuspensionOffsetX16 = static_cast<int32_t>(0x0000C000); // 0.75
    static constexpr int32_t kSteerFilterShift = 2;
    static constexpr int32_t kPitchFilterShift = 1;
    static constexpr int32_t kRollFilterShift = 1;
    static constexpr int32_t kSuspFilterShift = 1;
    static constexpr int32_t kSpinDegPerKmhX16 = 2200;
    static constexpr int32_t kDefaultWheelbaseRaw = 0x0001B332; // ~1.70
    static constexpr int32_t kDefaultTrackRaw = 0x00011998;     // ~1.10
    static constexpr int32_t kBodyPitchSign = 1;
    static constexpr int32_t kBodyRollSign = 1;
    static constexpr int32_t kRadToDegApprox = 57;
    static constexpr int32_t kPitchDeadzoneRaw = (1 << 15); // 0.5
    static constexpr int32_t kRollDeadzoneRaw = (1 << 15);
    static constexpr int32_t kDeltaFilterShift = 1;
    static constexpr int32_t kMaxDeltaJumpRaw = 40 << 16; // damp F/R flicker on seams
    // Asymmetric pitch rate: nose-down (decline) tracks topology ASAP; climb anti-empino.
    static constexpr int32_t kMaxPitchStepDownDegX16 = 16 << 16; // toward lower nose
    static constexpr int32_t kMaxPitchStepUpDegX16 = 4 << 16;   // toward higher nose
    static constexpr int32_t kMaxRollStepDegX16 = 8 << 16;

    void RefreshWheelGeometryFromCenters();

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
    int32_t heldPitchDegX16_ = 0; // last good sample (hold when a corner misses)
    int32_t heldRollDegX16_ = 0;
    int32_t filteredDeltaYRaw_ = 0;      // pitch: front−rear surface ΔY
    int32_t filteredRollDeltaYRaw_ = 0;  // roll: right−left surface ΔY
    bool deltaFilterInit_ = false;
    bool rollDeltaFilterInit_ = false;
    int32_t wheelbaseRaw_ = kDefaultWheelbaseRaw;
    int32_t trackRaw_ = kDefaultTrackRaw;
    std::array<int32_t, 4> wheelSpinDegX16_{0, 0, 0, 0};
    std::array<int32_t, 4> wheelSuspensionOffsetX16_{0, 0, 0, 0};
};
