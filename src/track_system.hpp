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
        uint32_t initialSegments = 20;
        uint32_t minSegments = 20;
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
                     const SRL::Math::Types::Vector3D& cameraLocation);

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
        int id = 0;
        std::unique_ptr<TrackRenderer> renderer;
        SRL::Math::Types::Vector3D center{};
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

    using SegmentPool = TrackSegmentPool<kTrackSegmentLimit, SegmentRenderEntry>;
    using SegmentHandle = SegmentPool::Handle;

    static SRL::Math::Types::Vector3D ComputeRendererCenter(const TrackRenderer& renderer);
    void ReleaseRawSegmentCatalog();
    void ReleaseSeg1Texbanks();
    bool LoadSeg1TexbankIndexToCart(size_t lodIndex, int lodValue);
    const TrackSegmentCopy* FindRawSegmentCopyById(int id) const;
    const char* FindExistingPath(const char* const* paths, size_t count);
    const char* ResolveSegmentPath(size_t id);
    TrackSegmentCopy CopySegmentById(size_t id);
    std::vector<TrackSegmentEntry> CopyAllTrackSegments(size_t maxSegments);
    std::vector<SegmentRenderEntry> BuildSegmentRenderers(std::vector<TrackSegmentEntry>& entries);
    std::vector<SegmentHandle> BuildSegmentHandleTable();

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
    std::array<std::vector<int32_t>, 4> seg1RendererFaceSlotsByLod_{};
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
