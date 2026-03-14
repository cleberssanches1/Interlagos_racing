#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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

class TrackSystem
{
public:
    static constexpr size_t kTrackSegmentLimit = 30;

    struct Config
    {
        // Initial visible segment count before adaptive budget tuning.
        uint32_t initialSegments = 30;
        uint32_t minSegments = 19;
        uint32_t initialMeshes = 128;
        uint32_t initialFaces = 32000;
        bool useSlave = true;
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

private:
    struct TrackSegmentEntry
    {
        int id = 0;
        TrackSegmentCopy copy;
    };

    struct SegmentRenderEntry
    {
        struct SegmentLodState
        {
            bool ready = false;
            bool hasPerFaceRankOffsets = false;
            uint8_t currentLodIndex = 0xFF; // 0:8, 1:16, 2:32, 3:64
            int16_t currentBaseRank = -1;
            std::vector<uint16_t> faceFamilyIds{};
            std::vector<uint8_t> faceRankOffsets{};
            std::vector<int32_t> currentFaceSlots{};
        };

        int id = 0;
        uint8_t logicalSegmentCount = 0;
        std::unique_ptr<TrackRenderer> renderer;
        SRL::Math::Types::Vector3D center{};
        SegmentLodState lodState{};
    };
    struct RawSegmentEntry
    {
        int id = 0;
        TrackSegmentCopy copy{};
    };
    struct Seg1FamilySlotEntry
    {
        uint16_t familyId = 0;
        std::array<uint16_t, 4> lodSlots{{0, 0, 0, 0}}; // 0:8, 1:16, 2:32, 3:64
    };
    struct Seg1TexbankEntry
    {
        uint16_t familyId = 0;
        uint32_t offset = 0;
        uint32_t size = 0;
    };
    struct Seg1TexbankCart
    {
        int lod = 8;
        void* cartPtr = nullptr;
        uint32_t size = 0;
        std::vector<Seg1TexbankEntry> entries{};
    };
    struct Seg1TgaCartEntry
    {
        char name[64]{};
        void* cartPtr = nullptr;
        uint32_t size = 0;
    };

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
    void InitializeFamilySlots(std::vector<Seg1FamilySlotEntry>& outSlots,
                               const int* familyIds,
                               size_t count) const;
    void InitializeFamilySlots(std::vector<Seg1FamilySlotEntry>& outSlots,
                               const std::vector<uint16_t>& familyIds) const;
    Seg1FamilySlotEntry* FindFamilySlot(std::vector<Seg1FamilySlotEntry>& familySlots, uint16_t familyId);
    const Seg1FamilySlotEntry* FindFamilySlot(const std::vector<Seg1FamilySlotEntry>& familySlots, uint16_t familyId) const;
    bool TryGetFamilyLodSlot(const std::vector<Seg1FamilySlotEntry>& familySlots,
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
    // Build per family texture slots for all lod levels used by segment renderers.
    bool BuildTrackFamilyLodSlots(std::vector<Seg1FamilySlotEntry>& outSlots);
    // Build per face slot tables for one segment renderer across all lod levels.
    bool BuildSegmentLodState(SegmentRenderEntry& entry,
                              const SegmentComponent::Blob& matBlob,
                              const SegmentComponent::Loader::MatView& matView,
                              std::vector<Seg1FamilySlotEntry>& familySlots);
    // Rebuild one segment face slot table on demand for the selected lod band.
    bool RebuildSegmentFaceSlotsForLod(SegmentRenderEntry& entry,
                                       uint8_t lodIndex,
                                       std::vector<Seg1FamilySlotEntry>& familySlots);
    // Rebuild one batch face slot table using the first logical rank carried by that batch.
    bool RebuildSegmentFaceSlotsForBaseRank(SegmentRenderEntry& entry,
                                            size_t baseRank,
                                            std::vector<Seg1FamilySlotEntry>& familySlots);
    // Upload one family texture slot only when a lod band actually needs it.
    bool EnsureFamilyLodSlotLoaded(std::vector<Seg1FamilySlotEntry>& familySlots,
                                   uint16_t familyId,
                                   uint8_t lodIndex);
    // Resolve the target lod band for a visible segment rank near the camera.
    uint8_t ResolveSegmentLodIndexByRank(size_t rank) const;
    // Apply lod changes only for segments whose desired band changed.
    void UpdateVisibleSegmentLods(const std::vector<SegmentHandle>& nearToFarHandles);
    bool BuildSegmentCenterCatalog();
    bool RebuildActiveSegmentWindow(int32_t startSegmentId, size_t loadLimit);
    bool SlideActiveSegmentWindowForward(size_t stepCount);
    void PrewarmNextSegmentLod8();
    void ResetSlidePrefetchState();
    bool BuildSegmentIntoSlideScratch(int32_t segmentId,
                                      SRL::Math::Types::Vector3D& outCenter,
                                      std::vector<uint16_t>& outFamilyIds);
    void TryPrefetchUpcomingSegment();
    void CaptureTrackTextureHeapBase();
    bool ShouldRecycleTrackTextureHeap() const;
    void RecycleTrackTextureHeap();
    void MergeCurrentWindowFamilies();
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
    std::vector<TrackSegmentEntry> CopyAllTrackSegments(size_t maxSegments);
    std::vector<SegmentRenderEntry> BuildSegmentRenderers(std::vector<TrackSegmentEntry>& entries);
    std::vector<SegmentHandle> BuildSegmentHandleTable();
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
    uint32_t fixedVisibleSegmentCap_ = 1;
    uint16_t totalSegmentCount_ = 0;
    int32_t activeWindowStartId_ = 1;
    size_t activeWindowHead_ = 0;
    uint8_t activeWindowSwitchCooldown_ = 0;
    std::vector<SRL::Math::Types::Vector3D> segmentCenterCatalog_{};
    std::unique_ptr<TrackRenderer> slideScratchRenderer_{};
    int32_t slidePrefetchSegmentId_ = -1;
    SRL::Math::Types::Vector3D slidePrefetchCenter_{};
    std::vector<uint16_t> slidePrefetchFamilyIds_{};
    uint16_t trackTextureHeapBase_ = 0;
    bool trackTextureHeapBaseValid_ = false;
    uint16_t trackTextureRecycleCount_ = 0;
    uint8_t textureUploadsThisFrame_ = 0;
    static constexpr uint8_t kTextureUploadsBudgetPerFrame = 8;
    mutable std::array<int16_t, 4096> familySlotIndex_{};
    mutable bool familySlotIndexDirty_ = true;
    uint8_t familyMergeCooldown_ = 0;

    std::vector<TrackSegmentEntry> segmentEntries_{};
    std::vector<RawSegmentEntry> rawSegmentCatalog_{};
    std::vector<SegmentRenderEntry> segmentRenderers_{};
    bool seg1ComponentEnabled_ = false;
    SRL::Math::Types::Vector3D seg1ComponentCenter_{};
    std::vector<SRL::Math::Types::Vector3D> seg1ComponentVerts_{};
    std::vector<SRL::Types::Polygon> seg1ComponentFaces_{};
    std::vector<SRL::Types::Attribute> seg1ComponentAttrs_{};
    std::vector<uint16_t> seg1FaceFamilyIds_{};
    std::vector<Seg1FamilySlotEntry> seg1FamilySlots_{};
    std::array<Seg1TexbankCart, 4> seg1Texbanks_{};
    std::vector<Seg1TgaCartEntry> seg1TgaCatalog_{};
    std::array<std::vector<int32_t>, 4> seg1RendererFaceSlotsByLod_{};
    uint16_t seg1TgaPreloadCount_ = 0;
    uint16_t seg1TgaAttemptCount_ = 0;
    uint16_t seg1TgaFailCount_ = 0;
    uint8_t seg1TgaJsonOk_ = 0;
    bool seg1RendererLodReady_ = false;
    bool seg1SingleFaceSwapReady_ = false;
    bool seg1SingleFaceSwapUseAlt_ = false;
    uint16_t seg1SingleFaceSwapCounter_ = 0;
    uint16_t seg1SingleFaceSwapFrames_ = 180; // ~3s @60fps
    int32_t seg1SingleFaceSwapFace_ = -1;
    int32_t seg1SingleFaceSwapBaseSlot_ = -1;
    int32_t seg1SingleFaceSwapAltSlot_ = -1;
    std::vector<int32_t> seg1SingleFaceSlots_{};
    uint8_t seg1CurrentLodIndex_ = 0;
    uint16_t seg1LodFrameCounter_ = 0;
    uint16_t seg1LodSwapFrames_ = 60; // ~1s @60fps (teste visual)
    SegmentPool segmentPool_{};
    std::vector<SegmentHandle> segmentHandles_{};
    std::array<uint16_t, kTrackSegmentLimit + 1> lastSortRank_{};
    DoubleBufferedTrackDrawProducer<SegmentHandle, kTrackSegmentLimit> producer_{};
    TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit> coordinator_{producer_};
    AdaptiveTrackBudgetController budgetController_{};
    SoakMonitor soakMonitor_{};
};
