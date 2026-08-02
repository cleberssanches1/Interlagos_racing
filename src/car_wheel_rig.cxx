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

bool TryComputeWheelSizeScore(ModelObject& model,
                              bool isSmoothMesh,
                              size_t meshId,
                              int64_t& outSizeScore)
{
    outSizeScore = 0;
    int32_t minX = 0;
    int32_t minY = 0;
    int32_t minZ = 0;
    int32_t maxX = 0;
    int32_t maxY = 0;
    int32_t maxZ = 0;
    bool first = true;

    auto accumulateVerts = [&](const SRL::Math::Types::Vector3D* verts, size_t count) -> bool
    {
        if (!verts || count == 0u) return false;
        for (size_t i = 0; i < count; ++i)
        {
            const int32_t x = verts[i].X.RawValue();
            const int32_t y = verts[i].Y.RawValue();
            const int32_t z = verts[i].Z.RawValue();
            if (first)
            {
                minX = maxX = x;
                minY = maxY = y;
                minZ = maxZ = z;
                first = false;
                continue;
            }
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
            if (z < minZ) minZ = z;
            if (z > maxZ) maxZ = z;
        }
        return true;
    };

    if (isSmoothMesh)
    {
        auto* mesh = model.GetMesh<SRL::Types::SmoothMesh>(meshId);
        if (!mesh || mesh->VertexCount == 0u || !mesh->Vertices) return false;
        if (!accumulateVerts(mesh->Vertices, mesh->VertexCount)) return false;
    }
    else
    {
        auto* mesh = model.GetMesh<SRL::Types::Mesh>(meshId);
        if (!mesh || mesh->VertexCount == 0u || !mesh->Vertices) return false;
        if (!accumulateVerts(mesh->Vertices, mesh->VertexCount)) return false;
    }

    if (first) return false;

    const int64_t dx = static_cast<int64_t>(maxX) - static_cast<int64_t>(minX);
    const int64_t dy = static_cast<int64_t>(maxY) - static_cast<int64_t>(minY);
    const int64_t dz = static_cast<int64_t>(maxZ) - static_cast<int64_t>(minZ);
    // Diagonal^2 proxy: larger wheel mesh -> larger score.
    outSizeScore = (dx * dx) + (dy * dy) + (dz * dz);
    return true;
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
    const bool wheelIdsFromMeshtex =
        DetectWheelIdsFromMeshtex(meshCount, wheelIds, wheelIdCount);
    if (!wheelIdsFromMeshtex)
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

    ClassifyWheels(wheelIds, model, isSmoothMesh, meshCenters, wheelIdsFromMeshtex);
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

    // --- World 3D attitude from wheel contact altitudes ---------------------------
    // For each axle/side, MapHeight gives asphalt Y under the wheel (Y-down:
    // larger Y = lower altitude). Rigid body: the LOW asphalt corner must go
    // DOWN in the world. Mesh is drawn with RotateX(180)+RotateZ(180), so
    // visual pitch/roll signs are inverted via kBodyPitchSign / kBodyRollSign.
    //
    //   pitch ≈ sign * atan((Yfront − Yrear) / wheelbase_from_mesh)
    //   roll  ≈ sign * atan((Yright − Yleft) / track_from_mesh)
    //
    // Wheel centers (mesh) define L and T after ClassifyWheels.
    int32_t targetPitch = heldPitchDegX16_;
    int32_t targetRoadRoll = heldRollDegX16_;
    int32_t targetFrontSusp = 0;
    int32_t targetRearSusp = 0;
    int32_t targetLeftSusp = 0;
    int32_t targetRightSusp = 0;
    const bool rearValid = (input.groundMask & 0x1u) != 0u;
    const bool leftValid = (input.groundMask & 0x2u) != 0u;
    const bool frontValid = (input.groundMask & 0x4u) != 0u;
    const bool rightValid = (input.groundMask & 0x8u) != 0u;

    auto resolveYRaw = [](int32_t raw, int16_t asInt) -> int32_t
    {
        if (raw != 0 || asInt == 0) return raw;
        return static_cast<int32_t>(asInt) << 16;
    };

    auto filterDelta = [](int32_t sample,
                          int32_t& filtered,
                          bool& init,
                          int32_t maxJump,
                          int32_t filterShift) -> int32_t
    {
        if (!init)
        {
            filtered = sample;
            init = true;
            return filtered;
        }
        int32_t s = sample;
        const int32_t jump = s - filtered;
        if (jump > maxJump) s = filtered + maxJump;
        else if (jump < -maxJump) s = filtered - maxJump;
        filtered += (s - filtered) >> filterShift;
        return filtered;
    };

    const int32_t wb = (wheelbaseRaw_ > 0) ? wheelbaseRaw_ : kDefaultWheelbaseRaw;
    const int32_t tr = (trackRaw_ > 0) ? trackRaw_ : kDefaultTrackRaw;

    bool havePitchSample = false;
    bool haveRollSample = false;

    if (frontValid && rearValid)
    {
        const int32_t frontYRaw = resolveYRaw(input.groundFrontYRaw, input.groundFrontY);
        const int32_t rearYRaw = resolveYRaw(input.groundRearYRaw, input.groundRearY);
        // ΔY > 0 ⇒ front asphalt lower altitude ⇒ nose must go down in world.
        int32_t pitchDelta = filterDelta(frontYRaw - rearYRaw,
                                         filteredDeltaYRaw_,
                                         deltaFilterInit_,
                                         kMaxDeltaJumpRaw,
                                         kDeltaFilterShift);
        if (std::abs(pitchDelta) < kPitchDeadzoneRaw)
        {
            pitchDelta = 0;
        }
        havePitchSample = true;
        int64_t pitchDegX16 =
            (static_cast<int64_t>(pitchDelta) * 65536 / wb) * kRadToDegApprox;
        pitchDegX16 *= kBodyPitchSign;
        if (pitchDegX16 > kMaxPitchDegX16) pitchDegX16 = kMaxPitchDegX16;
        if (pitchDegX16 < -kMaxPitchDegX16) pitchDegX16 = -kMaxPitchDegX16;
        targetPitch = static_cast<int32_t>(pitchDegX16);
        heldPitchDegX16_ = targetPitch;
        // Suspension: lower asphalt corner gets more +Y offset after X180 mesh.
        // Match visual to body pitch sign so wheels follow the plane.
        const int32_t susp = ClampInt((pitchDelta * kBodyPitchSign) >> 5,
                                      -kMaxSuspensionOffsetX16,
                                      kMaxSuspensionOffsetX16);
        targetFrontSusp = susp;
        targetRearSusp = -susp;
    }

    if (leftValid && rightValid)
    {
        const int32_t leftYRaw = resolveYRaw(input.groundLeftYRaw, input.groundLeftY);
        const int32_t rightYRaw = resolveYRaw(input.groundRightYRaw, input.groundRightY);
        // ΔY > 0 ⇒ right asphalt lower ⇒ right corner must go down in world.
        int32_t rollDelta = filterDelta(rightYRaw - leftYRaw,
                                        filteredRollDeltaYRaw_,
                                        rollDeltaFilterInit_,
                                        kMaxDeltaJumpRaw,
                                        kDeltaFilterShift);
        if (std::abs(rollDelta) < kRollDeadzoneRaw)
        {
            rollDelta = 0;
        }
        haveRollSample = true;
        int64_t rollDegX16 =
            (static_cast<int64_t>(rollDelta) * 65536 / tr) * kRadToDegApprox;
        rollDegX16 *= kBodyRollSign;
        if (rollDegX16 > kMaxRollDegX16) rollDegX16 = kMaxRollDegX16;
        if (rollDegX16 < -kMaxRollDegX16) rollDegX16 = -kMaxRollDegX16;
        targetRoadRoll = static_cast<int32_t>(rollDegX16);
        heldRollDegX16_ = targetRoadRoll;
        const int32_t susp = ClampInt((rollDelta * kBodyRollSign) >> 5,
                                      -kMaxSuspensionOffsetX16,
                                      kMaxSuspensionOffsetX16);
        targetRightSusp = susp;
        targetLeftSusp = -susp;
    }

    // Hold last good plane when parked; rate-limit pitch ASYMMETRICALLY:
    // nose-down (decline) reacts faster; nose-up (climb) is slower (anti-empino).
    {
        const int32_t previousPitch = bodyPitchDegX16_;
        bodyPitchDegX16_ = StepToward(bodyPitchDegX16_, targetPitch, kPitchFilterShift);
        int32_t pitchStep = bodyPitchDegX16_ - previousPitch;
        // With kBodyPitchSign=+1: positive pitch = nose down (after model orientation).
        // Cap steps by direction in pitch-angle space.
        if (pitchStep > kMaxPitchStepDownDegX16)
            bodyPitchDegX16_ = previousPitch + kMaxPitchStepDownDegX16;
        else if (pitchStep < -kMaxPitchStepUpDegX16)
            bodyPitchDegX16_ = previousPitch - kMaxPitchStepUpDegX16;
    }

    {
        const int32_t speedNorm256 = std::min<int32_t>(256, (clampedSpeed * 256) / 200);
        const int32_t steerLean =
            ClampInt((steerDegX16_ * speedNorm256) / 3072, -(2 << 16), (2 << 16));
        // Steer lean is visual only; road plane roll dominates.
        const int32_t targetRoll = ClampInt(targetRoadRoll + (haveRollSample ? (steerLean >> 3) : 0),
                                            -kMaxRollDegX16,
                                            kMaxRollDegX16);
        const int32_t previousRoll = bodyRollDegX16_;
        bodyRollDegX16_ = StepToward(bodyRollDegX16_, targetRoll, kRollFilterShift);
        const int32_t rollStep = bodyRollDegX16_ - previousRoll;
        if (rollStep > kMaxRollStepDegX16)
            bodyRollDegX16_ = previousRoll + kMaxRollStepDegX16;
        else if (rollStep < -kMaxRollStepDegX16)
            bodyRollDegX16_ = previousRoll - kMaxRollStepDegX16;
    }

    // Each wheel corner: lower asphalt → that corner of the body rotates down;
    // residual travel keeps the tire mesh near the face.
    for (size_t i = 0; i < 4u; ++i)
    {
        int32_t targetSusp = wheelSlots_[i].front ? targetFrontSusp : targetRearSusp;
        targetSusp += wheelSlots_[i].left ? targetLeftSusp : targetRightSusp;
        targetSusp = ClampInt(targetSusp, -kMaxSuspensionOffsetX16, kMaxSuspensionOffsetX16);
        wheelSuspensionOffsetX16_[i] =
            StepToward(wheelSuspensionOffsetX16_[i], targetSusp, kSuspFilterShift);
    }
    (void)havePitchSample;
    (void)haveRollSample;
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
                                 ModelObject& model,
                                 bool isSmoothMesh,
                                 const SRL::Math::Types::Vector3D* meshCenters,
                                 bool preferMeshtexOrderFrontAxle)
{
    if (!meshCenters) return;
    std::array<size_t, 4> localIds{
        meshIds[0], meshIds[1], meshIds[2], meshIds[3]
    };

    std::array<int64_t, 4> sizeScore{0, 0, 0, 0};
    std::array<size_t, 4> sizeOrder{0u, 1u, 2u, 3u};
    bool hasSizeScores = true;
    for (size_t i = 0; i < 4u; ++i)
    {
        if (!TryComputeWheelSizeScore(model, isSmoothMesh, localIds[i], sizeScore[i]))
        {
            hasSizeScores = false;
            break;
        }
    }
    if (hasSizeScores)
    {
        std::sort(sizeOrder.begin(), sizeOrder.end(), [&](size_t a, size_t b)
        {
            if (sizeScore[a] != sizeScore[b]) return sizeScore[a] < sizeScore[b];
            return localIds[a] < localIds[b];
        });

        const size_t f0 = sizeOrder[0];
        const size_t f1 = sizeOrder[1];
        const size_t r0 = sizeOrder[2];
        const size_t r1 = sizeOrder[3];
        const bool f0Left =
            meshCenters[localIds[f0]].X.RawValue() <= meshCenters[localIds[f1]].X.RawValue();
        const bool r0Left =
            meshCenters[localIds[r0]].X.RawValue() <= meshCenters[localIds[r1]].X.RawValue();

        const size_t fl = f0Left ? f0 : f1;
        const size_t fr = f0Left ? f1 : f0;
        const size_t rl = r0Left ? r0 : r1;
        const size_t rr = r0Left ? r1 : r0;
        const std::array<size_t, 4> mapped{fl, fr, rl, rr};

        SRL::Debug::Print(1, 24, "WHL size f:%u/%u r:%u/%u",
                          static_cast<unsigned>(localIds[fl]),
                          static_cast<unsigned>(localIds[fr]),
                          static_cast<unsigned>(localIds[rl]),
                          static_cast<unsigned>(localIds[rr]));

        for (size_t i = 0; i < 4u; ++i)
        {
            const size_t srcIdx = mapped[i];
            const size_t meshId = localIds[srcIdx];
            wheelMeshIds_[i] = meshId;
            wheelSlots_[i].meshId = meshId;
            wheelSlots_[i].center = meshCenters[meshId];
            wheelSlots_[i].front = (i < 2u);
            wheelSlots_[i].left = (i == 0u || i == 2u);
        }
        RefreshWheelGeometryFromCenters();
        return;
    }

    // Meshtex exports preserve wheel object order for this car:
    // roda1/roda2 = axle dianteiro, roda3/roda4 = traseiro.
    // When available, this avoids front/rear swaps from noisy geometric heuristics.
    if (preferMeshtexOrderFrontAxle)
    {
        const bool frontLeftFirst =
            meshCenters[localIds[0]].X.RawValue() <= meshCenters[localIds[1]].X.RawValue();
        const bool rearLeftFirst =
            meshCenters[localIds[2]].X.RawValue() <= meshCenters[localIds[3]].X.RawValue();

        const size_t fl = frontLeftFirst ? 0u : 1u;
        const size_t fr = frontLeftFirst ? 1u : 0u;
        const size_t rl = rearLeftFirst ? 2u : 3u;
        const size_t rr = rearLeftFirst ? 3u : 2u;

        const int64_t carCenterXRaw =
            (static_cast<int64_t>(meshCenters[localIds[fl]].X.RawValue()) +
             static_cast<int64_t>(meshCenters[localIds[fr]].X.RawValue()) +
             static_cast<int64_t>(meshCenters[localIds[rl]].X.RawValue()) +
             static_cast<int64_t>(meshCenters[localIds[rr]].X.RawValue())) / 4;
        const int64_t carCenterZRaw =
            (static_cast<int64_t>(meshCenters[localIds[fl]].Z.RawValue()) +
             static_cast<int64_t>(meshCenters[localIds[fr]].Z.RawValue()) +
             static_cast<int64_t>(meshCenters[localIds[rl]].Z.RawValue()) +
             static_cast<int64_t>(meshCenters[localIds[rr]].Z.RawValue())) / 4;
        const auto axleScore = [&](size_t a, size_t b) -> int64_t
        {
            const int64_t midX =
                (static_cast<int64_t>(meshCenters[localIds[a]].X.RawValue()) +
                 static_cast<int64_t>(meshCenters[localIds[b]].X.RawValue())) / 2;
            const int64_t midZ =
                (static_cast<int64_t>(meshCenters[localIds[a]].Z.RawValue()) +
                 static_cast<int64_t>(meshCenters[localIds[b]].Z.RawValue())) / 2;
            const int64_t dx = midX - carCenterXRaw;
            const int64_t dz = midZ - carCenterZRaw;
            return (dx * dx) + (dz * dz);
        };
        const int64_t scorePairA = axleScore(fl, fr);
        const int64_t scorePairB = axleScore(rl, rr);
        const bool pairAIsFront = scorePairA <= scorePairB;
        const std::array<size_t, 4> mapped =
            pairAIsFront
                ? std::array<size_t, 4>{fl, fr, rl, rr}
                : std::array<size_t, 4>{rl, rr, fl, fr};
        SRL::Debug::Print(1, 24, "WHL near pair:%c A:%ld B:%ld",
                          pairAIsFront ? 'A' : 'B',
                          static_cast<long>(scorePairA >> 16),
                          static_cast<long>(scorePairB >> 16));

        for (size_t i = 0; i < 4u; ++i)
        {
            const size_t srcIdx = mapped[i];
            const size_t meshId = localIds[srcIdx];
            wheelMeshIds_[i] = meshId;
            wheelSlots_[i].meshId = meshId;
            wheelSlots_[i].center = meshCenters[meshId];
            wheelSlots_[i].front = (i < 2u);
            wheelSlots_[i].left = (i == 0u || i == 2u);
        }
        RefreshWheelGeometryFromCenters();
        return;
    }

    std::array<size_t, 4> order{0u, 1u, 2u, 3u};
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
    {
        return meshCenters[localIds[a]].Z.RawValue() < meshCenters[localIds[b]].Z.RawValue();
    });

    const size_t f0 = order[0];
    const size_t f1 = order[1];
    const size_t r0 = order[2];
    const size_t r1 = order[3];
    const bool f0Left =
        meshCenters[localIds[f0]].X.RawValue() <= meshCenters[localIds[f1]].X.RawValue();
    const bool r0Left =
        meshCenters[localIds[r0]].X.RawValue() <= meshCenters[localIds[r1]].X.RawValue();

    const size_t fl = f0Left ? f0 : f1;
    const size_t fr = f0Left ? f1 : f0;
    const size_t rl = r0Left ? r0 : r1;
    const size_t rr = r0Left ? r1 : r0;

    const int64_t carCenterXRaw =
        (static_cast<int64_t>(meshCenters[localIds[fl]].X.RawValue()) +
         static_cast<int64_t>(meshCenters[localIds[fr]].X.RawValue()) +
         static_cast<int64_t>(meshCenters[localIds[rl]].X.RawValue()) +
         static_cast<int64_t>(meshCenters[localIds[rr]].X.RawValue())) / 4;
    const int64_t carCenterZRaw =
        (static_cast<int64_t>(meshCenters[localIds[fl]].Z.RawValue()) +
         static_cast<int64_t>(meshCenters[localIds[fr]].Z.RawValue()) +
         static_cast<int64_t>(meshCenters[localIds[rl]].Z.RawValue()) +
         static_cast<int64_t>(meshCenters[localIds[rr]].Z.RawValue())) / 4;
    const auto axleScore = [&](size_t a, size_t b) -> int64_t
    {
        const int64_t midX =
            (static_cast<int64_t>(meshCenters[localIds[a]].X.RawValue()) +
             static_cast<int64_t>(meshCenters[localIds[b]].X.RawValue())) / 2;
        const int64_t midZ =
            (static_cast<int64_t>(meshCenters[localIds[a]].Z.RawValue()) +
             static_cast<int64_t>(meshCenters[localIds[b]].Z.RawValue())) / 2;
        const int64_t dx = midX - carCenterXRaw;
        const int64_t dz = midZ - carCenterZRaw;
        return (dx * dx) + (dz * dz);
    };
    const int64_t scorePairA = axleScore(fl, fr);
    const int64_t scorePairB = axleScore(rl, rr);
    const bool pairAIsFront = scorePairA <= scorePairB;
    const std::array<size_t, 4> mapped =
        pairAIsFront
            ? std::array<size_t, 4>{fl, fr, rl, rr}
            : std::array<size_t, 4>{rl, rr, fl, fr};
    SRL::Debug::Print(1, 24, "WHL near pair:%c A:%ld B:%ld",
                      pairAIsFront ? 'A' : 'B',
                      static_cast<long>(scorePairA >> 16),
                      static_cast<long>(scorePairB >> 16));

    for (size_t i = 0; i < 4u; ++i)
    {
        const size_t srcIdx = mapped[i];
        const size_t meshId = localIds[srcIdx];
        wheelMeshIds_[i] = meshId;
        wheelSlots_[i].meshId = meshId;
        wheelSlots_[i].center = meshCenters[meshId];
        wheelSlots_[i].front = (i < 2u);
        wheelSlots_[i].left = (i == 0u || i == 2u);
    }
    RefreshWheelGeometryFromCenters();
}

