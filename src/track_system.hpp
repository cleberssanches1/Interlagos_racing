#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#define DOXYGEN 1
#include <srl.hpp>
#undef DOXYGEN

#include "frame_budget.hpp"
#include "frame_budget_controller.hpp"
#include "resource_loader.hpp"
#include "segment_component_loader.hpp"
#include "soak_monitor.hpp"
#include "track_draw_producer.hpp"
#include "track_lod_config.hpp"
#include "track_render_coordinator.hpp"
#include "track_renderer.hpp"
#include "track_segment_pool.hpp"
#include "track_zone_alloc.hpp"

template <typename T>
using TrackLowWorkVector = TrackLowWorkVectorBase<T>;
template <typename T>
using TrackHighWorkVector = TrackHighWorkVectorBase<T>;
using TrackLowWorkU16Vector = TrackLowWorkVector<uint16_t>;
using TrackLowWorkU8Vector = TrackLowWorkVector<uint8_t>;
using TrackLowWorkI8Vector = TrackLowWorkVector<int8_t>;
using TrackLowWorkI16Vector = TrackLowWorkVector<int16_t>;
using TrackHighWorkI16Vector = TrackHighWorkVector<int16_t>;

namespace Game
{
struct SurfaceContact;
}

namespace TrackPipeline
{
class TrackMaintenanceStage;
class TrackWindowStage;
class TrackPrefetchStage;
class TrackLodStage;
class TrackWorkingSetStage;
}

template <typename T, SRL::Memory::Zone ZoneValue>
struct TrackObjectDeleter
{
    void operator()(T* ptr) const noexcept
    {
        if (!ptr) return;
        ptr->~T();
        SRL::Memory::Free(ptr);
    }
};

template <typename T, SRL::Memory::Zone ZoneValue, typename... Args>
std::unique_ptr<T, TrackObjectDeleter<T, ZoneValue>> MakeTrackObjectUnique(Args&&... args)
{
    void* mem = SRL::Memory::Malloc(sizeof(T), ZoneValue);
    if (!mem) return {};
    T* obj = new (mem) T(std::forward<Args>(args)...);
    return std::unique_ptr<T, TrackObjectDeleter<T, ZoneValue>>(obj);
}

template <typename T>
using TrackLowWorkUniquePtr = std::unique_ptr<T, TrackObjectDeleter<T, SRL::Memory::Zone::LWRam>>;

class TrackSystem
{
public:
    static constexpr size_t kTrackSegmentLimit = 48;
    // Direct lookup only needs to cover the common low segment-id range.
    // Higher ids already fall back to the dynamic active-window tables.
    static constexpr size_t kWindowSegmentIdDirectIndexCap = 128;

    struct Config
    {
        // Initial visible segment count before adaptive budget tuning.
        uint16_t initialSegments = static_cast<uint16_t>(TrackLodConfig::kVisibleSegments);
        uint16_t minSegments = static_cast<uint16_t>(TrackLodConfig::kVisibleSegments);
        uint32_t initialMeshes = 128;
        uint32_t initialFaces = 32000;
        bool useSlave = true;
    };

    struct LowWorkCategoryBreakdown
    {
        uint32_t renderers = 0;
        uint32_t slotState = 0;
        uint32_t workingSet = 0;
        uint32_t familyCache = 0;
        uint32_t transient = 0;
        uint32_t metadata = 0;
        uint32_t total = 0;
    };

    // Load segments, build pools and initialize producer/coordinator state.
    bool Initialize(const Config& config);

    // Start frame accounting for producer and telemetry subsystems.
    void BeginFrame(uint32_t frameId);

    // Submit track rendering and enforce first segment position logging.
    void RenderFrame(bool renderTrack,
                     const SRL::Math::Types::Vector3D& trackOffset,
                     const SRL::Math::Types::Vector3D& lightDirection,
                     const SRL::Math::Types::Vector3D& cameraLocation,
                     const SRL::Math::Types::Vector3D& cameraLookTarget,
                     const SRL::Math::Types::Vector3D& carWorldPosition);
    void SetObservedCarSegmentId(int32_t segmentId);

    // Present telemetry and update adaptive budget targets for next frame.
    void EndFrame();

    // Resolve nearest loaded segment for simple collision and gameplay queries.
    bool FindNearestSegment(const SRL::Math::Types::Vector3D& worldPosition,
                            const SRL::Math::Types::Vector3D& trackOffset,
                            int32_t& outSegmentId,
                            SRL::Math::Types::Vector3D& outSegmentCenter) const;
    bool FindSurfaceYByFamilyId(const SRL::Math::Types::Vector3D& worldPosition,
                                const SRL::Math::Types::Vector3D& trackOffset,
                                uint16_t familyId,
                                SRL::Math::Types::Fxp& outSurfaceY,
                                int32_t* outSegmentId = nullptr,
                                int32_t seedSegmentId = -1,
                                bool allowFallback = true) const;
    bool FindSurfaceYByFamilySet(const SRL::Math::Types::Vector3D& worldPosition,
                                 const SRL::Math::Types::Vector3D& trackOffset,
                                 const uint16_t* familyIds,
                                 size_t familyCount,
                                 SRL::Math::Types::Fxp& outSurfaceY,
                                 int32_t* outSegmentId = nullptr,
                                 int32_t seedSegmentId = -1,
                                 bool allowFallback = true,
                                 uint16_t* outFamilyId = nullptr,
                                 uint8_t* outSurfaceType = nullptr,
                                 int16_t* outFaceIndex = nullptr,
                                 int16_t hintFaceIndex = -1,
                                 bool useSharedFaceCache = true,
                                 const uint8_t* allowedSurfaceTypes = nullptr,
                                 size_t allowedSurfaceTypeCount = 0u) const;
    bool FindSurfaceYBySurfaceTypeSet(const SRL::Math::Types::Vector3D& worldPosition,
                                      const SRL::Math::Types::Vector3D& trackOffset,
                                      const uint8_t* surfaceTypes,
                                      size_t surfaceTypeCount,
                                      SRL::Math::Types::Fxp& outSurfaceY,
                                      int32_t* outSegmentId = nullptr,
                                      int32_t seedSegmentId = -1,
                                      bool allowFallback = true,
                                      uint16_t* outFamilyId = nullptr,
                                      uint8_t* outSurfaceType = nullptr,
                                      int16_t* outFaceIndex = nullptr,
                                      int16_t hintFaceIndex = -1,
                                      bool useSharedFaceCache = true) const;
    bool FindSurfaceContact(const SRL::Math::Types::Vector3D& worldPosition,
                            const SRL::Math::Types::Vector3D& trackOffset,
                            Game::SurfaceContact& outContact,
                            int32_t seedSegmentId = -1,
                            bool allowFallback = true) const;
    bool FindPlanarWallPush(const SRL::Math::Types::Vector3D& worldPosition,
                            const SRL::Math::Types::Vector3D& trackOffset,
                            const SRL::Math::Types::Vector3D& forwardDirection,
                            SRL::Math::Types::Fxp collisionRadius,
                            SRL::Math::Types::Vector3D& outPush,
                            int32_t* outSegmentId = nullptr,
                            int32_t seedSegmentId = -1,
                            bool allowGlobalFallback = true) const;
    bool FindSegmentCenterById(int32_t segmentId,
                               const SRL::Math::Types::Vector3D& trackOffset,
                               SRL::Math::Types::Vector3D& outSegmentCenter) const;
    bool GetRenderWindowDebugSnapshot(int32_t& outStartSegmentId,
                                      int8_t& outDirection,
                                      uint16_t& outWindowCount) const;
    bool GetRenderWindowSegmentIdAt(size_t logicalIndex, int32_t& outSegmentId) const;

