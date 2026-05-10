#include "car_wheel_rig.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace
{
using Fxp = SRL::Math::Types::Fxp;
using Vector3D = SRL::Math::Types::Vector3D;

bool IsDigitAscii(char c)
{
    return c >= '0' && c <= '9';
}

bool ParseMeshIdToken(const char* begin, const char* end, size_t& outMeshId)
{
    if (!begin || !end || begin >= end) return false;
    size_t value = 0u;
    for (const char* p = begin; p < end; ++p)
    {
        if (!IsDigitAscii(*p)) return false;
        value = (value * 10u) + static_cast<size_t>(*p - '0');
    }
    outMeshId = value;
    return true;
}

bool ContainsWheelName(const char* begin, const char* end)
{
    if (!begin || !end || begin >= end) return false;
    char lowered[64]{};
    size_t n = 0u;
    for (const char* p = begin; p < end && n < (sizeof(lowered) - 1u); ++p)
    {
        lowered[n++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    }
    lowered[n] = '\0';

    auto contains = [&](const char* token) -> bool
    {
        if (!token || token[0] == '\0') return false;
        size_t tokenLen = 0u;
        while (token[tokenLen] != '\0') ++tokenLen;
        if (tokenLen == 0u || tokenLen > n) return false;
        for (size_t i = 0u; (i + tokenLen) <= n; ++i)
        {
            size_t k = 0u;
            while (k < tokenLen && lowered[i + k] == token[k]) ++k;
            if (k == tokenLen) return true;
        }
        return false;
    };

    return contains("roda") || contains("wheel");
}

bool TryReadMeshStats(ModelObject& model,
                      bool isSmoothMesh,
                      size_t meshId,
                      uint16_t& outFaceCount,
                      uint16_t& outVertexCount)
{
    if (isSmoothMesh)
    {
        auto* mesh = model.GetMesh<SRL::Types::SmoothMesh>(meshId);
        if (!mesh) return false;
        outFaceCount = static_cast<uint16_t>(mesh->FaceCount);
        outVertexCount = static_cast<uint16_t>(mesh->VertexCount);
        return (mesh->FaceCount > 0u) && (mesh->VertexCount > 0u);
    }

    auto* mesh = model.GetMesh<SRL::Types::Mesh>(meshId);
    if (!mesh) return false;
    outFaceCount = static_cast<uint16_t>(mesh->FaceCount);
    outVertexCount = static_cast<uint16_t>(mesh->VertexCount);
    return (mesh->FaceCount > 0u) && (mesh->VertexCount > 0u);
}
} // namespace

bool CarWheelRig::Initialize(ModelObject& model,
                             bool isSmoothMesh,
                             const SRL::Math::Types::Vector3D* meshCenters,
                             size_t meshCenterCount)
{
    ResetState();
    const size_t meshCount = model.GetMeshCount();
    if (meshCount < 5u || !meshCenters || meshCenterCount < meshCount)
    {
        return false;
    }

    std::array<size_t, 4> wheelIds{SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
    size_t wheelIdCount = 0u;
    if (!DetectWheelIdsFromMeshtex(meshCount, wheelIds, wheelIdCount))
    {
        (void)DetectWheelIdsFromMeshStats(model,
                                          isSmoothMesh,
                                          meshCenters,
                                          meshCenterCount,
                                          wheelIds,
                                          wheelIdCount);
    }
    if (wheelIdCount < 4u)
    {
        return false;
    }

    ClassifyWheels(wheelIds, meshCenters);
    wheelCount_ = 4u;
    return true;
}

void CarWheelRig::Update(const Input& input)
{
    if (!Ready()) return;

    const int32_t clampedSteer = std::clamp<int32_t>(input.steering, -100, 100);
    const int32_t targetSteer = (clampedSteer * kMaxSteerDegX16) / 100;
    steerDegX16_ = StepToward(steerDegX16_, targetSteer, kSteerFilterShift);

    const int32_t clampedSpeed = std::max<int32_t>(0, input.speedKmh);
    const int32_t spinStep = clampedSpeed * kSpinDegPerKmhX16;
    const int32_t fullTurn = 360 << 16;
    for (size_t i = 0; i < 4u; ++i)
    {
        wheelSpinDegX16_[i] += spinStep;
        if (wheelSpinDegX16_[i] >= fullTurn)
        {
            wheelSpinDegX16_[i] %= fullTurn;
        }
    }

    int32_t targetPitch = 0;
    int32_t targetFrontSusp = 0;
    int32_t targetRearSusp = 0;
    const bool rearValid = (input.groundMask & 0x1u) != 0u;
    const bool frontValid = (input.groundMask & 0x4u) != 0u;
    if (rearValid && frontValid)
    {
        const int32_t deltaY = static_cast<int32_t>(input.groundFrontY) -
                               static_cast<int32_t>(input.groundRearY);
        targetPitch = ClampInt(-(deltaY * (1 << 14)), -kMaxPitchDegX16, kMaxPitchDegX16);
        targetFrontSusp = ClampInt(deltaY * (1 << 11),
                                   -kMaxSuspensionOffsetX16,
                                   kMaxSuspensionOffsetX16);
        targetRearSusp = -targetFrontSusp;
    }

    bodyPitchDegX16_ = StepToward(bodyPitchDegX16_, targetPitch, kPitchFilterShift);

    const int32_t speedNorm256 = std::min<int32_t>(256, (clampedSpeed * 256) / 200);
    const int32_t targetRoll =
        ClampInt(-(steerDegX16_ * speedNorm256) / 512, -kMaxRollDegX16, kMaxRollDegX16);
    bodyRollDegX16_ = StepToward(bodyRollDegX16_, targetRoll, kRollFilterShift);

    for (size_t i = 0; i < 4u; ++i)
    {
        const int32_t targetSusp = wheelSlots_[i].front ? targetFrontSusp : targetRearSusp;
        wheelSuspensionOffsetX16_[i] =
            StepToward(wheelSuspensionOffsetX16_[i], targetSusp, kSuspFilterShift);
    }
}

void CarWheelRig::Apply(MeshRenderer& renderer) const
{
    renderer.SetBodyAttitude(
        SRL::Math::Types::Angle::FromDegrees(Fxp::BuildRaw(bodyPitchDegX16_)),
        SRL::Math::Types::Angle::FromDegrees(Fxp::BuildRaw(bodyRollDegX16_)));

    renderer.ClearMeshLocalTransforms();
    if (!Ready()) return;

    const SRL::Math::Types::Angle zero = SRL::Math::Types::Angle::FromDegrees(0.0f);
    const SRL::Math::Types::Angle steer =
        SRL::Math::Types::Angle::FromDegrees(Fxp::BuildRaw(steerDegX16_));

    for (size_t i = 0; i < 4u; ++i)
    {
        if (wheelSlots_[i].meshId == SIZE_MAX) continue;

        MeshRenderer::LocalTransform t{};
        t.enabled = true;
        t.pivot = wheelSlots_[i].center;
        t.translation = Vector3D(0.0,
                                 Fxp::BuildRaw(wheelSuspensionOffsetX16_[i]),
                                 0.0);
        t.rotateX = SRL::Math::Types::Angle::FromDegrees(Fxp::BuildRaw(wheelSpinDegX16_[i]));
        t.rotateY = wheelSlots_[i].front ? steer : zero;
        t.rotateZ = zero;
        renderer.SetMeshLocalTransform(wheelSlots_[i].meshId, t);
    }
}

bool CarWheelRig::DetectWheelIdsFromMeshtex(size_t meshCount,
                                            std::array<size_t, 4>& outIds,
                                            size_t& outCount) const
{
    outIds = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
    outCount = 0u;
    const char* candidates[] = {
        "/CD/DATA/CAR1.meshtex",
        "/CD/DATA/CAR1.MESHTEX",
        "/DATA/CAR1.meshtex",
        "/DATA/CAR1.MESHTEX",
        "CD/DATA/CAR1.meshtex",
        "CD/DATA/CAR1.MESHTEX",
        "DATA/CAR1.meshtex",
        "DATA/CAR1.MESHTEX",
        "cd/data/CAR1.meshtex",
        "cd/data/CAR1.MESHTEX",
        "CAR1.meshtex",
        "CAR1.MESHTEX",
        "CAR1.meshtex;1",
        "CAR1.MESHTEX;1",
    };

    const char* selectedPath = nullptr;
    for (size_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i)
    {
        SRL::Cd::File probe(candidates[i]);
        if (probe.Exists() && probe.Size.Bytes > 0 && probe.Open())
        {
            selectedPath = candidates[i];
            break;
        }
    }
    if (!selectedPath) return false;
    SRL::Cd::File file(selectedPath);
    if (!file.Exists() || file.Size.Bytes <= 0 || !file.Open()) return false;

    auto appendWheelId = [&](size_t id)
    {
        if (id >= meshCount) return;
        for (size_t i = 0u; i < outCount; ++i)
        {
            if (outIds[i] == id) return;
        }
        if (outCount >= outIds.size()) return;
        outIds[outCount++] = id;
    };

    auto consumeLine = [&](const char* line, size_t lineLen)
    {
        if (!line || lineLen == 0u) return;
        const char* begin = line;
        const char* end = line + lineLen;
        const char* sep1 = nullptr;
        const char* sep2 = nullptr;
        for (const char* p = begin; p < end; ++p)
        {
            if (*p != ';') continue;
            if (!sep1) sep1 = p;
            else { sep2 = p; break; }
        }
        if (!sep1) return;
        if (!ContainsWheelName(sep1 + 1, sep2 ? sep2 : end)) return;
        size_t meshId = SIZE_MAX;
        if (!ParseMeshIdToken(begin, sep1, meshId)) return;
        appendWheelId(meshId);
    };

    char line[128]{};
    size_t lineLen = 0u;
    uint8_t chunk[256]{};
    while (true)
    {
        const int32_t got = file.Read(static_cast<int32_t>(sizeof(chunk)), chunk);
        if (got <= 0) break;
        for (int32_t i = 0; i < got; ++i)
        {
            const char c = static_cast<char>(chunk[i]);
            if (c == '\r') continue;
            if (c == '\n')
            {
                consumeLine(line, lineLen);
                lineLen = 0u;
                if (outCount >= 4u) return true;
                continue;
            }
            if (lineLen + 1u < sizeof(line))
            {
                line[lineLen++] = c;
            }
        }
    }
    if (lineLen > 0u) consumeLine(line, lineLen);
    return outCount >= 4u;
}

bool CarWheelRig::DetectWheelIdsFromMeshStats(ModelObject& model,
                                              bool isSmoothMesh,
                                              const SRL::Math::Types::Vector3D* meshCenters,
                                              size_t meshCenterCount,
                                              std::array<size_t, 4>& outIds,
                                              size_t& outCount) const
{
    struct Candidate
    {
        size_t meshId = SIZE_MAX;
        uint16_t faceCount = 0u;
        uint16_t vertexCount = 0u;
        int32_t radiusScore = 0;
    };

    outIds = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
    outCount = 0u;
    if (!meshCenters || meshCenterCount == 0u) return false;
    std::array<Candidate, 64> candidates{};
    size_t candidateCount = 0u;
    const size_t meshCount = model.GetMeshCount();
    for (size_t meshId = 1u; meshId < meshCount; ++meshId)
    {
        if (candidateCount >= candidates.size()) break;
        uint16_t faces = 0;
        uint16_t verts = 0;
        if (!TryReadMeshStats(model, isSmoothMesh, meshId, faces, verts)) continue;
        if (meshId >= meshCenterCount) continue;
        const int32_t score =
            (meshCenters[meshId].X.Abs() + meshCenters[meshId].Z.Abs()).RawValue();
        candidates[candidateCount++] = Candidate{meshId, faces, verts, score};
    }

    if (candidateCount == 0u) return false;

    uint16_t bestFaces = 0u;
    uint16_t bestVerts = 0u;
    size_t bestCount = 0u;
    for (size_t i = 0; i < candidateCount; ++i)
    {
        const uint16_t faces = candidates[i].faceCount;
        const uint16_t verts = candidates[i].vertexCount;
        size_t sameCount = 0u;
        for (size_t j = 0; j < candidateCount; ++j)
        {
            if (candidates[j].faceCount == faces &&
                candidates[j].vertexCount == verts)
            {
                ++sameCount;
            }
        }
        if (sameCount > bestCount ||
            (sameCount == bestCount && sameCount >= 4u && faces < bestFaces))
        {
            bestCount = sameCount;
            bestFaces = faces;
            bestVerts = verts;
        }
    }

    std::array<Candidate, 64> filtered{};
    size_t filteredCount = 0u;
    if (bestCount >= 4u)
    {
        for (size_t i = 0; i < candidateCount; ++i)
        {
            const Candidate& c = candidates[i];
            if (c.faceCount == bestFaces && c.vertexCount == bestVerts)
            {
                filtered[filteredCount++] = c;
            }
        }
    }
    else
    {
        for (size_t i = 0; i < candidateCount; ++i)
        {
            filtered[filteredCount++] = candidates[i];
        }
    }

    std::sort(filtered.begin(), filtered.begin() + static_cast<ptrdiff_t>(filteredCount),
              [](const Candidate& a, const Candidate& b)
    {
        return a.radiusScore > b.radiusScore;
    });

    if (filteredCount < 4u) return false;
    for (size_t i = 0; i < 4u; ++i)
    {
        outIds[i] = filtered[i].meshId;
    }
    outCount = 4u;
    return true;
}

void CarWheelRig::ClassifyWheels(const std::array<size_t, 4>& meshIds,
                                 const SRL::Math::Types::Vector3D* meshCenters)
{
    if (!meshCenters) return;
    std::array<size_t, 4> localIds{
        meshIds[0], meshIds[1], meshIds[2], meshIds[3]
    };
    std::array<size_t, 4> order{0u, 1u, 2u, 3u};

    std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
    {
        return meshCenters[localIds[a]].Z.RawValue() < meshCenters[localIds[b]].Z.RawValue();
    });

    const size_t f0 = order[0];
    const size_t f1 = order[1];
    const size_t r0 = order[2];
    const size_t r1 = order[3];
    const bool f0Left = meshCenters[localIds[f0]].X.RawValue() <
                        meshCenters[localIds[f1]].X.RawValue();
    const bool r0Left = meshCenters[localIds[r0]].X.RawValue() <
                        meshCenters[localIds[r1]].X.RawValue();

    const size_t fl = f0Left ? f0 : f1;
    const size_t fr = f0Left ? f1 : f0;
    const size_t rl = r0Left ? r0 : r1;
    const size_t rr = r0Left ? r1 : r0;

    const std::array<size_t, 4> mapped{fl, fr, rl, rr};
    for (size_t i = 0; i < 4u; ++i)
    {
        const size_t srcIdx = mapped[i];
        const size_t meshId = localIds[srcIdx];
        wheelMeshIds_[i] = meshId;
        wheelSlots_[i].meshId = meshId;
        wheelSlots_[i].center = meshCenters[meshId];
        wheelSlots_[i].front = (i < 2u);
    }
}

void CarWheelRig::ResetState()
{
    wheelMeshIds_ = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
    wheelSlots_ = {};
    wheelCount_ = 0u;
    steerDegX16_ = 0;
    bodyPitchDegX16_ = 0;
    bodyRollDegX16_ = 0;
    wheelSpinDegX16_ = {0, 0, 0, 0};
    wheelSuspensionOffsetX16_ = {0, 0, 0, 0};
}

int32_t CarWheelRig::ClampInt(int32_t value, int32_t minValue, int32_t maxValue)
{
    return std::max<int32_t>(minValue, std::min<int32_t>(value, maxValue));
}

int32_t CarWheelRig::StepToward(int32_t current, int32_t target, int32_t shift)
{
    if (shift <= 0) return target;
    const int32_t delta = target - current;
    if (delta == 0) return current;
    int32_t step = (delta >> shift);
    if (step == 0)
    {
        step = (delta > 0) ? 1 : -1;
    }
    return current + step;
}
