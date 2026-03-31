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
using TrackLowWorkI16Vector = TrackLowWorkVector<int16_t>;
using TrackHighWorkI16Vector = TrackHighWorkVector<int16_t>;

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
    static constexpr size_t kTrackSegmentLimit = 30;

    struct Config
    {
        // Initial visible segment count before adaptive budget tuning.
        uint16_t initialSegments = 30;
        uint16_t minSegments = 19;
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
                     const SRL::Math::Types::Vector3D& carWorldPosition);
    void SetObservedCarSegmentId(int32_t segmentId);

    // Present telemetry and update adaptive budget targets for next frame.
    void EndFrame();

    // Resolve nearest loaded segment for simple collision and gameplay queries.
    bool FindNearestSegment(const SRL::Math::Types::Vector3D& worldPosition,
                            const SRL::Math::Types::Vector3D& trackOffset,
                            int32_t& outSegmentId,
                            SRL::Math::Types::Vector3D& outSegmentCenter) const;
    bool FindSegmentCenterById(int32_t segmentId,
                               const SRL::Math::Types::Vector3D& trackOffset,
                               SRL::Math::Types::Vector3D& outSegmentCenter) const;

    bool Ready() const { return ready_; }
    const char* LastResolvedPath() const { return lastSegmentPath_; }
    const FrameTelemetry& Telemetry() const { return coordinator_.Telemetry(); }
    uint16_t SegmentCount() const { return totalSegmentCount_; }
    bool HasSmoothSegments() const;
    uint32_t MaxSegmentFaceCount() const;
    uint32_t MaxSegmentVertexCount() const;
    int32_t LowWorkDrawPrepareDeltaThisFrame() const { return lowWorkDrawPrepareDeltaThisFrame_; }
    int32_t LowWorkDrawExecuteDeltaThisFrame() const { return lowWorkDrawExecuteDeltaThisFrame_; }
    int32_t LowWorkDrawOtherDeltaThisFrame() const { return lowWorkDrawOtherDeltaThisFrame_; }
    int32_t LowWorkDrawFrameDeltaThisFrame() const { return lowWorkDrawFrameDeltaThisFrame_; }
    uint32_t LowWorkEndFreeBytesThisFrame() const { return phaseLwrEnd_; }
    uint8_t SlidesThisFrame() const { return runtimeSlidesThisFrame_; }
    int16_t SlideSegmentIdThisFrame() const { return slideHwrTraceSegmentId_; }
    LowWorkCategoryBreakdown LowWorkBreakdownThisFrame() const { return lowWorkBreakdownEnd_; }
    uint16_t StreamTicksThisFrame() const { return sh2MasterStreamTicksThisFrame_; }
    uint16_t MaintenanceTicksThisFrame() const { return sh2MasterMaintenanceTicksThisFrame_; }
    uint16_t DrawTicksThisFrame() const { return sh2MasterDrawTicksThisFrame_; }
    uint16_t FrameTicksThisFrame() const { return sh2MasterFrameTicksThisFrame_; }
    uint16_t WindowTicksThisFrame() const { return sh2MasterWindowTicksThisFrame_; }
    uint16_t PrefetchTicksThisFrame() const { return sh2MasterPrefetchTicksThisFrame_; }
    uint16_t LodTicksThisFrame() const { return sh2MasterLodTicksThisFrame_; }
    uint16_t WorkingSetTicksThisFrame() const { return sh2MasterWorkingSetTicksThisFrame_; }
    void SetRuntimeStatsLogsEnabled(bool enabled) { runtimeStatsLogsEnabled_ = enabled; }
    bool RuntimeStatsLogsEnabled() const { return runtimeStatsLogsEnabled_; }

