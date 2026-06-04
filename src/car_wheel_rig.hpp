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
    };

    bool Initialize(ModelObject& model,
                    bool isSmoothMesh,
                    const SRL::Math::Types::Vector3D* meshCenters,
                    size_t meshCenterCount);

    void Update(const Input& input);
    void Apply(MeshRenderer& renderer) const;
    bool Ready() const { return wheelCount_ == 4u; }

    const std::array<size_t, 4>& WheelMeshIds() const { return wheelMeshIds_; }

private:
    struct WheelSlot
    {
        size_t meshId = SIZE_MAX;
        SRL::Math::Types::Vector3D center{0.0, 0.0, 0.0};
        bool front = false;
    };

    static constexpr int32_t kMaxSteerDegX16 = 12 << 16;
    static constexpr int32_t kMaxPitchDegX16 = 18 << 16;
    static constexpr int32_t kMaxRollDegX16 = 8 << 16;
    static constexpr int32_t kMaxSuspensionOffsetX16 = static_cast<int32_t>(0x00002000); // ~0.125 short travel
    static constexpr int32_t kSteerFilterShift = 2;  // 1/4
    static constexpr int32_t kPitchFilterShift = 1;  // 1/2 (faster body pitch response)
    static constexpr int32_t kRollFilterShift = 3;   // 1/8
    static constexpr int32_t kSuspFilterShift = 4;   // 1/16 smoother wheel travel
    static constexpr int32_t kSpinDegPerKmhX16 = 2200; // tune visual spin
    static constexpr int32_t kPitchDegPerUnitX16 = 1 << 16; // 50 percent lower pitch gain

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
    std::array<int32_t, 4> wheelSpinDegX16_{0, 0, 0, 0};
    std::array<int32_t, 4> wheelSuspensionOffsetX16_{0, 0, 0, 0};
};