    bool Ready() const { return ReadyFlag(); }
    const char* LastResolvedPath() const { return lastSegmentPath_; }
    const FrameTelemetry& Telemetry() const { return coordinator_.Telemetry(); }
    uint16_t SegmentCount() const { return totalSegmentCount_; }
    bool HasSmoothSegments() const;
    uint32_t MaxSegmentFaceCount() const;
    uint32_t MaxSegmentVertexCount() const;
    int32_t LowWorkDrawPrepareDeltaThisFrame() const { return frameMemoryTelemetry_.drawPrepareDeltaThisFrame; }
    int32_t LowWorkDrawExecuteDeltaThisFrame() const { return frameMemoryTelemetry_.drawExecuteDeltaThisFrame; }
    int32_t LowWorkDrawOtherDeltaThisFrame() const { return frameMemoryTelemetry_.drawOtherDeltaThisFrame; }
    int32_t LowWorkDrawFrameDeltaThisFrame() const { return frameMemoryTelemetry_.drawFrameDeltaThisFrame; }
    uint32_t LowWorkEndFreeBytesThisFrame() const { return frameMemoryTelemetry_.phaseLwrEnd; }
    uint8_t SlidesThisFrame() const { return runtimeSlidesThisFrame_; }
    int32_t SlideSegmentIdThisFrame() const { return slideHwrTrace_.segmentId; }
    LowWorkCategoryBreakdown LowWorkBreakdownThisFrame() const { return frameMemoryTelemetry_.lowWorkBreakdownEnd; }
    uint16_t StreamTicksThisFrame() const { return sh2MasterStreamTicksThisFrame_; }
    uint16_t MaintenanceTicksThisFrame() const { return sh2MasterMaintenanceTicksThisFrame_; }
    uint16_t DrawTicksThisFrame() const { return sh2MasterDrawTicksThisFrame_; }
    uint16_t FrameTicksThisFrame() const { return sh2MasterFrameTicksThisFrame_; }
    uint16_t WindowTicksThisFrame() const { return sh2MasterWindowTicksThisFrame_; }
    uint16_t PrefetchTicksThisFrame() const { return sh2MasterPrefetchTicksThisFrame_; }
    uint16_t LodTicksThisFrame() const { return sh2MasterLodTicksThisFrame_; }
    uint16_t WorkingSetTicksThisFrame() const { return sh2MasterWorkingSetTicksThisFrame_; }
    uint16_t SlaveSortTicksThisFrame() const { return sh2SlaveSortTicksThisFrame_; }
    uint16_t SlavePlanTicksThisFrame() const { return sh2SlavePlanTicksThisFrame_; }
    uint8_t PrefetchBuildAttemptsThisFrame() const { return prefetchBuildAttemptsThisFrame_; }
    uint8_t PrefetchBuildBudgetThisFrame() const { return prefetchBuildBudgetThisFrame_; }
    uint8_t PrefetchBuildBudgetDropsThisFrame() const { return prefetchBuildBudgetDropsThisFrame_; }
    uint32_t SurfaceQueryCallsThisFrame() const { return static_cast<uint32_t>(surfaceQueryCallsLastFrame_); }
    uint32_t SurfaceQueryFallbackHitsThisFrame() const { return static_cast<uint32_t>(surfaceQueryFallbackHitsLastFrame_); }
    uint32_t SurfaceQueryGlobalPassesThisFrame() const { return static_cast<uint32_t>(surfaceQueryGlobalPassesLastFrame_); }
    uint32_t SurfaceQueryLocalOnlyMissesThisFrame() const { return static_cast<uint32_t>(surfaceQueryLocalOnlyMissesLastFrame_); }
    uint32_t SurfaceQueryScmapSkipsThisFrame() const { return static_cast<uint32_t>(surfaceQueryScmapSkipsLastFrame_); }
    uint32_t SurfaceQuerySegmentsScannedThisFrame() const { return static_cast<uint32_t>(surfaceQuerySegmentsScannedLastFrame_); }
    uint32_t SurfaceQueryFacesScannedThisFrame() const { return static_cast<uint32_t>(surfaceQueryFacesScannedLastFrame_); }
    uint32_t SurfaceQueryCacheHitsThisFrame() const { return static_cast<uint32_t>(surfaceQueryCacheHitsLastFrame_); }
    uint32_t SurfaceQueryCacheMissesThisFrame() const { return static_cast<uint32_t>(surfaceQueryCacheMissesLastFrame_); }
    uint32_t WallQueryCallsThisFrame() const { return static_cast<uint32_t>(wallQueryCallsLastFrame_); }
    uint32_t WallQueryHitsThisFrame() const { return static_cast<uint32_t>(wallQueryHitsLastFrame_); }
    uint32_t WallQuerySegmentsScannedThisFrame() const { return static_cast<uint32_t>(wallQuerySegmentsScannedLastFrame_); }
    uint32_t WallQueryFacesScannedThisFrame() const { return static_cast<uint32_t>(wallQueryFacesScannedLastFrame_); }
    // Toggle track Slave usage at runtime for A/B performance measurements.
    void SetTrackSlaveMode(bool enabled);
    bool TrackSlaveModeRequested() const { return TrackSlaveModeRequestedFlag(); }
    void SetRuntimeStatsLogsEnabled(bool enabled) { runtimeDiagnostics_.SetRuntimeStatsLogsEnabled(enabled); }
    bool RuntimeStatsLogsEnabled() const { return runtimeDiagnostics_.RuntimeStatsLogsEnabled(); }
    // Re-anchor track texture heap base after loading non-track assets (e.g. car).
    void RebaseTrackTextureHeapBase();
#ifdef TRACK_LWR_STAGE_TRACE
    static void PrintLwrStageProbes();
#endif

private:
    enum : uint32_t
    {
        kSegmentCollisionMapReadyBit = 1u << 0,
        kPrefetchSpeedProxyValidBit = 1u << 1,
        kTrackSlaveModeRequestedBit = 1u << 2,
        kTrackSlaveProducerRequestedBit = 1u << 3,
        kTrackSlaveDepthSortRequestedBit = 1u << 4,
        kTrackSlaveBarrierLockstepBit = 1u << 5,
        kFullTrackFamilyCacheReadyBit = 1u << 6,
        kLastWindowFreeValidBit = 1u << 7,
        kActiveWindowLookupDirtyBit = 1u << 8,
        kFamilyWorkingSetDirtyBit = 1u << 9,
        kPendingLodWorkExistsBit = 1u << 10,
        kFamilySlotIndexDirtyBit = 1u << 11,
        kReadyBit = 1u << 12,
        kSegmentsReadyBit = 1u << 13,
        kCoordinatorReadyBit = 1u << 14,
        kTrackedCarSegmentValidBit = 1u << 15,
        kSlidePrefetchRendererReadyBit = 1u << 16,
        kSlidePrefetchLodReadyBit = 1u << 17,
        kTrackTextureHeapBaseValidBit = 1u << 18,
        kWallQueryPrevWorldPositionValidBit = 1u << 19,
        kSurfaceQueryLastInsideValidBit = 1u << 20,
        kSurfaceFamilyMapReadyBit = 1u << 21,
        kSeg1ComponentEnabledBit = 1u << 22,
        kSeg1RendererLodReadyBit = 1u << 23,
        kSeg1SingleFaceSwapReadyBit = 1u << 24,
        kSeg1SingleFaceSwapUseAltBit = 1u << 25
    };