void CarWheelRig::RefreshWheelGeometryFromCenters()
{
    // Measure real wheelbase / track from mesh centers (model space).
    // Order is always [FL, FR, RL, RR] after ClassifyWheels.
    int64_t frontZ = 0;
    int64_t rearZ = 0;
    int64_t leftX = 0;
    int64_t rightX = 0;
    int nF = 0, nR = 0, nL = 0, nRt = 0;
    for (size_t i = 0; i < 4u; ++i)
    {
        if (wheelSlots_[i].meshId == SIZE_MAX) continue;
        const int64_t x = wheelSlots_[i].center.X.RawValue();
        const int64_t z = wheelSlots_[i].center.Z.RawValue();
        if (wheelSlots_[i].front) { frontZ += z; ++nF; }
        else { rearZ += z; ++nR; }
        if (wheelSlots_[i].left) { leftX += x; ++nL; }
        else { rightX += x; ++nRt; }
    }
    if (nF > 0 && nR > 0)
    {
        frontZ /= nF;
        rearZ /= nR;
        int64_t wb = frontZ - rearZ;
        if (wb < 0) wb = -wb;
        if (wb > (1 << 14)) // > 0.25
        {
            wheelbaseRaw_ = static_cast<int32_t>(std::min<int64_t>(wb, 8ll << 16));
        }
    }
    if (nL > 0 && nRt > 0)
    {
        leftX /= nL;
        rightX /= nRt;
        int64_t tr = rightX - leftX;
        if (tr < 0) tr = -tr;
        if (tr > (1 << 14))
        {
            trackRaw_ = static_cast<int32_t>(std::min<int64_t>(tr, 6ll << 16));
        }
    }
    SRL::Debug::Print(1, 25, "WHL geo L:%d T:%d",
                      static_cast<int>(wheelbaseRaw_ >> 16),
                      static_cast<int>(trackRaw_ >> 16));
}

void CarWheelRig::ResetState()
{
    wheelMeshIds_ = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
    wheelSlots_ = {};
    wheelCount_ = 0u;
    steerDegX16_ = 0;
    bodyPitchDegX16_ = 0;
    bodyRollDegX16_ = 0;
    heldPitchDegX16_ = 0;
    heldRollDegX16_ = 0;
    filteredDeltaYRaw_ = 0;
    filteredRollDeltaYRaw_ = 0;
    deltaFilterInit_ = false;
    rollDeltaFilterInit_ = false;
    wheelbaseRaw_ = kDefaultWheelbaseRaw;
    trackRaw_ = kDefaultTrackRaw;
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
