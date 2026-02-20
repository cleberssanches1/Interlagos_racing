#include "track_system.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string.h>
#include <string>

#include "modelObject.hpp"
#include "resource_loader.hpp"

using SRL::Math::Types::Vector3D;

SRL::Math::Types::Vector3D TrackSystem::ComputeRendererCenter(const TrackRenderer& renderer)
{
    return renderer.StartMeshCenter() + renderer.Offset();
}

const char* TrackSystem::FindExistingPath(const char* const* paths, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        SRL::Cd::File f(paths[i]);
        const bool exists = f.Exists() && f.Size.Bytes > 0;
        ::strncpy(lastSegmentPath_, paths[i], sizeof(lastSegmentPath_));
        lastSegmentPath_[sizeof(lastSegmentPath_) - 1] = '\0';
        SRL::Debug::Print(1, 6, "Check cd path: %s -> %d", paths[i], exists ? 1 : 0);
        if (exists) return paths[i];
    }
    return nullptr;
}

const char* TrackSystem::ResolveSegmentPath(size_t id)
{
    constexpr size_t variantCount = kSegmentPathTemplates_.size();
    std::array<std::array<char, 64>, variantCount> buffers{};
    const char* candidates[variantCount]{};

    for (size_t i = 0; i < variantCount; ++i)
    {
        std::snprintf(buffers[i].data(), buffers[i].size(), kSegmentPathTemplates_[i], unsigned(id));
        candidates[i] = buffers[i].data();
    }
    return FindExistingPath(candidates, variantCount);
}

std::vector<TrackSystem::TrackSegmentEntry> TrackSystem::CopyAllTrackSegments(size_t maxSegments)
{
    std::vector<TrackSegmentEntry> segments;
    const size_t loadLimit = (maxSegments == 0) ? kTrackSegmentLimit : std::min(maxSegments, kTrackSegmentLimit);
    segments.reserve(loadLimit);
    for (size_t i = 1; i <= loadLimit; ++i)
    {
        const char* existingPath = ResolveSegmentPath(i);
        if (!existingPath)
        {
            SRL::Debug::Print(1, 12, "Segment %03u path missing (%u variants)", unsigned(i), unsigned(kSegmentPathTemplates_.size()));
            break;
        }
        TrackSegmentCopy copy = CopyTrackSegmentToCart(existingPath);
        segments.push_back({ static_cast<int>(i), copy });
        if (copy.cartPtr)
        {
            SRL::Debug::Print(1, 11, "Segment %03u copied (%u bytes)", unsigned(i), unsigned(copy.size));
        }
        else
        {
            SRL::Debug::Print(1, 12, "Segment %03u failed to copy (missing?)", unsigned(i));
            break;
        }
    }

    size_t valid = 0;
    for (const auto& segment : segments)
    {
        if (segment.copy.cartPtr && segment.copy.size > 0) ++valid;
    }
    SRL::Debug::Print(1, 13, "Track segments copied %u/%u", unsigned(valid), unsigned(segments.size()));
    return segments;
}

std::vector<TrackSystem::SegmentRenderEntry> TrackSystem::BuildSegmentRenderers(std::vector<TrackSegmentEntry>& entries)
{
    std::vector<SegmentRenderEntry> renderers;
    renderers.reserve(entries.size());
    for (auto& entry : entries)
    {
        if (!entry.copy.cartPtr || entry.copy.size == 0) continue;

        auto model = std::make_unique<ModelObject>();
        const int32_t cartFreeBefore = SRL::Memory::CartRam::GetFreeSpace();
        SRL::Debug::Print(1, 14, "Segment load begin %03d bytes:%u cartFree:%d",
                          entry.id, (unsigned)entry.copy.size, cartFreeBefore);

        if (!model->LoadFromMemory(entry.copy.cartPtr, entry.copy.size, 0, false, 0, false, true))
        {
            const int32_t cartFreeAfter = SRL::Memory::CartRam::GetFreeSpace();
            SRL::Debug::Print(1, 14, "Segment load fail %03d cartFreeNow:%d", entry.id, cartFreeAfter);
            SRL::Memory::CartRam::Free(entry.copy.cartPtr);
            entry.copy.cartPtr = nullptr;
            continue;
        }
        auto renderer = std::make_unique<TrackRenderer>();
        ModelObject* rawModel = model.release();

        if (!renderer->InitializeFromModelObject(rawModel, 0))
        {
            SRL::Debug::Print(1, 15, "Renderer init fail %03d", entry.id);
            delete rawModel;
            SRL::Memory::CartRam::Free(entry.copy.cartPtr);
            entry.copy.cartPtr = nullptr;
            continue;
        }

        // Keep original model path for stable segment placement.
        renderer->SetUseOriginal(true);
        // Keep original face visibility from model to avoid front/back overdraw artifacts.
        renderer->SetForceDoubleSided(false);
        renderer->SetDrawLimit(renderer->MeshCount());
        SegmentRenderEntry item{};
        item.id = entry.id;
        item.center = ComputeRendererCenter(*renderer);
        item.renderer = std::move(renderer);
        renderers.push_back(std::move(item));

        SRL::Memory::CartRam::Free(entry.copy.cartPtr);
        entry.copy.cartPtr = nullptr;
    }
    return renderers;
}