    bool HasStateFlag(uint32_t bit) const { return (stateFlags_ & bit) != 0u; }
    void SetStateFlag(uint32_t bit, bool enabled) const
    {
        if (enabled) stateFlags_ |= bit;
        else stateFlags_ &= ~bit;
    }
    bool SegmentCollisionMapReady() const { return HasStateFlag(kSegmentCollisionMapReadyBit); }
    void SetSegmentCollisionMapReady(bool enabled) const { SetStateFlag(kSegmentCollisionMapReadyBit, enabled); }
    bool PrefetchSpeedProxyValid() const { return HasStateFlag(kPrefetchSpeedProxyValidBit); }
    void SetPrefetchSpeedProxyValid(bool enabled) const { SetStateFlag(kPrefetchSpeedProxyValidBit, enabled); }
    bool TrackSlaveModeRequestedFlag() const { return HasStateFlag(kTrackSlaveModeRequestedBit); }
    void SetTrackSlaveModeRequestedFlag(bool enabled) const { SetStateFlag(kTrackSlaveModeRequestedBit, enabled); }
    bool TrackSlaveProducerRequestedFlag() const { return HasStateFlag(kTrackSlaveProducerRequestedBit); }
    void SetTrackSlaveProducerRequestedFlag(bool enabled) const { SetStateFlag(kTrackSlaveProducerRequestedBit, enabled); }
    bool TrackSlaveDepthSortRequestedFlag() const { return HasStateFlag(kTrackSlaveDepthSortRequestedBit); }
    void SetTrackSlaveDepthSortRequestedFlag(bool enabled) const { SetStateFlag(kTrackSlaveDepthSortRequestedBit, enabled); }
    bool TrackSlaveBarrierLockstepFlag() const { return HasStateFlag(kTrackSlaveBarrierLockstepBit); }
    void SetTrackSlaveBarrierLockstepFlag(bool enabled) const { SetStateFlag(kTrackSlaveBarrierLockstepBit, enabled); }
    bool FullTrackFamilyCacheReady() const { return HasStateFlag(kFullTrackFamilyCacheReadyBit); }
    void SetFullTrackFamilyCacheReady(bool enabled) const { SetStateFlag(kFullTrackFamilyCacheReadyBit, enabled); }
    bool LastWindowFreeValid() const { return HasStateFlag(kLastWindowFreeValidBit); }
    void SetLastWindowFreeValid(bool enabled) const { SetStateFlag(kLastWindowFreeValidBit, enabled); }
    bool ActiveWindowLookupDirty() const { return HasStateFlag(kActiveWindowLookupDirtyBit); }
    void SetActiveWindowLookupDirty(bool enabled) const { SetStateFlag(kActiveWindowLookupDirtyBit, enabled); }
    bool FamilyWorkingSetDirty() const { return HasStateFlag(kFamilyWorkingSetDirtyBit); }
    void SetFamilyWorkingSetDirty(bool enabled) const { SetStateFlag(kFamilyWorkingSetDirtyBit, enabled); }
    bool PendingLodWorkExists() const { return HasStateFlag(kPendingLodWorkExistsBit); }
    void SetPendingLodWorkExists(bool enabled) const { SetStateFlag(kPendingLodWorkExistsBit, enabled); }
    bool FamilySlotIndexDirty() const { return HasStateFlag(kFamilySlotIndexDirtyBit); }
    void SetFamilySlotIndexDirty(bool enabled) const { SetStateFlag(kFamilySlotIndexDirtyBit, enabled); }
    bool ReadyFlag() const { return HasStateFlag(kReadyBit); }
    void SetReadyFlag(bool enabled) const { SetStateFlag(kReadyBit, enabled); }
    bool SegmentsReady() const { return HasStateFlag(kSegmentsReadyBit); }
    void SetSegmentsReady(bool enabled) const { SetStateFlag(kSegmentsReadyBit, enabled); }
    bool CoordinatorReady() const { return HasStateFlag(kCoordinatorReadyBit); }
    void SetCoordinatorReady(bool enabled) const { SetStateFlag(kCoordinatorReadyBit, enabled); }
    bool TrackedCarSegmentValid() const { return HasStateFlag(kTrackedCarSegmentValidBit); }
    void SetTrackedCarSegmentValid(bool enabled) const { SetStateFlag(kTrackedCarSegmentValidBit, enabled); }
    bool SlidePrefetchRendererReady() const { return HasStateFlag(kSlidePrefetchRendererReadyBit); }
    void SetSlidePrefetchRendererReady(bool enabled) const { SetStateFlag(kSlidePrefetchRendererReadyBit, enabled); }
    bool SlidePrefetchLodReady() const { return HasStateFlag(kSlidePrefetchLodReadyBit); }
    void SetSlidePrefetchLodReady(bool enabled) const { SetStateFlag(kSlidePrefetchLodReadyBit, enabled); }
    bool TrackTextureHeapBaseValid() const { return HasStateFlag(kTrackTextureHeapBaseValidBit); }
    void SetTrackTextureHeapBaseValid(bool enabled) const { SetStateFlag(kTrackTextureHeapBaseValidBit, enabled); }
    bool WallQueryPrevWorldPositionValid() const { return HasStateFlag(kWallQueryPrevWorldPositionValidBit); }
    void SetWallQueryPrevWorldPositionValid(bool enabled) const { SetStateFlag(kWallQueryPrevWorldPositionValidBit, enabled); }
    bool SurfaceQueryLastInsideValid() const { return HasStateFlag(kSurfaceQueryLastInsideValidBit); }
    void SetSurfaceQueryLastInsideValid(bool enabled) const { SetStateFlag(kSurfaceQueryLastInsideValidBit, enabled); }
    bool SurfaceFamilyMapReady() const { return HasStateFlag(kSurfaceFamilyMapReadyBit); }
    void SetSurfaceFamilyMapReady(bool enabled) const { SetStateFlag(kSurfaceFamilyMapReadyBit, enabled); }
    bool Seg1ComponentEnabled() const { return HasStateFlag(kSeg1ComponentEnabledBit); }
    void SetSeg1ComponentEnabled(bool enabled) const { SetStateFlag(kSeg1ComponentEnabledBit, enabled); }
    bool Seg1RendererLodReady() const { return HasStateFlag(kSeg1RendererLodReadyBit); }
    void SetSeg1RendererLodReady(bool enabled) const { SetStateFlag(kSeg1RendererLodReadyBit, enabled); }
    bool Seg1SingleFaceSwapReady() const { return HasStateFlag(kSeg1SingleFaceSwapReadyBit); }
    void SetSeg1SingleFaceSwapReady(bool enabled) const { SetStateFlag(kSeg1SingleFaceSwapReadyBit, enabled); }
    bool Seg1SingleFaceSwapUseAlt() const { return HasStateFlag(kSeg1SingleFaceSwapUseAltBit); }
    void SetSeg1SingleFaceSwapUseAlt(bool enabled) const { SetStateFlag(kSeg1SingleFaceSwapUseAltBit, enabled); }

    friend class TrackPipeline::TrackMaintenanceStage;
    friend class TrackPipeline::TrackWindowStage;
    friend class TrackPipeline::TrackPrefetchStage;
    friend class TrackPipeline::TrackLodStage;
    friend class TrackPipeline::TrackWorkingSetStage;

    enum class MemoryPressureLevel : uint8_t
    {
        Normal = 0,
        Pressure = 1,
        Critical = 2,
    };

    struct TrackSegmentEntry
    {
        int32_t id = 0;
        TrackSegmentCopy copy;
    };

    struct SegmentRenderEntry
    {
        struct WallSegment2D
        {
            int32_t axRaw = 0;
            int32_t azRaw = 0;
            int32_t bxRaw = 0;
            int32_t bzRaw = 0;
            int32_t minXRaw = 0;
            int32_t maxXRaw = 0;
            int32_t minZRaw = 0;
            int32_t maxZRaw = 0;
            int32_t minYRaw = 0;
            int32_t maxYRaw = 0;
            int32_t nxRaw = 0;
            int32_t nzRaw = 0;
        };

        struct SegmentLodState
        {
            enum : uint8_t
            {
                kReadyBit = 1u << 0,
                kHasPerFaceRankOffsetsBit = 1u << 1,
                kWorkingSetCacheDirtyBit = 1u << 2
            };

            // Resident state currently visible in the renderer.
            uint8_t currentLodIndex = 0xFF; // 2:32, 3:64 (0/1 reserved)
            int8_t currentBaseRank = -1;
            // Design mesh tier: 0 = high (lod_0 / TRKRDR), 1 = low (lod_1+ / TRKRDRL).
            uint8_t currentDesignGeoTier = 0xFF;
            // Desired state derived from the logical rank in the sliding window.
            uint8_t desiredLodIndex = 0xFF;
            int8_t desiredBaseRank = -1;
            uint8_t desiredDesignGeoTier = 0xFF;
            uint8_t flags = kWorkingSetCacheDirtyBit;
            TrackLowWorkU16Vector faceFamilyIds{};
            TrackLowWorkU8Vector faceRankOffsets{};
            TrackLowWorkI16Vector currentFaceSlots{};
            TrackLowWorkU16Vector workingSetFamilies{};
            TrackLowWorkU8Vector workingSetLodIndices{};
            TrackLowWorkI16Vector workingSetSlots{};

            bool Ready() const { return (flags & kReadyBit) != 0u; }
            bool HasPerFaceRankOffsets() const { return (flags & kHasPerFaceRankOffsetsBit) != 0u; }
            bool WorkingSetCacheDirty() const { return (flags & kWorkingSetCacheDirtyBit) != 0u; }
            void SetReady(bool enabled)
            {
                if (enabled) flags |= kReadyBit;
                else flags &= static_cast<uint8_t>(~kReadyBit);
            }
            void SetHasPerFaceRankOffsets(bool enabled)
            {
                if (enabled) flags |= kHasPerFaceRankOffsetsBit;
                else flags &= static_cast<uint8_t>(~kHasPerFaceRankOffsetsBit);
            }
            void SetWorkingSetCacheDirty(bool enabled)
            {
                if (enabled) flags |= kWorkingSetCacheDirtyBit;
                else flags &= static_cast<uint8_t>(~kWorkingSetCacheDirtyBit);
            }
        };

