#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <srl.hpp>

#include "frame_budget.hpp"
#include "frame_telemetry.hpp"
#include "render_chunk_pool.hpp"
#include "track_draw_producer.hpp"

template <typename Handle, size_t Capacity>
class TrackRenderCoordinator
{
public:
    // Precomputed chunk staged in HWRAM for deterministic render cost.
    struct PreparedChunk
    {
        Handle handle{};
        int32_t segmentId = -1;
        SRL::Math::Types::Vector3D center{};
        uint32_t estimatedMeshes = 0;
        uint32_t estimatedFaces = 0;
        bool valid = false;
    };

    struct RenderResult
    {
        bool rendered = false;
        uint32_t meshes = 0;
        uint32_t faces = 0;
    };

    struct Config
    {
        FrameBudget budget{};
        size_t chunkCapacity = Capacity;
    };

    explicit TrackRenderCoordinator(ITrackDrawProducer<Handle, Capacity>& producer)
        : producer_(producer)
    {}

    // Initialize internal pools and reset runtime counters.
    bool Initialize(const Config& cfg)
    {
        config_ = cfg;
        usage_.Reset();
        telemetry_ = {};
        return chunkPool_.Initialize(config_.chunkCapacity);
    }

    // Update runtime budget used by subsequent frames.
    void SetBudget(const FrameBudget& budget)
    {
        config_.budget = budget;
    }

    // Start a new frame and propagate frame context to the producer.
    void BeginFrame(uint32_t frameId)
    {
        producer_.BeginFrame(frameId);
        usage_.Reset();
        telemetry_.BeginFrame(frameId);
    }

    // Build draw list and stage precomputed chunks in HWRAM.
    template <typename Resolver, typename CostEstimator, typename MetadataExtractor>
    void Prepare(const std::vector<Handle>& orderedHandles,
                 size_t limit,
                 Resolver resolve,
                 CostEstimator estimateCost,
                 MetadataExtractor extractMetadata)
    {
        producer_.Build(orderedHandles, limit);
        const auto& drawList = producer_.Consume();
        telemetry_.drawListCount = drawList.count;

        std::array<PreparedChunk, Capacity> prepared{};
        size_t preparedCount = 0;
        for (uint16_t i = 0; i < drawList.count && preparedCount < Capacity; ++i)
        {
            auto* entry = resolve(drawList.items[i]);
            if (!entry)
            {
                ++telemetry_.drawListResolveMisses;
                continue;
            }

            const RenderResult estimate = estimateCost(*entry);
            PreparedChunk chunk{};
            chunk.handle = drawList.items[i];
            chunk.estimatedMeshes = estimate.meshes;
            chunk.estimatedFaces = estimate.faces;
            extractMetadata(*entry, chunk);
            chunk.valid = true;
            prepared[preparedCount++] = chunk;
        }

        chunkPool_.SetActive(prepared.data(), preparedCount);
        telemetry_.trackSegmentsPrepared = static_cast<uint32_t>(preparedCount);
    }

    // Inject producer runtime counters into frame telemetry.
    void SetProducerStats(const TrackDrawProducerStats& stats)
    {
        telemetry_.producer = stats;
    }

    // Consume precomputed chunks and submit actual rendering.
    template <typename Resolver, typename Renderer>
    void Execute(Resolver resolve, Renderer renderOne)
    {
        const PreparedChunk* chunks = chunkPool_.Active();
        const size_t count = chunkPool_.ActiveCount();
        if (!chunks || count == 0) return;

        for (size_t i = 0; i < count; ++i)
        {
            const PreparedChunk& chunk = chunks[i];
            if (!chunk.valid)
            {
                ++telemetry_.invalidChunks;
                continue;
            }

            auto* entry = resolve(chunk.handle);
            if (!entry)
            {
                ++telemetry_.executeResolveMisses;
                continue;
            }

            if (!usage_.CanDrawSegment(config_.budget))
            {
                ++telemetry_.trackSegmentsSkippedByBudget;
                continue;
            }

            if (!usage_.CanDrawMeshCount(config_.budget, chunk.estimatedMeshes) ||
                !usage_.CanDrawFaces(config_.budget, chunk.estimatedFaces))
            {
                ++telemetry_.trackSegmentsSkippedByBudget;
                continue;
            }

            RenderResult result = renderOne(*entry, chunk);
            if (!result.rendered) continue;
            usage_.Consume(result.meshes, result.faces);
        }

        telemetry_.submittedTrackSegments = usage_.drawnTrackSegments;
        telemetry_.submittedTrackMeshes = usage_.drawnTrackMeshes;
        telemetry_.submittedTrackFaces = usage_.drawnTrackFaces;
    }

    void PresentTelemetry() const
    {
        telemetry_.Present(config_.budget, usage_);
    }

    const FrameTelemetry& Telemetry() const { return telemetry_; }
    const FrameBudgetUsage& Usage() const { return usage_; }
    const FrameBudget& Budget() const { return config_.budget; }

private:
    ITrackDrawProducer<Handle, Capacity>& producer_;
    RenderChunkPool<PreparedChunk> chunkPool_{};
    Config config_{};
    FrameBudgetUsage usage_{};
    FrameTelemetry telemetry_{};
};