std::vector<TrackSystem::SegmentHandle> TrackSystem::BuildSegmentHandleTable()
{
    segmentPool_.Reset();
    std::vector<SegmentHandle> handles;
    handles.reserve(segmentRenderers_.size());
    for (auto& entry : segmentRenderers_)
    {
        handles.push_back(segmentPool_.Add(&entry));
    }
    return handles;
}

bool TrackSystem::Initialize(const Config& config)
{
    ready_ = false;
    segmentsReady_ = false;
    segmentEntries_.clear();
    segmentRenderers_.clear();
    segmentHandles_.clear();
    soakMonitor_.Reset();

    const size_t loadLimit =
        (config.initialSegments == 0) ? kTrackSegmentLimit : std::min<size_t>(config.initialSegments, kTrackSegmentLimit);
    segmentEntries_.reserve(loadLimit);
    segmentRenderers_.reserve(loadLimit);

    size_t copiedCount = 0;
    for (size_t i = 1; i <= loadLimit; ++i)
    {
        const char* existingPath = ResolveSegmentPath(i);
        if (!existingPath)
        {
            SRL::Debug::Print(1, 12, "Segment %03u path missing (%u variants)", unsigned(i), unsigned(kSegmentPathTemplates_.size()));
            break;
        }

        TrackSegmentCopy copy = CopyTrackSegmentToCart(existingPath);
        if (!copy.cartPtr || copy.size == 0)
        {
            SRL::Debug::Print(1, 12, "Segment %03u failed to copy (missing?)", unsigned(i));
            break;
        }

        ++copiedCount;
        SRL::Debug::Print(1, 11, "Segment %03u copied (%u bytes)", unsigned(i), unsigned(copy.size));

        std::vector<TrackSegmentEntry> singleEntry;
        singleEntry.reserve(1);
        singleEntry.push_back({ static_cast<int>(i), copy });

        auto built = BuildSegmentRenderers(singleEntry);
        if (!built.empty())
        {
            segmentRenderers_.push_back(std::move(built[0]));
        }

        // Keep only the segment registry id; copy buffer is consumed/freed by BuildSegmentRenderers.
        segmentEntries_.push_back({ static_cast<int>(i), {} });
    }
    SRL::Debug::Print(1, 13, "Track segments copied %u/%u", unsigned(copiedCount), unsigned(segmentEntries_.size()));
    SRL::Debug::Print(1, 26, "Track segment registry entries:%lu", (unsigned long)segmentEntries_.size());
    segmentsReady_ = !segmentRenderers_.empty();
    segmentHandles_ = BuildSegmentHandleTable();

    producer_.SetUseSlave(config.useSlave);
    producer_.SetMaxFramesInFlight(2);
    producer_.SetRecoveryFrames(120);
    producer_.SetSafeModeStallThreshold(4);
    producer_.SetSafeModeCooldownFrames(90);

    TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::Config coordinatorConfig{};
    coordinatorConfig.budget.maxTrackSegments = std::min<uint32_t>(config.initialSegments, static_cast<uint32_t>(kTrackSegmentLimit));
    coordinatorConfig.budget.maxTrackMeshes = config.initialMeshes;
    coordinatorConfig.budget.maxTrackFaces = config.initialFaces;
    coordinatorConfig.chunkCapacity = kTrackSegmentLimit;

    const bool coordinatorReady = coordinator_.Initialize(coordinatorConfig);
    if (!coordinatorReady)
    {
        SRL::Debug::Print(1, 31, "TrackRenderCoordinator HWR alloc failed");
    }

    AdaptiveTrackBudgetController::Limits adaptiveBudgetLimits{};
    const uint32_t minSegmentsRequested = std::max<uint32_t>(1, config.minSegments);
    const uint32_t maxSegmentsCap = static_cast<uint32_t>(kTrackSegmentLimit);
    adaptiveBudgetLimits.minSegments = std::min<uint32_t>(minSegmentsRequested, maxSegmentsCap);
    adaptiveBudgetLimits.maxSegments = adaptiveBudgetLimits.minSegments;
    // Lock mesh/face budget to configured startup values to avoid runtime shrink.
    adaptiveBudgetLimits.minMeshes = std::max<uint32_t>(1u, config.initialMeshes);
    adaptiveBudgetLimits.maxMeshes = adaptiveBudgetLimits.minMeshes;
    adaptiveBudgetLimits.minFaces = std::max<uint32_t>(1000u, config.initialFaces);
    adaptiveBudgetLimits.maxFaces = adaptiveBudgetLimits.minFaces;
    budgetController_ = AdaptiveTrackBudgetController(adaptiveBudgetLimits);

    if (!segmentsReady_)
    {
        SRL::Debug::Print(1, 28, "Track rendering skipped: segments missing");
        if (lastSegmentPath_[0] != '\0')
        {
            SRL::Debug::Print(1, 29, "Last segment path tested: %s", lastSegmentPath_);
        }
    }

    if (!segmentRenderers_.empty())
    {
        std::string ids;
        const size_t count = std::min(segmentRenderers_.size(), size_t(3));
        for (size_t i = 0; i < count; ++i)
        {
            ids += std::to_string(segmentRenderers_[i].id);
            if (i + 1 < count) ids += ",";
        }
        SRL::Debug::Print(1, 27, "Nearest segment candidates (%lu): %s", (unsigned long)count, ids.c_str());
    }

    ready_ = coordinatorReady && segmentsReady_;
    return ready_;
}