        int32_t id = 0;
        uint8_t logicalSegmentCount = 0;
        TrackLowWorkUniquePtr<TrackRenderer> renderer;
        SRL::Math::Types::Vector3D center{};
        SegmentLodState lodState{};
        mutable TrackLowWorkVector<WallSegment2D> wallSegments2D{};
        mutable uint16_t wallSegmentsCacheVertCount = 0u;
        mutable uint16_t wallSegmentsCacheFaceCount = 0u;
        mutable uint16_t wallSegmentsCacheFamilyCount = 0u;
        mutable int16_t wallSegmentsCacheSegmentId = -1;
        mutable uint8_t wallSegmentsCacheLodIndex = 0xFF;
        mutable uint8_t wallSegmentsCacheFlags = 0u;

        bool WallSegmentsCacheReady() const { return (wallSegmentsCacheFlags & 1u) != 0u; }
        void SetWallSegmentsCacheReady(bool enabled) const
        {
            if (enabled) wallSegmentsCacheFlags |= 1u;
            else wallSegmentsCacheFlags &= static_cast<uint8_t>(~1u);
        }
    };
    struct RawSegmentEntry
    {
        int32_t id = 0;
        TrackSegmentCopy copy{};
    };
    struct Seg1FamilySlotEntry
    {
        uint16_t familyId = 0;
        std::array<uint16_t, 4> lodSlots{{0, 0, 0, 0}}; // 2:32, 3:64 (0/1 reserved)
        std::array<uint16_t, 4> workingRefs{{0, 0, 0, 0}};
        std::array<uint8_t, 4> unusedFrames{{0, 0, 0, 0}};
    };
    struct Seg1TexbankEntry
    {
        uint16_t familyId = 0;
        uint32_t offset = 0;
        uint32_t size = 0;
    };
    struct Seg1TexbankCart
    {
        int16_t lod = 8;
        void* cartPtr = nullptr;
        uint32_t size = 0;
        TrackLowWorkVector<Seg1TexbankEntry> entries{};
    };
    struct Seg1TgaCartEntry
    {
        char name[64]{};
        void* cartPtr = nullptr;
        uint32_t size = 0;
    };
    struct SlideBoundaryUpdate
    {
        uint8_t flags = 0u;
        int16_t segmentId = -1;
        uint8_t desiredLodIndex = 0xFF;
        int8_t desiredBaseRank = -1;
        TrackLowWorkI16Vector preparedFaceSlots{};

        bool Active() const { return (flags & 1u) != 0u; }
        void SetActive(bool enabled)
        {
            if (enabled) flags |= 1u;
            else flags &= static_cast<uint8_t>(~1u);
        }
    };
    struct SlideBackBuffer
    {
        uint8_t flags = 0u;
        int8_t direction = 1;
        uint8_t dropIdx = 0;
        int16_t incomingSegmentId = -1;
        int16_t outgoingSegmentId = -1;
        int16_t nextStartId = 1;
        SRL::Math::Types::Vector3D incomingCenter{};
        TrackLowWorkU16Vector incomingFamilyIds{};
        TrackLowWorkI16Vector incomingFaceSlots{};
        uint8_t incomingResidentLodIndex = 0xFF;
        int8_t incomingResidentBaseRank = -1;
        std::array<SlideBoundaryUpdate, 4> boundaryUpdates{};

        bool Ready() const { return (flags & 1u) != 0u; }
        void SetReady(bool enabled)
        {
            if (enabled) flags |= 1u;
            else flags &= static_cast<uint8_t>(~1u);
        }
    };

    using SegmentEntryVector = TrackLowWorkVector<TrackSegmentEntry>;
    using RawSegmentVector = TrackLowWorkVector<RawSegmentEntry>;
    using CenterCatalogVector = TrackLowWorkVector<SRL::Math::Types::Vector3D>;
    using FamilyIdCatalogVector = TrackLowWorkU16Vector;
    using FamilyIdVector = TrackLowWorkU16Vector;
    using FaceRankOffsetVector = TrackLowWorkU8Vector;
    using FamilySlotVector = TrackLowWorkVector<Seg1FamilySlotEntry>;

    using SegmentPool = TrackSegmentPool<kTrackSegmentLimit, SegmentRenderEntry>;
    using SegmentHandle = SegmentPool::Handle;

    struct TrackFramePlan
    {
        static constexpr uint8_t kValidBit = 1u << 0;

        uint32_t frameId = 0;
        uint8_t flags = 0u;
        uint16_t plannerTicksSlave = 0;
        // Indexed by logical rank in the active window (0..windowCount-1).
        std::array<uint8_t, kTrackSegmentLimit> desiredLodByLogicalRank{};
        std::array<int8_t, kTrackSegmentLimit> desiredBaseRankByLogicalRank{};

        bool Valid() const { return (flags & kValidBit) != 0u; }
        void SetValid(bool enabled)
        {
            if (enabled) flags |= kValidBit;
            else flags &= static_cast<uint8_t>(~kValidBit);
        }
    };