private:
    enum class MemoryPressureLevel : uint8_t
    {
        Normal = 0,
        Pressure = 1,
        Critical = 2,
    };

    struct TrackSegmentEntry
    {
        int16_t id = 0;
        TrackSegmentCopy copy;
    };

    struct SegmentRenderEntry
    {
        struct SegmentLodState
        {
            bool ready = false;
            bool hasPerFaceRankOffsets = false;
            // Resident state currently visible in the renderer.
            uint8_t currentLodIndex = 0xFF; // 0:8, 1:16, 2:32, 3:64
            int16_t currentBaseRank = -1;
            // Desired state derived from the logical rank in the sliding window.
            uint8_t desiredLodIndex = 0xFF;
            int16_t desiredBaseRank = -1;
            bool workingSetCacheDirty = true;
            TrackLowWorkU16Vector faceFamilyIds{};
            TrackLowWorkU8Vector faceRankOffsets{};
            TrackLowWorkI16Vector currentFaceSlots{};
            TrackLowWorkU16Vector workingSetFamilies{};
            TrackLowWorkU8Vector workingSetLodIndices{};
            TrackLowWorkI16Vector workingSetSlots{};
        };

        int16_t id = 0;
        uint8_t logicalSegmentCount = 0;
        TrackLowWorkUniquePtr<TrackRenderer> renderer;
        SRL::Math::Types::Vector3D center{};
        SegmentLodState lodState{};
    };
    struct RawSegmentEntry
    {
        int16_t id = 0;
        TrackSegmentCopy copy{};
    };
    struct Seg1FamilySlotEntry
    {
        uint16_t familyId = 0;
        std::array<uint16_t, 4> lodSlots{{0, 0, 0, 0}}; // 0:8, 1:16, 2:32, 3:64
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
        bool active = false;
        int16_t segmentId = -1;
        uint8_t desiredLodIndex = 0xFF;
        int16_t desiredBaseRank = -1;
        TrackLowWorkI16Vector preparedFaceSlots{};
    };
    struct SlideBackBuffer
    {
        bool ready = false;
        int8_t direction = 1;
        size_t dropIdx = 0;
        int16_t incomingSegmentId = -1;
        int16_t outgoingSegmentId = -1;
        int16_t nextStartId = 1;
        SRL::Math::Types::Vector3D incomingCenter{};
        TrackLowWorkU16Vector incomingFamilyIds{};
        TrackLowWorkI16Vector incomingFaceSlots{};
        uint8_t incomingResidentLodIndex = 0xFF;
        int16_t incomingResidentBaseRank = -1;
        std::array<SlideBoundaryUpdate, 4> boundaryUpdates{};
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
    SegmentRenderEntry* FindWindowEntryByIdFast(int32_t segmentId);
    const SegmentRenderEntry* FindWindowEntryByIdFast(int32_t segmentId) const;
    void UpdateDesiredStabilizedWindowLodTargets();
    void InvalidateEntryWorkingSetCache(SegmentRenderEntry& entry);
    bool RebuildEntryWorkingSetCache(SegmentRenderEntry& entry);
    void RebuildUsedTextureSlotFlagsFromWorkingRefs();
    uint32_t GetStrictPendingLodPriority(size_t logicalRank) const;
    bool HasPendingStabilizedWindowLodChanges() const;
    void ResetPendingStabilizedLodRanks();
    void QueuePendingStabilizedLodRank(size_t logicalRank);
    void SeedPendingStabilizedLodRanksForWindow();
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
    void PrewarmNextSegmentLod8();
    void PrewarmUpcomingBoundaryLods();
    void ResetSlidePrefetchState();
    bool BuildSegmentIntoPrefetch(int32_t segmentId, bool allowSlotWarmup = true);
    bool BuildSegmentIntoRenderer(int32_t segmentId,
                                  TrackRenderer& renderer,
                                  SRL::Math::Types::Vector3D& outCenter,
                                  FamilyIdVector& outFamilyIds);
    bool BuildSegmentIntoSlideScratch(int32_t segmentId,
                                      SRL::Math::Types::Vector3D& outCenter,
                                      FamilyIdVector& outFamilyIds);
    void TryPrefetchUpcomingSegment();
    void PrimeRuntimeScratchCapacities();
    void ApplyActiveRendererCapacityFloor(TrackRenderer& renderer);
    void CaptureTrackTextureHeapBase();
    bool RebuildTrackTextureResidencyForWindow();
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
    void RefreshFamilyWorkingSet(bool releaseUnused);
    void ReleaseUnusedFamilyResourcesEndFrame();
    void EmitFamilyWorkingSetTelemetry() const;
    void MergeCurrentWindowFamilies();
    void ValidateStabilizedWindowInvariants();
    bool UpdateActiveSegmentWindowForPosition(const SRL::Math::Types::Vector3D& worldPosition,
                                              const SRL::Math::Types::Vector3D& trackOffset);
    std::vector<SegmentHandle> BuildVisibleSegmentOrder(const SRL::Math::Types::Vector3D& trackOffset,
                                                        const SRL::Math::Types::Vector3D& cameraLocation);
    void RunSeg1DiagnosticsForFrame();
    void RenderVisibleSegmentOrder(const std::vector<SegmentHandle>& orderedHandles,
                                   const SRL::Math::Types::Vector3D& trackOffset,
                                   const SRL::Math::Types::Vector3D& lightDirection,
                                   const SRL::Math::Types::Vector3D& cameraLocation,
                                   std::array<uint8_t, kTrackSegmentLimit + 1>& preparedCountById,
                                   std::array<uint8_t, kTrackSegmentLimit + 1>& renderedCountById,
                                   bool& segment01Logged,
                                   bool& segment01Prepared);
    const TrackSegmentCopy* FindRawSegmentCopyById(int id) const;
    const char* FindExistingPath(const char* const* paths, size_t count);
    const char* ResolveSegmentPath(size_t id);
    TrackSegmentCopy CopySegmentById(size_t id);
    SegmentEntryVector CopyAllTrackSegments(size_t maxSegments);
    TrackLowWorkVector<SegmentRenderEntry> BuildSegmentRenderers(SegmentEntryVector& entries);
    TrackLowWorkVector<SegmentHandle> BuildSegmentHandleTable();
    void ResetInitializationState();
    size_t ResolveInitialLoadLimit(const Config& config) const;
    void PrepareInitialSegmentPackages(size_t loadLimit);
    void ConfigureCoordinatorAndBudget(const Config& config);
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
    bool ready_ = false;
    bool segmentsReady_ = false;
    bool coordinatorReady_ = false;
    uint16_t fixedVisibleSegmentCap_ = 1;
    uint16_t totalSegmentCount_ = 0;
    int16_t activeWindowStartId_ = 1;
    uint16_t activeWindowHead_ = 0;
    int8_t windowDirection_ = 1;
    uint8_t activeWindowSwitchCooldown_ = 0;
    int16_t targetWindowStartId_ = 1;
    int16_t trackedCarSegmentId_ = 1;
    bool trackedCarSegmentValid_ = false;
    int16_t observedCarSegmentId_ = -1;
    int16_t lastLapWrapProbeSegmentId_ = -1;
    uint8_t lapWrapScrubCooldown_ = 0;
    uint16_t slotFaceCapacityFloor_ = 0;
    uint16_t rendererVertexCapacityFloor_ = 0;
    uint16_t rendererFaceCapacityFloor_ = 0;
    CenterCatalogVector segmentCenterCatalog_{};
    TrackLowWorkUniquePtr<TrackRenderer> slideScratchRenderer_{};
    FamilyIdVector slideIncomingFamilyIdsScratch_{};
    FaceRankOffsetVector slideIncomingFaceRankOffsetsScratch_{};
    TrackLowWorkI16Vector slideIncomingFaceSlotsScratch_{};
    FamilyIdVector slideRollbackFamilyIdsScratch_{};
    FaceRankOffsetVector slideRollbackFaceRankOffsetsScratch_{};
    TrackLowWorkI16Vector slideRollbackFaceSlotsScratch_{};
    TrackHighWorkI16Vector runtimeRenderFaceSlotsScratch_{};
    SlideBackBuffer slideBackBuffer_{};
    int16_t slidePrefetchSegmentId_ = -1;
    SRL::Math::Types::Vector3D slidePrefetchCenter_{};
    FamilyIdVector slidePrefetchFamilyIds_{};
    TrackLowWorkI16Vector slidePrefetchFaceSlots_{};
    TrackLowWorkUniquePtr<TrackRenderer> slidePrefetchRenderer_{};
    bool slidePrefetchRendererReady_ = false;
    bool slidePrefetchLod8Ready_ = false;
    uint16_t trackTextureHeapBase_ = 0;
    bool trackTextureHeapBaseValid_ = false;
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
    uint16_t sh2MasterStreamTicksThisFrame_ = 0;
    uint16_t sh2MasterDrawTicksThisFrame_ = 0;
    uint16_t sh2MasterFrameTicksThisFrame_ = 0;
    uint16_t sh2MasterMaintenanceTicksThisFrame_ = 0;
    uint16_t sh2MasterWindowTicksThisFrame_ = 0;
    uint16_t sh2MasterPrefetchTicksThisFrame_ = 0;
    uint16_t sh2MasterLodTicksThisFrame_ = 0;
    uint16_t sh2MasterWorkingSetTicksThisFrame_ = 0;
    uint32_t phaseHwrBeforeStream_ = 0;
    uint32_t phaseHwrAfterStream_ = 0;
    uint32_t phaseHwrAfterDraw_ = 0;
    uint32_t phaseHwrEnd_ = 0;
    uint32_t phaseLwrBeforeStream_ = 0;
    uint32_t phaseLwrAfterStream_ = 0;
    uint32_t phaseLwrAfterDraw_ = 0;
    uint32_t phaseLwrEnd_ = 0;
    LowWorkCategoryBreakdown lowWorkBreakdownEnd_{};
    int32_t lowWorkDrawPrepareDeltaThisFrame_ = 0;
    int32_t lowWorkDrawExecuteDeltaThisFrame_ = 0;
    int32_t lowWorkDrawOtherDeltaThisFrame_ = 0;
    int32_t lowWorkDrawFrameDeltaThisFrame_ = 0;
    int16_t slideHwrTraceSegmentId_ = -1;
    uint8_t slideHwrTraceFlags_ = 0;
    uint32_t slideHwrTraceCheck_ = 0;
    uint32_t slideHwrTraceAfterTrim_ = 0;
    uint32_t slideHwrTraceAfterResetPrefetch_ = 0;
    uint32_t slideHwrTraceAfterBuildPrefetch_ = 0;
    uint32_t slideHwrTraceAfterPrepare_ = 0;
    uint32_t slideHwrTraceAfterCommit_ = 0;
    uint8_t prewarmCooldown_ = 0;
    uint8_t boundaryPrewarmCooldown_ = 0;
    uint8_t prefetchRetryCooldown_ = 0;
    uint8_t textureHeapCompactCooldown_ = 0;
    uint8_t workRamTrimCooldown_ = 0;
    uint8_t workRamWindowRebuildCooldown_ = 0;
    uint8_t workRamTelemetryCooldown_ = 0;
    uint8_t lodDegradeCooldown_ = 0;
    uint8_t pendingLodCursor_ = 0;
    uint8_t pendingLodFrameCooldown_ = 0;
    std::array<uint8_t, kTrackSegmentLimit> pendingLodRetryCooldowns_{};
    uint16_t workRamRepairCount_ = 0;
    uint16_t workRamEmergencyReserveReleases_ = 0;
    uint16_t releasedNowSlotsThisFrame_ = 0;
    uint16_t releasedEndFrameSlotsThisFrame_ = 0;
    uint16_t releasedPrefetchNowThisFrame_ = 0;
    uint8_t memoryPressureLevelThisFrame_ = 0;
    bool runtimeStatsLogsEnabled_ = true;
    bool fullTrackFamilyCacheReady_ = false;
    size_t lastWindowFreeBytes_ = 0;
    bool lastWindowFreeValid_ = false;
    bool activeWindowLookupDirty_ = true;
    bool familyWorkingSetDirty_ = true;
    std::array<uint8_t, kTrackSegmentLimit> pendingLodRankFlags_{};
    bool pendingLodWorkExists_ = false;
    mutable std::array<int16_t, 4096> familySlotIndex_{};
    mutable bool familySlotIndexDirty_ = true;
    uint8_t familyMergeCooldown_ = 0;
    std::array<uint8_t, SRL_MAX_TEXTURES> usedTextureSlotsThisFrame_{};
    TrackLowWorkVector<int16_t> activeWindowEntryIndexBySegmentId_{};
    TrackLowWorkVector<int16_t> activeWindowLogicalRankBySegmentId_{};
    void* workRamEmergencyReserve_ = nullptr;
    uint32_t workRamEmergencyReserveBytes_ = 0;

    SegmentEntryVector segmentEntries_{};
    RawSegmentVector rawSegmentCatalog_{};
    TrackLowWorkVector<SegmentRenderEntry> segmentRenderers_{};
    bool seg1ComponentEnabled_ = false;
    SRL::Math::Types::Vector3D seg1ComponentCenter_{};
    TrackLowWorkVector<SRL::Math::Types::Vector3D> seg1ComponentVerts_{};
    TrackLowWorkVector<SRL::Types::Polygon> seg1ComponentFaces_{};
    TrackLowWorkVector<SRL::Types::Attribute> seg1ComponentAttrs_{};
    FamilyIdCatalogVector seg1FaceFamilyIds_{};
    FamilySlotVector seg1FamilySlots_{};
    FamilySlotVector familyMergeCurrentWindowScratch_{};
    FamilySlotVector familyMergeNextScratch_{};
    FamilySlotVector slidePrefetchFamilySlotsScratch_{};
    std::array<Seg1TexbankCart, 4> seg1Texbanks_{};
    TrackLowWorkVector<Seg1TgaCartEntry> seg1TgaCatalog_{};
    std::array<TrackLowWorkI16Vector, 4> seg1RendererFaceSlotsByLod_{};
    uint16_t seg1TgaPreloadCount_ = 0;
    uint16_t seg1TgaAttemptCount_ = 0;
    uint16_t seg1TgaFailCount_ = 0;
    uint8_t seg1TgaJsonOk_ = 0;
    bool seg1RendererLodReady_ = false;
    bool seg1SingleFaceSwapReady_ = false;
    bool seg1SingleFaceSwapUseAlt_ = false;
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
    std::array<uint16_t, kTrackSegmentLimit + 1> lastSortRank_{};
    SlaveTrackDrawProducer<SegmentHandle, kTrackSegmentLimit> producer_{};
    TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit> coordinator_{producer_};
    AdaptiveTrackBudgetController budgetController_{};
    SoakMonitor soakMonitor_{};
};