void TrackSystem::BeginFrame(uint32_t frameId)
{
    coordinator_.BeginFrame(frameId);
}

void TrackSystem::RenderFrame(bool renderTrack,
                              const Vector3D& trackOffset,
                              const Vector3D& lightDirection,
                              const Vector3D& cameraLocation)
{
    if (!renderTrack || !ready_)
    {
        return;
    }

    (void)cameraLocation;
    bool segment01Logged = false;
    bool segment01Prepared = false;
    std::vector<SegmentHandle> orderedHandles = segmentHandles_;
    std::sort(orderedHandles.begin(), orderedHandles.end(),
        [&](const SegmentHandle& a, const SegmentHandle& b)
        {
            const auto* ea = segmentPool_.Resolve(a);
            const auto* eb = segmentPool_.Resolve(b);
            if (!ea && !eb) return false;
            if (!ea) return false;
            if (!eb) return true;
            // Render fixed order from last to first for first 4 segments: 4,3,2,1.
            return ea->id > eb->id;
        });
    orderedHandles.erase(
        std::remove_if(orderedHandles.begin(), orderedHandles.end(),
            [&](const SegmentHandle& h)
            {
                const auto* e = segmentPool_.Resolve(h);
                if (!e) return true;
                return e->id < 1 || e->id > 4;
            }),
        orderedHandles.end());
    if (!orderedHandles.empty())
    {
        const size_t keepCount =
            std::min<size_t>(static_cast<size_t>(coordinator_.Budget().maxTrackSegments), orderedHandles.size());
        orderedHandles.resize(keepCount);
    }

    std::array<uint8_t, kTrackSegmentLimit + 1> preparedCountById{};
    std::array<uint8_t, kTrackSegmentLimit + 1> renderedCountById{};

    coordinator_.Prepare(
        orderedHandles,
        coordinator_.Budget().maxTrackSegments,
        [&](const SegmentHandle& handle) -> SegmentRenderEntry*
        {
            return segmentPool_.Resolve(handle);
        },
        [&](SegmentRenderEntry& entry) -> TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult
        {
            TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult estimate{};
            auto* renderer = entry.renderer.get();
            if (!renderer)
            {
                return estimate;
            }
            estimate.rendered = true;
            estimate.meshes = static_cast<uint32_t>(renderer->DrawLimit());
            estimate.faces = renderer->FaceCount();
            return estimate;
        },
        [&](SegmentRenderEntry& entry, TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::PreparedChunk& chunk)
        {
            chunk.segmentId = entry.id;
            chunk.center = entry.center;
            if (entry.id > 0 && entry.id <= static_cast<int>(kTrackSegmentLimit))
            {
                const size_t idx = static_cast<size_t>(entry.id);
                if (preparedCountById[idx] < 255) ++preparedCountById[idx];
            }
            if (entry.id == 1)
            {
                segment01Prepared = true;
            }
        });

    coordinator_.SetProducerStats(producer_.Stats());
    coordinator_.Execute(
        [&](const SegmentHandle& handle) -> SegmentRenderEntry*
        {
            return segmentPool_.Resolve(handle);
        },
        [&](SegmentRenderEntry& entry,
            const TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::PreparedChunk& chunk)
            -> TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult
        {
            auto* renderer = entry.renderer.get();
            if (!renderer)
            {
                return {};
            }

            renderer->SetOffset(trackOffset);
            if (chunk.segmentId > 0 && chunk.segmentId <= static_cast<int>(kTrackSegmentLimit))
            {
                if (renderedCountById[static_cast<size_t>(chunk.segmentId)] < 255)
                {
                    ++renderedCountById[static_cast<size_t>(chunk.segmentId)];
                }
            }
            renderer->Render(lightDirection, cameraLocation);

            if (!segment01Logged && chunk.segmentId == 1)
            {
                const auto firstSegmentCenter = chunk.center + trackOffset;
                SRL::Debug::Print(1, 19, "Seg01 center %d %d %d",
                                  firstSegmentCenter.X.As<int16_t>(),
                                  firstSegmentCenter.Y.As<int16_t>(),
                                  firstSegmentCenter.Z.As<int16_t>());
                segment01Logged = true;
            }

            TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult result{};
            result.rendered = renderer->LastDrawnMeshes() > 0;
            result.meshes = renderer->LastDrawnMeshes();
            result.faces = renderer->LastDrawnFaces();
            return result;
        });

    for (size_t id = 1; id <= kTrackSegmentLimit; ++id)
    {
        if (preparedCountById[id] > 1)
        {
            SRL::Debug::Print(1, 25, "WARN prep dup seg:%u count:%u", (unsigned)id, (unsigned)preparedCountById[id]);
        }
        if (renderedCountById[id] > 1)
        {
            SRL::Debug::Print(1, 24, "WARN rend dup seg:%u count:%u", (unsigned)id, (unsigned)renderedCountById[id]);
        }
    }

    if (!segment01Logged)
    {
        if (segment01Prepared)
        {
            SRL::Debug::Print(1, 19, "Seg01 center unavailable");
        }
        else
        {
            SRL::Debug::Print(1, 19, "Seg01 center unavailable (not prepared)");
        }
    }
}