    static SRL::Math::Types::Vector3D ComputeRendererCenter(const TrackRenderer& renderer);
    void ReleaseRawSegmentCatalog();
    void ReleaseSeg1Texbanks();
    void ReleaseSeg1TgaCatalog();
    bool PreloadTgaCatalogFromSegmentsMap();
    bool LoadSeg1TexbankIndexToCart(size_t lodIndex, int lodValue);
    bool BuildSeg1TexbankCandidatePaths(int lodValue,
                                        std::array<std::array<char, 40>, 16>& storage,
                                        const char** outCandidates,
                                        size_t& outCount);
    void InvalidateFamilySlotIndex() const;
    void RebuildFamilySlotIndex() const;
    void ResetFamilyLookupTables();
    void EnsureSurfaceFamilyLookupCapacity(size_t requiredEntries);
    void EnsureFamilySlotIndexCapacity(size_t requiredEntries) const;
    void InitializeFamilySlots(FamilySlotVector& outSlots,
                               const int* familyIds,
                               size_t count) const;
    void InitializeFamilySlots(FamilySlotVector& outSlots,
                               const FamilyIdCatalogVector& familyIds) const;
    Seg1FamilySlotEntry* FindFamilySlot(FamilySlotVector& familySlots, uint16_t familyId);
    const Seg1FamilySlotEntry* FindFamilySlot(const FamilySlotVector& familySlots, uint16_t familyId) const;
    bool TryGetFamilyLodSlot(const FamilySlotVector& familySlots,
                             uint16_t familyId,
                             uint8_t lodIndex,
                             uint16_t& outSlot) const;
    const Seg1TexbankEntry* FindTexbankEntryByFamily(const Seg1TexbankCart& bank, uint16_t familyId) const;
    bool TryLoadFamilyLodSlot(Seg1FamilySlotEntry& slotEntry,
                              uint8_t targetLodIndex,
                              bool fallbackToLowerLods,
                              bool fallbackToHigherLods,
                              bool* outSawMissingFamily,
                              bool* outSawDecodeFail,
                              bool* outSawUploadFail,
                              int* outLoadedFromLodValue);
    bool PreloadFullTrackFamilyLodCache();
    // Build per family texture slots for all lod levels used by segment renderers.
    bool BuildTrackFamilyLodSlots(FamilySlotVector& outSlots);
    // Build per face slot tables for one segment renderer across all lod levels.
    bool BuildSegmentLodState(SegmentRenderEntry& entry,
                              const SegmentComponent::Blob& matBlob,
                              const SegmentComponent::Loader::MatView& matView,
                              FamilySlotVector& familySlots);
    // Rebuild one segment face slot table on demand for the selected lod band.
    bool RebuildSegmentFaceSlotsForLod(SegmentRenderEntry& entry,
                                       uint8_t lodIndex,
                                       FamilySlotVector& familySlots,
                                       bool bypassUploadBudget = false);
    // Rebuild one batch face slot table using the first logical rank carried by that batch.
    bool RebuildSegmentFaceSlotsForBaseRank(SegmentRenderEntry& entry,
                                            size_t baseRank,
                                            FamilySlotVector& familySlots,
                                            bool bypassUploadBudget = false);
    // Upload one family texture slot only when a lod band actually needs it.
    bool EnsureFamilyLodSlotLoaded(FamilySlotVector& familySlots,
                                   uint16_t familyId,
                                   uint8_t lodIndex,
                                   bool bypassUploadBudget = false);
    bool TryGetBestFamilyLodSlot(FamilySlotVector& familySlots,
                                 uint16_t familyId,
                                 uint8_t preferredLodIndex,
                                 uint16_t& outSlot,
                                 uint8_t* outResolvedLodIndex = nullptr,
                                 bool tryLoadFallback = false,
                                 bool bypassUploadBudget = false);
    bool ResolveBestEffortFaceSlots(const FamilyIdVector& faceFamilyIds,
                                    uint8_t preferredLodIndex,
                                    FamilySlotVector& familySlots,
                                    TrackLowWorkI16Vector& outFaceSlots,
                                    bool tryLoadFallback = false,
                                    bool bypassUploadBudget = false);
    bool RebuildSafeSegmentEntry(SegmentRenderEntry& entry);
    // Resolve the target lod band for a visible segment rank near the camera.
    uint8_t ResolveSegmentLodIndexByRank(size_t rank) const;
    bool TryGetWindowLogicalRank(int32_t segmentId, size_t& outRank) const;
    void RebuildActiveWindowLookupTables();
    void InvalidateActiveWindowLookupTables();
    size_t LogicalToPhysicalWindowIndex(size_t logicalIndex, size_t windowCount) const;
    int32_t ResolveWindowOutgoingSegmentId(int8_t direction, size_t windowCount) const;
    int32_t ResolveWindowIncomingSegmentId(int8_t direction, size_t windowCount) const;
    bool TryResolveWindowEntryIndexBySegmentId(int32_t segmentId, size_t& outIndex);
    bool TryResolveWindowEntryIndexBySegmentId(int32_t segmentId, size_t& outIndex) const;
    bool ResolveWindowDropIndexByDirection(int8_t direction, size_t windowCount, size_t& outDropIdx);
    bool ResolveWindowHeadByStartId(size_t fallbackIndex);
    bool AdvanceWindowHeadByDirection(int8_t direction, size_t windowCount);
    bool EnsureWallSegmentCache(SegmentRenderEntry& entry) const;
    SegmentRenderEntry* FindWindowEntryByIdFast(int32_t segmentId);
    const SegmentRenderEntry* FindWindowEntryByIdFast(int32_t segmentId) const;
    void UpdateDesiredStabilizedWindowLodTargets();
    void InvalidateEntryWorkingSetCache(SegmentRenderEntry& entry);
    bool RebuildEntryWorkingSetCache(SegmentRenderEntry& entry);
    void RebuildUsedTextureSlotFlagsFromWorkingRefs();
    void RebuildUsedTextureSlotFlagsFromCurrentFaces();
    uint32_t GetStrictPendingLodPriority(size_t logicalRank) const;
    bool HasPendingStabilizedWindowLodChanges() const;
    void ResetPendingStabilizedLodRanks();
    void QueuePendingStabilizedLodRank(size_t logicalRank);
    void SeedPendingStabilizedLodRanksForWindow();
    template <typename FaceFamilyVecT, typename FaceSlotsVecT>
    bool ResolveFaceSlotsForFixedLodFromFamilies(const FaceFamilyVecT& faceFamilyIds,
                                                 uint8_t lodIndex,
                                                 FamilySlotVector& familySlots,
                                                 FaceSlotsVecT& outFaceSlots,
                                                 bool bypassUploadBudget = false);
    template <typename FaceFamilyVecT, typename RankOffsetVecT, typename FaceSlotsVecT>
    bool ResolveFaceSlotsForBaseRankFromFamilies(const FaceFamilyVecT& faceFamilyIds,
                                                 const RankOffsetVecT& faceRankOffsets,
                                                 size_t baseRank,
                                                 FamilySlotVector& familySlots,
                                                 FaceSlotsVecT& outFaceSlots,
                                                 bool bypassUploadBudget = false);
    template <typename FaceSlotsVecT>
    bool ResolvePreparedFaceSlotsForLod(const SegmentRenderEntry& entry,
                                        uint8_t lodIndex,
                                        FamilySlotVector& familySlots,
                                        FaceSlotsVecT& outFaceSlots,
                                        bool bypassUploadBudget = false);
    template <typename FaceSlotsVecT>
    bool ResolvePreparedFaceSlotsForBaseRank(const SegmentRenderEntry& entry,
                                             size_t baseRank,
                                             FamilySlotVector& familySlots,
                                             FaceSlotsVecT& outFaceSlots,
                                             bool bypassUploadBudget = false);
    bool ApplyStabilizedLodForLogicalRank(size_t logicalRank);
    void UpdateStabilizedWindowLodBoundaries();
    void UpdateStabilizedWindowLodBands();
    void ProcessPendingStabilizedWindowLodChanges(uint8_t maxUpdates);
    void ResetSlideBackBuffer();
    bool ExecuteDeterministicStabilizedSlide(size_t dropIdx,
                                             int8_t direction,
                                             int32_t nextId,
                                             int32_t nextStartId);
    bool PrepareStabilizedSlideBackBuffer(size_t dropIdx,
                                          int8_t direction,
                                          int32_t nextId,
                                          int32_t nextStartId);
    bool CommitStabilizedSlideBackBuffer();
    // Apply lod changes only for segments whose desired band changed.
    void UpdateVisibleSegmentLods(const std::vector<SegmentHandle>& nearToFarHandles);
    bool BuildSegmentCenterCatalog();
    bool RebuildActiveSegmentWindow(int32_t startSegmentId, size_t loadLimit, int8_t direction);
    bool SlideActiveSegmentWindow(size_t stepCount, int8_t direction);
    void PrewarmNextSegmentLod32();
    void PrewarmUpcomingBoundaryLods();
    void ResetSlidePrefetchState();
    bool BuildSegmentIntoPrefetch(int32_t segmentId, bool allowSlotWarmup = true);
    // designGeoTier: 0=high (lod_0), 1=low (lod_1/2 mesh). 0xFF = high default.
    bool BuildSegmentIntoRenderer(int32_t segmentId,
                                  TrackRenderer& renderer,
                                  SRL::Math::Types::Vector3D& outCenter,
                                  FamilyIdVector& outFamilyIds,
                                  uint8_t designGeoTier = 0xFF);
    // designGeoTier: 0=high, 1=low, 0xFF=default high (legacy). Prefer rank-based.
    bool BuildSegmentIntoSlideScratch(int32_t segmentId,
                                      SRL::Math::Types::Vector3D& outCenter,
                                      FamilyIdVector& outFamilyIds,
                                      uint8_t designGeoTier = 0xFF);
    // Clear demobilized slot metadata after drop (walls, caches, geo tier).
    void DemobilizeSegmentSlotMetadata(SegmentRenderEntry& slot);
    void TryPrefetchUpcomingSegment();
    void PrimeRuntimeScratchCapacities();
    void ApplyActiveRendererCapacityFloor(TrackRenderer& renderer);
    void CaptureTrackTextureHeapBase();
    bool RebuildTrackTextureResidencyForWindow(bool forceStrongReset = false);
    bool ShouldRecycleTrackTextureHeap() const;
    bool ShouldCompactTrackTextureHeapInStabilization() const;
    void RecycleTrackTextureHeap();
    uint32_t EstimateWorkRamRetainedBytes() const;
    uint32_t EstimateLowWorkRamRetainedBytes() const;
    LowWorkCategoryBreakdown CaptureLowWorkBreakdown() const;
    bool TrimWorkRamRetainedCapacities(bool aggressive, int32_t* outFreeDelta);
    void AllocateWorkRamEmergencyReserve();
    void ReleaseWorkRamEmergencyReserve();
    void ReacquireWorkRamEmergencyReserve();
    bool ValidateAndRepairWindowState();
    void EmitWorkRamLivePointersTelemetry();
    MemoryPressureLevel ClassifyMemoryPressure(size_t freeBytes, bool freeValid) const;
    uint16_t ReleaseTrackFamilyResourcesImmediate(MemoryPressureLevel level);
    void RunWorkRamMaintenance(bool windowSlid);
    bool RunInitialMaintenanceStage();
    bool RunWindowStage(const SRL::Math::Types::Vector3D& carWorldPosition,
                        const SRL::Math::Types::Vector3D& trackOffset);
    void RunTextureCompactionStage(bool windowSlid);
    void RunPrefetchStage(bool windowSlid);
    void UpdatePrefetchSpeedProxy(const SRL::Math::Types::Vector3D& carWorldPosition);
    bool RunPostSlideMaintenanceStage(bool slidThisFrame);
    bool RunLegacyMaintenanceStage(bool windowSlid);
    void ResetFramePlan(TrackFramePlan& plan) const;
    void ApplyFramePlanLodTargets(const TrackFramePlan& plan);
    void PromoteLastValidFramePlanForCurrentFrame(bool markStale);
    void BuildAndApplyFramePlanStage(const SRL::Math::Types::Vector3D& trackOffset,
                                     const SRL::Math::Types::Vector3D& cameraLocation,
                                     const SRL::Math::Types::Vector3D& carWorldPosition);
    void BuildOrderedHandlesStage(const SRL::Math::Types::Vector3D& trackOffset,
                                  const SRL::Math::Types::Vector3D& cameraLocation,
                                  std::vector<SegmentHandle>& outOrderedHandles);
    bool RunWorkingSetStage();
    void RunDrawStage(const std::vector<SegmentHandle>* orderedHandles,
                      const SRL::Math::Types::Vector3D& trackOffset,
                      const SRL::Math::Types::Vector3D& lightDirection,
                      const SRL::Math::Types::Vector3D& cameraLocation,
                      std::array<uint8_t, kTrackSegmentLimit + 1>& preparedCountById,
                      std::array<uint8_t, kTrackSegmentLimit + 1>* renderedCountById,
                      bool& segment01Logged,
                      bool& segment01Prepared);
    void RefreshFamilyWorkingSet(bool releaseUnused);
    void ReleaseUnusedFamilyResourcesEndFrame();
    void EmitFamilyWorkingSetTelemetry() const;
    void MergeCurrentWindowFamilies();
    void ValidateStabilizedWindowInvariants();
    void TickRuntimeFrameCooldowns();
    bool ShouldRunPostSlideMaintenance(bool slidThisFrame) const;
    bool RunPendingLodRecoveryStage(bool slidThisFrame);
    const TrackLowWorkVector<SegmentHandle>& BuildStabilizedSortedHandles(
        const SRL::Math::Types::Vector3D& trackOffset,
        const SRL::Math::Types::Vector3D& cameraLocation);
    void RenderVisibleSegmentOrderStabilized(
        const SRL::Math::Types::Vector3D& trackOffset,
        const SRL::Math::Types::Vector3D& lightDirection,
        const SRL::Math::Types::Vector3D& cameraLocation,
        std::array<uint8_t, kTrackSegmentLimit + 1>& preparedCountById,
        std::array<uint8_t, kTrackSegmentLimit + 1>* renderedCountById,
        bool& segment01Prepared);
    bool UpdateActiveSegmentWindowForPosition(const SRL::Math::Types::Vector3D& worldPosition,
                                              const SRL::Math::Types::Vector3D& trackOffset);
    int8_t ResolveCameraWindowDirection(const SRL::Math::Types::Vector3D& trackOffset,
                                        const SRL::Math::Types::Vector3D& cameraLocation,
                                        const SRL::Math::Types::Vector3D& cameraLookTarget) const;
    void UpdateCameraDrivenWindowDirection(const SRL::Math::Types::Vector3D& trackOffset,
                                           const SRL::Math::Types::Vector3D& cameraLocation,
                                           const SRL::Math::Types::Vector3D& cameraLookTarget);
    std::vector<SegmentHandle> BuildVisibleSegmentOrder(const SRL::Math::Types::Vector3D& trackOffset,
                                                        const SRL::Math::Types::Vector3D& cameraLocation);
    void RunSeg1DiagnosticsForFrame();
    void RenderVisibleSegmentOrder(const std::vector<SegmentHandle>* orderedHandles,
                                   const SRL::Math::Types::Vector3D& trackOffset,
                                   const SRL::Math::Types::Vector3D& lightDirection,
                                   const SRL::Math::Types::Vector3D& cameraLocation,
                                   std::array<uint8_t, kTrackSegmentLimit + 1>& preparedCountById,
                                   std::array<uint8_t, kTrackSegmentLimit + 1>* renderedCountById,
                                   bool& segment01Logged,
                                   bool& segment01Prepared);
    bool ShouldRunFramePlanThisFrame(bool slidThisFrame);
    void RunFramePlanStage(const SRL::Math::Types::Vector3D& trackOffset,
                           const SRL::Math::Types::Vector3D& cameraLocation,
                           const SRL::Math::Types::Vector3D& carWorldPosition,
                           bool slidThisFrame);
    void FinalizeDrawStage(uint16_t frameTicksStart,
                           const std::array<uint8_t, kTrackSegmentLimit + 1>& preparedCountById,
                           const std::array<uint8_t, kTrackSegmentLimit + 1>* renderedCountById);
    void PresentCoordinatorTelemetryAndSoak();
    void PresentSh2UsageOverlay();
    void RunEndFrameResourceMaintenance();
    void UpdateAdaptiveBudgetAfterFrame();
    void PresentVdp1FpsTelemetry();
    void PresentPerFrameDebugOverlay();
    const TrackSegmentCopy* FindRawSegmentCopyById(int id) const;
    const char* FindExistingPath(const char* const* paths, size_t count);
    const char* ResolveSegmentPath(size_t id);
    TrackSegmentCopy CopySegmentById(size_t id);
    SegmentEntryVector CopyAllTrackSegments(size_t maxSegments);
    TrackLowWorkVector<SegmentRenderEntry> BuildSegmentRenderers(SegmentEntryVector& entries);
    void BuildSegmentHandleTable();
    void ResetInitializationState();
    size_t ResolveInitialLoadLimit(const Config& config) const;
    void PrepareInitialSegmentPackages(size_t loadLimit);
    void ConfigureCoordinatorAndBudget(const Config& config);
    void LoadSurfaceCollisionMaps();
    void ApplyTrackSlaveMode();
    void LogInitialSegmentDiagnostics() const;
    void ApplyInitialSdrFamilySlots();

    static constexpr std::array<const char*, 28> kSegmentPathTemplates_ = {{
        "CD/SETORES/SEG_%03u.NYA",
        "CD/SETORES/SEG_%03u.NYA;1",
        "/SETORES/SEG_%03u.NYA",
        "/SETORES/SEG_%03u.NYA;1",
        "CD/DATA/SETORES/SEG_%03u.NYA",
        "CD/DATA/SETORES/SEG_%03u.NYA;1",
        "cd/data/SETORES/SEG_%03u.NYA",
        "cd/data/SETORES/SEG_%03u.NYA;1",
        "cd/data/setores/seg_%03u.nya",
        "cd/data/setores/seg_%03u.nya;1",
        "cd/setores/SEG_%03u.NYA",
        "cd/setores/SEG_%03u.NYA;1",
        "SETORES/SEG_%03u.NYA",
        "SETORES/SEG_%03u.NYA;1",
        "setores/seg_%03u.nya",
        "setores/seg_%03u.nya;1",
        "CD/DATA/SEG_%03u.NYA",
        "CD/DATA/SEG_%03u.NYA;1",
        "cd/data/SEG_%03u.NYA",
        "cd/data/SEG_%03u.NYA;1",
        "SEG_%03u.NYA",
        "SEG_%03u.NYA;1",
        "SEG/SEG_%03u.NYA",
        "SEG/SEG_%03u.NYA;1",
        "BuildDrop/Interlagos_racing/SEG_%03u.NYA",
        "BuildDrop/Interlagos_racing/SEG_%03u.NYA;1",
        "CD/SEG_%03u.NYA",
        "cd/seg_%03u.nya"
    }};

    char lastSegmentPath_[128]{};
    uint16_t fixedVisibleSegmentCap_ = 1;
    uint16_t totalSegmentCount_ = 0;
    int16_t activeWindowStartId_ = 1;
    uint16_t activeWindowHead_ = 0;
    int8_t windowDirection_ = 1;
    int8_t cameraWindowDirection_ = 1;
    // Camera direction switch stabilization to avoid render window flicker on 360 turns.
    int8_t cameraDirectionPending_ = 1;
    uint8_t cameraDirectionConfirmFrames_ = 0;
    uint8_t cameraDirectionFlipCooldown_ = 0;
    uint8_t activeWindowSwitchCooldown_ = 0;
    int16_t targetWindowStartId_ = 1;
    int16_t trackedCarSegmentId_ = 1;
    int16_t observedCarSegmentId_ = -1;
    // Snapshot for seam two-pass near classification (set each RenderFrame).
    SRL::Math::Types::Vector3D seamCarWorldPosition_{};
    bool seamCarWorldValid_ = false;
    int32_t lastLapWrapProbeSegmentId_ = -1;
    uint8_t lapWrapScrubCooldown_ = 0;
    uint16_t slotFaceCapacityFloor_ = 0;
    uint16_t familySlotCapacityFloor_ = 0;
    uint16_t rendererVertexCapacityFloor_ = 0;
    uint16_t rendererFaceCapacityFloor_ = 0;
    CenterCatalogVector segmentCenterCatalog_{};
    // === FIXED SLOT POOL ===
    // Slots pré-alocados em LWR: N ativos + 1 staging.
    // Em leak-isolation mode: 20 ativos + 1 staging = 21 slots.
    // Em produção (todos os LOD bands): 20 ativos + 1 staging = 21 slots.
    // Slides são realizados como permutação de ponteiros O(1), sem malloc/free.
    // NOTA: o pool ainda não está alocado — vide RebuildActiveSegmentWindow.
    static constexpr size_t kSlotPoolSize = 21; // leak-isolation window: 20 active + 1 staging
    uint8_t stagingSlotIdx_ = 20;               // = kSlotPoolSize - 1
    std::array<SegmentRenderEntry*, kSlotPoolSize> slotPool_{};
    // === FIM FIXED SLOT POOL ===
    // === SLIDE SCRATCH BUFFERS (persistent LWR — evita alloc/free por slide no caminho estabilizado) ===
    // slideScratchEntry_ substitui o local "SegmentRenderEntry incomingPrepared{}" em
    // ExecuteDeterministicStabilizedSlide e PrepareStabilizedSlideBackBuffer.
    // Seus vetores são trocados com os slots via swap() — nenhum free de LWR por slide.
    SegmentRenderEntry slideScratchEntry_{};
    std::array<TrackLowWorkI16Vector, 3> slideScratchBoundarySlots_{};
    // === FIM SLIDE SCRATCH BUFFERS ===