void TrackSystem::EndFrame()
{
    coordinator_.PresentTelemetry();
    soakMonitor_.Update(ready_, coordinator_.Telemetry());
    soakMonitor_.Present();

    const FrameBudget nextBudget = budgetController_.Update(coordinator_.Budget(), coordinator_.Telemetry());
    coordinator_.SetBudget(nextBudget);
}

bool TrackSystem::FindNearestSegment(const Vector3D& worldPosition,
                                     const Vector3D& trackOffset,
                                     int32_t& outSegmentId,
                                     Vector3D& outSegmentCenter) const
{
    if (segmentRenderers_.empty())
    {
        outSegmentId = -1;
        outSegmentCenter = Vector3D(0.0, 0.0, 0.0);
        return false;
    }

    bool hasCandidate = false;
    SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
    for (const auto& segment : segmentRenderers_)
    {
        const Vector3D center = segment.center + trackOffset;
        const SRL::Math::Types::Fxp dx = (center.X - worldPosition.X).Abs();
        const SRL::Math::Types::Fxp dz = (center.Z - worldPosition.Z).Abs();
        const SRL::Math::Types::Fxp score = dx + dz;
        if (!hasCandidate || score < bestScore)
        {
            hasCandidate = true;
            bestScore = score;
            outSegmentId = segment.id;
            outSegmentCenter = center;
        }
    }
    if (!hasCandidate)
    {
        outSegmentId = -1;
        outSegmentCenter = Vector3D(0.0, 0.0, 0.0);
    }
    return hasCandidate;
}

bool TrackSystem::FindSegmentCenterById(const int32_t segmentId,
                                        const Vector3D& trackOffset,
                                        Vector3D& outSegmentCenter) const
{
    for (const auto& segment : segmentRenderers_)
    {
        if (segment.id != segmentId)
        {
            continue;
        }

        outSegmentCenter = segment.center + trackOffset;
        return true;
    }

    outSegmentCenter = Vector3D(0.0, 0.0, 0.0);
    return false;
}

bool TrackSystem::HasSmoothSegments() const
{
    for (const auto& segment : segmentRenderers_)
    {
        if (segment.renderer && segment.renderer->IsSmooth())
        {
            return true;
        }
    }
    return false;
}

uint32_t TrackSystem::MaxSegmentFaceCount() const
{
    uint32_t maxFaces = 0;
    for (const auto& segment : segmentRenderers_)
    {
        if (!segment.renderer) continue;
        maxFaces = std::max(maxFaces, segment.renderer->FaceCount());
    }
    return maxFaces;
}

uint32_t TrackSystem::MaxSegmentVertexCount() const
{
    uint32_t maxVertices = 0;
    for (const auto& segment : segmentRenderers_)
    {
        if (!segment.renderer) continue;
        maxVertices = std::max(maxVertices, segment.renderer->VertexCount());
    }
    return maxVertices;
}