    TrackLowWorkUniquePtr<TrackRenderer> slideScratchRenderer_{};
    FamilyIdVector slideIncomingFamilyIdsScratch_{};
    FaceRankOffsetVector slideIncomingFaceRankOffsetsScratch_{};
    TrackLowWorkI16Vector slideIncomingFaceSlotsScratch_{};
    TrackLowWorkI16Vector slideRollbackFaceSlotsScratch_{};
    TrackLowWorkI16Vector runtimeRenderFaceSlotsScratch_{};
    SlideBackBuffer slideBackBuffer_{};
    int32_t slidePrefetchSegmentId_ = -1;
    SRL::Math::Types::Vector3D slidePrefetchCenter_{};
    FamilyIdVector slidePrefetchFamilyIds_{};
    TrackLowWorkI16Vector slidePrefetchFaceSlots_{};
    TrackLowWorkUniquePtr<TrackRenderer> slidePrefetchRenderer_{};
    uint16_t trackTextureHeapBase_ = 0;
    uint16_t trackTextureRecycleCount_ = 0;
    uint8_t textureUploadsThisFrame_ = 0;
    static constexpr uint8_t kTextureUploadsBudgetPerFrame = 4;
    uint8_t GetTextureUploadBudgetPerFrame() const;
    uint8_t runtimeRdrBuildsThisFrame_ = 0;
    uint8_t runtimeSdrBuildsThisFrame_ = 0;
    uint8_t runtimeFaceRemapsThisFrame_ = 0;
    uint8_t runtimeSlidesThisFrame_ = 0;
    uint8_t runtimeSlideStallsThisFrame_ = 0;
    uint8_t runtimePrefetchHitsThisFrame_ = 0;
    uint8_t runtimePrefetchMissesThisFrame_ = 0;
    uint8_t runtimeLodSegmentUpdatesThisFrame_ = 0;
    uint8_t runtimeSafeRenderedThisFrame_ = 0;
    uint8_t runtimeSafeSkippedThisFrame_ = 0;
    uint8_t runtimeSafeNoDrawThisFrame_ = 0;
    uint8_t runtimeSafeReappliedThisFrame_ = 0;
    mutable uint16_t surfaceQueryCallsThisFrame_ = 0;
    mutable uint16_t surfaceQueryFallbackHitsThisFrame_ = 0;
    mutable uint16_t surfaceQueryGlobalPassesThisFrame_ = 0;
    mutable uint16_t surfaceQueryLocalOnlyMissesThisFrame_ = 0;
    mutable uint16_t surfaceQueryScmapSkipsThisFrame_ = 0;
    mutable uint16_t surfaceQuerySegmentsScannedThisFrame_ = 0;
    mutable uint16_t surfaceQueryFacesScannedThisFrame_ = 0;
    mutable uint16_t surfaceQueryCacheHitsThisFrame_ = 0;
    mutable uint16_t surfaceQueryCacheMissesThisFrame_ = 0;
    uint16_t surfaceQueryCallsLastFrame_ = 0;
    uint16_t surfaceQueryFallbackHitsLastFrame_ = 0;
    uint16_t surfaceQueryGlobalPassesLastFrame_ = 0;
    uint16_t surfaceQueryLocalOnlyMissesLastFrame_ = 0;
    uint16_t surfaceQueryScmapSkipsLastFrame_ = 0;
    uint16_t surfaceQuerySegmentsScannedLastFrame_ = 0;
    uint16_t surfaceQueryFacesScannedLastFrame_ = 0;
    uint16_t surfaceQueryCacheHitsLastFrame_ = 0;
    uint16_t surfaceQueryCacheMissesLastFrame_ = 0;
    mutable uint16_t wallQueryCallsThisFrame_ = 0;
    mutable uint16_t wallQueryHitsThisFrame_ = 0;
    mutable uint16_t wallQuerySegmentsScannedThisFrame_ = 0;
    mutable uint16_t wallQueryFacesScannedThisFrame_ = 0;
    uint16_t wallQueryCallsLastFrame_ = 0;
    uint16_t wallQueryHitsLastFrame_ = 0;
    uint16_t wallQuerySegmentsScannedLastFrame_ = 0;
    uint16_t wallQueryFacesScannedLastFrame_ = 0;
    mutable SRL::Math::Types::Vector3D wallQueryPrevWorldPosition_{};
    mutable int16_t surfaceQueryLastInsideSegmentId_ = -1;
    mutable int16_t surfaceQueryLastInsideFaceIndex_ = -1;
    mutable uint16_t surfaceQueryLastInsideFamilyId_ = 0u;
    mutable uint8_t surfaceQueryLastInsideType_ = 0u;
    TrackLowWorkU8Vector surfaceTypeByFamilyId_{};
    TrackLowWorkU8Vector segmentSurfaceFlagsById_{};
    // FSMAP remains in expansion Cart RAM; only these two scalars consume
    // Work RAM.  Queries parse records in place and never clone face data.
    void* faceSurfaceMapCartPtr_ = nullptr;
    uint32_t faceSurfaceMapCartBytes_ = 0u;
    mutable uint32_t stateFlags_ =
        (kTrackSlaveModeRequestedBit |
         kTrackSlaveProducerRequestedBit |
         kTrackSlaveDepthSortRequestedBit |
         kActiveWindowLookupDirtyBit |
         kFamilyWorkingSetDirtyBit |
         kFamilySlotIndexDirtyBit);
    uint16_t sh2MasterStreamTicksThisFrame_ = 0;
    uint16_t sh2MasterDrawTicksThisFrame_ = 0;
    uint16_t sh2MasterFrameTicksThisFrame_ = 0;
    uint16_t sh2MasterMaintenanceTicksThisFrame_ = 0;
    uint16_t sh2MasterWindowTicksThisFrame_ = 0;
    uint16_t sh2MasterPrefetchTicksThisFrame_ = 0;
    uint16_t sh2MasterPlanTicksThisFrame_ = 0;
    uint16_t sh2MasterLodTicksThisFrame_ = 0;
    uint16_t sh2MasterWorkingSetTicksThisFrame_ = 0;
    uint16_t sh2SlavePlanTicksThisFrame_ = 0;
    uint16_t sh2SlaveSortTicksThisFrame_ = 0;
    uint8_t sh2ProducerListUsedThisFrame_ = 0;
    uint8_t sh2ProducerListFallbacksThisFrame_ = 0;
    uint32_t frameIdThisFrame_ = 0;
    struct FrameMemoryTelemetryState
    {
        uint32_t phaseHwrBeforeStream = 0;
        uint32_t phaseHwrAfterStream = 0;
        uint32_t phaseHwrAfterDraw = 0;
        uint32_t phaseHwrEnd = 0;
        uint32_t phaseLwrBeforeStream = 0;
        uint32_t phaseLwrAfterStream = 0;
        uint32_t phaseLwrAfterDraw = 0;
        uint32_t phaseLwrEnd = 0;
        LowWorkCategoryBreakdown lowWorkBreakdownEnd{};
        int32_t drawPrepareDeltaThisFrame = 0;
        int32_t drawExecuteDeltaThisFrame = 0;
        int32_t drawOtherDeltaThisFrame = 0;
        int32_t drawFrameDeltaThisFrame = 0;
    };
    FrameMemoryTelemetryState frameMemoryTelemetry_{};

    struct SlideHwrTraceState
    {
        int32_t segmentId = -1;
        uint32_t check = 0;
        uint32_t afterTrim = 0;
        uint32_t afterResetPrefetch = 0;
        uint32_t afterBuildPrefetch = 0;
        uint32_t afterPrepare = 0;
        uint32_t afterCommit = 0;
        uint8_t flags = 0;
    };
    SlideHwrTraceState slideHwrTrace_{};
    uint8_t prewarmCooldown_ = 0;
    uint8_t boundaryPrewarmCooldown_ = 0;
    uint8_t prefetchRetryCooldown_ = 0;
    uint8_t prefetchBuildAttemptsThisFrame_ = 0;
    uint8_t prefetchBuildBudgetThisFrame_ = 1;
    uint8_t prefetchBuildBudgetDropsThisFrame_ = 0;
    uint16_t prefetchSpeedProxyRaw_ = 0;
    SRL::Math::Types::Vector3D prefetchLastCarWorldPosition_{};
    uint8_t textureHeapCompactCooldown_ = 0;
    uint8_t workRamTrimCooldown_ = 0;
    uint8_t workRamWindowRebuildCooldown_ = 0;
    uint8_t workRamTelemetryCooldown_ = 0;
    uint8_t lodDegradeCooldown_ = 0;
    uint8_t pendingLodCursor_ = 0;
    uint8_t pendingLodFrameCooldown_ = 0;
    std::array<uint8_t, kTrackSegmentLimit> pendingLodRetryCooldowns_{};
    struct WorkRamMaintenanceState
    {
        uint16_t workRamRepairCount = 0;
        uint16_t workRamEmergencyReserveReleases = 0;
        uint16_t releasedNowSlotsThisFrame = 0;
        uint16_t releasedEndFrameSlotsThisFrame = 0;
        uint16_t releasedPrefetchNowThisFrame = 0;
        uint8_t memoryPressureLevelThisFrame = 0;
    };
    WorkRamMaintenanceState workRamMaintenance_{};
    struct Sh2PerfBucket
    {
        uint16_t sampleFrames = 0;
        uint16_t samplesAccum = 0;
        uint32_t sumMasterFrameTicks = 0;
        uint32_t sumMasterDrawTicks = 0;
        uint32_t sumProducerTicks = 0;
        uint32_t sumSortTicks = 0;
        uint16_t sumProducerFallbacks = 0;
        uint16_t sumProducerListUsed = 0;
        uint16_t sumProducerListFallbacks = 0;
        uint16_t avgMasterFrameTicks = 0;
        uint16_t avgMasterDrawTicks = 0;
        uint16_t avgProducerTicks = 0;
        uint16_t avgSortTicks = 0;
        uint16_t avgProducerFallbacks = 0;
        uint16_t avgProducerListUsed = 0;
        uint16_t avgProducerListFallbacks = 0;
    };
    static constexpr uint16_t kSh2PerfSampleWindowFrames = 120u;
    struct RuntimeDiagnosticsState
    {
        static constexpr uint8_t kRuntimeStatsLogsEnabledBit = 1u << 0;
        static constexpr uint8_t kLeakProbePrevValidBit = 1u << 1;

        Sh2PerfBucket sh2PerfSingle{}; // Master only
        Sh2PerfBucket sh2PerfDual{};   // Master + Slave
        uint32_t leakProbePrevHwrFree = 0;
        uint32_t leakProbePrevLwrFree = 0;
        uint32_t leakProbePrevRetainedHwr = 0;
        uint32_t leakProbePrevRetainedLwr = 0;
        uint32_t lowWorkBaselineFree = 0;
        uint16_t leakProbeSlidesObserved = 0;
        uint8_t flags = 0u;

        bool RuntimeStatsLogsEnabled() const { return (flags & kRuntimeStatsLogsEnabledBit) != 0u; }
        bool LeakProbePrevValid() const { return (flags & kLeakProbePrevValidBit) != 0u; }
        void SetRuntimeStatsLogsEnabled(bool enabled)
        {
            if (enabled) flags |= kRuntimeStatsLogsEnabledBit;
            else flags &= static_cast<uint8_t>(~kRuntimeStatsLogsEnabledBit);
        }
        void SetLeakProbePrevValid(bool enabled)
        {
            if (enabled) flags |= kLeakProbePrevValidBit;
            else flags &= static_cast<uint8_t>(~kLeakProbePrevValidBit);
        }
    };
    RuntimeDiagnosticsState runtimeDiagnostics_{};
    size_t lastWindowFreeBytes_ = 0;
    std::array<uint8_t, kTrackSegmentLimit> pendingLodRankFlags_{};
    mutable TrackLowWorkI16Vector familySlotIndex_{};
    mutable std::array<int8_t, kWindowSegmentIdDirectIndexCap> windowEntryIndexBySegmentId_{};
    mutable std::array<int8_t, kWindowSegmentIdDirectIndexCap> windowLogicalRankBySegmentId_{};
    uint8_t familyMergeCooldown_ = 0;
    std::array<uint8_t, SRL_MAX_TEXTURES> usedTextureSlotsThisFrame_{};
    TrackLowWorkI16Vector activeWindowLookupSegmentIds_{};
    TrackLowWorkI8Vector activeWindowEntryIndexBySegmentId_{};
    TrackLowWorkI8Vector activeWindowLogicalRankBySegmentId_{};
    void* workRamEmergencyReserve_ = nullptr;
    uint32_t workRamEmergencyReserveBytes_ = 0;

    SegmentEntryVector segmentEntries_{};
    RawSegmentVector rawSegmentCatalog_{};
    TrackLowWorkVector<SegmentRenderEntry> segmentRenderers_{};
    SRL::Math::Types::Vector3D seg1ComponentCenter_{};
    TrackLowWorkVector<SRL::Math::Types::Vector3D> seg1ComponentVerts_{};
    TrackLowWorkVector<SRL::Types::Polygon> seg1ComponentFaces_{};
    TrackLowWorkVector<SRL::Types::Attribute> seg1ComponentAttrs_{};
    FamilyIdCatalogVector seg1FaceFamilyIds_{};
    FamilySlotVector seg1FamilySlots_{};
    FamilySlotVector familyMergeCurrentWindowScratch_{};
    FamilySlotVector slidePrefetchFamilySlotsScratch_{};
    std::array<Seg1TexbankCart, 4> seg1Texbanks_{};
    TrackLowWorkVector<Seg1TgaCartEntry> seg1TgaCatalog_{};
    std::array<TrackLowWorkI16Vector, 4> seg1RendererFaceSlotsByLod_{};
    uint16_t seg1TgaPreloadCount_ = 0;
    uint16_t seg1TgaAttemptCount_ = 0;
    uint16_t seg1TgaFailCount_ = 0;
    uint8_t seg1TgaJsonOk_ = 0;
    uint16_t seg1SingleFaceSwapCounter_ = 0;
    uint16_t seg1SingleFaceSwapFrames_ = 180; // ~3s @60fps
    int16_t seg1SingleFaceSwapFace_ = -1;
    int16_t seg1SingleFaceSwapBaseSlot_ = -1;
    int16_t seg1SingleFaceSwapAltSlot_ = -1;
    TrackLowWorkI16Vector seg1SingleFaceSlots_{};
    uint8_t seg1CurrentLodIndex_ = 0;
    uint16_t seg1LodFrameCounter_ = 0;
    uint16_t seg1LodSwapFrames_ = 60; // ~1s @60fps (teste visual)
    SegmentPool segmentPool_{};
    TrackLowWorkVector<SegmentHandle> segmentHandles_{};
    TrackFramePlan framePlanCurrent_{};
    TrackFramePlan framePlanLastValid_{};
    TrackLowWorkVector<SegmentHandle> framePlanSortedHandles_{};
    TrackLowWorkVector<SegmentHandle> framePlanLastValidSortedHandles_{};
    TrackLowWorkVector<TrackDepthSortItem<SegmentHandle, int64_t>> stabilizedDepthItemsScratch_{};
    TrackLowWorkVector<SegmentHandle> stabilizedSortedHandlesScratch_{};
    std::array<uint8_t, kTrackSegmentLimit + 1> lastSortRank_{};
    SlaveTrackDepthSorter<SegmentHandle, int64_t, kTrackSegmentLimit> stabilizedDepthSorter_{};
    TrackDrawProducerStats stabilizedDepthStats_{};
    SlaveTrackDrawProducer<SegmentHandle, kTrackSegmentLimit> producer_{};
    TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit> coordinator_{producer_};
    AdaptiveTrackBudgetController budgetController_{};
    SoakMonitor soakMonitor_{};
};
