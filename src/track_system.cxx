#include "track_system.hpp"
#include "physics_feature_flags.hpp"
#include "interfaces.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <string.h>
#include <vector>
#include <utility>
#include <limits>

#ifndef TRACK_ENABLE_HOST_SEGMENTS_MAP_FALLBACK
#define TRACK_ENABLE_HOST_SEGMENTS_MAP_FALLBACK 0
#endif

#if TRACK_ENABLE_HOST_SEGMENTS_MAP_FALLBACK
#include <errno.h>
#endif

extern "C" uint32_t SRL_AppGetVblankCounter();

// ============================================================================
// TRACK_LWR_STAGE_TRACE â€” probes de LWR por funÃ§Ã£o para diagnÃ³stico de leak
//
// Ativar com: -DTRACK_LWR_STAGE_TRACE na linha de compilaÃ§Ã£o
// SaÃ­da: "LWP <nome> d:<delta>" via SRL::Debug::Print nas linhas 31-38.
// Cada linha mostra o delta de bytes livres de LWR antes/apÃ³s a funÃ§Ã£o.
// delta negativo = a funÃ§Ã£o consumiu LWR neste frame.
// ============================================================================
#ifdef TRACK_LWR_STAGE_TRACE
namespace {
struct LwrStageProbeAccum
{
    int32_t resetPrefetch    = 0;
    int32_t resetBackBuf     = 0;
    int32_t buildPrefetch    = 0;
    int32_t prepareSlide     = 0;
    int32_t commitSlide      = 0;
    int32_t mergeFamilies    = 0;
    int32_t buildHandleTable = 0;
    int32_t drawStage        = 0;
    int32_t releaseEndFrame  = 0;
    int32_t maintenanceTrim  = 0;
    int32_t flushRetiredSlots = 0; // FlushPendingRetiredTrackTextureSlots + ReacquireEmergencyReserve
    int32_t validateWindow   = 0; // ValidateStabilizedWindowInvariants + breakdown sampling
    int32_t beginFrameOps    = 0; // BeginFrame catch-up slide + TryPrefetchUpcomingSegment
};
static LwrStageProbeAccum g_lwrStageAccum{};

inline uint32_t LwrProbeCapture()
{
    return static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetReport().FreeSize);
}
inline int32_t LwrProbeDelta(uint32_t before)
{
    return static_cast<int32_t>(LwrProbeCapture()) - static_cast<int32_t>(before);
}
} // namespace

// Imprime o acumulado de probes do frame atual (chamar uma vez por frame).
// Uso: adicionar TrackSystem::PrintLwrStageProbes() no game loop de debug.
void TrackSystem::PrintLwrStageProbes()
{
    SRL::Debug::Print(2, 9, "LWP rst:%d/%d bld:%d      ",
                      static_cast<int>(g_lwrStageAccum.resetPrefetch),
                      static_cast<int>(g_lwrStageAccum.resetBackBuf),
                      static_cast<int>(g_lwrStageAccum.buildPrefetch));
    SRL::Debug::Print(2, 10, "LWP prp:%d cmt:%d mrg:%d hnd:%d  ",
                      static_cast<int>(g_lwrStageAccum.prepareSlide),
                      static_cast<int>(g_lwrStageAccum.commitSlide),
                      static_cast<int>(g_lwrStageAccum.mergeFamilies),
                      static_cast<int>(g_lwrStageAccum.buildHandleTable));
    SRL::Debug::Print(2, 11, "LWP drw:%d efr:%d mnt:%d       ",
                      static_cast<int>(g_lwrStageAccum.drawStage),
                      static_cast<int>(g_lwrStageAccum.releaseEndFrame),
                      static_cast<int>(g_lwrStageAccum.maintenanceTrim));
    SRL::Debug::Print(2, 12, "LWP frs:%d vld:%d bfr:%d       ",
                      static_cast<int>(g_lwrStageAccum.flushRetiredSlots),
                      static_cast<int>(g_lwrStageAccum.validateWindow),
                      static_cast<int>(g_lwrStageAccum.beginFrameOps));
    g_lwrStageAccum = LwrStageProbeAccum{};
}

#define LWR_PROBE_BEGIN()  const uint32_t _lwrRef = LwrProbeCapture()
#define LWR_PROBE_END(acc) (acc) += LwrProbeDelta(_lwrRef)
#else
#define LWR_PROBE_BEGIN()  ((void)0)
#define LWR_PROBE_END(acc) ((void)0)
#endif // TRACK_LWR_STAGE_TRACE

#include "modelObject.hpp"
#include "resource_loader.hpp"
#include "segment_component_loader.hpp"
#include "segment_draw_ready_loader.hpp"
#include "segment_runtime_draw_loader.hpp"
#include "track_runtime_pack_loader.hpp"
#include "batch_draw_ready_loader.hpp"
#include "track_pipeline_stages.hpp"
#include "sh2_frt_profiler.hpp"
#include "srl_tga.hpp"

using SRL::Math::Types::Fxp;
using SRL::Math::Types::Vector3D;

namespace
{
inline void SaturatingIncrementU16(uint16_t& value)
{
    if (value < std::numeric_limits<uint16_t>::max()) ++value;
}

inline void SaturatingAddU16(uint16_t& value, size_t amount)
{
    const uint32_t sum =
        static_cast<uint32_t>(value) + static_cast<uint32_t>(amount);
    value = static_cast<uint16_t>(std::min<uint32_t>(
        static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()),
        sum));
}

inline void SaturatingAddU16Value(uint16_t& value, uint32_t amount)
{
    const uint32_t sum = static_cast<uint32_t>(value) + amount;
    value = static_cast<uint16_t>(std::min<uint32_t>(
        static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()),
        sum));
}

struct Segment1TextureJson
{
    int familyIds[512]{};
    char familyTex64[512][64]{};
    size_t familyCount = 0;
    TrackLowWorkVector<int> faceFamily{};
};

struct TexbankIndexLod
{
    int familyIds[128]{};
    char files[128][64]{};
    size_t count = 0;
};

struct RenTextureMapEntry
{
    int lod = 0;
    char sourceName[64]{};
    char targetName[32]{};
};

struct RenTextureMap
{
    TrackLowWorkVector<RenTextureMapEntry> entries{};
};

struct PackedAssetEntryMeta
{
    char name[65]{};
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct PackedAssetCache
{
    void* cartPtr = nullptr;
    uint32_t size = 0;
    char sourcePath[96]{}; 
    TrackLowWorkVector<PackedAssetEntryMeta> entries{};
    uint32_t trackedEntryBytes = 0;
};

struct TrackRuntimePackCache
{
    void* cartPtr = nullptr;
    uint32_t size = 0;
    char sourcePath[96]{};
    TrackRuntimePack::Loader::View view{};
};

struct RdrBuildScratch
{
    SegmentRuntimeDraw::Blob blob{};
    TrackLowWorkVector<SRL::Math::Types::Vector3D> verts{};
    TrackLowWorkVector<SRL::Types::Polygon> faces{};
    TrackLowWorkVector<SRL::Types::Attribute> attrs{};
};

struct SdrBuildScratch
{
    SegmentDrawReady::Blob blob{};
    TrackLowWorkVector<SRL::Math::Types::Vector3D> verts{};
    TrackLowWorkVector<SRL::Types::Polygon> faces{};
    TrackLowWorkVector<SRL::Types::Attribute> attrs{};
};

struct BdrBatchBuildResult
{
    TrackLowWorkUniquePtr<TrackRenderer> renderer{};
    Vector3D center{};
    TrackLowWorkU16Vector familyIds{};
    TrackLowWorkU8Vector faceRankOffsets{};
};

// Sticky diagnostics for TGA preload path resolution.
static char g_tgaLastTry[96] = "none";
static char g_tgaLastResult[96] = "none";
static char g_tgaLastName[64] = "none";
static uint32_t g_smapBytes = 0;
static char g_smapSig[24] = "none";
static char g_smapHead[48] = "none";
static Segment1TextureJson g_seg1MapCache{};
static bool g_seg1MapCacheValid = false;
static uint32_t g_packedAssetCacheEntryBytesLwr = 0;
static RdrBuildScratch g_rdrBuildScratch{};
static SdrBuildScratch g_sdrBuildScratch{};
static SegmentRuntimeDraw::Blob g_rdrFamilyIdsScratch{};
static SegmentDrawReady::Blob g_sdrFamilyIdsScratch{};
// Visible window LOD distribution (50 segments total):
// 25x 64x64 + 25x 32x32 only.
static constexpr uint32_t kLodBand64Count = 10u;
static constexpr uint32_t kLodBand32Count = 10u;
static constexpr size_t kWorkRamPlanningHeadroomBytes = 48u * 1024u;
static constexpr size_t kWorkRamHardFloorBytes = 24u * 1024u;
// Release the emergency reserve slightly earlier so runtime maintenance does
// not stay stuck in "critical" mode when HWR hovers around 28-32 KiB.
static constexpr size_t kWorkRamSlideSafeFloorBytes = kWorkRamHardFloorBytes + (8u * 1024u);
static constexpr size_t kWorkRamCatastrophicFloorBytes = 8u * 1024u;
static constexpr size_t kWorkRamLodDegradeBytes = 8u * 1024u;
static constexpr size_t kWorkRamLodPendingMinBytes = 16u * 1024u;
static constexpr size_t kTrackWorkRamEmergencyReserveBytes = 24u * 1024u;
static constexpr size_t kTrackWorkRamEmergencyReserveReacquireFloorBytes =
    kTrackWorkRamEmergencyReserveBytes + (48u * 1024u);
// Soft floor tuned for runtime: 320 KB was triggering maintenance almost every
// frame in stabilized mode, creating trim churn and frame spikes at segment
// boundaries. Keep protection, but only after free space is actually tighter.
static constexpr size_t kLowWorkRamSoftFloorBytes = 224u * 1024u;
static constexpr size_t kLowWorkRamHardFloorBytes = 160u * 1024u;
// Compact by LWR baseline drop, but keep hysteresis wide enough to avoid
// frequent full residency rebuilds while driving.
static constexpr size_t kLowWorkCompactDropBytes = 24u * 1024u;
static constexpr size_t kLowWorkCompactCriticalDropBytes = 48u * 1024u;
static constexpr size_t kLodMandatoryFreeBytes = kWorkRamHardFloorBytes;
static constexpr size_t kLodExactRecoveryFreeBytes = kWorkRamHardFloorBytes;
// When the track owns only a small slice of HWR, aggressive recovery work in
// the track system is mostly churn: it burns frame time and barely moves free
// memory. Treat that case as "not the track's problem" sooner.
static constexpr size_t kWorkRamTrackOwnedBypassBytes = 32u * 1024u;
// Stabilized runtime policy: keep a fixed pre-reserved floor for per-segment
// face/vertex vectors so long laps do not keep growing capacities as the car
// reaches heavier segments later in the track.
static constexpr size_t kStabilizedFaceCapacityFloorCap = 384u;
static constexpr size_t kStabilizedVertexCapacityFloorCap = 384u;
static constexpr size_t kLodMandatoryBandSegmentCount =
    kLodBand64Count + kLodBand32Count;
static constexpr uint16_t kTrackRuntimeMemRev = 7u;
static constexpr uint8_t kSlideHwrTraceLowMemBit = 1u << 0;
static constexpr uint8_t kSlideHwrTraceResetPrefetchBit = 1u << 1;
static constexpr uint8_t kSlideHwrTraceBuildPrefetchBit = 1u << 2;
static constexpr uint8_t kSlideHwrTracePrepareOkBit = 1u << 3;
static constexpr uint8_t kSlideHwrTraceCommitOkBit = 1u << 4;
static constexpr uint8_t kSlideHwrTraceDeferredBit = 1u << 5;
static constexpr uint8_t kSlideHwrTracePrepareFailBit = 1u << 6;
static constexpr uint8_t kSlideHwrTraceCommitFailBit = 1u << 7;
static constexpr uint8_t kLodDegradeCooldownFrames = 6u;
static constexpr size_t kLodRecoveryFreeBytes = kWorkRamHardFloorBytes + (48u * 1024u);
// Temporary stabilization mode:
// - keep the sliding window running
// - keep tail segments entering in 32x32
// - disable visible-segment lod promotions/demotions during gameplay
// - disable destructive texture/palette release while racing
// This isolates runtime lifetime bugs from the offline asset pipeline.
static constexpr bool kEnableTrackRuntimeStabilization = true;
// Leak isolation mode:
// - fixed 50-segment window
// - keep runtime sliding active (new segments keep entering/leaving the 20-slot window)
// - disable prefetch/recovery/texture-compaction dynamics
// - mixed profile fixed in leak-isolation:
//   first 25 ranks in 64x64, next 25 ranks in 32x32
// Use this mode to isolate allocator/retention behavior with controlled texture churn.
static constexpr bool kEnableTrackLeakIsolationFixed64Pipeline = true;
static constexpr size_t kTrackLeakIsolationWindowSegments = 20u;
static constexpr bool kEnableLeakIsolationMixedLodProfile = true;
static constexpr uint8_t kLeakIsolationNearLodIndex = 3u; // 64x64
static constexpr uint8_t kLeakIsolationFarLodIndex = 2u;  // 32x32
static constexpr size_t kLeakIsolationNearLodCount = 10u;
static_assert(kLeakIsolationNearLodCount <= kTrackLeakIsolationWindowSegments,
              "Near LOD count must fit leak-isolation window.");
// Keep active window storage persistent and reuse slot renderers on rebuild.
// This is a stepping stone before migrating to a full fixed ring N+staging pool.
static constexpr bool kEnableTrackWindowFixedStorage = true;
// Even in stabilized runtime, end-of-frame recycle for palettes that were
// already detached from draw usage is safe and prevents CRAM accumulation.
// No estado atual, reciclar palette no fim de frame causa flashes roxos
// intermitentes em alguns emuladores/hardware timing. Mantemos desligado.
static constexpr bool kEnableStabilizedEndFramePaletteRecycle = false;
static constexpr bool kEnableTrackLodBandsInStabilization = true;
static constexpr bool kEnableDeterministicStabilizedSlide = true;
static constexpr bool kEnableStabilizedDepthSortOnSlave = true;
static constexpr bool kEnableStabilizedProducerOnSlave = true;
// Lockstep mode: master waits for slave producer/sort completion.
// Prioritizes deterministic sequencing and explicit workload split.
static constexpr bool kEnableTrackSlaveBarrierLockstep = true;
static constexpr bool kEnableSafeModeSingleRenderBackend = false;
static constexpr bool kEnableTrackOverlayRows16To22 = false;
static constexpr bool kEnableTrackPhaseRamTelemetry = false;
static constexpr bool kEnableLowWorkDrawStageTelemetry = false;
// Compactacao por "queda de baseline LWR" gera churn agressivo no fluxo atual.
// Mantemos desabilitado no hotfix de estabilidade para priorizar residencias estaveis.
static constexpr bool kEnableTrackCompactByBaselineDrop = false;
// Rebuild de janela inteira durante runtime gera picos grandes de alocacao e
// queda brusca de FPS. Em modo estabilizado mantemos apenas slide incremental.
static constexpr bool kEnableRuntimeWindowRebuildRepairs = false;
// Compactacao de heap de textura no meio da corrida pode causar churn e
// flashes (rebind de slots/paleta). Mantemos desabilitada no caminho critico.
static constexpr bool kEnableRuntimeTextureCompaction = false;
// Keep leak-isolation mode conservative: texture compaction during runtime
// introduced visible corruption/stalls in long runs.
static constexpr bool kEnableLeakIsolationTextureCompaction = false;
// Prefetch can run safely in leak-isolation because it does not force texture
// compaction/recycle. Keeping it enabled reduces synchronous SDR misses on slide.
static constexpr bool kEnableLeakIsolationPrefetch = true;
// Reciclagem pesada por manutencao durante slide tende a introduzir oscillation
// de slots/texturas; mantemos desligado e deixamos so liberacoes deterministicas.
static constexpr bool kEnableRuntimeTextureRecycleOnMaintenance = false;
// A/B leak isolation mode:
// - when true, keeps streaming/slide/prefetch/upload running
// - skips track draw submission (no SGL/VDP1 track draw path)
// Toggle to false for baseline A run.
static constexpr bool kEnableLeakABBypassTrackDraw = false;
// A/B leak isolation mode 2:
// - keeps draw stage/coordinator execution
// - skips only final renderer submission (no slPutPolygon/slSetSprite path)
static constexpr bool kEnableLeakABSkipTrackRenderSubmit = false;
// Runtime diagnostics profile requested for WorkRAM leak tracking.
// Overlays muito verbosos (strings variaveis por frame) aumentam churn de LWR
// e pioram desempenho durante testes de longa duracao.
static constexpr bool kEnableTrackWindowOverlayTelemetry = false;
static constexpr bool kEnableLegacyFamilyOverlayTelemetry = false;
static constexpr bool kEnableLegacyTrackOverlayTelemetry = false;
// SH2 overlays disabled to keep screen focused on FPS + WorkRAM tracking.
static constexpr bool kEnableSh2UsageOverlay = false;
static constexpr bool kEnableLegacyTrackOverlaySh2Telemetry = false;
// Test mode: force track draw order by segment id ascending (1..N).
// This is useful to validate seam overlap behavior independent of depth sort.
static constexpr bool kTestRenderSegmentsAscendingById = false;
// Prefetch speed tiers are based on planar car displacement (world units/frame).
// Tier 1: moderate speed, Tier 2: high speed.
static constexpr uint16_t kPrefetchSpeedTier1UnitsPerFrame = 6u;
static constexpr uint16_t kPrefetchSpeedTier2UnitsPerFrame = 12u;
static constexpr uint8_t kSafeModeRenderBackendId = 1; // 0:Scene3D 1:SglDirect 2:Vdp1
static constexpr size_t kSegmentFamilyDedupScratchCap = 64u;
static constexpr uint8_t kTrackFramePlanFlagFallback = 1u << 0;
static constexpr uint8_t kTrackFramePlanFlagPartial = 1u << 1;
static constexpr uint8_t kTrackFramePlanFlagStale = 1u << 2;
static constexpr uint8_t kTrackLod32Index = 2u;
static constexpr uint8_t kTrackLod64Index = 3u;

static inline uint8_t NormalizeTrackTextureLodIndex(uint8_t lodIndex)
{
    return (lodIndex >= kTrackLod64Index) ? kTrackLod64Index : kTrackLod32Index;
}

static inline int TrackTextureLodValue(uint8_t lodIndex)
{
    return (NormalizeTrackTextureLodIndex(lodIndex) == kTrackLod64Index) ? 64 : 32;
}

static inline void ClearUsedTextureSlots(std::array<uint8_t, SRL_MAX_TEXTURES>& flags)
{
    ::memset(flags.data(), 0, flags.size());
}

static int32_t WrapSegmentIdToRange(int32_t segmentId, uint16_t totalSegmentCount)
{
    if (totalSegmentCount == 0) return -1;
    const int32_t total = static_cast<int32_t>(totalSegmentCount);
    // Convert arbitrary integer to 1..N id range while preserving valid 1-based ids.
    int32_t normalized = (segmentId - 1) % total;
    if (normalized < 0) normalized += total;
    return normalized + 1;
}

static int32_t WrapDistanceForward(int32_t fromId, int32_t toId, uint16_t totalSegmentCount)
{
    if (totalSegmentCount == 0) return 0;
    const int32_t total = static_cast<int32_t>(totalSegmentCount);
    int32_t d = (toId - fromId) % total;
    if (d < 0) d += total;
    return d;
}

static int32_t ResolveWindowStartFromCarSegment(int32_t carSegmentId,
                                                uint16_t totalSegmentCount,
                                                int8_t windowDirection)
{
    if (totalSegmentCount == 0) return -1;
    const int32_t carId = WrapSegmentIdToRange(carSegmentId, totalSegmentCount);
    if (carId <= 0) return -1;
    const int32_t dir = (windowDirection < 0) ? -1 : 1;
    // Keep the car on the 2nd rendered segment:
    // dir +1 => [car-1, car, car+1...]
    // dir -1 => [car+1, car, car-1...]
    return WrapSegmentIdToRange(carId - dir, totalSegmentCount);
}

static bool BuildNormalizedFlatDirectionRaw(int32_t dxRaw,
                                            int32_t dzRaw,
                                            Vector3D& outDirection)
{
    const int32_t adx = (dxRaw < 0) ? -dxRaw : dxRaw;
    const int32_t adz = (dzRaw < 0) ? -dzRaw : dzRaw;
    const int32_t maxAxis = (adx > adz) ? adx : adz;
    if (maxAxis <= 0) return false;

    const int64_t nxRaw = (static_cast<int64_t>(dxRaw) << 16) / maxAxis;
    const int64_t nzRaw = (static_cast<int64_t>(dzRaw) << 16) / maxAxis;
    outDirection = Vector3D(Fxp::BuildRaw(static_cast<int32_t>(nxRaw)),
                            Fxp::BuildRaw(0),
                            Fxp::BuildRaw(static_cast<int32_t>(nzRaw)));
    return true;
}

static bool IsVdp1TextureSlotLive(uint16_t slot)
{
    if (slot == No_Texture) return false;
    if (slot >= SRL_MAX_TEXTURES) return false;
    if (slot >= SRL::VDP1::GetTextureCount()) return false;
    return SRL::VDP1::Metadata[slot].Texture != nullptr;
}

static size_t GetHighWorkRamFreeBytesSafe(bool* outValid = nullptr)
{
    const auto report = SRL::Memory::HighWorkRam::GetReport();
    const bool valid = (report.TotalSize > 0u) && (report.FreeSize <= report.TotalSize);
    if (outValid) *outValid = valid;
    return valid ? report.FreeSize : 0u;
}

static size_t GetLowWorkRamFreeBytesSafe(bool* outValid = nullptr)
{
    const auto report = SRL::Memory::LowWorkRam::GetReport();
    const bool valid = (report.TotalSize > 0u) && (report.FreeSize <= report.TotalSize);
    if (outValid) *outValid = valid;
    return valid ? report.FreeSize : 0u;
}

static inline bool IsDriveableSurfaceTypeId(uint8_t surfaceTypeId)
{
    return (surfaceTypeId == 1u) || (surfaceTypeId == 2u) || (surfaceTypeId == 3u);
}

static void SetTrackWorkRamDebugTag(SRL::Memory::DebugTag tag)
{
    SRL::Memory::HighWorkRam::SetDebugTag(tag);
    SRL::Memory::LowWorkRam::SetDebugTag(tag);
}

struct LowWorkRamStageSample
{
    uint32_t freeBytes = 0u;
    uint32_t payloadBytes = 0u;
    uint32_t overheadBytes = 0u;
    uint32_t largestFreeBytes = 0u;
    uint32_t freeBlocks = 0u;
};

struct LowWorkRamStageDelta
{
    const char* name = "na";
    int32_t freeDelta = 0;
    int32_t payloadDelta = 0;
    int32_t overheadDelta = 0;
    int32_t largestFreeDelta = 0;
    int32_t freeBlocksDelta = 0;
};

static LowWorkRamStageSample CaptureLowWorkRamStageSample()
{
    const auto report = SRL::Memory::LowWorkRam::GetReport();
    const uint32_t freeBytes =
        (report.TotalSize > 0u && report.FreeSize <= report.TotalSize)
            ? static_cast<uint32_t>(report.FreeSize)
            : 0u;
    const uint32_t usedBytes =
        (report.TotalSize >= report.FreeSize)
            ? static_cast<uint32_t>(report.TotalSize - report.FreeSize)
            : 0u;
    const uint32_t payloadBytes = static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetUsedPayloadBytes());
    const uint32_t overheadBytes = (usedBytes >= payloadBytes) ? (usedBytes - payloadBytes) : 0u;
    return LowWorkRamStageSample{
        freeBytes,
        payloadBytes,
        overheadBytes,
        static_cast<uint32_t>(SRL::Memory::LowWorkRam::GetLargestFreeBlockSize()),
        static_cast<uint32_t>(report.FreeBlocks),
    };
}

static int32_t SignedLowWorkRamDelta(uint32_t before, uint32_t after)
{
    return static_cast<int32_t>(after) - static_cast<int32_t>(before);
}

static LowWorkRamStageDelta BuildLowWorkRamStageDelta(const char* name,
                                                      const LowWorkRamStageSample& before,
                                                      const LowWorkRamStageSample& after)
{
    LowWorkRamStageDelta delta{};
    delta.name = name;
    delta.freeDelta = SignedLowWorkRamDelta(before.freeBytes, after.freeBytes);
    delta.payloadDelta = SignedLowWorkRamDelta(before.payloadBytes, after.payloadBytes);
    delta.overheadDelta = SignedLowWorkRamDelta(before.overheadBytes, after.overheadBytes);
    delta.largestFreeDelta = SignedLowWorkRamDelta(before.largestFreeBytes, after.largestFreeBytes);
    delta.freeBlocksDelta = SignedLowWorkRamDelta(before.freeBlocks, after.freeBlocks);
    return delta;
}

template <typename KeyT>
static size_t FindScratchKeyIndex(const std::array<KeyT, kSegmentFamilyDedupScratchCap>& keys,
                                  size_t count,
                                  KeyT key)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (keys[i] == key) return i;
    }
    return kSegmentFamilyDedupScratchCap;
}

template <typename SlotContainer>
static bool HasMissingFaceTextureSlots(const SlotContainer& slots)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (static_cast<int32_t>(slots[i]) < 0) return true;
    }
    return false;
}

template <typename SlotContainer, typename FamilyVecT>
static bool HasMissingRequiredFaceTextureSlots(const SlotContainer& slots,
                                               const FamilyVecT* familyIds)
{
    if (!familyIds || familyIds->size() != slots.size())
    {
        return HasMissingFaceTextureSlots(slots);
    }

    for (size_t i = 0; i < slots.size(); ++i)
    {
        if ((*familyIds)[i] == 0) continue;
        if (static_cast<int32_t>(slots[i]) < 0) return true;
    }
    return false;
}

template <typename SlotContainer, typename FamilyVecT>
static uint32_t CountMissingOrDeadRequiredFaceTextureSlots(const SlotContainer& slots,
                                                           const FamilyVecT* familyIds)
{
    if (!familyIds || familyIds->size() != slots.size())
    {
        uint32_t missing = 0;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            const int32_t slot = static_cast<int32_t>(slots[i]);
            if (slot < 0) ++missing;
        }
        return missing;
    }

    uint32_t missing = 0;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if ((*familyIds)[i] == 0) continue;
        const int32_t slot = static_cast<int32_t>(slots[i]);
        if (slot < 0)
        {
            ++missing;
            continue;
        }
        if (slot >= static_cast<int32_t>(SRL_MAX_TEXTURES))
        {
            ++missing;
            continue;
        }
        if (!IsVdp1TextureSlotLive(static_cast<uint16_t>(slot)))
        {
            ++missing;
        }
    }
    return missing;
}

template <typename VecT>
static uint32_t VectorCapacityBytesSafe(const VecT& v)
{
    using T = typename VecT::value_type;
    const uint64_t bytes = static_cast<uint64_t>(v.capacity()) * static_cast<uint64_t>(sizeof(T));
    return (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(bytes);
}

template <typename VecT>
static uint32_t VectorCapacityElementsSafe(const VecT& v)
{
    return (v.capacity() > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(v.capacity());
}

// lwrFreeHint: pre-queried LowWorkRam free bytes from the caller. SIZE_MAX = query inline.
// Callers that invoke TrimVectorSlack in a tight loop MUST query LowWorkRam::GetReport()
// ONCE before the loop and pass the result here â€” repeated TLSF free-list scans inside
// a single bulk trim are O(n * free_blocks) and will stall the frame on fragmented heaps.
template <typename VecT>
static bool TrimVectorSlack(VecT& v, size_t keepCapacityElements, bool aggressive,
                             size_t lwrFreeHint = SIZE_MAX)
{
    using T = typename VecT::value_type;
    const size_t desired = std::max(keepCapacityElements, v.size());
    if (v.capacity() <= desired) return false;

    const size_t slackElements = v.capacity() - desired;
    const size_t slackBytes = slackElements * sizeof(T);
    if (!aggressive)
    {
        if (v.capacity() <= (desired * 2u + 8u)) return false;
        if (slackBytes < (2u * 1024u)) return false;
    }

    // Safety: compact.reserve() needs 'desired * sizeof(T)' contiguous LWR bytes
    // BEFORE the old block is freed. Attempting this when LWR is nearly exhausted
    // creates a catastrophic double-allocation that consumes the last available bytes.
    const size_t compactBytes = desired * sizeof(T);
    if (compactBytes > 0u)
    {
        static constexpr size_t kTrimLwrSafetyMarginBytes = 2u * 1024u;
        const size_t lwrFree = (lwrFreeHint != SIZE_MAX)
            ? lwrFreeHint
            : static_cast<size_t>(SRL::Memory::LowWorkRam::GetReport().FreeSize);
        if (lwrFree < compactBytes + kTrimLwrSafetyMarginBytes)
            return false;
    }

    VecT compact{};
    compact.reserve(desired);
    compact.insert(compact.end(), v.begin(), v.end());
    v.swap(compact);
    return true;
}

template <typename VecT>
static bool CompactEmptyVectorForTarget(VecT& v, size_t targetCapacityElements,
                                         size_t lwrFreeHint = SIZE_MAX)
{
    using T = typename VecT::value_type;
    if (!v.empty()) return false;
    const size_t cap = v.capacity();
    if (cap <= targetCapacityElements) return false;
    if (cap <= (targetCapacityElements * 2u + 8u)) return false;

    const size_t slackElements = cap - targetCapacityElements;
    const size_t slackBytes = slackElements * sizeof(T);
    if (slackBytes < 512u) return false;

    // Same double-alloc safety as TrimVectorSlack: compact.reserve() and old block
    // coexist in LWR until the swap â€” guard against exhausting the last bytes.
    const size_t compactBytes = targetCapacityElements * sizeof(T);
    if (compactBytes > 0u)
    {
        static constexpr size_t kTrimLwrSafetyMarginBytes = 2u * 1024u;
        const size_t lwrFree = (lwrFreeHint != SIZE_MAX)
            ? lwrFreeHint
            : static_cast<size_t>(SRL::Memory::LowWorkRam::GetReport().FreeSize);
        if (lwrFree < compactBytes + kTrimLwrSafetyMarginBytes)
            return false;
    }

    VecT compact{};
    compact.reserve(targetCapacityElements);
    v.swap(compact);
    return true;
}

template <typename VecT>
static void EnsureVectorCapacityFloor(VecT& v, size_t floor)
{
    if (floor == 0u) return;
    if (v.capacity() >= floor) return;
    v.reserve(floor);
}

template <typename SrcVecT, typename DstVecT>
static void CopyFaceSlotsToScratch(const SrcVecT& src, DstVecT& dst)
{
    dst.assign(src.begin(), src.end());
}

template <typename DstVecT, typename SrcVecT>
static void RestoreFaceSlotsFromScratch(DstVecT& dst, const SrcVecT& src)
{
    dst.assign(src.begin(), src.end());
}

static size_t CapacityFloorTrimTarget(size_t size, size_t floor, bool aggressive, size_t slack)
{
    if (aggressive) return size;
    const size_t desired = size + slack;
    return std::max(desired, floor);
}

template <typename TBlob>
static void ResetBlobBytes(TBlob& blob)
{
    // Use clear() not swap-to-empty: preserves pre-allocated capacity in LWR to
    // avoid a free+realloc TLSF overhead cycle when the next prefetch immediately
    // needs the same buffer. This mirrors the verts/faces/attrs treatment above.
    blob.bytes.clear();
    blob.size = 0;
    blob.loaded = false;
}

static void TrimRuntimeBlobScratchCaches(bool aggressive)
{
    if (aggressive)
    {
        ResetBlobBytes(g_rdrBuildScratch.blob);
        ResetBlobBytes(g_sdrBuildScratch.blob);
        ResetBlobBytes(g_rdrFamilyIdsScratch);
        ResetBlobBytes(g_sdrFamilyIdsScratch);
        // Use clear() instead of swap-to-empty: preserves pre-primed capacity from
        // PrimeRuntimeScratchCapacities(), avoiding re-allocation TLSF overhead when
        // BuildSegmentIntoPrefetch immediately needs them again.
        g_rdrBuildScratch.verts.clear();
        g_sdrBuildScratch.verts.clear();
        g_rdrBuildScratch.faces.clear();
        g_sdrBuildScratch.faces.clear();
        g_rdrBuildScratch.attrs.clear();
        g_sdrBuildScratch.attrs.clear();
        return;
    }

    // Query once for the non-aggressive blob trim pass.
    const size_t blobLwrFreeHint =
        static_cast<size_t>(SRL::Memory::LowWorkRam::GetReport().FreeSize);
    (void)TrimVectorSlack(g_rdrBuildScratch.blob.bytes, 0u, true, blobLwrFreeHint);
    (void)TrimVectorSlack(g_sdrBuildScratch.blob.bytes, 0u, true, blobLwrFreeHint);
    (void)TrimVectorSlack(g_rdrFamilyIdsScratch.bytes, 0u, true, blobLwrFreeHint);
    (void)TrimVectorSlack(g_sdrFamilyIdsScratch.bytes, 0u, true, blobLwrFreeHint);
    (void)TrimVectorSlack(g_rdrBuildScratch.verts, 0u, true, blobLwrFreeHint);
    (void)TrimVectorSlack(g_sdrBuildScratch.verts, 0u, true, blobLwrFreeHint);
    (void)TrimVectorSlack(g_rdrBuildScratch.faces, 0u, true, blobLwrFreeHint);
    (void)TrimVectorSlack(g_sdrBuildScratch.faces, 0u, true, blobLwrFreeHint);
    (void)TrimVectorSlack(g_rdrBuildScratch.attrs, 0u, true, blobLwrFreeHint);
    (void)TrimVectorSlack(g_sdrBuildScratch.attrs, 0u, true, blobLwrFreeHint);
}

static uint32_t EstimateRuntimeBlobScratchBytesHigh()
{
    // Runtime build scratch was moved to LWR to avoid HWR exhaustion/fragmentation.
    return 0u;
}

static uint32_t GetTrackOwnedHighWorkBytesExact()
{
    uint64_t bytes = 0;
    bytes += SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackCore);
    bytes += SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackPrepare);
    bytes += SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackLod);
    bytes += SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackTexture);
    bytes += SRL::Memory::HighWorkRam::GetUsedBytesByTag(SRL::Memory::DebugTag::TrackBackend);
    return (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(bytes);
}

static uint32_t ResolveTrackOwnedHighWorkBytesForPressure(uint32_t estimatedBytes)
{
    return std::max<uint32_t>(estimatedBytes, GetTrackOwnedHighWorkBytesExact());
}

static uint32_t EstimateRuntimeBlobScratchBytesLow()
{
    uint64_t bytes = 0;
    bytes += VectorCapacityBytesSafe(g_rdrBuildScratch.blob.bytes);
    bytes += VectorCapacityBytesSafe(g_sdrBuildScratch.blob.bytes);
    bytes += VectorCapacityBytesSafe(g_rdrFamilyIdsScratch.bytes);
    bytes += VectorCapacityBytesSafe(g_sdrFamilyIdsScratch.bytes);
    bytes += VectorCapacityBytesSafe(g_rdrBuildScratch.verts);
    bytes += VectorCapacityBytesSafe(g_sdrBuildScratch.verts);
    bytes += VectorCapacityBytesSafe(g_rdrBuildScratch.faces);
    bytes += VectorCapacityBytesSafe(g_sdrBuildScratch.faces);
    bytes += VectorCapacityBytesSafe(g_rdrBuildScratch.attrs);
    bytes += VectorCapacityBytesSafe(g_sdrBuildScratch.attrs);
    return (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(bytes);
}

static void NormalizeTextureFileName(const char* in, char* out, size_t outSize);
static void InvalidatePackedAssetCache(PackedAssetCache& cache);
static void InvalidateTrackRuntimePackCache(TrackRuntimePackCache& cache);
static void UpdatePackedAssetCacheTrackedBytes(PackedAssetCache& cache);
static bool ReadCdFileFully(SRL::Cd::File& file, uint32_t totalBytes, uint8_t* dst, uint32_t& outReadBytes);
static bool ParseRenTextureCopyMap(const char* json, RenTextureMap& out);
static bool ParseFaceFamilyArrayForSegment1(const char* json, Segment1TextureJson& out);
static bool ParseSegment1TextureJson(const char* json, Segment1TextureJson& out);
static bool LoadTrackRuntimePackToCart(const char* const* candidates, size_t count, TrackRuntimePackCache& cache);
static bool LoadPackedAssetIndexToCart(const char* const* candidates, size_t count, PackedAssetCache& cache);
static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentComponent::Blob& out);
static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentDrawReady::Blob& out);
static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentRuntimeDraw::Blob& out);
static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, BatchDrawReady::Blob& out);


// Validate a renderer before issuing draw calls.
// This prevents invalid state from reaching VDP1 command generation.
static bool IsRendererStateIntegral(const TrackRenderer& renderer)
{
    if (!renderer.HasTrack()) return false;
    if (renderer.MeshCount() == 0) return false;
    if (renderer.FaceCount() == 0) return false;
    if (renderer.VertexCount() == 0) return false;
    if (renderer.DrawLimit() == 0) return false;
    if (renderer.DrawLimit() > renderer.MeshCount()) return false;
    const auto stats = renderer.MemStats();
    if (stats.faces > 0 && renderer.FaceCount() != stats.faces) return false;
    if (stats.verts > 0 && renderer.VertexCount() != stats.verts) return false;
    return true;
}

// Try to repair a renderer state with safe defaults.
// Returns true when the renderer becomes integral after repair.
static bool TryRepairRendererState(TrackRenderer& renderer)
{
    if (IsRendererStateIntegral(renderer)) return true;
    if (!renderer.HasTrack()) return false;
    if (renderer.MeshCount() == 0) return false;
    renderer.SetDrawLimit(renderer.MeshCount());
    return IsRendererStateIntegral(renderer);
}

static void ConfigureStreamedRendererDefaults(TrackRenderer& renderer)
{
    renderer.ReleaseMeshCaches();
    renderer.SetUseOriginal(true);
    const bool safeSingleBackend = kEnableTrackRuntimeStabilization && kEnableSafeModeSingleRenderBackend;
    renderer.SetSglDirect(safeSingleBackend && kSafeModeRenderBackendId == 1u);
    renderer.SetVdp1Commands(safeSingleBackend && kSafeModeRenderBackendId == 2u);
    renderer.SetDirect2D(false);
    renderer.SetForceDoubleSided(false);
    renderer.SetScale(SRL::Math::Types::Fxp::BuildRaw(1 << 16));
    renderer.SetDrawLimit(renderer.MeshCount());
    // Leak-isolation mode focuses on lifetime behavior. Avoid aggressive
    // compact/realloc churn on every incoming segment.
    if (!kEnableTrackLeakIsolationFixed64Pipeline)
    {
        // Streamed segment slots must not inherit the worst-case capacity of the
        // shared scratch vectors; compact to the current segment size.
        (void)renderer.CompactRuntimeState(true);
    }
}

struct CartTextCacheEntry
{
    char key[96]{};
    void* cartPtr = nullptr;
    uint32_t size = 0;
};

static TrackLowWorkVector<CartTextCacheEntry> g_textCache{};

static void ReleaseTextCache()
{
    for (size_t i = 0; i < g_textCache.size(); ++i)
    {
        if (!g_textCache[i].cartPtr) continue;
        SRL::Memory::CartRam::Free(g_textCache[i].cartPtr);
        g_textCache[i].cartPtr = nullptr;
        g_textCache[i].size = 0;
        g_textCache[i].key[0] = '\0';
    }
    decltype(g_textCache){}.swap(g_textCache);
}

static void NormalizeLoadedTextEncoding(std::vector<char>& text)
{
    if (text.empty()) return;

    auto asU8 = [](char c) -> uint8_t { return static_cast<uint8_t>(c); };

    // Remove UTF-8 BOM
    if (text.size() >= 3 &&
        asU8(text[0]) == 0xEF &&
        asU8(text[1]) == 0xBB &&
        asU8(text[2]) == 0xBF)
    {
        text.erase(text.begin(), text.begin() + 3);
    }

    if (text.empty()) return;

    const bool hasUtf16LeBom =
        text.size() >= 2 &&
        asU8(text[0]) == 0xFF &&
        asU8(text[1]) == 0xFE;

    // Heuristic: if many NUL bytes in first chunk, assume UTF-16LE.
    bool looksUtf16Le = hasUtf16LeBom;
    if (!looksUtf16Le)
    {
        const size_t sample = (text.size() > 128) ? 128 : text.size();
        size_t nulCount = 0;
        for (size_t i = 0; i < sample; ++i)
        {
            if (text[i] == '\0') ++nulCount;
        }
        if (nulCount > sample / 4) looksUtf16Le = true;
    }

    if (looksUtf16Le)
    {
        const size_t start = hasUtf16LeBom ? 2 : 0;
        std::vector<char> out{};
        out.reserve((text.size() - start) / 2 + 1);
        for (size_t i = start; i + 1 < text.size(); i += 2)
        {
            out.push_back(text[i]); // keep low byte (ASCII subset)
        }
        text.swap(out);
    }

    // Strip trailing NULs and ensure a single terminator.
    while (!text.empty() && text.back() == '\0') text.pop_back();
    text.push_back('\0');
}

static void BuildTextCacheKey(const char* path, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!path || path[0] == '\0') return;

    const char* base = path;
    for (const char* p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    size_t n = 0;
    while (base[n] != '\0' && base[n] != ';' && n + 1 < outSize)
    {
        char c = base[n];
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        out[n] = c;
        ++n;
    }
    out[n] = '\0';
}

static bool TryReadCachedTextByKey(const char* key, std::vector<char>& outText)
{
    if (!key || key[0] == '\0') return false;
    for (size_t i = 0; i < g_textCache.size(); ++i)
    {
        const auto& e = g_textCache[i];
        if (::strcmp(e.key, key) != 0) continue;
        if (!e.cartPtr || e.size == 0) return false;

        const char* src = static_cast<const char*>(e.cartPtr);
        outText.assign(src, src + e.size);
        return !outText.empty();
    }
    return false;
}

static void StoreCachedTextByKey(const char* key, const std::vector<char>& text)
{
    if (!key || key[0] == '\0' || text.empty()) return;
    for (size_t i = 0; i < g_textCache.size(); ++i)
    {
        if (::strcmp(g_textCache[i].key, key) == 0) return;
    }

    const uint32_t bytes = static_cast<uint32_t>(text.size());
    void* mem = SRL::Memory::CartRam::Malloc(bytes);
    if (!mem) return;
    ::memcpy(mem, text.data(), bytes);

    CartTextCacheEntry e{};
    ::strncpy(e.key, key, sizeof(e.key) - 1);
    e.key[sizeof(e.key) - 1] = '\0';
    e.cartPtr = mem;
    e.size = bytes;
    g_textCache.push_back(e);
}

static bool ReadCdFileText(const char* const* names, size_t count, std::vector<char>& outText)
{
    auto tryReadOne = [&](const char* path) -> bool
    {
        if (!path || path[0] == '\0') return false;
        SRL::Debug::Print(1, 3, "TGA cd cand:%s", path);
        SRL::Cd::File f(path);
        if (!f.Exists() || f.Size.Bytes <= 0) return false;
        if (!f.Open()) return false;
        const size_t size = static_cast<size_t>(f.Size.Bytes);
        outText.assign(size + 1, '\0');
        uint32_t readBytes = 0;
        if (!ReadCdFileFully(f,
                             static_cast<uint32_t>(size),
                             reinterpret_cast<uint8_t*>(outText.data()),
                             readBytes) ||
            readBytes == 0)
        {
            outText.clear();
            return false;
        }
        outText.resize(readBytes);
        outText.push_back('\0');
        NormalizeLoadedTextEncoding(outText);
        char key[96]{};
        BuildTextCacheKey(path, key, sizeof(key));
        StoreCachedTextByKey(key, outText);
        SRL::Debug::Print(1, 3, "TGA cd ok:%s", path);
        return true;
    };

    for (size_t i = 0; i < count; ++i)
    {
        const char* n = names[i];
        if (!n || n[0] == '\0') continue;
        char key[96]{};
        BuildTextCacheKey(n, key, sizeof(key));
        if (TryReadCachedTextByKey(key, outText)) return true;
        if (tryReadOne(n)) return true;

        // Some ISO builds expose versions other than ;1 (e.g. ;11).
        if (::strchr(n, ';') == nullptr)
        {
            for (int v = 1; v <= 31; ++v)
            {
                char nv[128]{};
                std::snprintf(nv, sizeof(nv), "%s;%d", n, v);
                if (tryReadOne(nv)) return true;
            }
        }
    }
    SRL::Debug::Print(1, 3, "TGA map miss");
    return false;
}

#if TRACK_ENABLE_HOST_SEGMENTS_MAP_FALLBACK
static bool ReadLocalSegmentsMap(std::vector<char>& outText)
{
    const char* names[] = {
       // "sap.json",
      //  "SAP.JSON",
      //  "SAP",
      //  "SAP.json",
      //  "SAP;1",
        "smap",
      //  "seg_map",
      //  "SEG_MAP",
       // "segmap",
      //  "segments_map.json",
      //  "segments_map_before_rebuild.json",
      //  "segments_map",
        "SMAP"
    };
    const char* dirs[] = {
        "",
        "cd/data",
        "CD/DATA"
    };

    for (size_t d = 0; d < sizeof(dirs)/sizeof(dirs[0]); ++d)
    {
        const char* dir = dirs[d];
        for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); ++i)
        {
            const char* base = names[i];
            char path[256]{};
            if (dir[0] == '\0')
            {
                std::snprintf(path, sizeof(path), "%s", base);
            }
            else
            {
                std::snprintf(path, sizeof(path), "%s/%s", dir, base);
            }
            SRL::Debug::Print(1, 3, "TGA cand:%s", path);
            FILE* f = std::fopen(path, "rb");
            if (!f)
            {
                SRL::Debug::Print(1, 6, "TGA miss:%s e:%d", path, errno);
                continue;
            }
            std::fseek(f, 0, SEEK_END);
            const long size = std::ftell(f);
            if (size <= 0)
            {
                std::fclose(f);
                SRL::Debug::Print(1, 6, "TGA emp:%s", path);
                continue;
            }
            std::fseek(f, 0, SEEK_SET);
            outText.assign(static_cast<size_t>(size + 1), '\0');
            const size_t read = std::fread(outText.data(), 1, static_cast<size_t>(size), f);
            std::fclose(f);
            if (read == 0)
            {
                SRL::Debug::Print(1, 6, "TGA rd fail:%s", path);
                continue;
            }
            outText.resize(read);
            outText.push_back('\0');
            NormalizeLoadedTextEncoding(outText);
            SRL::Debug::Print(1, 3, "TGA loc ok:%s", path);
            return true;
        }
    }
    return false;
}
#endif

static bool ReadCdFileBinary(const char* const* names, size_t count, std::vector<uint8_t>& outData)
{
    auto tryReadOne = [&](const char* path) -> bool
    {
        if (!path || path[0] == '\0') return false;
        SRL::Cd::File f(path);
        if (!f.Exists() || f.Size.Bytes <= 0) return false;
        if (!f.Open()) return false;
        const size_t size = static_cast<size_t>(f.Size.Bytes);
        outData.resize(size);
        uint32_t readBytes = 0;
        if (!ReadCdFileFully(f,
                             static_cast<uint32_t>(size),
                             outData.data(),
                             readBytes) ||
            readBytes == 0)
        {
            outData.clear();
            return false;
        }
        outData.resize(readBytes);
        return true;
    };

    for (size_t i = 0; i < count; ++i)
    {
        const char* n = names[i];
        if (!n || n[0] == '\0') continue;
        if (tryReadOne(n)) return true;

        if (::strchr(n, ';') == nullptr)
        {
            for (int v = 1; v <= 31; ++v)
            {
                char nv[128]{};
                std::snprintf(nv, sizeof(nv), "%s;%d", n, v);
                if (tryReadOne(nv)) return true;
            }
        }
    }
    return false;
}

static constexpr const char* kSeg1RenMapCandidates[] = {
    "CD/DATA/RTMAP.TXT",
    "CD/DATA/RTMAP.TXT;1",
    "DATA/RTMAP.TXT",
    "DATA/RTMAP.TXT;1",
    "RTMAP.TXT",
    "RTMAP.TXT;1",
    "CD/DATA/ren_textures_copy_map.json",
    "CD/DATA/ren_textures_copy_map.json;1",
    "DATA/ren_textures_copy_map.json",
    "DATA/ren_textures_copy_map.json;1",
    "CD/DATA/REN_TEXTURES_COPY_MAP.JSON",
    "CD/DATA/REN_TEXTURES_COPY_MAP.JSON;1",
    "DATA/REN_TEXTURES_COPY_MAP.JSON",
    "DATA/REN_TEXTURES_COPY_MAP.JSON;1",
    "ren_textures_copy_map.json",
    "ren_textures_copy_map.json;1"
};

static constexpr const char* kSeg1SmapCandidates[] = {
    "SMAP.TXT",
    "SMAP.TXT;1",
    "CD/DATA/SMAP.TXT",
    "CD/DATA/SMAP.TXT;1",
    "DATA/SMAP.TXT",
    "DATA/SMAP.TXT;1"
};

static constexpr const char* kSeg1LegacySegmentsMapCandidates[] = {
    "CD/DATA/segments_map.json",
    "CD/DATA/segments_map.json;1",
    "DATA/segments_map.json",
    "DATA/segments_map.json;1",
    "segments_map.json",
    "segments_map.json;1"
};

static constexpr const char* kSeg1AnyTextureMapCandidates[] = {
    "CD/DATA/segments_map.json",
    "CD/DATA/segments_map.json;1",
    "DATA/segments_map.json",
    "DATA/segments_map.json;1",
    "segments_map.json",
    "segments_map.json;1",
    "SMAP.TXT;1",
    "CD/DATA/SMAP.TXT",
    "CD/DATA/SMAP.TXT;1",
    "DATA/SMAP.TXT",
    "DATA/SMAP.TXT;1",
    "SMAP.TXT"
};

static constexpr const char* kSeg1FaceFamilyMapCandidates[] = {
    "CD/DATA/S001FAM.BIN",
    "CD/DATA/S001FAM.BIN;1",
    "DATA/S001FAM.BIN",
    "DATA/S001FAM.BIN;1",
    "S001FAM.BIN",
    "S001FAM.BIN;1",
    "s001fam.bin",
    "s001fam.bin;1"
};

static constexpr int kSeg1FamilyLodValues[4] = { 32, 64, 32, 64 };

static void SetSeg1TgaLastName(const char* name);
template <typename Catalog>
static bool HasSeg1TgaCartEntry(const Catalog& catalog, const char* name);

static bool ReadSeg1RenMapText(std::vector<char>& outText)
{
    return ReadCdFileText(kSeg1RenMapCandidates, std::size(kSeg1RenMapCandidates), outText);
}

static bool ReadSeg1SmapText(std::vector<char>& outText)
{
    return ReadCdFileText(kSeg1SmapCandidates, std::size(kSeg1SmapCandidates), outText);
}

static bool ReadSeg1LegacySegmentsMapText(std::vector<char>& outText)
{
    return ReadCdFileText(kSeg1LegacySegmentsMapCandidates,
                          std::size(kSeg1LegacySegmentsMapCandidates),
                          outText);
}

static bool ReadSeg1AnyTextureMapText(std::vector<char>& outText)
{
    return ReadCdFileText(kSeg1AnyTextureMapCandidates,
                          std::size(kSeg1AnyTextureMapCandidates),
                          outText);
}

static bool LoadSeg1RenTextureCopyMapFromCd(std::vector<char>& outText, RenTextureMap& out)
{
    return ReadSeg1RenMapText(outText) && ParseRenTextureCopyMap(outText.data(), out);
}

static bool LoadSeg1TextureJsonFromSmapCd(std::vector<char>& outText, Segment1TextureJson& out)
{
    return ReadSeg1SmapText(outText) && ParseSegment1TextureJson(outText.data(), out);
}

static bool LoadSeg1TextureJsonFromLegacyMapCd(std::vector<char>& outText, Segment1TextureJson& out)
{
    return ReadSeg1LegacySegmentsMapText(outText) && ParseSegment1TextureJson(outText.data(), out);
}

static bool LoadSeg1TextureJsonFromAnyMapCd(std::vector<char>& outText, Segment1TextureJson& out)
{
    return ReadSeg1AnyTextureMapText(outText) && ParseSegment1TextureJson(outText.data(), out);
}

static bool LoadSeg1FaceFamilyArrayFromAnyMapCd(std::vector<char>& outText, Segment1TextureJson& out)
{
    return ReadSeg1AnyTextureMapText(outText) && ParseFaceFamilyArrayForSegment1(outText.data(), out);
}

template <typename Catalog, typename Loader>
static size_t ScanSeg1TgaTokensAndLoad(const Catalog& catalog,
                                       const std::vector<char>& text,
                                       Loader&& loadNameToCart,
                                       size_t& outTokenHits,
                                       char* firstToken,
                                       size_t firstTokenSize)
{
    outTokenHits = 0;
    if (firstToken && firstTokenSize > 0) firstToken[0] = '\0';
    auto isTgaNameChar = [](char c) -> bool
    {
        return (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               c == '_' || c == '-' || c == '.';
    };

    const char* scan = text.data();
    size_t extracted = 0;
    while (scan && *scan)
    {
        const char* dot = ::strstr(scan, ".tga");
        if (!dot) dot = ::strstr(scan, ".TGA");
        if (!dot) break;

        const char* begin = dot;
        while (begin > text.data() && isTgaNameChar(*(begin - 1))) --begin;

        char name[64]{};
        size_t nameLen = static_cast<size_t>((dot - begin) + 4);
        if (nameLen >= sizeof(name)) nameLen = sizeof(name) - 1;
        ::memcpy(name, begin, nameLen);
        name[nameLen] = '\0';
        SetSeg1TgaLastName(name);
        ++outTokenHits;

        if (firstToken && firstTokenSize > 0 && firstToken[0] == '\0')
        {
            ::strncpy(firstToken, name, firstTokenSize - 1);
            firstToken[firstTokenSize - 1] = '\0';
        }

        if (name[0] != '\0' && !HasSeg1TgaCartEntry(catalog, name))
        {
            if (loadNameToCart(name)) ++extracted;
            else SRL::Debug::Print(1, 7, "TGA miss:%s", name);
        }
        scan = dot + 4;
    }

    return extracted;
}

static bool ParseIntAfterKey(const char* p, const char* key, int& out)
{
    char pattern[64]{};
    std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* k = strstr(p, pattern);
    if (!k) return false;
    const char* c = strchr(k, ':');
    if (!c) return false;
    ++c;
    while (*c && std::isspace(static_cast<unsigned char>(*c))) ++c;
    const long v = strtol(c, nullptr, 10);
    out = static_cast<int>(v);
    return true;
}

static bool ParseString64AfterKey(const char* p, const char* key, char* out, size_t outSize)
{
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    char pattern[64]{};
    std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* k = strstr(p, pattern);
    if (!k) return false;
    const char* c = strchr(k, ':');
    if (!c) return false;
    const char* q0 = strchr(c, '"');
    if (!q0) return false;
    ++q0;
    const char* q1 = strchr(q0, '"');
    if (!q1) return false;
    size_t n = static_cast<size_t>(q1 - q0);
    if (n >= outSize) n = outSize - 1;
    memcpy(out, q0, n);
    out[n] = '\0';
    return n > 0;
}

static bool ParseTexbankIndex(const char* json, TexbankIndexLod& out)
{
    out.count = 0;
    if (!json || json[0] == '\0') return false;

    const char* entries = strstr(json, "\"entries\"");
    if (!entries) return false;

    const char* p = entries;
    while (p && out.count < 128)
    {
        const char* idk = strstr(p, "\"familyId\"");
        if (!idk) break;

        int fam = -1;
        if (!ParseIntAfterKey(idk, "familyId", fam) || fam <= 0)
        {
            p = idk + 10;
            continue;
        }

        char file[64]{};
        if (!ParseString64AfterKey(idk, "file", file, sizeof(file)))
        {
            p = idk + 10;
            continue;
        }

        out.familyIds[out.count] = fam;
        strncpy(out.files[out.count], file, sizeof(out.files[out.count]) - 1);
        ++out.count;
        p = idk + 10;
    }
    return out.count > 0;
}

static bool ParseRenTextureCopyMap(const char* json, RenTextureMap& out)
{
    out.entries.clear();
    out.entries.reserve(1024);
    if (!json || json[0] == '\0') return false;

    const char* items = strstr(json, "\"items\"");
    if (!items) return false;
    const char* p = items;
    while (p)
    {
        const char* sk = strstr(p, "\"source_name\"");
        if (!sk) break;

        char src[64]{};
        char dst[32]{};
        int lod = 0;
        if (!ParseString64AfterKey(sk, "source_name", src, sizeof(src)))
        {
            p = sk + 12;
            continue;
        }
        if (!ParseString64AfterKey(sk, "target_name", dst, sizeof(dst)))
        {
            p = sk + 12;
            continue;
        }
        (void)ParseIntAfterKey(sk, "lod", lod);
        if (lod != 8 && lod != 16 && lod != 32 && lod != 64)
        {
            p = sk + 12;
            continue;
        }

        RenTextureMapEntry e{};
        strncpy(e.sourceName, src, sizeof(e.sourceName) - 1);
        strncpy(e.targetName, dst, sizeof(e.targetName) - 1);
        e.lod = lod;
        out.entries.push_back(e);
        p = sk + 12;
    }
    return !out.entries.empty();
}

static int FindTexbankFileIndexByFamily(const TexbankIndexLod& idx, int familyId)
{
    for (size_t i = 0; i < idx.count; ++i)
    {
        if (idx.familyIds[i] == familyId) return static_cast<int>(i);
    }
    return -1;
}

static bool FindRenamedTarget(const RenTextureMap& map, const char* sourceName, int lod, char* out, size_t outSize)
{
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (!sourceName || sourceName[0] == '\0') return false;

    char srcNorm[64]{};
    NormalizeTextureFileName(sourceName, srcNorm, sizeof(srcNorm));

    auto eqIgnoreCase = [](const char* a, const char* b) -> bool
    {
        if (!a || !b) return false;
        while (*a && *b)
        {
            char ca = *a;
            char cb = *b;
            if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 'a' + 'A');
            if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 'a' + 'A');
            if (ca != cb) return false;
            ++a; ++b;
        }
        return (*a == '\0' && *b == '\0');
    };

    for (size_t i = 0; i < map.entries.size(); ++i)
    {
        const auto& e = map.entries[i];
        if (e.lod != lod) continue;
        if (eqIgnoreCase(e.sourceName, srcNorm))
        {
            strncpy(out, e.targetName, outSize - 1);
            out[outSize - 1] = '\0';
            return out[0] != '\0';
        }
    }
    return false;
}

static bool ParseFaceFamilyArrayForSegment1(const char* json, Segment1TextureJson& out)
{
    const char* segs = strstr(json, "\"segments\"");
    if (!segs) return false;
    const char* arrKey = nullptr;
    const char* scanPos = segs;
    while (scanPos && *scanPos)
    {
        const char* idk = strstr(scanPos, "\"id\"");
        if (!idk) break;

        int segId = -1;
        if (!ParseIntAfterKey(idk, "id", segId))
        {
            scanPos = idk + 4;
            continue;
        }

        const char* nextId = strstr(idk + 4, "\"id\"");
        const char* objEnd = nextId ? nextId : (json + strlen(json));
        if (segId == 1)
        {
            const char* candidate = strstr(idk, "\"faceTextureFamily\"");
            if (candidate && candidate < objEnd)
            {
                arrKey = candidate;
                break;
            }
        }
        scanPos = idk + 4;
    }
    if (!arrKey) return false;
    const char* b0 = strchr(arrKey, '[');
    if (!b0) return false;
    const char* b1 = strchr(b0, ']');
    if (!b1) return false;

    out.faceFamily.clear();
    const char* arrPos = b0 + 1;
    while (arrPos < b1)
    {
        while (arrPos < b1 && (std::isspace(static_cast<unsigned char>(*arrPos)) || *arrPos == ',')) ++arrPos;
        if (arrPos >= b1) break;
        char* endp = nullptr;
        const long v = strtol(arrPos, &endp, 10);
        if (endp == arrPos)
        {
            ++arrPos;
            continue;
        }
        out.faceFamily.push_back(static_cast<int>(v));
        arrPos = endp;
    }
    return !out.faceFamily.empty();
}

static bool ParseTextureFamilies64(const char* json, Segment1TextureJson& out)
{
    out.familyCount = 0;
    const char* tf = strstr(json, "\"textureFamilies\"");
    if (!tf) return false;
    const char* end = strstr(tf, "\"segments\"");
    if (!end) end = json + strlen(json);

    const char* p = tf;
    while (p && p < end && out.familyCount < 512)
    {
        const char* idk = strstr(p, "\"id\"");
        if (!idk || idk >= end) break;
        int famId = -1;
        if (!ParseIntAfterKey(idk, "id", famId)) { p = idk + 4; continue; }

        // Prefer imageFiles -> "64", fallback variants -> "64"
        char tex[64]{};
        const char* nextId = strstr(idk + 4, "\"id\"");
        if (!nextId || nextId > end) nextId = end;

        const char* img = strstr(idk, "\"imageFiles\"");
        if (img && img < nextId)
        {
            (void)ParseString64AfterKey(img, "64", tex, sizeof(tex));
        }
        if (tex[0] == '\0')
        {
            const char* var = strstr(idk, "\"variants\"");
            if (var && var < nextId)
            {
                (void)ParseString64AfterKey(var, "64", tex, sizeof(tex));
            }
        }
        if (famId >= 0 && tex[0] != '\0')
        {
            out.familyIds[out.familyCount] = famId;
            strncpy(out.familyTex64[out.familyCount], tex, sizeof(out.familyTex64[out.familyCount]) - 1);
            ++out.familyCount;
        }
        p = idk + 4;
    }
    return out.familyCount > 0;
}

static void NormalizeTextureFileName(const char* in, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!in || in[0] == '\0') return;

    const char* base = in;
    for (const char* p = in; *p; ++p)
    {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    strncpy(out, base, outSize - 1);
    out[outSize - 1] = '\0';

    const char* dot = strrchr(out, '.');
    if (!dot)
    {
        const size_t n = strlen(out);
        if (n + 4 < outSize) strcat(out, ".tga");
        return;
    }

    auto ieqExt = [](const char* a, const char* b) -> bool
    {
        if (!a || !b) return false;
        while (*a && *b)
        {
            char ca = *a;
            char cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
            if (ca != cb) return false;
            ++a; ++b;
        }
        return (*a == '\0' && *b == '\0');
    };
    if (ieqExt(dot, ".tga") || ieqExt(dot, ".png"))
    {
        return;
    }

    const size_t idx = static_cast<size_t>(dot - out);
    out[idx] = '\0';
    const size_t n = strlen(out);
    if (n + 4 < outSize) strcat(out, ".tga");
}

static void BuildLodTextureName(const char* srcName, int lod, bool compactNoUnderscore, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!srcName || srcName[0] == '\0') return;

    char norm[64]{};
    NormalizeTextureFileName(srcName, norm, sizeof(norm));
    if (norm[0] == '\0') return;

    const char* dot = ::strrchr(norm, '.');
    char base[64]{};
    char ext[16]{};
    if (dot)
    {
        const size_t bn = static_cast<size_t>(dot - norm);
        const size_t cpy = (bn < sizeof(base) - 1) ? bn : (sizeof(base) - 1);
        ::memcpy(base, norm, cpy);
        base[cpy] = '\0';
        ::strncpy(ext, dot, sizeof(ext) - 1);
    }
    else
    {
        ::strncpy(base, norm, sizeof(base) - 1);
        ::strncpy(ext, ".tga", sizeof(ext) - 1);
    }

    char lodTxt[4]{};
    std::snprintf(lodTxt, sizeof(lodTxt), "%d", lod);

    auto ends_with = [](const char* s, const char* suf) -> bool
    {
        const size_t ls = ::strlen(s);
        const size_t lf = ::strlen(suf);
        if (ls < lf) return false;
        return ::strncmp(s + (ls - lf), suf, lf) == 0;
    };

    bool replaced = false;
    const char* tails[] = { "_64", "_32", "64", "32" };
    for (size_t i = 0; i < sizeof(tails) / sizeof(tails[0]); ++i)
    {
        if (!ends_with(base, tails[i])) continue;
        const size_t lb = ::strlen(base);
        const size_t lt = ::strlen(tails[i]);
        const size_t keep = (lb >= lt) ? (lb - lt) : 0;
        char tmp[64]{};
        if (keep > 0) ::memcpy(tmp, base, keep);
        tmp[keep] = '\0';
        if (tails[i][0] == '_')
        {
            std::snprintf(base, sizeof(base), "%s_%s", tmp, lodTxt);
        }
        else
        {
            std::snprintf(base, sizeof(base), "%s%s", tmp, lodTxt);
        }
        replaced = true;
        break;
    }

    if (!replaced)
    {
        const size_t lb = ::strlen(base);
        const size_t needU = lb + 1 + ::strlen(lodTxt);
        if (!compactNoUnderscore && needU <= 8)
        {
            std::snprintf(base, sizeof(base), "%s_%s", base, lodTxt);
        }
        else
        {
            std::snprintf(base, sizeof(base), "%s%s", base, lodTxt);
        }
    }

    if (compactNoUnderscore)
    {
        const size_t lb = ::strlen(base);
        if (lb >= 2)
        {
            const char c0 = base[lb - 1];
            const char c1 = base[lb - 2];
            const bool isDigit0 = (c0 >= '0' && c0 <= '9');
            const bool isDigit1 = (c1 >= '0' && c1 <= '9');
            if (isDigit0 && base[lb - 2] == '_' && (lb >= 3))
            {
                ::memmove(base + lb - 2, base + lb - 1, 2);
            }
            else if (isDigit0 && isDigit1 && (lb >= 3) && base[lb - 3] == '_')
            {
                ::memmove(base + lb - 3, base + lb - 2, 3);
            }
        }
    }

    std::snprintf(out, outSize, "%s%s", base, ext);
}

static int32_t TryLoadTextureFromCd(const char* fileName)
{
    if (!fileName || fileName[0] == '\0') return -1;

    static char sLastTriedTexPath[96]{};
    static uint8_t sLastTriedTexOpen = 0;

    char upperName[80]{};
    ::strncpy(upperName, fileName, sizeof(upperName) - 1);
    for (size_t i = 0; upperName[i] != '\0'; ++i)
    {
        if (upperName[i] >= 'a' && upperName[i] <= 'z')
        {
            upperName[i] = static_cast<char>(upperName[i] - 'a' + 'A');
        }
    }

    // ISO9660 Level-1 fallback alias (8.3): e.g. ASFALTO_32.TGA -> ASFALTO_.TGA
    char iso83[80]{};
    {
        const char* dot = ::strrchr(upperName, '.');
        if (dot)
        {
            const size_t baseLen = static_cast<size_t>(dot - upperName);
            const char* ext = dot + 1;
            char base[16]{};
            size_t n = baseLen;
            if (n > 8) n = 8;
            for (size_t i = 0; i < n; ++i) base[i] = upperName[i];
            base[n] = '\0';

            char ext3[8]{};
            size_t e = 0;
            while (ext[e] != '\0' && e < 3) { ext3[e] = ext[e]; ++e; }
            ext3[e] = '\0';

            if (base[0] != '\0' && ext3[0] != '\0')
            {
                std::snprintf(iso83, sizeof(iso83), "%s.%s", base, ext3);
            }
        }
    }

    char p0[96]{}, p1[96]{}, p2[96]{}, p3[96]{}, p4[80]{}, p5[80]{}, p6[96]{}, p7[96]{};
    char u0[96]{}, u1[96]{}, u2[96]{}, u3[96]{}, u4[80]{}, u5[80]{}, u6[96]{}, u7[96]{};
    std::snprintf(p0, sizeof(p0), "CD/DATA/%s", fileName);
    std::snprintf(p1, sizeof(p1), "CD/DATA/%s;1", fileName);
    std::snprintf(p2, sizeof(p2), "DATA/%s", fileName);
    std::snprintf(p3, sizeof(p3), "DATA/%s;1", fileName);
    std::snprintf(p4, sizeof(p4), "%s", fileName);
    std::snprintf(p5, sizeof(p5), "%s;1", fileName);
    std::snprintf(p6, sizeof(p6), "data/%s", fileName);
    std::snprintf(p7, sizeof(p7), "data/%s;1", fileName);
    std::snprintf(u0, sizeof(u0), "CD/DATA/%s", upperName);
    std::snprintf(u1, sizeof(u1), "CD/DATA/%s;1", upperName);
    std::snprintf(u2, sizeof(u2), "DATA/%s", upperName);
    std::snprintf(u3, sizeof(u3), "DATA/%s;1", upperName);
    std::snprintf(u4, sizeof(u4), "%s", upperName);
    std::snprintf(u5, sizeof(u5), "%s;1", upperName);
    std::snprintf(u6, sizeof(u6), "data/%s", upperName);
    std::snprintf(u7, sizeof(u7), "data/%s;1", upperName);

    char i0[96]{}, i1[96]{}, i2[96]{}, i3[96]{}, i4[80]{}, i5[80]{};
    if (iso83[0] != '\0')
    {
        std::snprintf(i0, sizeof(i0), "CD/DATA/%s", iso83);
        std::snprintf(i1, sizeof(i1), "CD/DATA/%s;1", iso83);
        std::snprintf(i2, sizeof(i2), "DATA/%s", iso83);
        std::snprintf(i3, sizeof(i3), "DATA/%s;1", iso83);
        std::snprintf(i4, sizeof(i4), "%s", iso83);
        std::snprintf(i5, sizeof(i5), "%s;1", iso83);
    }

    const char* cands[] = { p0, p1, p2, p3, p4, p5, p6, p7, u0, u1, u2, u3, u4, u5, u6, u7, i0, i1, i2, i3, i4, i5 };
    for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]); ++i)
    {
        ::strncpy(sLastTriedTexPath, cands[i], sizeof(sLastTriedTexPath) - 1);
        sLastTriedTexPath[sizeof(sLastTriedTexPath) - 1] = '\0';
        sLastTriedTexOpen = 0;
        SRL::Cd::File f(cands[i]);
        if (!f.Exists() || f.Size.Bytes <= 0) continue;
        if (!f.Open()) continue;
        sLastTriedTexOpen = 1;
        SRL::Bitmap::TGA bmp(&f);
        const int32_t slot = SRL::VDP1::TryLoadTexture((SRL::Bitmap::IBitmap*)&bmp);
        if (slot > 0) return slot;
    }

    // Probe version suffixes ;1..;31 (some ISO builds can expose higher versions).
    auto tryVersionRange = [&](const char* base) -> int32_t
    {
        if (!base || base[0] == '\0') return -1;
        for (int v = 1; v <= 31; ++v)
        {
            char n[96]{};
            std::snprintf(n, sizeof(n), "%s;%d", base, v);
            ::strncpy(sLastTriedTexPath, n, sizeof(sLastTriedTexPath) - 1);
            sLastTriedTexPath[sizeof(sLastTriedTexPath) - 1] = '\0';
            sLastTriedTexOpen = 0;
            SRL::Cd::File f(n);
            if (!f.Exists() || f.Size.Bytes <= 0) continue;
            if (!f.Open()) continue;
            sLastTriedTexOpen = 1;
            SRL::Bitmap::TGA bmp(&f);
            const int32_t slot = SRL::VDP1::TryLoadTexture((SRL::Bitmap::IBitmap*)&bmp);
            if (slot > 0) return slot;
        }
        return -1;
    };
    {
        const char* bases[] = { p0, p2, p4, p6, u0, u2, u4, u6, i0, i2, i4 };
        for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i)
        {
            const int32_t s = tryVersionRange(bases[i]);
            if (s > 0) return s;
        }
    }

    // Fallback: some CD setups only resolve by current directory.
    const char* dir1[] = { "DATA", "data", nullptr };
    const char* names[] = { fileName, upperName };
    for (size_t d = 0; d < sizeof(dir1) / sizeof(dir1[0]); ++d)
    {
        SRL::Cd::ChangeDir((const char*)0);
        if (dir1[d]) SRL::Cd::ChangeDir(dir1[d]);
        for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); ++n)
        {
            if (!names[n] || names[n][0] == '\0') continue;
            char n0[80]{}, n1[80]{};
            std::snprintf(n0, sizeof(n0), "%s", names[n]);
            std::snprintf(n1, sizeof(n1), "%s;1", names[n]);
            const char* nc[] = { n0, n1 };
            for (size_t i = 0; i < sizeof(nc) / sizeof(nc[0]); ++i)
            {
                ::strncpy(sLastTriedTexPath, nc[i], sizeof(sLastTriedTexPath) - 1);
                sLastTriedTexPath[sizeof(sLastTriedTexPath) - 1] = '\0';
                sLastTriedTexOpen = 0;
                SRL::Cd::File f(nc[i]);
                if (!f.Exists() || f.Size.Bytes <= 0) continue;
                if (!f.Open()) continue;
                sLastTriedTexOpen = 1;
                SRL::Bitmap::TGA bmp(&f);
                const int32_t slot = SRL::VDP1::TryLoadTexture((SRL::Bitmap::IBitmap*)&bmp);
                if (slot > 0)
                {
                    SRL::Cd::ChangeDir((const char*)0);
                    return slot;
                }
            }
        }
    }
    SRL::Cd::ChangeDir((const char*)0);
    SRL::Debug::Print(1, 19, "TXf op:%u p:%s", (unsigned)sLastTriedTexOpen, sLastTriedTexPath);
    return -1;
}

static uint32_t TextureByteSize(uint16_t width, uint16_t height, SRL::CRAM::TextureColorMode colorMode)
{
    uint16_t sh = 0;
    switch (colorMode)
    {
    case SRL::CRAM::TextureColorMode::Paletted256:
    case SRL::CRAM::TextureColorMode::Paletted128:
    case SRL::CRAM::TextureColorMode::Paletted64:
        sh = 1;
        break;
    case SRL::CRAM::TextureColorMode::Paletted16:
        sh = 2;
        break;
    default:
        sh = 0;
        break;
    }
    return static_cast<uint32_t>(((static_cast<uint32_t>(width) * static_cast<uint32_t>(height)) << 1) >> sh);
}

static bool TryOverwriteTextureSlotFromCd(int32_t slot, const char* fileName)
{
    if (slot <= 0 || slot >= SRL_MAX_TEXTURES) return false;
    auto* tex = SRL::VDP1::Metadata[slot].Texture;
    if (!tex) return false;

    const char* dirs[][2] = {
        { "DATA", nullptr },
        { "data", nullptr },
        { nullptr, nullptr }
    };
    for (const auto& d : dirs)
    {
        SRL::Cd::ChangeDir((const char*)0);
        if (d[0]) SRL::Cd::ChangeDir(d[0]);
        if (d[1]) SRL::Cd::ChangeDir(d[1]);

        SRL::Cd::File f(fileName);
        if (!f.Exists() || f.Size.Bytes <= 0) continue;
        SRL::Bitmap::TGA bmp(&f);
        const SRL::Bitmap::BitmapInfo info = bmp.GetInfo();
        const auto expectedMode = SRL::VDP1::Metadata[slot].ColorMode;
        if (static_cast<uint16_t>(info.Width) != tex->Width || static_cast<uint16_t>(info.Height) != tex->Height)
        {
            SRL::Cd::ChangeDir((const char*)0);
            return false;
        }
        if (static_cast<SRL::CRAM::TextureColorMode>(info.ColorMode) != expectedMode)
        {
            SRL::Cd::ChangeDir((const char*)0);
            return false;
        }
        uint8_t* dst = SRL::VDP1::Metadata[slot].GetData();
        if (!dst || !bmp.GetData())
        {
            SRL::Cd::ChangeDir((const char*)0);
            return false;
        }
        const uint32_t bytes = TextureByteSize(tex->Width, tex->Height, expectedMode);
        slDMACopy(bmp.GetData(), dst, bytes);
        SRL::Cd::ChangeDir((const char*)0);
        return true;
    }
    SRL::Cd::ChangeDir((const char*)0);
    return false;
}

static int FindFamilyIndex(const Segment1TextureJson& map1, int familyId)
{
    for (size_t i = 0; i < map1.familyCount; ++i)
    {
        if (map1.familyIds[i] == familyId) return static_cast<int>(i);
    }
    return -1;
}

template <typename FaceFamilyVecT>
static size_t BuildUniqueUsedFamilies(const FaceFamilyVecT& faceFamily, int* outIds, size_t outCap)
{
    if (!outIds || outCap == 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < faceFamily.size(); ++i)
    {
        const int fam = faceFamily[i];
        if (fam < 0) continue;
        bool exists = false;
        for (size_t j = 0; j < count; ++j)
        {
            if (outIds[j] == fam) { exists = true; break; }
        }
        if (!exists && count < outCap)
        {
            outIds[count++] = fam;
        }
    }
    return count;
}

static bool ContainsFamily(const int* ids, size_t count, int familyId)
{
    if (!ids) return false;
    for (size_t i = 0; i < count; ++i)
    {
        if (ids[i] == familyId) return true;
    }
    return false;
}

static bool ParseSegment1TextureJson(const char* json, Segment1TextureJson& out)
{
    out.faceFamily.clear();
    out.familyCount = 0;
    if (!json || json[0] == '\0') return false;
    (void)ParseTextureFamilies64(json, out);
    const bool facesOk = ParseFaceFamilyArrayForSegment1(json, out);
    return facesOk;
}

static size_t CollectAllVariantTextureNames(const char* json, char outNames[][64], size_t outCap)
{
    if (!json || !outNames || outCap == 0) return 0;
    auto isTgaNameChar = [](char c) -> bool
    {
        return (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               c == '_' || c == '-' || c == '.';
    };
    auto nameEqualsIgnoreCase = [](const char* a, const char* b) -> bool
    {
        if (!a || !b) return false;
        while (*a && *b)
        {
            char ca = *a;
            char cb = *b;
            if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 'a' + 'A');
            if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 'a' + 'A');
            if (ca != cb) return false;
            ++a; ++b;
        }
        return (*a == '\0' && *b == '\0');
    };

    size_t count = 0;
    const char* p = json;
    while (p && *p)
    {
        const char* v = strstr(p, "\"variants\"");
        if (!v) break;
        const char* obj0 = strchr(v, '{');
        if (!obj0) { p = v + 10; continue; }
        const char* obj1 = strchr(obj0, '}');
        if (!obj1) { p = obj0 + 1; continue; }

        const char* q = obj0;
        while (q && q < obj1)
        {
            const char* tga = strstr(q, ".TGA");
            if (!tga || tga >= obj1) break;
            const char* b = tga;
            while (b > obj0 && isTgaNameChar(*(b - 1))) --b;
            char name[64]{};
            size_t n = static_cast<size_t>((tga - b) + 4);
            if (n >= sizeof(name)) n = sizeof(name) - 1;
            memcpy(name, b, n);
            name[n] = '\0';

            bool exists = false;
            for (size_t i = 0; i < count; ++i)
            {
                if (nameEqualsIgnoreCase(outNames[i], name)) { exists = true; break; }
            }
            if (!exists && count < outCap)
            {
                ::strncpy(outNames[count], name, 63);
                outNames[count][63] = '\0';
                ++count;
            }
            q = tga + 4;
        }
        p = obj1 + 1;
    }
    return count;
}

static uint16_t ReadLe16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

static uint32_t ReadLe32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static bool LoadSeg1FaceFamilyMapFromCd(Segment1TextureJson& outMap)
{
    std::vector<uint8_t> mapBin{};
    if (!ReadCdFileBinary(kSeg1FaceFamilyMapCandidates,
                          std::size(kSeg1FaceFamilyMapCandidates),
                          mapBin) ||
        mapBin.size() < 12)
    {
        return false;
    }

    const uint32_t magic = ReadLe32(mapBin.data() + 0);
    const uint16_t ver = ReadLe16(mapBin.data() + 4);
    const uint16_t segId = ReadLe16(mapBin.data() + 8);
    const uint16_t faceCount = ReadLe16(mapBin.data() + 10);
    const size_t need = static_cast<size_t>(12) + static_cast<size_t>(faceCount) * sizeof(uint16_t);
    if (magic != 0x4D463153 || ver != 1 || segId != 1 || mapBin.size() < need)
    {
        return false;
    }

    outMap.faceFamily.clear();
    outMap.faceFamily.reserve(faceCount);
    for (uint16_t i = 0; i < faceCount; ++i)
    {
        const size_t off = 12 + static_cast<size_t>(i) * 2;
        outMap.faceFamily.push_back(static_cast<int>(ReadLe16(mapBin.data() + off)));
    }

    return !outMap.faceFamily.empty();
}

struct TrackedPaletteBankState
{
    std::array<uint8_t, 128> pal16{};
    std::array<uint8_t, 32> pal64{};
    std::array<uint8_t, 16> pal128{};
    std::array<uint8_t, 8> pal256{};
};

static TrackedPaletteBankState g_trackPaletteBanks{};
static TrackLowWorkU16Vector g_trackReusableTextureSlots{};
static TrackLowWorkU16Vector g_trackPendingRetiredTextureSlots{};
// Keep queue capacities stable to avoid end-frame LWR churn when slots are
// retired/reused continuously during long races.
static size_t g_trackTextureSlotQueueCapacityFloor = 0u;
static std::array<uint8_t, SRL_MAX_TEXTURES> g_trackReusableTextureSlotFlags{};
static std::array<uint8_t, SRL_MAX_TEXTURES> g_trackPendingRetiredTextureSlotFlags{};

// Verifica se o slot estÃ¡ vivo E ainda pertence exclusivamente a esta famÃ­lia
// (i.e., nÃ£o foi aposentado nem estÃ¡ no pool de reuso).
// Slots aposentados/reutilizÃ¡veis podem ter sido realocados para outra textura,
// causando textura errada na face mesmo com IsVdp1TextureSlotLive() == true.
static bool IsVdp1TextureSlotActiveAndOwned(uint16_t slot)
{
    if (!IsVdp1TextureSlotLive(slot)) return false;
    if (slot < g_trackPendingRetiredTextureSlotFlags.size() &&
        g_trackPendingRetiredTextureSlotFlags[slot] != 0u) return false;
    if (slot < g_trackReusableTextureSlotFlags.size() &&
        g_trackReusableTextureSlotFlags[slot] != 0u) return false;
    return true;
}

static std::array<uint8_t, SRL_MAX_TEXTURES> g_trackReusableTextureSlotPaletteReleased{};
static std::array<uint8_t, SRL_MAX_TEXTURES> g_trackReusableTextureSlotReuseCooldown{};
// Keep the effective byte capacity per slot. Width/height metadata can shrink
// when a large slot is reused for a smaller lod; capacity must stay at the
// largest allocation so the same slot can be reused again for larger lods.
static std::array<uint32_t, SRL_MAX_TEXTURES> g_trackTextureSlotCapacityBytes{};
static uint16_t g_trackUploadsFreshThisFrame = 0u;
static uint16_t g_trackUploadsReusedThisFrame = 0u;
static uint16_t g_trackRetiredQueuedThisFrame = 0u;
static uint16_t g_trackRetiredFlushedThisFrame = 0u;
// Give VDP1/CRAM state more time to age out before a retired slot is reused.
// A short delay can show as one-frame wrong colors when a slot/palette pair is
// recycled while the previous frame is still effectively in flight.
// Reuse retired slots on the next frame. A long delay inflates the live slot
// count during continuous slides and drains LWR even when the 20-slot window is
// stable.
// Delay minimo > 1 frame reduz risco de "palette tear" visivel como roxo.
// Reuse on the next frame to keep the texture slot pool stable during
// continuous slides (fast movement / low FPS scenarios).
static constexpr uint8_t kReusableTrackSlotReuseDelayFrames = 1u;
static constexpr bool kDisableTrackReusableSlotReuseForDiagnostics = false;

// In stabilized runtime, keep family merge cadence short enough to recycle
// outgoing families promptly without forcing a merge every single slide.
static constexpr uint8_t kStabilizedFamilyMergeCooldownFrames = 2u;
// In fixed64 leak-isolation runs, aggressive maintenance on every frame/slide
// costs too much CPU and collapses FPS. Keep emergency reaction immediate, but
// run regular maintenance at a lower cadence.
static constexpr uint8_t kLeakIsolationInitialMaintenanceCadenceFrames = 90u;
static constexpr uint8_t kLeakIsolationPostSlideMaintenanceCadenceFrames = 90u;

static uint8_t ResolveFamilyMergeCooldownFrames(bool fullTrackFamilyCacheReady)
{
    if (kEnableTrackLeakIsolationFixed64Pipeline) return 0u;
    return fullTrackFamilyCacheReady ? 1u : kStabilizedFamilyMergeCooldownFrames;
}

static void ReleaseTrackedPaletteBankForSlot(uint16_t slot);

static void EnsureTrackTextureSlotQueueCapacityFloor(size_t floor)
{
    if (floor == 0u) return;
    floor = std::min<size_t>(floor, static_cast<size_t>(SRL_MAX_TEXTURES));
    if (floor == 0u) return;
    if (floor > g_trackTextureSlotQueueCapacityFloor)
    {
        g_trackTextureSlotQueueCapacityFloor = floor;
    }
    if (g_trackReusableTextureSlots.capacity() < g_trackTextureSlotQueueCapacityFloor)
    {
        g_trackReusableTextureSlots.reserve(g_trackTextureSlotQueueCapacityFloor);
    }
    if (g_trackPendingRetiredTextureSlots.capacity() < g_trackTextureSlotQueueCapacityFloor)
    {
        g_trackPendingRetiredTextureSlots.reserve(g_trackTextureSlotQueueCapacityFloor);
    }
}

static void ResetReusableTrackTextureSlots()
{
    if (g_trackTextureSlotQueueCapacityFloor == 0u)
    {
        g_trackTextureSlotQueueCapacityFloor =
            std::min<size_t>(static_cast<size_t>(SRL_MAX_TEXTURES), 256u);
    }
    g_trackReusableTextureSlots.clear();
    g_trackPendingRetiredTextureSlots.clear();
    g_trackReusableTextureSlotFlags.fill(0u);
    g_trackPendingRetiredTextureSlotFlags.fill(0u);
    g_trackReusableTextureSlotPaletteReleased.fill(0u);
    g_trackReusableTextureSlotReuseCooldown.fill(0u);
    g_trackTextureSlotCapacityBytes.fill(0u);
    EnsureTrackTextureSlotQueueCapacityFloor(g_trackTextureSlotQueueCapacityFloor);
}

static void RemoveReusableTrackTextureSlotAt(size_t index)
{
    if (index >= g_trackReusableTextureSlots.size()) return;
    const uint16_t slot = g_trackReusableTextureSlots[index];
    if (slot < g_trackReusableTextureSlotFlags.size())
    {
        g_trackReusableTextureSlotFlags[slot] = 0u;
        g_trackReusableTextureSlotPaletteReleased[slot] = 0u;
        g_trackReusableTextureSlotReuseCooldown[slot] = 0u;
    }
    if (index + 1u < g_trackReusableTextureSlots.size())
    {
        g_trackReusableTextureSlots[index] = g_trackReusableTextureSlots.back();
    }
    g_trackReusableTextureSlots.pop_back();
}

static void QueueReusableTrackTextureSlot(uint16_t slot)
{
    if (kDisableTrackReusableSlotReuseForDiagnostics) return;
    if (slot == No_Texture || slot == 0u) return;
    if (slot >= SRL_MAX_TEXTURES) return;
    if (!IsVdp1TextureSlotLive(slot)) return;
    if (g_trackReusableTextureSlotFlags[slot] != 0u) return;

    g_trackReusableTextureSlotFlags[slot] = 1u;
    g_trackReusableTextureSlotPaletteReleased[slot] = 0u;
    g_trackReusableTextureSlotReuseCooldown[slot] = kReusableTrackSlotReuseDelayFrames;
    if (g_trackTextureSlotCapacityBytes[slot] == 0u)
    {
        const auto& meta = SRL::VDP1::Metadata[slot];
        if (meta.Texture)
        {
            g_trackTextureSlotCapacityBytes[slot] = TextureByteSize(
                meta.Texture->Width,
                meta.Texture->Height,
                meta.ColorMode);
        }
    }
    g_trackReusableTextureSlots.push_back(slot);
}

static void QueuePendingRetiredTrackTextureSlot(uint16_t slot)
{
    if (slot == No_Texture || slot == 0u) return;
    if (slot >= SRL_MAX_TEXTURES) return;
    if (!IsVdp1TextureSlotLive(slot)) return;
    if (g_trackReusableTextureSlotFlags[slot] != 0u) return;
    if (g_trackPendingRetiredTextureSlotFlags[slot] != 0u) return;

    g_trackPendingRetiredTextureSlotFlags[slot] = 1u;
    if (g_trackTextureSlotCapacityBytes[slot] == 0u)
    {
        const auto& meta = SRL::VDP1::Metadata[slot];
        if (meta.Texture)
        {
            g_trackTextureSlotCapacityBytes[slot] = TextureByteSize(
                meta.Texture->Width,
                meta.Texture->Height,
                meta.ColorMode);
        }
    }
    g_trackPendingRetiredTextureSlots.push_back(slot);
    if (g_trackRetiredQueuedThisFrame < std::numeric_limits<uint16_t>::max())
    {
        ++g_trackRetiredQueuedThisFrame;
    }
}

static void AdvanceReusableTrackTextureSlotCooldowns()
{
    for (size_t i = 0; i < g_trackReusableTextureSlots.size(); ++i)
    {
        const uint16_t slot = g_trackReusableTextureSlots[i];
        if (slot >= g_trackReusableTextureSlotReuseCooldown.size()) continue;
        uint8_t& cooldown = g_trackReusableTextureSlotReuseCooldown[slot];
        if (cooldown > 0u) --cooldown;
    }
}

static uint16_t CountReusableTrackTextureSlots()
{
    return static_cast<uint16_t>(std::min<size_t>(
        g_trackReusableTextureSlots.size(),
        static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
}

static uint16_t FlushPendingRetiredTrackTextureSlots()
{
    uint16_t queued = 0;
    for (size_t i = 0; i < g_trackPendingRetiredTextureSlots.size(); ++i)
    {
        const uint16_t slot = g_trackPendingRetiredTextureSlots[i];
        if (slot < g_trackPendingRetiredTextureSlotFlags.size())
        {
            g_trackPendingRetiredTextureSlotFlags[slot] = 0u;
        }

        const size_t reusableBefore = g_trackReusableTextureSlots.size();
        QueueReusableTrackTextureSlot(slot);
        if (g_trackReusableTextureSlots.size() != reusableBefore &&
            queued < std::numeric_limits<uint16_t>::max())
        {
            ++queued;
        }
    }
    g_trackPendingRetiredTextureSlots.clear();
    g_trackRetiredFlushedThisFrame = static_cast<uint16_t>(
        std::min<uint32_t>(
            static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()),
            static_cast<uint32_t>(g_trackRetiredFlushedThisFrame) +
                static_cast<uint32_t>(queued)));
    return queued;
}

static void MarkTrackPaletteBank(SRL::CRAM::TextureColorMode mode, uint16_t bank)
{
    switch (mode)
    {
    case SRL::CRAM::TextureColorMode::Paletted16:
        if (bank < g_trackPaletteBanks.pal16.size()) g_trackPaletteBanks.pal16[bank] = 1;
        break;
    case SRL::CRAM::TextureColorMode::Paletted64:
        if (bank < g_trackPaletteBanks.pal64.size()) g_trackPaletteBanks.pal64[bank] = 1;
        break;
    case SRL::CRAM::TextureColorMode::Paletted128:
        if (bank < g_trackPaletteBanks.pal128.size()) g_trackPaletteBanks.pal128[bank] = 1;
        break;
    case SRL::CRAM::TextureColorMode::Paletted256:
        if (bank < g_trackPaletteBanks.pal256.size()) g_trackPaletteBanks.pal256[bank] = 1;
        break;
    default:
        break;
    }
}

static void ReleaseTrackedPaletteBanks()
{
    for (uint16_t bank = 0; bank < g_trackPaletteBanks.pal16.size(); ++bank)
    {
        if (!g_trackPaletteBanks.pal16[bank]) continue;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted16, false);
        g_trackPaletteBanks.pal16[bank] = 0;
    }
    for (uint16_t bank = 0; bank < g_trackPaletteBanks.pal64.size(); ++bank)
    {
        if (!g_trackPaletteBanks.pal64[bank]) continue;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted64, false);
        g_trackPaletteBanks.pal64[bank] = 0;
    }
    for (uint16_t bank = 0; bank < g_trackPaletteBanks.pal128.size(); ++bank)
    {
        if (!g_trackPaletteBanks.pal128[bank]) continue;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted128, false);
        g_trackPaletteBanks.pal128[bank] = 0;
    }
    for (uint16_t bank = 0; bank < g_trackPaletteBanks.pal256.size(); ++bank)
    {
        if (!g_trackPaletteBanks.pal256[bank]) continue;
        SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted256, false);
        g_trackPaletteBanks.pal256[bank] = 0;
    }
}

static void ReleaseTrackedPaletteBankForSlot(uint16_t slot)
{
    if (!IsVdp1TextureSlotLive(slot)) return;

    const auto& meta = SRL::VDP1::Metadata[slot];
    const uint16_t bank = static_cast<uint16_t>(meta.PaletteId);
    switch (meta.ColorMode)
    {
    case SRL::CRAM::TextureColorMode::Paletted16:
        if (bank < g_trackPaletteBanks.pal16.size())
        {
            g_trackPaletteBanks.pal16[bank] = 0;
            SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted16, false);
        }
        break;
    case SRL::CRAM::TextureColorMode::Paletted64:
        if (bank < g_trackPaletteBanks.pal64.size())
        {
            g_trackPaletteBanks.pal64[bank] = 0;
            SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted64, false);
        }
        break;
    case SRL::CRAM::TextureColorMode::Paletted128:
        if (bank < g_trackPaletteBanks.pal128.size())
        {
            g_trackPaletteBanks.pal128[bank] = 0;
            SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted128, false);
        }
        break;
    case SRL::CRAM::TextureColorMode::Paletted256:
        if (bank < g_trackPaletteBanks.pal256.size())
        {
            g_trackPaletteBanks.pal256[bank] = 0;
            SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted256, false);
        }
        break;
    default:
        break;
    }
}

static void ReleaseTrackedPaletteBankById(SRL::CRAM::TextureColorMode mode, uint16_t bank)
{
    switch (mode)
    {
    case SRL::CRAM::TextureColorMode::Paletted16:
        if (bank < g_trackPaletteBanks.pal16.size())
        {
            g_trackPaletteBanks.pal16[bank] = 0;
            SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted16, false);
        }
        break;
    case SRL::CRAM::TextureColorMode::Paletted64:
        if (bank < g_trackPaletteBanks.pal64.size())
        {
            g_trackPaletteBanks.pal64[bank] = 0;
            SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted64, false);
        }
        break;
    case SRL::CRAM::TextureColorMode::Paletted128:
        if (bank < g_trackPaletteBanks.pal128.size())
        {
            g_trackPaletteBanks.pal128[bank] = 0;
            SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted128, false);
        }
        break;
    case SRL::CRAM::TextureColorMode::Paletted256:
        if (bank < g_trackPaletteBanks.pal256.size())
        {
            g_trackPaletteBanks.pal256[bank] = 0;
            SRL::CRAM::SetBankUsedState(bank, SRL::CRAM::TextureColorMode::Paletted256, false);
        }
        break;
    default:
        break;
    }
}

template <size_t N>
static uint16_t CountTrackedBanks(const std::array<uint8_t, N>& banks)
{
    uint16_t count = 0;
    for (size_t i = 0; i < banks.size(); ++i)
    {
        if (banks[i] != 0u) ++count;
    }
    return count;
}

static uint16_t ReleaseReusableTrackSlotPalettesEndFrame(
    const std::array<uint8_t, SRL_MAX_TEXTURES>& usedTextureSlots)
{
    uint16_t released = 0;
    for (size_t i = 0; i < g_trackReusableTextureSlots.size(); ++i)
    {
        const uint16_t slot = g_trackReusableTextureSlots[i];
        if (slot == No_Texture || slot >= usedTextureSlots.size()) continue;
        if (usedTextureSlots[slot] != 0u) continue;
        if (!IsVdp1TextureSlotLive(slot)) continue;
        if (slot < g_trackReusableTextureSlotReuseCooldown.size() &&
            g_trackReusableTextureSlotReuseCooldown[slot] != 0u)
        {
            continue;
        }
        if (slot < g_trackReusableTextureSlotPaletteReleased.size() &&
            g_trackReusableTextureSlotPaletteReleased[slot] != 0u)
        {
            continue;
        }

        const auto& meta = SRL::VDP1::Metadata[slot];
        if (meta.ColorMode == SRL::CRAM::TextureColorMode::RGB555) continue;
        ReleaseTrackedPaletteBankForSlot(slot);
        if (slot < g_trackReusableTextureSlotPaletteReleased.size())
        {
            g_trackReusableTextureSlotPaletteReleased[slot] = 1u;
        }
        ++released;
    }
    return released;
}

static bool ReleaseReusableTrackPaletteBankForMode(SRL::CRAM::TextureColorMode mode)
{
    if (mode == SRL::CRAM::TextureColorMode::RGB555) return false;

    for (size_t i = 0; i < g_trackReusableTextureSlots.size();)
    {
        const uint16_t slot = g_trackReusableTextureSlots[i];
        if (!IsVdp1TextureSlotLive(slot))
        {
            if (slot < g_trackTextureSlotCapacityBytes.size())
            {
                g_trackTextureSlotCapacityBytes[slot] = 0u;
            }
            RemoveReusableTrackTextureSlotAt(i);
            continue;
        }
        if (slot < g_trackReusableTextureSlotReuseCooldown.size() &&
            g_trackReusableTextureSlotReuseCooldown[slot] != 0u)
        {
            ++i;
            continue;
        }
        if (slot < g_trackReusableTextureSlotPaletteReleased.size() &&
            g_trackReusableTextureSlotPaletteReleased[slot] != 0u)
        {
            ++i;
            continue;
        }

        const auto& meta = SRL::VDP1::Metadata[slot];
        if (meta.ColorMode != mode)
        {
            ++i;
            continue;
        }

        ReleaseTrackedPaletteBankForSlot(slot);
        if (slot < g_trackReusableTextureSlotPaletteReleased.size())
        {
            g_trackReusableTextureSlotPaletteReleased[slot] = 1u;
        }
        return true;
    }
    return false;
}

static int32_t AllocatePaletteBankForMode(SRL::CRAM::TextureColorMode mode)
{
    if (mode == SRL::CRAM::TextureColorMode::RGB555) return 0;
    auto tryAllocate = [&]() -> int32_t
    {
        uint16_t start = 0;
        uint16_t limit = 0;
        switch (mode)
        {
        case SRL::CRAM::TextureColorMode::Paletted256: start = 1; limit = 8; break;
        case SRL::CRAM::TextureColorMode::Paletted128: start = 2; limit = 16; break;
        case SRL::CRAM::TextureColorMode::Paletted64:  start = 4; limit = 32; break;
        case SRL::CRAM::TextureColorMode::Paletted16:  start = 16; limit = 128; break;
        default: return -1;
        }
        for (uint16_t bank = start; bank < limit; ++bank)
        {
            if (!SRL::CRAM::GetBankUsedState(bank, mode))
            {
                SRL::CRAM::SetBankUsedState(bank, mode, true);
                MarkTrackPaletteBank(mode, bank);
                return static_cast<int32_t>(bank);
            }
        }
        return -1;
    };

    int32_t bankId = tryAllocate();
    if (bankId >= 0) return bankId;

    // In stabilized streaming, avoid releasing palettes eagerly at end-frame.
    // If we run out of banks, scavenge only from expired reusable slots of the
    // same palette mode. This keeps old slots color-stable while still giving
    // us a pressure escape hatch.
    if (ReleaseReusableTrackPaletteBankForMode(mode))
    {
        bankId = tryAllocate();
        if (bankId >= 0) return bankId;
    }

    return -1;
}

struct DecodedTgaTexture
{
    uint16_t width = 0;
    uint16_t height = 0;
    SRL::CRAM::TextureColorMode mode = SRL::CRAM::TextureColorMode::RGB555;
    TrackLowWorkVector<SRL::Types::HighColor> palette{};
    TrackLowWorkVector<uint8_t> pixels{};
};

// Keep decode scratch bounded. The decoder uses a static scratch object so one
// oversized texture can otherwise keep TrackTexture-tagged LWR capacity high
// for the whole race session.
static void NormalizeDecodedTextureScratch(DecodedTgaTexture& decoded)
{
    decoded.width = 0;
    decoded.height = 0;
    decoded.mode = SRL::CRAM::TextureColorMode::RGB555;
    decoded.palette.clear();
    decoded.pixels.clear();

    constexpr size_t kPaletteFloorEntries = 256u;
    constexpr size_t kPixelFloorBytes = 16u * 1024u;
    constexpr size_t kPaletteCompactThresholdEntries = kPaletteFloorEntries * 4u;
    constexpr size_t kPixelCompactThresholdBytes = kPixelFloorBytes * 4u;

    const size_t lwrFreeHint =
        static_cast<size_t>(SRL::Memory::LowWorkRam::GetReport().FreeSize);
    const bool lowWorkPressure =
        lwrFreeHint <= (kLowWorkRamHardFloorBytes + (16u * 1024u));

    if (lowWorkPressure || decoded.palette.capacity() > kPaletteCompactThresholdEntries)
    {
        (void)CompactEmptyVectorForTarget(decoded.palette, kPaletteFloorEntries, lwrFreeHint);
    }
    else
    {
        EnsureVectorCapacityFloor(decoded.palette, kPaletteFloorEntries);
    }

    if (lowWorkPressure || VectorCapacityBytesSafe(decoded.pixels) > kPixelCompactThresholdBytes)
    {
        (void)CompactEmptyVectorForTarget(decoded.pixels, kPixelFloorBytes, lwrFreeHint);
    }
    else
    {
        EnsureVectorCapacityFloor(decoded.pixels, kPixelFloorBytes);
    }
}

// Compact paletted TGA data down to the indices that are actually used.
template <typename PaletteVecT, typename OutPaletteVecT, typename OutIndexVecT>
static size_t CompactUsedPalette(const uint8_t* srcPixels,
                                 size_t pixelCount,
                                 const PaletteVecT& srcPalette,
                                 OutPaletteVecT& outPalette,
                                 OutIndexVecT& outIndices)
{
    outPalette.clear();
    outIndices.clear();
    if (!srcPixels || pixelCount == 0 || srcPalette.empty()) return 0;

    std::array<int16_t, 256> remap{};
    remap.fill(-1);

    if (srcPalette.size() > 0)
    {
        outPalette.push_back(srcPalette[0]);
        remap[0] = 0;
    }

    for (size_t i = 0; i < pixelCount; ++i)
    {
        const uint8_t idx = srcPixels[i];
        if (idx >= srcPalette.size()) return 0;
        if (remap[idx] >= 0) continue;
        if (outPalette.size() >= 256) return 0;
        remap[idx] = static_cast<int16_t>(outPalette.size());
        outPalette.push_back(srcPalette[idx]);
    }

    outIndices.resize(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const uint8_t idx = srcPixels[i];
        const int16_t mapped = remap[idx];
        if (mapped < 0 || mapped > 255) return 0;
        outIndices[i] = static_cast<uint8_t>(mapped);
    }

    return outPalette.size();
}

static bool DecodePalettedTgaMemory(const uint8_t* data, size_t size, DecodedTgaTexture& out)
{
    out.width = 0;
    out.height = 0;
    out.mode = SRL::CRAM::TextureColorMode::RGB555;
    out.palette.clear();
    out.pixels.clear();
    if (!data || size < 18) return false;
    const uint8_t idLen = data[0];
    const uint8_t colorMapType = data[1];
    const uint8_t imageType = data[2];
    out.width = ReadLe16(data + 12);
    out.height = ReadLe16(data + 14);
    const uint8_t pixelDepth = data[16];
    if (out.width == 0 || out.height == 0) return false;

    size_t off = 18 + static_cast<size_t>(idLen);
    if (colorMapType == 1 && imageType == 1)
    {
        // Reuse decode scratch to avoid alloc/free churn in TrackTexture-tagged LWR.
        static TrackLowWorkVector<SRL::Types::HighColor> sSrcPaletteScratch{};
        static TrackLowWorkVector<SRL::Types::HighColor> sCompactPaletteScratch{};
        static TrackLowWorkVector<uint8_t> sCompactIndicesScratch{};
        if (sSrcPaletteScratch.capacity() < 256u) sSrcPaletteScratch.reserve(256u);
        if (sCompactPaletteScratch.capacity() < 256u) sCompactPaletteScratch.reserve(256u);
        if (sCompactIndicesScratch.capacity() < (16u * 1024u))
        {
            sCompactIndicesScratch.reserve(16u * 1024u);
        }
        sSrcPaletteScratch.clear();
        sCompactPaletteScratch.clear();
        sCompactIndicesScratch.clear();

        const uint16_t cmapFirst = ReadLe16(data + 3);
        const uint16_t cmapLen = ReadLe16(data + 5);
        const uint8_t cmapDepth = data[7];
        (void)cmapFirst;
        if (pixelDepth != 8) return false;
        if (cmapLen == 0 || cmapLen > 256) return false;
        if (!(cmapDepth == 24 || cmapDepth == 32 || cmapDepth == 16)) return false;

        const size_t cmapBytes = static_cast<size_t>(cmapLen) * static_cast<size_t>(cmapDepth / 8);
        if (off + cmapBytes > size) return false;

        sSrcPaletteScratch.resize(cmapLen);
        for (size_t i = 0; i < cmapLen; ++i)
        {
            const uint8_t* c = data + off + i * (cmapDepth / 8);
            SRL::Types::HighColor hc{};
            if (cmapDepth == 24)
            {
                const uint32_t rgb = (static_cast<uint32_t>(c[2]) << 16) |
                                     (static_cast<uint32_t>(c[1]) << 8) |
                                     static_cast<uint32_t>(c[0]);
                hc = SRL::Types::HighColor::FromRGB24(rgb);
            }
            else if (cmapDepth == 32)
            {
                const uint32_t argb = (static_cast<uint32_t>(c[3]) << 24) |
                                      (static_cast<uint32_t>(c[2]) << 16) |
                                      (static_cast<uint32_t>(c[1]) << 8) |
                                      static_cast<uint32_t>(c[0]);
                hc = SRL::Types::HighColor::FromARGB32(argb);
            }
            else
            {
                hc = SRL::Types::HighColor::FromARGB15(ReadLe16(c));
            }
            sSrcPaletteScratch[i] = hc;
        }
        off += cmapBytes;

        const size_t srcPixels = static_cast<size_t>(out.width) * static_cast<size_t>(out.height);
        if (off + srcPixels > size) return false;
        const uint8_t* src = data + off;

        size_t usedPaletteCount = CompactUsedPalette(src,
                                                     srcPixels,
                                                     sSrcPaletteScratch,
                                                     sCompactPaletteScratch,
                                                     sCompactIndicesScratch);
        if (usedPaletteCount == 0)
        {
            sCompactPaletteScratch = sSrcPaletteScratch;
            sCompactIndicesScratch.assign(src, src + srcPixels);
            usedPaletteCount = sCompactPaletteScratch.size();
        }

        out.palette = sCompactPaletteScratch;

        if (usedPaletteCount <= 16)
        {
            out.mode = SRL::CRAM::TextureColorMode::Paletted16;
            out.pixels.resize((srcPixels + 1) / 2);
            for (size_t i = 0; i + 1 < srcPixels; i += 2)
            {
                out.pixels[i / 2] = static_cast<uint8_t>(((sCompactIndicesScratch[i] & 0x0F) << 4) |
                                                         (sCompactIndicesScratch[i + 1] & 0x0F));
            }
            if ((srcPixels & 1u) != 0u)
            {
                out.pixels[srcPixels / 2] =
                    static_cast<uint8_t>((sCompactIndicesScratch[srcPixels - 1] & 0x0F) << 4);
            }
        }
        else if (usedPaletteCount <= 64)
        {
            out.mode = SRL::CRAM::TextureColorMode::Paletted64;
            out.pixels = sCompactIndicesScratch;
        }
        else if (usedPaletteCount <= 128)
        {
            out.mode = SRL::CRAM::TextureColorMode::Paletted128;
            out.pixels = sCompactIndicesScratch;
        }
        else
        {
            out.mode = SRL::CRAM::TextureColorMode::Paletted256;
            out.pixels = sCompactIndicesScratch;
        }
        return true;
    }

    if (colorMapType == 0 && imageType == 2)
    {
        const size_t bpp = static_cast<size_t>(pixelDepth / 8);
        if (!(pixelDepth == 16 || pixelDepth == 24 || pixelDepth == 32)) return false;
        const size_t srcBytes = static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * bpp;
        if (off + srcBytes > size) return false;

        out.mode = SRL::CRAM::TextureColorMode::RGB555;
        out.palette.clear();
        out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 2);

        const uint8_t* src = data + off;
        uint8_t* dst = out.pixels.data();
        const size_t pixelCount = static_cast<size_t>(out.width) * static_cast<size_t>(out.height);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const uint8_t* px = src + (i * bpp);
            uint16_t c16 = 0;
            if (pixelDepth == 16)
            {
                c16 = ReadLe16(px);
            }
            else
            {
                const uint8_t b = px[0];
                const uint8_t g = px[1];
                const uint8_t r = px[2];
                const uint8_t r5 = static_cast<uint8_t>(r >> 3);
                const uint8_t g5 = static_cast<uint8_t>(g >> 3);
                const uint8_t b5 = static_cast<uint8_t>(b >> 3);
                c16 = static_cast<uint16_t>(0x8000u | (static_cast<uint16_t>(r5) << 10) |
                                            (static_cast<uint16_t>(g5) << 5) |
                                            static_cast<uint16_t>(b5));
            }
            dst[i * 2 + 0] = static_cast<uint8_t>(c16 & 0xFF);
            dst[i * 2 + 1] = static_cast<uint8_t>((c16 >> 8) & 0xFF);
        }
        return true;
    }

    return false;
}

static int32_t UploadDecodedTextureToVdp1(const DecodedTgaTexture& tex)
{
    // SGL uses texture slot 0 as No_Texture.
    // Reserve slot 0 with a dummy texture before the first dynamic upload so
    // all real runtime textures start at slot 1.
    if (SRL::VDP1::GetTextureCount() == 0)
    {
        static uint16_t sDummyTexture[8 * 8]{};
        (void)SRL::VDP1::TryLoadTexture(
            8,
            8,
            SRL::CRAM::TextureColorMode::RGB555,
            0,
            sDummyTexture);
    }

    auto tryReuseQueuedSlot = [&]() -> int32_t
    {
        if (g_trackReusableTextureSlots.empty()) return -1;

        const uint32_t requiredBytes = TextureByteSize(tex.width, tex.height, tex.mode);
        std::array<uint8_t, SRL_MAX_TEXTURES> triedSlots{};
        triedSlots.fill(0u);

        auto findBestReusableCandidate = [&]() -> size_t
        {
            size_t bestIndex = std::numeric_limits<size_t>::max();
            uint32_t bestCapacity = std::numeric_limits<uint32_t>::max();

            for (size_t i = 0; i < g_trackReusableTextureSlots.size();)
            {
                const uint16_t slot = g_trackReusableTextureSlots[i];
                if (!IsVdp1TextureSlotLive(slot))
                {
                    RemoveReusableTrackTextureSlotAt(i);
                    continue;
                }

                auto& meta = SRL::VDP1::Metadata[slot];
                auto* liveTex = meta.Texture;
                if (!liveTex)
                {
                    RemoveReusableTrackTextureSlotAt(i);
                    continue;
                }
                if (slot < triedSlots.size() && triedSlots[slot] != 0u)
                {
                    ++i;
                    continue;
                }
                if (slot < g_trackReusableTextureSlotReuseCooldown.size() &&
                    g_trackReusableTextureSlotReuseCooldown[slot] != 0u)
                {
                    ++i;
                    continue;
                }

                const uint32_t currentBytes = TextureByteSize(liveTex->Width, liveTex->Height, meta.ColorMode);
                uint32_t capacityBytes = (slot < g_trackTextureSlotCapacityBytes.size())
                    ? g_trackTextureSlotCapacityBytes[slot]
                    : 0u;
                if (capacityBytes < currentBytes) capacityBytes = currentBytes;
                if (slot < g_trackTextureSlotCapacityBytes.size())
                {
                    g_trackTextureSlotCapacityBytes[slot] = capacityBytes;
                }
                if (capacityBytes < requiredBytes)
                {
                    ++i;
                    continue;
                }

                if (bestIndex == std::numeric_limits<size_t>::max() || capacityBytes < bestCapacity)
                {
                    bestIndex = i;
                    bestCapacity = capacityBytes;
                }
                ++i;
            }

            return bestIndex;
        };

        for (;;)
        {
            const size_t candidateIndex = findBestReusableCandidate();
            if (candidateIndex == std::numeric_limits<size_t>::max()) return -1;

            const uint16_t slot = g_trackReusableTextureSlots[candidateIndex];
            auto& meta = SRL::VDP1::Metadata[slot];
            auto* liveTex = meta.Texture;
            if (!liveTex)
            {
                RemoveReusableTrackTextureSlotAt(candidateIndex);
                continue;
            }

            const auto previousMode = meta.ColorMode;
            const uint16_t previousPaletteId = static_cast<uint16_t>(meta.PaletteId);
            const uint32_t currentBytes = TextureByteSize(liveTex->Width, liveTex->Height, meta.ColorMode);
            uint32_t capacityBytes = (slot < g_trackTextureSlotCapacityBytes.size())
                ? g_trackTextureSlotCapacityBytes[slot]
                : 0u;
            if (capacityBytes < currentBytes) capacityBytes = currentBytes;

            uint16_t paletteId = 0;
            const bool paletteAlreadyReleased =
                (slot < g_trackReusableTextureSlotPaletteReleased.size()) &&
                (g_trackReusableTextureSlotPaletteReleased[slot] != 0u);

            if (tex.mode != SRL::CRAM::TextureColorMode::RGB555)
            {
                const bool canReusePaletteBank =
                    !paletteAlreadyReleased &&
                    previousMode == tex.mode &&
                    previousPaletteId != 0u &&
                    SRL::CRAM::GetBankUsedState(previousPaletteId, previousMode);
                if (canReusePaletteBank)
                {
                    paletteId = previousPaletteId;
                    MarkTrackPaletteBank(tex.mode, paletteId);
                }
                else
                {
                    if (!paletteAlreadyReleased &&
                        previousMode != SRL::CRAM::TextureColorMode::RGB555)
                    {
                        ReleaseTrackedPaletteBankById(previousMode, previousPaletteId);
                        if (slot < g_trackReusableTextureSlotPaletteReleased.size())
                        {
                            g_trackReusableTextureSlotPaletteReleased[slot] = 1u;
                        }
                    }
                    const int32_t bankId = AllocatePaletteBankForMode(tex.mode);
                    if (bankId < 0)
                    {
                        if (slot < triedSlots.size()) triedSlots[slot] = 1u;
                        continue;
                    }
                    paletteId = static_cast<uint16_t>(bankId);
                }

                SRL::CRAM::Palette cramPalette(tex.mode, paletteId);
                if (!tex.palette.empty())
                {
                    cramPalette.Load(const_cast<SRL::Types::HighColor*>(tex.palette.data()),
                                     static_cast<int16_t>(tex.palette.size()));
                }
            }
            else if (!paletteAlreadyReleased &&
                     previousMode != SRL::CRAM::TextureColorMode::RGB555)
            {
                ReleaseTrackedPaletteBankById(previousMode, previousPaletteId);
                if (slot < g_trackReusableTextureSlotPaletteReleased.size())
                {
                    g_trackReusableTextureSlotPaletteReleased[slot] = 1u;
                }
            }

            const uint16_t address = liveTex->Address;
            *liveTex = SRL::VDP1::Texture(tex.width, tex.height, address);
            meta.ColorMode = tex.mode;
            meta.PaletteId = paletteId;
            slDMACopy(const_cast<uint8_t*>(tex.pixels.data()),
                      meta.GetData(),
                      requiredBytes);

            if (slot < g_trackTextureSlotCapacityBytes.size())
            {
                g_trackTextureSlotCapacityBytes[slot] =
                    std::max<uint32_t>(capacityBytes, requiredBytes);
            }
            if (slot < g_trackReusableTextureSlotPaletteReleased.size())
            {
                g_trackReusableTextureSlotPaletteReleased[slot] = 0u;
            }
            RemoveReusableTrackTextureSlotAt(candidateIndex);
            return static_cast<int32_t>(slot);
        }
    };

    const int32_t reusedSlot = tryReuseQueuedSlot();
    if (reusedSlot >= 0)
    {
        if (g_trackUploadsReusedThisFrame < std::numeric_limits<uint16_t>::max())
        {
            ++g_trackUploadsReusedThisFrame;
        }
        return reusedSlot;
    }

    uint16_t paletteId = 0;
    if (tex.mode != SRL::CRAM::TextureColorMode::RGB555)
    {
        const int32_t bankId = AllocatePaletteBankForMode(tex.mode);
        if (bankId < 0) return -1;
        paletteId = static_cast<uint16_t>(bankId);
        SRL::CRAM::Palette cramPalette(tex.mode, paletteId);
        if (!tex.palette.empty())
        {
            cramPalette.Load(const_cast<SRL::Types::HighColor*>(tex.palette.data()),
                             static_cast<int16_t>(tex.palette.size()));
        }
    }
    const uint32_t requiredBytes = TextureByteSize(tex.width, tex.height, tex.mode);
    const int32_t slot = SRL::VDP1::TryLoadTexture(tex.width, tex.height, tex.mode, paletteId, const_cast<uint8_t*>(tex.pixels.data()));
    if (slot < 0 && tex.mode != SRL::CRAM::TextureColorMode::RGB555)
    {
        ReleaseTrackedPaletteBankById(tex.mode, paletteId);
    }
    if (slot >= 0 && static_cast<size_t>(slot) < g_trackTextureSlotCapacityBytes.size())
    {
        g_trackTextureSlotCapacityBytes[static_cast<size_t>(slot)] =
            std::max<uint32_t>(g_trackTextureSlotCapacityBytes[static_cast<size_t>(slot)],
                               requiredBytes);
    }
    if (slot >= 0 && g_trackUploadsFreshThisFrame < std::numeric_limits<uint16_t>::max())
    {
        ++g_trackUploadsFreshThisFrame;
    }
    return slot;
}

static bool LoadMat8ForSegment(int segmentId, SegmentComponent::Blob& outBlob, SegmentComponent::Loader::MatView& outView)
{
    static PackedAssetCache sMat8PackCache{};
    char packedName[20]{};
    std::snprintf(packedName, sizeof(packedName), "S%03dM8.MAT", segmentId);
    const char* packCandidates[] = {
        "CD/DATA/MAT8.BIN",
        "CD/DATA/MAT8.BIN;1",
        "DATA/MAT8.BIN",
        "DATA/MAT8.BIN;1",
        "MAT8.BIN",
        "MAT8.BIN;1"
    };
    if (LoadPackedAssetIndexToCart(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]), sMat8PackCache) &&
        LoadPackedAssetEntryToBlob(sMat8PackCache, packedName, outBlob))
    {
        return SegmentComponent::Loader::ParseMat(outBlob, outView);
    }
    (void)segmentId;
    outBlob = {};
    outView = {};
    return false;
}

static bool LoadSdrForSegment(int segmentId,
                              SegmentDrawReady::Blob& outBlob,
                              SegmentDrawReady::Loader::View& outView,
                              bool quietMissLog = false)
{
static PackedAssetCache sSdrPackCache{};
    static char sSdrPinnedPath[96]{};
    static size_t sSdrTrustedEntries = 0;
    static char sSdrBestPath[96]{};
    static size_t sSdrBestEntries = 0;
    static std::array<int32_t, 4097> sSdrEntryById{};
    static bool sSdrEntryByIdBuilt = false;
    static size_t sSdrEntryByIdCount = 0;
    static char sSdrEntryByIdSource[96]{};
    char packedName[16]{};
    std::snprintf(packedName, sizeof(packedName), "S%03d.SDR", segmentId);
    const char* packCandidates[] = {
        "/CD/DATA/SDR.BIN",
        "/CD/DATA/SDR.BIN;1",
        "/DATA/SDR.BIN",
        "/DATA/SDR.BIN;1",
        "CD/DATA/SDR.BIN",
        "CD/DATA/SDR.BIN;1",
        "DATA/SDR.BIN",
        "DATA/SDR.BIN;1",
        "cd/data/SDR.BIN",
        "cd/data/SDR.BIN;1",
        "cd/data/sdr.bin",
        "cd/data/sdr.bin;1",
        "data/SDR.BIN",
        "data/SDR.BIN;1",
        "data/sdr.bin",
        "data/sdr.bin;1",
        "SDR.BIN",
        "SDR.BIN;1",
        "sdr.bin",
        "sdr.bin;1"
    };

    auto indexAcceptable = [&](size_t entryCount) -> bool
    {
        if (entryCount == 0) return false;
        if (segmentId > 0 && entryCount < static_cast<size_t>(segmentId)) return false;
        // Never downgrade to a smaller SDR index once a larger trusted source
        // has been observed, otherwise runtime sliding can stall on higher ids.
        if (sSdrTrustedEntries > 0 && entryCount < sSdrTrustedEntries) return false;
        return true;
    };

    auto parseSdrNameToId = [&](const char* name, int32_t& outId) -> bool
    {
        outId = -1;
        if (!name) return false;
        if (name[0] != 'S' && name[0] != 's') return false;
        if (name[1] < '0' || name[1] > '9') return false;
        if (name[2] < '0' || name[2] > '9') return false;
        if (name[3] < '0' || name[3] > '9') return false;
        if (name[4] != '.') return false;
        outId = (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0');
        return outId > 0;
    };

    auto rebuildSdrEntryLookup = [&]()
    {
        for (size_t i = 0; i < sSdrEntryById.size(); ++i) sSdrEntryById[i] = -1;
        for (size_t i = 0; i < sSdrPackCache.entries.size(); ++i)
        {
            int32_t id = -1;
            if (!parseSdrNameToId(sSdrPackCache.entries[i].name, id)) continue;
            if (id <= 0 || static_cast<size_t>(id) >= sSdrEntryById.size()) continue;
            if (sSdrEntryById[static_cast<size_t>(id)] < 0)
            {
                sSdrEntryById[static_cast<size_t>(id)] = static_cast<int32_t>(i);
            }
        }
        sSdrEntryByIdBuilt = true;
        sSdrEntryByIdCount = sSdrPackCache.entries.size();
        if (sSdrPackCache.sourcePath[0] != '\0')
        {
            ::strncpy(sSdrEntryByIdSource, sSdrPackCache.sourcePath, sizeof(sSdrEntryByIdSource) - 1);
            sSdrEntryByIdSource[sizeof(sSdrEntryByIdSource) - 1] = '\0';
        }
        else
        {
            sSdrEntryByIdSource[0] = '\0';
        }
    };

    auto tryLoadSdrBySegmentId = [&]() -> bool
    {
        if (segmentId <= 0) return false;
        if (static_cast<size_t>(segmentId) >= sSdrEntryById.size()) return false;
        const bool sourceChanged = (::strcmp(sSdrEntryByIdSource, sSdrPackCache.sourcePath) != 0);
        if (!sSdrEntryByIdBuilt || sSdrEntryByIdCount != sSdrPackCache.entries.size() || sourceChanged)
        {
            rebuildSdrEntryLookup();
        }

        const int32_t idx = sSdrEntryById[static_cast<size_t>(segmentId)];
        if (idx < 0) return false;
        const size_t uidx = static_cast<size_t>(idx);
        if (uidx >= sSdrPackCache.entries.size()) return false;
        const auto& e = sSdrPackCache.entries[uidx];
        if (e.size == 0) return false;
        if (static_cast<uint64_t>(e.offset) + static_cast<uint64_t>(e.size) > static_cast<uint64_t>(sSdrPackCache.size))
        {
            return false;
        }

        const uint8_t* src = static_cast<const uint8_t*>(sSdrPackCache.cartPtr) + e.offset;
        outBlob.bytes.resize(e.size);
        ::memcpy(outBlob.bytes.data(), src, e.size);
        if (outBlob.bytes.size() < sizeof(SegmentDrawReady::HeaderV1)) return false;
        if (ReadLe32(outBlob.bytes.data()) != SegmentDrawReady::kMagicSdr1) return false;
        if (ReadLe16(outBlob.bytes.data() + 4) != SegmentDrawReady::kVersion1) return false;
        outBlob.loaded = true;
        outBlob.size = outBlob.bytes.size();
        return true;
    };

    auto rememberTrustedSource = [&]()
    {
        const size_t entryCount = sSdrPackCache.entries.size();
        bool shouldPin = false;
        if (entryCount > sSdrTrustedEntries)
        {
            sSdrTrustedEntries = entryCount;
            shouldPin = true;
        }
        else if (sSdrPinnedPath[0] == '\0' && entryCount > 0)
        {
            // Bootstrap pin when no trusted source has been recorded yet.
            shouldPin = true;
        }
        else if (entryCount == sSdrTrustedEntries && entryCount > 0)
        {
            // Keep pin synchronized only for equal-capacity trusted indexes.
            shouldPin = true;
        }

        if (shouldPin && sSdrPackCache.sourcePath[0] != '\0')
        {
            ::strncpy(sSdrPinnedPath, sSdrPackCache.sourcePath, sizeof(sSdrPinnedPath) - 1);
            sSdrPinnedPath[sizeof(sSdrPinnedPath) - 1] = '\0';
        }
        if (entryCount >= sSdrBestEntries && sSdrPackCache.sourcePath[0] != '\0')
        {
            sSdrBestEntries = entryCount;
            ::strncpy(sSdrBestPath, sSdrPackCache.sourcePath, sizeof(sSdrBestPath) - 1);
            sSdrBestPath[sizeof(sSdrBestPath) - 1] = '\0';
        }
    };

    auto probePackedEntryCount = [&](const char* path, size_t& outEntryCount) -> bool
    {
        outEntryCount = 0;
        if (!path || path[0] == '\0') return false;
        SRL::Cd::File f(path);
        if (!f.Exists() || f.Size.Bytes < 12) return false;
        if (!f.Open()) return false;

        uint8_t hdr[12]{};
        const int32_t got = f.Read(12, hdr);
        if (got < 12) return false;

        const uint32_t magic = ReadLe32(hdr + 0);
        const uint32_t version = ReadLe32(hdr + 4);
        const uint32_t entryCount = ReadLe32(hdr + 8);
        if (magic != 0x314B4150 || version != 1u || entryCount == 0u) return false;
        outEntryCount = static_cast<size_t>(entryCount);
        return true;
    };

    auto loadBestCandidate = [&](const char* const* candidates, size_t count) -> bool
    {
        if (sSdrPackCache.cartPtr && sSdrPackCache.size > 0 &&
            indexAcceptable(sSdrPackCache.entries.size()))
        {
            return true;
        }

        if (sSdrBestPath[0] != '\0')
        {
            const char* bestOnly[] = { sSdrBestPath };
            if (LoadPackedAssetIndexToCart(bestOnly, 1, sSdrPackCache) &&
                indexAcceptable(sSdrPackCache.entries.size()))
            {
                return true;
            }
            InvalidatePackedAssetCache(sSdrPackCache);
        }

        size_t bestEntries = 0;
        bool found = false;
        for (size_t i = 0; i < count; ++i)
        {
            if (!candidates[i] || candidates[i][0] == '\0') continue;
            size_t entryCount = 0;
            if (!probePackedEntryCount(candidates[i], entryCount))
            {
                continue;
            }
            if (!indexAcceptable(entryCount))
            {
                continue;
            }

            if (!found || entryCount > bestEntries)
            {
                bestEntries = entryCount;
                found = true;
                ::strncpy(sSdrBestPath, candidates[i], sizeof(sSdrBestPath) - 1);
                sSdrBestPath[sizeof(sSdrBestPath) - 1] = '\0';
            }
        }

        if (!found) return false;
        sSdrBestEntries = bestEntries;
        const char* bestOnly[] = { sSdrBestPath };
        if (!LoadPackedAssetIndexToCart(bestOnly, 1, sSdrPackCache))
        {
            InvalidatePackedAssetCache(sSdrPackCache);
            return false;
        }
        if (!indexAcceptable(sSdrPackCache.entries.size()))
        {
            InvalidatePackedAssetCache(sSdrPackCache);
            return false;
        }
        return true;
    };

    auto tryLoadFromCandidates = [&](const char* const* candidates, size_t count) -> bool
    {
        if (sSdrPackCache.cartPtr && sSdrPackCache.size > 0)
        {
            if (tryLoadSdrBySegmentId() ||
                LoadPackedAssetEntryToBlob(sSdrPackCache, packedName, outBlob))
            {
                if (indexAcceptable(sSdrPackCache.entries.size()))
                {
                    rememberTrustedSource();
                }
                return true;
            }
        }
        if (!loadBestCandidate(candidates, count))
        {
            if (sSdrPackCache.cartPtr && sSdrPackCache.size > 0)
            {
                return LoadPackedAssetEntryToBlob(sSdrPackCache, packedName, outBlob);
            }
            return false;
        }
        if (!tryLoadSdrBySegmentId() &&
            !LoadPackedAssetEntryToBlob(sSdrPackCache, packedName, outBlob))
        {
            return false;
        }
        rememberTrustedSource();
        return true;
    };

    auto tryLoad = [&]() -> bool
    {
        if (sSdrPinnedPath[0] != '\0')
        {
            const char* pinnedCandidates[] = { sSdrPinnedPath };
            if (tryLoadFromCandidates(pinnedCandidates, 1)) return true;
        }
        return tryLoadFromCandidates(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]));
    };

    auto tryParseLoaded = [&]() -> bool
    {
        if (SegmentDrawReady::Loader::Parse(outBlob, outView)) return true;
        const uint32_t m = (outBlob.bytes.size() >= 4) ? ReadLe32(outBlob.bytes.data()) : 0u;
        const uint16_t v = (outBlob.bytes.size() >= 6) ? ReadLe16(outBlob.bytes.data() + 4) : 0u;
        SRL::Debug::Print(1, 15, "SDRp %03d m:%lx v:%u s:%u",
                          segmentId,
                          static_cast<unsigned long>(m),
                          static_cast<unsigned>(v),
                          static_cast<unsigned>(outBlob.bytes.size()));
        outView = {};
        return false;
    };

    if (tryLoad())
    {
        if (tryParseLoaded()) return true;
    }

    // Recovery: force one full pack reload when cached index/entry lookup fails.
    // SDR runtime stays cart-only (4MB) for performance.
    InvalidatePackedAssetCache(sSdrPackCache);
    if (tryLoad())
    {
        if (tryParseLoaded()) return true;
    }

    if (!quietMissLog)
    {
        const auto cart = SRL::Memory::CartRam::GetReport();
        SRL::Debug::Print(1, 15, "SDRm id:%d c:%u e:%u t:%u f:%u",
                          segmentId,
                          static_cast<unsigned>(sSdrPackCache.size),
                          static_cast<unsigned>(sSdrPackCache.entries.size()),
                          static_cast<unsigned>(sSdrTrustedEntries),
                          static_cast<unsigned>(cart.FreeSize));
        if (sSdrPackCache.sourcePath[0] != '\0')
        {
            SRL::Debug::Print(1, 16, "SDRs %s", sSdrPackCache.sourcePath);
        }
    }
    (void)segmentId;
    outBlob.loaded = false;
    outBlob.size = 0;
    outBlob.bytes.clear();
    outView = {};
    return false;
}

// Read only SDR header counts used for memory planning.
static bool LoadSdrHeaderForSegment(int segmentId,
                                    SegmentDrawReady::HeaderV1& outHeader,
                                    bool quietMissLog = false)
{
    SegmentDrawReady::Blob blob{};
    SegmentDrawReady::Loader::View view{};
    if (!LoadSdrForSegment(segmentId, blob, view, quietMissLog)) return false;
    outHeader = view.header;
    return true;
}

static bool LoadRdrForSegment(int segmentId,
                              SegmentRuntimeDraw::Blob& outBlob,
                              SegmentRuntimeDraw::Loader::View& outView,
                              bool quietMissLog = false)
{
    static PackedAssetCache sRdrPackCache{};
    static char sRdrPinnedPath[96]{};
    static size_t sRdrTrustedEntries = 0;
    static char sRdrBestPath[96]{};
    static size_t sRdrBestEntries = 0;
    static std::array<int32_t, 4097> sRdrEntryById{};
    static bool sRdrEntryByIdBuilt = false;
    static size_t sRdrEntryByIdCount = 0;
    static char sRdrEntryByIdSource[96]{};
    char packedName[16]{};
    std::snprintf(packedName, sizeof(packedName), "S%03d.RDR", segmentId);
    const char* packCandidates[] = {
        "/CD/DATA/RDR.BIN",
        "/CD/DATA/RDR.BIN;1",
        "/DATA/RDR.BIN",
        "/DATA/RDR.BIN;1",
        "CD/DATA/RDR.BIN",
        "CD/DATA/RDR.BIN;1",
        "DATA/RDR.BIN",
        "DATA/RDR.BIN;1",
        "cd/data/RDR.BIN",
        "cd/data/RDR.BIN;1",
        "cd/data/rdr.bin",
        "cd/data/rdr.bin;1",
        "data/RDR.BIN",
        "data/RDR.BIN;1",
        "data/rdr.bin",
        "data/rdr.bin;1",
        "RDR.BIN",
        "RDR.BIN;1",
        "rdr.bin",
        "rdr.bin;1"
    };

    auto indexAcceptable = [&](size_t entryCount) -> bool
    {
        if (entryCount == 0) return false;
        if (segmentId > 0 && entryCount < static_cast<size_t>(segmentId)) return false;
        if (sRdrTrustedEntries > 0 && entryCount < sRdrTrustedEntries) return false;
        return true;
    };

    auto parseRdrNameToId = [&](const char* name, int32_t& outId) -> bool
    {
        outId = -1;
        if (!name) return false;
        if (name[0] != 'S' && name[0] != 's') return false;
        if (name[1] < '0' || name[1] > '9') return false;
        if (name[2] < '0' || name[2] > '9') return false;
        if (name[3] < '0' || name[3] > '9') return false;
        if (name[4] != '.') return false;
        outId = (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0');
        return outId > 0;
    };

    auto rebuildRdrEntryLookup = [&]()
    {
        for (size_t i = 0; i < sRdrEntryById.size(); ++i) sRdrEntryById[i] = -1;
        for (size_t i = 0; i < sRdrPackCache.entries.size(); ++i)
        {
            int32_t id = -1;
            if (!parseRdrNameToId(sRdrPackCache.entries[i].name, id)) continue;
            if (id <= 0 || static_cast<size_t>(id) >= sRdrEntryById.size()) continue;
            if (sRdrEntryById[static_cast<size_t>(id)] < 0)
            {
                sRdrEntryById[static_cast<size_t>(id)] = static_cast<int32_t>(i);
            }
        }
        sRdrEntryByIdBuilt = true;
        sRdrEntryByIdCount = sRdrPackCache.entries.size();
        if (sRdrPackCache.sourcePath[0] != '\0')
        {
            ::strncpy(sRdrEntryByIdSource, sRdrPackCache.sourcePath, sizeof(sRdrEntryByIdSource) - 1);
            sRdrEntryByIdSource[sizeof(sRdrEntryByIdSource) - 1] = '\0';
        }
        else
        {
            sRdrEntryByIdSource[0] = '\0';
        }
    };

    auto tryLoadRdrBySegmentId = [&]() -> bool
    {
        if (segmentId <= 0) return false;
        if (static_cast<size_t>(segmentId) >= sRdrEntryById.size()) return false;
        const bool sourceChanged = (::strcmp(sRdrEntryByIdSource, sRdrPackCache.sourcePath) != 0);
        if (!sRdrEntryByIdBuilt || sRdrEntryByIdCount != sRdrPackCache.entries.size() || sourceChanged)
        {
            rebuildRdrEntryLookup();
        }

        const int32_t idx = sRdrEntryById[static_cast<size_t>(segmentId)];
        if (idx < 0) return false;
        const size_t uidx = static_cast<size_t>(idx);
        if (uidx >= sRdrPackCache.entries.size()) return false;
        const auto& e = sRdrPackCache.entries[uidx];
        if (e.size == 0) return false;
        if (static_cast<uint64_t>(e.offset) + static_cast<uint64_t>(e.size) > static_cast<uint64_t>(sRdrPackCache.size))
        {
            return false;
        }

        const uint8_t* src = static_cast<const uint8_t*>(sRdrPackCache.cartPtr) + e.offset;
        outBlob.bytes.resize(e.size);
        ::memcpy(outBlob.bytes.data(), src, e.size);
        if (outBlob.bytes.size() < sizeof(SegmentRuntimeDraw::HeaderV1)) return false;
        if (ReadLe32(outBlob.bytes.data()) != SegmentRuntimeDraw::kMagicRdr1) return false;
        if (ReadLe16(outBlob.bytes.data() + 4) != SegmentRuntimeDraw::kVersion1) return false;
        outBlob.loaded = true;
        outBlob.size = outBlob.bytes.size();
        return true;
    };

    auto rememberTrustedSource = [&]()
    {
        const size_t entryCount = sRdrPackCache.entries.size();
        bool shouldPin = false;
        if (entryCount > sRdrTrustedEntries)
        {
            sRdrTrustedEntries = entryCount;
            shouldPin = true;
        }
        else if (sRdrPinnedPath[0] == '\0' && entryCount > 0)
        {
            shouldPin = true;
        }
        else if (entryCount == sRdrTrustedEntries && entryCount > 0)
        {
            shouldPin = true;
        }

        if (shouldPin && sRdrPackCache.sourcePath[0] != '\0')
        {
            ::strncpy(sRdrPinnedPath, sRdrPackCache.sourcePath, sizeof(sRdrPinnedPath) - 1);
            sRdrPinnedPath[sizeof(sRdrPinnedPath) - 1] = '\0';
        }
        if (entryCount >= sRdrBestEntries && sRdrPackCache.sourcePath[0] != '\0')
        {
            sRdrBestEntries = entryCount;
            ::strncpy(sRdrBestPath, sRdrPackCache.sourcePath, sizeof(sRdrBestPath) - 1);
            sRdrBestPath[sizeof(sRdrBestPath) - 1] = '\0';
        }
    };

    auto probePackedEntryCount = [&](const char* path, size_t& outEntryCount) -> bool
    {
        outEntryCount = 0;
        if (!path || path[0] == '\0') return false;
        SRL::Cd::File f(path);
        if (!f.Exists() || f.Size.Bytes < 12) return false;
        if (!f.Open()) return false;

        uint8_t hdr[12]{};
        const int32_t got = f.Read(12, hdr);
        if (got < 12) return false;

        const uint32_t magic = ReadLe32(hdr + 0);
        const uint32_t version = ReadLe32(hdr + 4);
        const uint32_t entryCount = ReadLe32(hdr + 8);
        if (magic != 0x314B4150 || version != 1u || entryCount == 0u) return false;
        outEntryCount = static_cast<size_t>(entryCount);
        return true;
    };

    auto loadBestCandidate = [&](const char* const* candidates, size_t count) -> bool
    {
        if (sRdrPackCache.cartPtr && sRdrPackCache.size > 0 &&
            indexAcceptable(sRdrPackCache.entries.size()))
        {
            return true;
        }

        if (sRdrBestPath[0] != '\0')
        {
            const char* bestOnly[] = { sRdrBestPath };
            if (LoadPackedAssetIndexToCart(bestOnly, 1, sRdrPackCache) &&
                indexAcceptable(sRdrPackCache.entries.size()))
            {
                return true;
            }
            InvalidatePackedAssetCache(sRdrPackCache);
        }

        size_t bestEntries = 0;
        bool found = false;
        for (size_t i = 0; i < count; ++i)
        {
            if (!candidates[i] || candidates[i][0] == '\0') continue;
            size_t entryCount = 0;
            if (!probePackedEntryCount(candidates[i], entryCount)) continue;
            if (!indexAcceptable(entryCount)) continue;

            if (!found || entryCount > bestEntries)
            {
                bestEntries = entryCount;
                found = true;
                ::strncpy(sRdrBestPath, candidates[i], sizeof(sRdrBestPath) - 1);
                sRdrBestPath[sizeof(sRdrBestPath) - 1] = '\0';
            }
        }

        if (!found) return false;
        sRdrBestEntries = bestEntries;
        const char* bestOnly[] = { sRdrBestPath };
        if (!LoadPackedAssetIndexToCart(bestOnly, 1, sRdrPackCache))
        {
            InvalidatePackedAssetCache(sRdrPackCache);
            return false;
        }
        if (!indexAcceptable(sRdrPackCache.entries.size()))
        {
            InvalidatePackedAssetCache(sRdrPackCache);
            return false;
        }
        return true;
    };

    auto tryLoadFromCandidates = [&](const char* const* candidates, size_t count) -> bool
    {
        if (sRdrPackCache.cartPtr && sRdrPackCache.size > 0)
        {
            if (tryLoadRdrBySegmentId() ||
                LoadPackedAssetEntryToBlob(sRdrPackCache, packedName, outBlob))
            {
                if (indexAcceptable(sRdrPackCache.entries.size()))
                {
                    rememberTrustedSource();
                }
                return true;
            }
        }
        if (!loadBestCandidate(candidates, count))
        {
            if (sRdrPackCache.cartPtr && sRdrPackCache.size > 0)
            {
                return LoadPackedAssetEntryToBlob(sRdrPackCache, packedName, outBlob);
            }
            return false;
        }
        if (!tryLoadRdrBySegmentId() &&
            !LoadPackedAssetEntryToBlob(sRdrPackCache, packedName, outBlob))
        {
            return false;
        }
        rememberTrustedSource();
        return true;
    };

    auto tryLoad = [&]() -> bool
    {
        if (sRdrPinnedPath[0] != '\0')
        {
            const char* pinnedCandidates[] = { sRdrPinnedPath };
            if (tryLoadFromCandidates(pinnedCandidates, 1)) return true;
        }
        return tryLoadFromCandidates(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]));
    };

    auto tryParseLoaded = [&]() -> bool
    {
        if (SegmentRuntimeDraw::Loader::Parse(outBlob, outView)) return true;
        const uint32_t m = (outBlob.bytes.size() >= 4) ? ReadLe32(outBlob.bytes.data()) : 0u;
        const uint16_t v = (outBlob.bytes.size() >= 6) ? ReadLe16(outBlob.bytes.data() + 4) : 0u;
        SRL::Debug::Print(1, 15, "RDRp %03d m:%lx v:%u s:%u",
                          segmentId,
                          static_cast<unsigned long>(m),
                          static_cast<unsigned>(v),
                          static_cast<unsigned>(outBlob.bytes.size()));
        outView = {};
        return false;
    };

    if (tryLoad())
    {
        if (tryParseLoaded()) return true;
    }

    InvalidatePackedAssetCache(sRdrPackCache);
    if (tryLoad())
    {
        if (tryParseLoaded()) return true;
    }

    if (!quietMissLog)
    {
        const auto cart = SRL::Memory::CartRam::GetReport();
        SRL::Debug::Print(1, 15, "RDRm id:%d c:%u e:%u t:%u f:%u",
                          segmentId,
                          static_cast<unsigned>(sRdrPackCache.size),
                          static_cast<unsigned>(sRdrPackCache.entries.size()),
                          static_cast<unsigned>(sRdrTrustedEntries),
                          static_cast<unsigned>(cart.FreeSize));
        if (sRdrPackCache.sourcePath[0] != '\0')
        {
            SRL::Debug::Print(1, 16, "RDRs %s", sRdrPackCache.sourcePath);
        }
    }

    outBlob.loaded = false;
    outBlob.size = 0;
    outBlob.bytes.clear();
    outView = {};
    return false;
}

static bool LoadRdrMappedForSegment(int segmentId,
                                    SegmentRuntimeDraw::MappedBlob& outBlob,
                                    SegmentRuntimeDraw::Loader::View& outView,
                                    SegmentRuntimeDraw::Blob* fallbackBlob = nullptr,
                                    bool quietMissLog = false)
{
    static TrackRuntimePackCache sTrackRdrPackCache{};
    const char* packCandidates[] = {
        "/CD/DATA/TRKRDR.BIN",
        "/CD/DATA/TRKRDR.BIN;1",
        "/DATA/TRKRDR.BIN",
        "/DATA/TRKRDR.BIN;1",
        "CD/DATA/TRKRDR.BIN",
        "CD/DATA/TRKRDR.BIN;1",
        "DATA/TRKRDR.BIN",
        "DATA/TRKRDR.BIN;1",
        "cd/data/TRKRDR.BIN",
        "cd/data/TRKRDR.BIN;1",
        "cd/data/trkrdr.bin",
        "cd/data/trkrdr.bin;1",
        "data/TRKRDR.BIN",
        "data/TRKRDR.BIN;1",
        "data/trkrdr.bin",
        "data/trkrdr.bin;1",
        "TRKRDR.BIN",
        "TRKRDR.BIN;1",
        "trkrdr.bin",
        "trkrdr.bin;1"
    };

    outBlob = {};
    outView = {};
    bool directPackLoaded = false;

    if (LoadTrackRuntimePackToCart(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]), sTrackRdrPackCache))
    {
        directPackLoaded = true;
        const uint8_t* blobData = nullptr;
        size_t blobSize = 0;
        TrackRuntimePack::DirectoryEntryV1 entry{};
        if (TrackRuntimePack::Loader::ResolveEntryRange(sTrackRdrPackCache.view, segmentId, blobData, blobSize, &entry))
        {
            outBlob.data = blobData;
            outBlob.size = blobSize;
            outBlob.loaded = true;
            if (SegmentRuntimeDraw::Loader::Parse(outBlob, outView))
            {
                if (!kEnableTrackRuntimeStabilization)
                {
                    if (g_rdrBuildScratch.verts.capacity() < sTrackRdrPackCache.view.header.maxVertexCount)
                    {
                        g_rdrBuildScratch.verts.reserve(sTrackRdrPackCache.view.header.maxVertexCount);
                    }
                    if (g_rdrBuildScratch.faces.capacity() < sTrackRdrPackCache.view.header.maxFaceCount)
                    {
                        g_rdrBuildScratch.faces.reserve(sTrackRdrPackCache.view.header.maxFaceCount);
                    }
                    if (g_rdrBuildScratch.attrs.capacity() < sTrackRdrPackCache.view.header.maxFaceCount)
                    {
                        g_rdrBuildScratch.attrs.reserve(sTrackRdrPackCache.view.header.maxFaceCount);
                    }
                }
                return true;
            }

            const uint32_t m = (blobSize >= 4) ? ReadLe32(blobData + 0) : 0u;
            const uint16_t v = (blobSize >= 6) ? ReadLe16(blobData + 4) : 0u;
            SRL::Debug::Print(1, 15, "TRp %03d m:%lx v:%u s:%u",
                              segmentId,
                              static_cast<unsigned long>(m),
                              static_cast<unsigned>(v),
                              static_cast<unsigned>(blobSize));
        }
    }

    if (!directPackLoaded && fallbackBlob)
    {
        fallbackBlob->loaded = false;
        fallbackBlob->size = 0;
        fallbackBlob->bytes.clear();
        if (LoadRdrForSegment(segmentId, *fallbackBlob, outView, quietMissLog))
        {
            outBlob.data = fallbackBlob->bytes.data();
            outBlob.size = fallbackBlob->bytes.size();
            outBlob.loaded = true;
            return true;
        }
    }

    if (!quietMissLog)
    {
        const auto cart = SRL::Memory::CartRam::GetReport();
        SRL::Debug::Print(1, 15, "RDRm id:%d c:%u e:%u t:%u f:%u",
                          segmentId,
                          static_cast<unsigned>(sTrackRdrPackCache.size),
                          directPackLoaded
                              ? static_cast<unsigned>(sTrackRdrPackCache.view.header.segmentCount)
                              : 0u,
                          directPackLoaded
                              ? static_cast<unsigned>(sTrackRdrPackCache.view.header.maxSegmentId)
                              : 0u,
                          static_cast<unsigned>(cart.FreeSize));
        if (sTrackRdrPackCache.sourcePath[0] != '\0')
        {
            SRL::Debug::Print(1, 16, "RDRs %s", sTrackRdrPackCache.sourcePath);
        }
    }
    return false;
}

static bool LoadRdrHeaderForSegment(int segmentId,
                                    SegmentRuntimeDraw::HeaderV1& outHeader,
                                    bool quietMissLog = false)
{
    SegmentRuntimeDraw::MappedBlob blob{};
    SegmentRuntimeDraw::Loader::View view{};
    SegmentRuntimeDraw::Blob fallbackBlob{};
    if (!LoadRdrMappedForSegment(segmentId, blob, view, &fallbackBlob, quietMissLog)) return false;
    outHeader = view.header;
    return true;
}

// Estimate a safe package size for runtime batch assembly using current High Work RAM.
static size_t ComputeSafeSegmentsPerPackage(size_t requestedSegments)
{
    if (requestedSegments == 0) return 1;

    SegmentDrawReady::HeaderV1 ref{};
    if (!LoadSdrHeaderForSegment(1, ref) || ref.vertexCount == 0 || ref.faceCount == 0)
    {
        return requestedSegments;
    }

    const size_t bytesPerSeg =
        static_cast<size_t>(ref.vertexCount) * sizeof(SRL::Math::Types::Vector3D) +
        static_cast<size_t>(ref.faceCount) * (sizeof(SRL::Types::Polygon) + sizeof(SRL::Types::Attribute)) +
        static_cast<size_t>(ref.faceCount) * (sizeof(uint16_t) + sizeof(uint8_t));

    // Keep a fixed reserve for frame runtime systems and transient data.
    const size_t hwrFree = GetHighWorkRamFreeBytesSafe();
    const size_t usable = (hwrFree > kWorkRamPlanningHeadroomBytes) ? (hwrFree - kWorkRamPlanningHeadroomBytes) : 0;

    const size_t byMem = (bytesPerSeg > 0 && usable > 0) ? (usable / bytesPerSeg) : 1;
    const size_t byIndex = (ref.vertexCount > 0) ? (65000u / static_cast<size_t>(ref.vertexCount)) : requestedSegments;

    size_t safe = std::min(requestedSegments, std::max<size_t>(1, std::min(byMem, byIndex)));

    // In test mode we prefer preserving the requested package size.
    // A too-conservative clamp here collapses the view to a single segment package.
    if (safe < requestedSegments)
    {
        SRL::Debug::Print(1, 12, "PKG guard bypass req:%u est:%u hwr:%u",
                          static_cast<unsigned>(requestedSegments),
                          static_cast<unsigned>(safe),
                          static_cast<unsigned>(hwrFree));
        safe = requestedSegments;
    }

    SRL::Debug::Print(1, 12, "PKG safe req:%u got:%u hwr:%u",
                      static_cast<unsigned>(requestedSegments),
                      static_cast<unsigned>(safe),
                      static_cast<unsigned>(hwrFree));
    return safe;
}

// Estimate bytes needed in runtime containers for one SDR segment.
// This is a conservative estimate used only for memory admission checks.
static size_t EstimateSdrSegmentRuntimeBytes(const SegmentDrawReady::HeaderV1& hdr)
{
    const size_t v = static_cast<size_t>(hdr.vertexCount);
    const size_t f = static_cast<size_t>(hdr.faceCount);
    const size_t vertsBytes = v * sizeof(SRL::Math::Types::Vector3D);
    const size_t facesBytes = f * sizeof(SRL::Types::Polygon);
    const size_t attrsBytes = f * sizeof(SRL::Types::Attribute);
    const size_t lodBytes = f * (sizeof(uint16_t) + sizeof(uint8_t) + sizeof(int32_t));
    // Keep a small fixed overhead for vector metadata and allocator alignment.
    const size_t overhead = 2u * 1024u;
    return vertsBytes + facesBytes + attrsBytes + lodBytes + overhead;
}

// Check if a contiguous segment batch can be admitted with current High Work RAM.
// Keeps a fixed reserve to avoid starving other systems in the same frame.
static bool CanAdmitSdrBatchInHighWorkRam(size_t firstSegmentId,
                                          size_t lastSegmentId,
                                          size_t reserveBytes,
                                          size_t& outEstimatedBytes,
                                          size_t& outFreeBytes)
{
    outEstimatedBytes = 0;
    bool freeValid = false;
    outFreeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    if (!freeValid)
    {
        // Keep planner permissive when report telemetry is temporarily invalid.
        outFreeBytes = std::numeric_limits<size_t>::max();
    }
    if (lastSegmentId < firstSegmentId) return false;

    for (size_t sid = firstSegmentId; sid <= lastSegmentId; ++sid)
    {
        SegmentDrawReady::HeaderV1 hdr{};
        if (!LoadSdrHeaderForSegment(static_cast<int>(sid), hdr))
        {
            return false;
        }
        outEstimatedBytes += EstimateSdrSegmentRuntimeBytes(hdr);
    }

    if (outFreeBytes <= reserveBytes + kWorkRamHardFloorBytes) return false;
    const size_t usable = outFreeBytes - reserveBytes;
    // Safety margin on top of estimate to absorb per-allocation metadata and
    // temporary vectors while a segment renderer is being built.
    const size_t guardedEstimate = outEstimatedBytes + (outEstimatedBytes / 5u); // +20%
    if (guardedEstimate <= usable) return true;

    // Fallback: allow exact estimate when free memory after admission still
    // keeps a hard floor for runtime systems.
    if (outEstimatedBytes <= usable)
    {
        const size_t freeAfter = outFreeBytes - outEstimatedBytes;
        return freeAfter >= kWorkRamHardFloorBytes;
    }
    return false;
}

static bool LoadSdrFamilyIdsForSegment(int segmentId, TrackLowWorkU16Vector& outFamilyIds)
{
    outFamilyIds.clear();

    // Trim only on LWR pressure â€” g_sdrFamilyIdsScratch lives in LWR.
    auto trimStaticBlobIfLow = [&]()
    {
        bool freeValid = false;
        const size_t freeBytes = GetLowWorkRamFreeBytesSafe(&freeValid);
        if (!freeValid) return;
        if (freeBytes > kLowWorkRamSoftFloorBytes) return;
        ResetBlobBytes(g_sdrFamilyIdsScratch);
    };
    trimStaticBlobIfLow();
    g_sdrFamilyIdsScratch.loaded = false;
    g_sdrFamilyIdsScratch.size = 0;
    g_sdrFamilyIdsScratch.bytes.clear();
    SegmentDrawReady::Loader::View sdrView{};
    if (!LoadSdrForSegment(segmentId, g_sdrFamilyIdsScratch, sdrView))
    {
        return false;
    }

    if (sdrView.header.faceCount == 0) return false;
    outFamilyIds.assign(static_cast<size_t>(sdrView.header.faceCount), 0);
    for (uint32_t fi = 0; fi < sdrView.header.faceCount; ++fi)
    {
        uint16_t familyId = 0;
        const size_t off = sdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
        if (!SegmentDrawReady::Loader::ReadFamilyIdLeAt(g_sdrFamilyIdsScratch.bytes, off, familyId)) return false;
        outFamilyIds[static_cast<size_t>(fi)] = familyId;
    }

    trimStaticBlobIfLow();
    return true;
}

static bool LoadRuntimeFamilyIdsForSegment(int segmentId, TrackLowWorkU16Vector& outFamilyIds)
{
    outFamilyIds.clear();

    // Trim only on LWR pressure â€” g_rdrFamilyIdsScratch lives in LWR.
    auto trimStaticBlobIfLow = [&]()
    {
        bool freeValid = false;
        const size_t freeBytes = GetLowWorkRamFreeBytesSafe(&freeValid);
        if (!freeValid) return;
        if (freeBytes > kLowWorkRamSoftFloorBytes) return;
        ResetBlobBytes(g_rdrFamilyIdsScratch);
    };
    trimStaticBlobIfLow();

    SegmentRuntimeDraw::MappedBlob rdrBlob{};
    SegmentRuntimeDraw::Loader::View rdrView{};
    if (LoadRdrMappedForSegment(segmentId, rdrBlob, rdrView, &g_rdrFamilyIdsScratch))
    {
        outFamilyIds.resize(rdrView.header.faceCount);
        for (uint32_t fi = 0; fi < rdrView.header.faceCount; ++fi)
        {
            uint16_t familyId = 0;
            const size_t off = rdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
            if (!SegmentRuntimeDraw::Loader::ReadFamilyIdLeAt(rdrView.data, rdrView.blobSize, off, familyId))
            {
                SRL::Debug::Print(1, 15, "RDR fam read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
                outFamilyIds.clear();
                break;
            }
            outFamilyIds[static_cast<size_t>(fi)] = familyId;
        }
        trimStaticBlobIfLow();
        if (!outFamilyIds.empty()) return true;
    }

    if (kEnableTrackRuntimeStabilization)
    {
        return false;
    }

    return LoadSdrFamilyIdsForSegment(segmentId, outFamilyIds);
}

static size_t ApplyMatFamiliesToRenderer(TrackRenderer& renderer,
                                         const SegmentComponent::Blob& matBlob,
                                         const SegmentComponent::Loader::MatView& matView,
                                         const uint16_t* familyIds,
                                         const int32_t* familySlots,
                                         size_t familyCount)
{
    const size_t rendererFaces = static_cast<size_t>(renderer.FaceCount());
    if (rendererFaces == 0) return 0;
    const size_t nFaces = std::min(rendererFaces, static_cast<size_t>(matView.header.faceCount));
    if (nFaces == 0) return 0;

    TrackLowWorkI16Vector remap(rendererFaces, -1);
    size_t changed = 0;
    size_t missingFamilies = 0;
    uint16_t lastMissingFamily = 0;

    for (size_t fi = 0; fi < nFaces; ++fi)
    {
        SegmentComponent::MatFaceBinding mb{};
        const size_t moff = matView.bindingOffset + fi * sizeof(SegmentComponent::MatFaceBinding);
        if (!SegmentComponent::Loader::ReadMatFaceBindingLeAt(matBlob.bytes, moff, mb)) continue;
        const uint16_t fam = static_cast<uint16_t>(mb.materialId);
        if (fam == 0) continue;

        int32_t slot = -1;
        for (size_t i = 0; i < familyCount; ++i)
        {
            if (familyIds[i] == fam)
            {
                slot = familySlots[i];
                break;
            }
        }

        if (slot > 0)
        {
            remap[fi] = slot;
            ++changed;
        }
        else
        {
            ++missingFamilies;
            lastMissingFamily = fam;
        }
    }

    if (missingFamilies > 0)
    {
        SRL::Debug::Print(1, 19, "MAT8 miss face:%u lastFam:%u", (unsigned)missingFamilies, (unsigned)lastMissingFamily);
    }
    (void)renderer.ApplyFaceTextureSlotsGlobal(remap);
    return changed;
}

static bool LoadGeoForSegment(int segmentId, SegmentComponent::Blob& outBlob, SegmentComponent::Loader::GeoView& outView)
{
    static PackedAssetCache sGeoPackCache{};
    char packedName[16]{};
    std::snprintf(packedName, sizeof(packedName), "S%03d.GEO", segmentId);
    const char* packCandidates[] = {
        "CD/DATA/GEO.BIN",
        "CD/DATA/GEO.BIN;1",
        "DATA/GEO.BIN",
        "DATA/GEO.BIN;1",
        "GEO.BIN",
        "GEO.BIN;1"
    };
    if (LoadPackedAssetIndexToCart(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]), sGeoPackCache) &&
        LoadPackedAssetEntryToBlob(sGeoPackCache, packedName, outBlob))
    {
        return SegmentComponent::Loader::ParseGeo(outBlob, outView);
    }
    (void)segmentId;
    outBlob = {};
    outView = {};
    return false;
}

static bool BuildRendererFromRdr(int segmentId,
                                 TrackRenderer& renderer,
                                 Vector3D* outCenter,
                                 TrackLowWorkU16Vector* outFamilyIds = nullptr)
{
    const auto previousHwrTag = SRL::Memory::HighWorkRam::GetDebugTag();
    const auto previousLwrTag = SRL::Memory::LowWorkRam::GetDebugTag();
    SetTrackWorkRamDebugTag(SRL::Memory::DebugTag::TrackPrepare);
    // Trim only when LWR (where the blobs actually live) is under pressure.
    // The old check used HWR which is always low (~44KB), causing free+realloc
    // fragmentation on every build call even when LWR has hundreds of KB free.
    {
        bool freeValid = false;
        const size_t freeBytes = GetLowWorkRamFreeBytesSafe(&freeValid);
        if (freeValid && freeBytes <= kLowWorkRamSoftFloorBytes)
            TrimRuntimeBlobScratchCaches(true);
    }

    SegmentRuntimeDraw::MappedBlob rdrBlob{};
    SegmentRuntimeDraw::Loader::View rdrView{};
    if (!LoadRdrMappedForSegment(segmentId, rdrBlob, rdrView, &g_rdrBuildScratch.blob))
    {
        SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
        SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
        return false;
    }

    auto& verts = g_rdrBuildScratch.verts;
    auto& faces = g_rdrBuildScratch.faces;
    auto& attrs = g_rdrBuildScratch.attrs;
    verts.clear();
    faces.clear();
    attrs.clear();
    // Use floor-only reservation â€” never shrink below pre-reserved capacity.
    // CompactEmptyVectorForTarget was shrinking to segment size, undoing PrimeRuntimeScratchCapacities.
    EnsureVectorCapacityFloor(verts, rdrView.header.vertexCount);
    EnsureVectorCapacityFloor(faces, rdrView.header.faceCount);
    EnsureVectorCapacityFloor(attrs, rdrView.header.faceCount);
    if (outFamilyIds)
    {
        outFamilyIds->clear();
        outFamilyIds->reserve(rdrView.header.faceCount);
        for (uint32_t fi = 0; fi < rdrView.header.faceCount; ++fi)
        {
            uint16_t familyId = 0;
            const size_t off = rdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
            if (!SegmentRuntimeDraw::Loader::ReadFamilyIdLeAt(rdrView.data, rdrView.blobSize, off, familyId))
            {
                SRL::Debug::Print(1, 15, "RDR fam read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
                SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
                SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
                return false;
            }
            outFamilyIds->push_back(familyId);
        }
    }

    for (uint32_t vi = 0; vi < rdrView.header.vertexCount; ++vi)
    {
        SegmentRuntimeDraw::Vertex sv{};
        const size_t off = rdrView.verticesOffset + static_cast<size_t>(vi) * sizeof(SegmentRuntimeDraw::Vertex);
        if (!SegmentRuntimeDraw::Loader::ReadVertexLeAt(rdrView.data, rdrView.blobSize, off, sv))
        {
            SRL::Debug::Print(1, 15, "RDR vtx read fail %03d v:%u", segmentId, static_cast<unsigned>(vi));
            SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
            SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
            return false;
        }
        verts.push_back(Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sv.x),
            SRL::Math::Types::Fxp::BuildRaw(sv.y),
            SRL::Math::Types::Fxp::BuildRaw(sv.z)));
    }

    for (uint32_t fi = 0; fi < rdrView.header.faceCount; ++fi)
    {
        SegmentRuntimeDraw::Face sf{};
        SegmentRuntimeDraw::Attr sa{};
        const size_t foff = rdrView.facesOffset + static_cast<size_t>(fi) * sizeof(SegmentRuntimeDraw::Face);
        const size_t aoff = rdrView.attrsOffset + static_cast<size_t>(fi) * sizeof(SegmentRuntimeDraw::Attr);
        if (!SegmentRuntimeDraw::Loader::ReadFaceLeAt(rdrView.data, rdrView.blobSize, foff, sf))
        {
            SRL::Debug::Print(1, 15, "RDR face read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
            SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
            SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
            return false;
        }
        if (!SegmentRuntimeDraw::Loader::ReadAttrLeAt(rdrView.data, rdrView.blobSize, aoff, sa))
        {
            SRL::Debug::Print(1, 15, "RDR attr read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
            SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
            SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
            return false;
        }

        const uint16_t indices[4] = { sf.v0, sf.v1, sf.v2, sf.v3 };
        for (size_t i = 0; i < 4; ++i)
        {
            if (static_cast<size_t>(indices[i]) >= verts.size())
            {
                SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
                SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
                return false;
            }
        }

        SRL::Types::Polygon p{};
        p.Vertices[0] = sf.v0;
        p.Vertices[1] = sf.v1;
        p.Vertices[2] = sf.v2;
        p.Vertices[3] = sf.v3;
        p.Normal = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sf.normalX),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalY),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalZ));
        faces.push_back(p);

        SRL::Types::Attribute attr{};
        attr.Visibility = (sa.visibility != 0)
            ? SRL::Types::Attribute::FaceVisibility::DoubleSided
            : SRL::Types::Attribute::FaceVisibility::SingleSided;
        attr.Sort = sa.sort;
        attr.Texture = sa.texture;
        attr.Display = sa.display;
        attr.ColorMode = sa.colorMode;
        attr.Gouraud = sa.gouraud;
        attr.Direction = sa.direction;
        attrs.push_back(attr);
    }

    const bool ok = renderer.InitializeFromComponentDataRecycled(verts, faces, attrs);
    if (!ok)
    {
        SRL::Debug::Print(1, 15, "RDR init cmp fail %03d", segmentId);
    }
    if (ok && outCenter)
    {
        *outCenter = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(rdrView.header.centerX),
            SRL::Math::Types::Fxp::BuildRaw(rdrView.header.centerY),
            SRL::Math::Types::Fxp::BuildRaw(rdrView.header.centerZ));
    }
    SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
    SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
    return ok;
}

static bool BuildRendererFromSdr(int segmentId,
                                 TrackRenderer& renderer,
                                 Vector3D* outCenter,
                                 TrackLowWorkU16Vector* outFamilyIds = nullptr)
{
    const auto previousHwrTag = SRL::Memory::HighWorkRam::GetDebugTag();
    const auto previousLwrTag = SRL::Memory::LowWorkRam::GetDebugTag();
    SetTrackWorkRamDebugTag(SRL::Memory::DebugTag::TrackPrepare);
    // Trim only on LWR pressure â€” not on HWR pressure (blobs live in LWR).
    {
        bool freeValid = false;
        const size_t freeBytes = GetLowWorkRamFreeBytesSafe(&freeValid);
        if (freeValid && freeBytes <= kLowWorkRamSoftFloorBytes)
            TrimRuntimeBlobScratchCaches(true);
    }
    g_sdrBuildScratch.blob.loaded = false;
    g_sdrBuildScratch.blob.size = 0;
    g_sdrBuildScratch.blob.bytes.clear();
    SegmentDrawReady::Loader::View sdrView{};
    if (!LoadSdrForSegment(segmentId, g_sdrBuildScratch.blob, sdrView))
    {
        SRL::Debug::Print(1, 15, "SDR load fail %03d", segmentId);
        SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
        SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
        return false;
    }

    auto& verts = g_sdrBuildScratch.verts;
    auto& faces = g_sdrBuildScratch.faces;
    auto& attrs = g_sdrBuildScratch.attrs;
    verts.clear();
    faces.clear();
    attrs.clear();
    EnsureVectorCapacityFloor(verts, sdrView.header.vertexCount);
    EnsureVectorCapacityFloor(faces, sdrView.header.faceCount);
    EnsureVectorCapacityFloor(attrs, sdrView.header.faceCount);
    if (outFamilyIds)
    {
        outFamilyIds->clear();
        outFamilyIds->reserve(sdrView.header.faceCount);
        for (uint32_t fi = 0; fi < sdrView.header.faceCount; ++fi)
        {
            uint16_t familyId = 0;
            const size_t off = sdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
            if (!SegmentDrawReady::Loader::ReadFamilyIdLeAt(g_sdrBuildScratch.blob.bytes, off, familyId))
            {
                SRL::Debug::Print(1, 15, "SDR fam read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
                SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
                SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
                return false;
            }
            outFamilyIds->push_back(familyId);
        }
    }

    for (uint32_t vi = 0; vi < sdrView.header.vertexCount; ++vi)
    {
        SegmentDrawReady::Vertex sv{};
        const size_t off = sdrView.verticesOffset + static_cast<size_t>(vi) * sizeof(SegmentDrawReady::Vertex);
        if (!SegmentDrawReady::Loader::ReadVertexLeAt(g_sdrBuildScratch.blob.bytes, off, sv))
        {
            SRL::Debug::Print(1, 15, "SDR vtx read fail %03d v:%u", segmentId, static_cast<unsigned>(vi));
            SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
            SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
            return false;
        }

        verts.push_back(Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sv.x),
            SRL::Math::Types::Fxp::BuildRaw(sv.y),
            SRL::Math::Types::Fxp::BuildRaw(sv.z)));
    }

    auto DecodeSortMode = [](uint16_t raw) -> SRL::Types::Attribute::SortMode
    {
        const uint16_t clamped = (raw > 3u) ? 0u : raw;
        return static_cast<SRL::Types::Attribute::SortMode>(SRL::Types::Attribute::SortMode::Center - clamped);
    };

    auto BuildSdrBaseAttr = [&](const SegmentDrawReady::AttrBase& sa) -> SRL::Types::Attribute
    {
        const auto visibility =
            (sa.visibility == static_cast<uint16_t>(SegmentDrawReady::VisibilityMode::SingleSided))
                ? SRL::Types::Attribute::FaceVisibility::SingleSided
                : SRL::Types::Attribute::FaceVisibility::DoubleSided;
        const auto sortMode = DecodeSortMode(sa.sortMode);
        const uint16_t gouraud = sa.gouraudMode ? sa.gouraudMode : CL32KRGB;
        const uint16_t keepFlags = static_cast<uint16_t>(sa.flags & (CL_Trans | CL_Half | MESHon | MESHoff));
        const uint16_t display = static_cast<uint16_t>((sa.colorMode ? sa.colorMode : CL32KRGB) | keepFlags);
        const uint16_t spriteMode = sa.spriteMode ? sa.spriteMode : sprPolygon;
        const uint16_t direction = sa.useLight ? UseLight : UseGouraud;
        return SRL::Types::Attribute(
            visibility,
            sortMode,
            No_Texture,
            sa.baseColor,
            gouraud,
            display,
            spriteMode,
            direction);
    };

    for (uint32_t fi = 0; fi < sdrView.header.faceCount; ++fi)
    {
        SegmentDrawReady::Face sf{};
        SegmentDrawReady::AttrBase sa{};
        const size_t foff = sdrView.facesOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::Face);
        const size_t aoff = sdrView.attrsOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::AttrBase);
        if (!SegmentDrawReady::Loader::ReadFaceLeAt(g_sdrBuildScratch.blob.bytes, foff, sf))
        {
            SRL::Debug::Print(1, 15, "SDR face read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
            SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
            SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
            return false;
        }
        if (!SegmentDrawReady::Loader::ReadAttrBaseLeAt(g_sdrBuildScratch.blob.bytes, aoff, sa))
        {
            SRL::Debug::Print(1, 15, "SDR attr read fail %03d f:%u", segmentId, static_cast<unsigned>(fi));
            SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
            SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
            return false;
        }

        const uint16_t indices[4] = { sf.v0, sf.v1, sf.v2, sf.v3 };
        for (size_t i = 0; i < 4; ++i)
        {
            if (static_cast<size_t>(indices[i]) >= verts.size())
            {
                SRL::Debug::Print(1, 15, "SEG%03d SDR bad idx f:%u i:%u v:%u max:%u",
                                  segmentId,
                                  static_cast<unsigned>(fi),
                                  static_cast<unsigned>(i),
                                  static_cast<unsigned>(indices[i]),
                                  static_cast<unsigned>(verts.size()));
                SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
                SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
                return false;
            }
        }

        SRL::Types::Polygon p{};
        p.Vertices[0] = sf.v0;
        p.Vertices[1] = sf.v1;
        p.Vertices[2] = sf.v2;
        p.Vertices[3] = sf.v3;
        p.Normal = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sf.normalX),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalY),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalZ));
        faces.push_back(p);

        attrs.push_back(BuildSdrBaseAttr(sa));
    }

    const bool ok = renderer.InitializeFromComponentDataRecycled(verts, faces, attrs);
    if (!ok)
    {
        SRL::Debug::Print(1, 15, "SDR init cmp fail %03d", segmentId);
    }
    if (ok && outCenter)
    {
        *outCenter = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sdrView.header.centerX),
            SRL::Math::Types::Fxp::BuildRaw(sdrView.header.centerY),
            SRL::Math::Types::Fxp::BuildRaw(sdrView.header.centerZ));
    }
    SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
    SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
    return ok;
}

static bool BuildRendererFromRuntimeBlob(int segmentId,
                                         TrackRenderer& renderer,
                                         Vector3D* outCenter,
                                         TrackLowWorkU16Vector* outFamilyIds,
                                         bool* outUsedRdr)
{
    if (outUsedRdr) *outUsedRdr = false;
    if (BuildRendererFromRdr(segmentId, renderer, outCenter, outFamilyIds))
    {
        if (outUsedRdr) *outUsedRdr = true;
        return true;
    }
    if (kEnableTrackRuntimeStabilization)
    {
        return false;
    }
    return BuildRendererFromSdr(segmentId, renderer, outCenter, outFamilyIds);
}

// Load one precompiled draw-ready batch from BDR.BIN.
static bool LoadBdrForBatch(int firstSegmentId,
                            int lastSegmentId,
                            BatchDrawReady::Blob& outBlob,
                            BatchDrawReady::Loader::View& outView)
{
    static PackedAssetCache sBdrPackCache{};
    char packedName[24]{};
    std::snprintf(packedName, sizeof(packedName), "B%03d_%03d.BDR", firstSegmentId, lastSegmentId);
    const char* packCandidates[] = {
        "CD/DATA/BDR.BIN",
        "CD/DATA/BDR.BIN;1",
        "DATA/BDR.BIN",
        "DATA/BDR.BIN;1",
        "BDR.BIN",
        "BDR.BIN;1"
    };
    if (!LoadPackedAssetIndexToCart(packCandidates, sizeof(packCandidates) / sizeof(packCandidates[0]), sBdrPackCache))
    {
        return false;
    }
    if (!LoadPackedAssetEntryToBlob(sBdrPackCache, packedName, outBlob))
    {
        return false;
    }
    return BatchDrawReady::Loader::Parse(outBlob, outView);
}

// Build one renderer directly from a precompiled BDR batch.
static bool BuildRendererFromBdrBatch(int firstId,
                                      int lastId,
                                      uint8_t logicalSegmentCount,
                                      BdrBatchBuildResult& outBatch)
{
    if (firstId <= 0 || lastId < firstId || logicalSegmentCount == 0) return false;

    BatchDrawReady::Blob bdrBlob{};
    BatchDrawReady::Loader::View bdrView{};
    if (!LoadBdrForBatch(firstId, lastId, bdrBlob, bdrView))
    {
        return false;
    }

    TrackLowWorkVector<SRL::Math::Types::Vector3D> verts{};
    TrackLowWorkVector<SRL::Types::Polygon> faces{};
    TrackLowWorkVector<SRL::Types::Attribute> attrs{};
    TrackLowWorkU16Vector familyIds{};
    TrackLowWorkU8Vector faceRankOffsets{};

    verts.reserve(bdrView.header.vertexCount);
    faces.reserve(bdrView.header.faceCount);
    attrs.reserve(bdrView.header.faceCount);
    familyIds.reserve(bdrView.header.faceCount);
    faceRankOffsets.reserve(bdrView.header.faceCount);

    for (uint32_t vi = 0; vi < bdrView.header.vertexCount; ++vi)
    {
        SegmentDrawReady::Vertex sv{};
        const size_t off = bdrView.verticesOffset + static_cast<size_t>(vi) * sizeof(SegmentDrawReady::Vertex);
        if (!SegmentDrawReady::Loader::ReadVertexLeAt(bdrBlob.bytes, off, sv)) return false;

        verts.push_back(Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sv.x),
            SRL::Math::Types::Fxp::BuildRaw(sv.y),
            SRL::Math::Types::Fxp::BuildRaw(sv.z)));
    }

    auto DecodeSortMode = [](uint16_t raw) -> SRL::Types::Attribute::SortMode
    {
        const uint16_t clamped = (raw > 3u) ? 0u : raw;
        return static_cast<SRL::Types::Attribute::SortMode>(SRL::Types::Attribute::SortMode::Center - clamped);
    };

    auto BuildSdrBaseAttr = [&](const SegmentDrawReady::AttrBase& sa) -> SRL::Types::Attribute
    {
        const auto visibility =
            (sa.visibility == static_cast<uint16_t>(SegmentDrawReady::VisibilityMode::SingleSided))
                ? SRL::Types::Attribute::FaceVisibility::SingleSided
                : SRL::Types::Attribute::FaceVisibility::DoubleSided;
        const auto sortMode = DecodeSortMode(sa.sortMode);
        const uint16_t gouraud = sa.gouraudMode ? sa.gouraudMode : CL32KRGB;
        const uint16_t keepFlags = static_cast<uint16_t>(sa.flags & (CL_Trans | CL_Half | MESHon | MESHoff));
        const uint16_t display = static_cast<uint16_t>((sa.colorMode ? sa.colorMode : CL32KRGB) | keepFlags);
        const uint16_t spriteMode = sa.spriteMode ? sa.spriteMode : sprPolygon;
        const uint16_t direction = sa.useLight ? UseLight : UseGouraud;
        return SRL::Types::Attribute(
            visibility,
            sortMode,
            No_Texture,
            sa.baseColor,
            gouraud,
            display,
            spriteMode,
            direction);
    };

    for (uint32_t fi = 0; fi < bdrView.header.faceCount; ++fi)
    {
        SegmentDrawReady::Face sf{};
        SegmentDrawReady::AttrBase sa{};
        const size_t foff = bdrView.facesOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::Face);
        const size_t aoff = bdrView.attrsOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::AttrBase);
        const size_t ioff = bdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
        const size_t roff = bdrView.faceRankOffsetsOffset + static_cast<size_t>(fi);
        if (!SegmentDrawReady::Loader::ReadFaceLeAt(bdrBlob.bytes, foff, sf)) return false;
        if (!SegmentDrawReady::Loader::ReadAttrBaseLeAt(bdrBlob.bytes, aoff, sa)) return false;

        uint16_t familyId = 0;
        if (!SegmentDrawReady::Loader::ReadFamilyIdLeAt(bdrBlob.bytes, ioff, familyId)) return false;
        if (roff >= bdrBlob.bytes.size()) return false;
        const uint8_t rankOffset = bdrBlob.bytes[roff];

        const uint16_t indices[4] = { sf.v0, sf.v1, sf.v2, sf.v3 };
        for (size_t i = 0; i < 4; ++i)
        {
            if (static_cast<size_t>(indices[i]) >= verts.size()) return false;
        }

        SRL::Types::Polygon p{};
        p.Vertices[0] = sf.v0;
        p.Vertices[1] = sf.v1;
        p.Vertices[2] = sf.v2;
        p.Vertices[3] = sf.v3;
        p.Normal = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sf.normalX),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalY),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalZ));
        faces.push_back(p);

        attrs.push_back(BuildSdrBaseAttr(sa));

        familyIds.push_back(familyId);
        faceRankOffsets.push_back(rankOffset);
    }

    auto renderer = MakeTrackObjectUnique<TrackRenderer, SRL::Memory::Zone::LWRam>();
    if (!renderer->InitializeFromComponentDataRecycled(verts,
                                                       faces,
                                                       attrs))
    {
        return false;
    }

    renderer->SetUseOriginal(false);
    renderer->SetSglDirect(true);
    renderer->SetVdp1Commands(false);
    renderer->SetDirect2D(false);
    renderer->SetForceDoubleSided(false);
    renderer->SetScale(SRL::Math::Types::Fxp::BuildRaw(1 << 16));
    renderer->SetDrawLimit(renderer->MeshCount());

    outBatch = {};
    outBatch.center = Vector3D(
        SRL::Math::Types::Fxp::BuildRaw(bdrView.header.centerX),
        SRL::Math::Types::Fxp::BuildRaw(bdrView.header.centerY),
        SRL::Math::Types::Fxp::BuildRaw(bdrView.header.centerZ));
    outBatch.renderer = std::move(renderer);
    outBatch.familyIds = std::move(familyIds);
    outBatch.faceRankOffsets = std::move(faceRankOffsets);
    (void)logicalSegmentCount;
    return true;
}

static bool AppendSdrSegmentToBatch(int segmentId,
                                    uint8_t rankOffset,
                                    TrackLowWorkVector<SRL::Math::Types::Vector3D>& ioVerts,
                                    TrackLowWorkVector<SRL::Types::Polygon>& ioFaces,
                                    TrackLowWorkVector<SRL::Types::Attribute>& ioAttrs,
                                    TrackLowWorkU16Vector& ioFamilyIds,
                                    TrackLowWorkU8Vector& ioFaceRankOffsets,
                                    Vector3D& ioMin,
                                    Vector3D& ioMax)
{
    SegmentDrawReady::Blob sdrBlob{};
    SegmentDrawReady::Loader::View sdrView{};
    if (!LoadSdrForSegment(segmentId, sdrBlob, sdrView))
    {
        return false;
    }

    const size_t vertexBase = ioVerts.size();

    for (uint32_t vi = 0; vi < sdrView.header.vertexCount; ++vi)
    {
        SegmentDrawReady::Vertex sv{};
        const size_t off = sdrView.verticesOffset + static_cast<size_t>(vi) * sizeof(SegmentDrawReady::Vertex);
        if (!SegmentDrawReady::Loader::ReadVertexLeAt(sdrBlob.bytes, off, sv)) return false;

        const Vector3D v(
            SRL::Math::Types::Fxp::BuildRaw(sv.x),
            SRL::Math::Types::Fxp::BuildRaw(sv.y),
            SRL::Math::Types::Fxp::BuildRaw(sv.z));
        ioVerts.push_back(v);
        ioMin.X = SRL::Math::Min(ioMin.X, v.X);
        ioMin.Y = SRL::Math::Min(ioMin.Y, v.Y);
        ioMin.Z = SRL::Math::Min(ioMin.Z, v.Z);
        ioMax.X = SRL::Math::Max(ioMax.X, v.X);
        ioMax.Y = SRL::Math::Max(ioMax.Y, v.Y);
        ioMax.Z = SRL::Math::Max(ioMax.Z, v.Z);
    }

    auto DecodeSortMode = [](uint16_t raw) -> SRL::Types::Attribute::SortMode
    {
        const uint16_t clamped = (raw > 3u) ? 0u : raw;
        return static_cast<SRL::Types::Attribute::SortMode>(SRL::Types::Attribute::SortMode::Center - clamped);
    };

    auto BuildSdrBaseAttr = [&](const SegmentDrawReady::AttrBase& sa) -> SRL::Types::Attribute
    {
        const auto visibility =
            (sa.visibility == static_cast<uint16_t>(SegmentDrawReady::VisibilityMode::SingleSided))
                ? SRL::Types::Attribute::FaceVisibility::SingleSided
                : SRL::Types::Attribute::FaceVisibility::DoubleSided;
        const auto sortMode = DecodeSortMode(sa.sortMode);
        const uint16_t gouraud = sa.gouraudMode ? sa.gouraudMode : CL32KRGB;
        const uint16_t keepFlags = static_cast<uint16_t>(sa.flags & (CL_Trans | CL_Half | MESHon | MESHoff));
        const uint16_t display = static_cast<uint16_t>((sa.colorMode ? sa.colorMode : CL32KRGB) | keepFlags);
        const uint16_t spriteMode = sa.spriteMode ? sa.spriteMode : sprPolygon;
        const uint16_t direction = sa.useLight ? UseLight : UseGouraud;
        return SRL::Types::Attribute(
            visibility,
            sortMode,
            No_Texture,
            sa.baseColor,
            gouraud,
            display,
            spriteMode,
            direction);
    };

    for (uint32_t fi = 0; fi < sdrView.header.faceCount; ++fi)
    {
        SegmentDrawReady::Face sf{};
        SegmentDrawReady::AttrBase sa{};
        uint16_t familyId = 0;
        const size_t foff = sdrView.facesOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::Face);
        const size_t aoff = sdrView.attrsOffset + static_cast<size_t>(fi) * sizeof(SegmentDrawReady::AttrBase);
        const size_t ioff = sdrView.familyIdsOffset + static_cast<size_t>(fi) * sizeof(uint16_t);
        if (!SegmentDrawReady::Loader::ReadFaceLeAt(sdrBlob.bytes, foff, sf)) return false;
        if (!SegmentDrawReady::Loader::ReadAttrBaseLeAt(sdrBlob.bytes, aoff, sa)) return false;
        if (!SegmentDrawReady::Loader::ReadFamilyIdLeAt(sdrBlob.bytes, ioff, familyId)) return false;

        const uint16_t srcIdx[4] = { sf.v0, sf.v1, sf.v2, sf.v3 };
        for (size_t i = 0; i < 4; ++i)
        {
            if (static_cast<uint32_t>(srcIdx[i]) >= sdrView.header.vertexCount)
            {
                SRL::Debug::Print(1, 15, "SDR idx bad seg:%03d f:%u i:%u v:%u max:%u",
                                  segmentId,
                                  static_cast<unsigned>(fi),
                                  static_cast<unsigned>(i),
                                  static_cast<unsigned>(srcIdx[i]),
                                  static_cast<unsigned>(sdrView.header.vertexCount));
                return false;
            }
        }
        SRL::Types::Polygon p{};
        for (size_t i = 0; i < 4; ++i)
        {
            const size_t mapped = vertexBase + static_cast<size_t>(srcIdx[i]);
            if (mapped >= static_cast<size_t>(0xFFFF)) return false;
            p.Vertices[i] = static_cast<uint16_t>(mapped);
        }
        p.Normal = Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(sf.normalX),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalY),
            SRL::Math::Types::Fxp::BuildRaw(sf.normalZ));
        ioFaces.push_back(p);

        ioAttrs.push_back(BuildSdrBaseAttr(sa));

        ioFamilyIds.push_back(familyId);
        ioFaceRankOffsets.push_back(rankOffset);
    }

    return true;
}

// Reorder quad corners from GEO UVs so the SGL textured polygon path sees a stable corner order.
static bool ReorderQuadVerticesFromUv(const SegmentComponent::GeoFace& face, uint16_t outVertices[4])
{
    if (!outVertices) return false;

    int16_t minU = face.u[0];
    int16_t maxU = face.u[0];
    int16_t minV = face.v[0];
    int16_t maxV = face.v[0];
    for (size_t i = 1; i < 4; ++i)
    {
        minU = std::min(minU, face.u[i]);
        maxU = std::max(maxU, face.u[i]);
        minV = std::min(minV, face.v[i]);
        maxV = std::max(maxV, face.v[i]);
    }

    if (minU == maxU || minV == maxV) return false;

    size_t uEdgeCount = 0;
    size_t vEdgeCount = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        const bool onUEdge = (face.u[i] == minU) || (face.u[i] == maxU);
        const bool onVEdge = (face.v[i] == minV) || (face.v[i] == maxV);
        if (!onUEdge || !onVEdge) return false;
        if (face.u[i] == minU || face.u[i] == maxU) ++uEdgeCount;
        if (face.v[i] == minV || face.v[i] == maxV) ++vEdgeCount;
    }
    if (uEdgeCount != 4 || vEdgeCount != 4) return false;

    const int16_t targetU[4] = { minU, maxU, maxU, minU };
    const int16_t targetV[4] = { minV, minV, maxV, maxV };
    bool used[4] = { false, false, false, false };

    for (size_t corner = 0; corner < 4; ++corner)
    {
        int best = -1;
        int32_t bestScore = 0x7FFFFFFF;
        for (size_t src = 0; src < 4; ++src)
        {
            if (used[src]) continue;
            const int32_t du = static_cast<int32_t>(face.u[src]) - static_cast<int32_t>(targetU[corner]);
            const int32_t dv = static_cast<int32_t>(face.v[src]) - static_cast<int32_t>(targetV[corner]);
            const int32_t score = (du < 0 ? -du : du) + (dv < 0 ? -dv : dv);
            if (score < bestScore)
            {
                bestScore = score;
                best = static_cast<int>(src);
            }
        }
        if (best < 0) return false;
        used[best] = true;
        outVertices[corner] = face.vertex[best];
    }

    return true;
}

// Build a stable face normal from the first three corners of the polygon.
template <typename VecT>
static Vector3D BuildFaceNormalFromVerts(const VecT& verts,
                                         const uint16_t indices[4])
{
    if (verts.empty()) return Vector3D(0.0, 0.0, 0.0);
    const size_t ia = static_cast<size_t>(indices[0]);
    const size_t ib = static_cast<size_t>(indices[1]);
    const size_t ic = static_cast<size_t>(indices[2]);
    if (ia >= verts.size() || ib >= verts.size() || ic >= verts.size())
    {
        return Vector3D(0.0, 0.0, 0.0);
    }

    const auto& a = verts[ia];
    const auto& b = verts[ib];
    const auto& c = verts[ic];

    const int64_t abx = static_cast<int64_t>(b.X.RawValue()) - static_cast<int64_t>(a.X.RawValue());
    const int64_t aby = static_cast<int64_t>(b.Y.RawValue()) - static_cast<int64_t>(a.Y.RawValue());
    const int64_t abz = static_cast<int64_t>(b.Z.RawValue()) - static_cast<int64_t>(a.Z.RawValue());
    const int64_t acx = static_cast<int64_t>(c.X.RawValue()) - static_cast<int64_t>(a.X.RawValue());
    const int64_t acy = static_cast<int64_t>(c.Y.RawValue()) - static_cast<int64_t>(a.Y.RawValue());
    const int64_t acz = static_cast<int64_t>(c.Z.RawValue()) - static_cast<int64_t>(a.Z.RawValue());

    const int32_t nx = static_cast<int32_t>(((aby * acz) - (abz * acy)) >> 16);
    const int32_t ny = static_cast<int32_t>(((abz * acx) - (abx * acz)) >> 16);
    const int32_t nz = static_cast<int32_t>(((abx * acy) - (aby * acx)) >> 16);

    return Vector3D(SRL::Math::Types::Fxp::BuildRaw(nx),
                    SRL::Math::Types::Fxp::BuildRaw(ny),
                    SRL::Math::Types::Fxp::BuildRaw(nz));
}

// Build one component renderer and expose its center directly from GEO vertices.
static bool BuildRendererFromGeoMat8(int segmentId, TrackRenderer& renderer, Vector3D* outCenter)
{
    if (BuildRendererFromRdr(segmentId, renderer, outCenter) ||
        BuildRendererFromSdr(segmentId, renderer, outCenter))
    {
        return true;
    }

    SRL::Debug::Print(1, 15, "SEG%03d SDR load fail", segmentId);
    return false;
}
} // namespace

SRL::Math::Types::Vector3D TrackSystem::ComputeRendererCenter(const TrackRenderer& renderer)
{
    return renderer.StartMeshCenter() + renderer.Offset();
}

void TrackSystem::ReleaseRawSegmentCatalog()
{
    for (auto& e : rawSegmentCatalog_)
    {
        if (e.copy.cartPtr)
        {
            SRL::Memory::CartRam::Free(e.copy.cartPtr);
            e.copy.cartPtr = nullptr;
            e.copy.size = 0;
        }
    }
    rawSegmentCatalog_.clear();
}

void TrackSystem::ReleaseSeg1Texbanks()
{
    for (auto& b : seg1Texbanks_)
    {
        b.entries.clear();
        if (b.cartPtr)
        {
            SRL::Memory::CartRam::Free(b.cartPtr);
            b.cartPtr = nullptr;
        }
        b.size = 0;
    }
}

void TrackSystem::ReleaseSeg1TgaCatalog()
{
    for (auto& e : seg1TgaCatalog_)
    {
        if (e.cartPtr)
        {
            SRL::Memory::CartRam::Free(e.cartPtr);
            e.cartPtr = nullptr;
        }
        e.size = 0;
        e.name[0] = '\0';
    }
    seg1TgaCatalog_.clear();
}

static bool IsTgaNameChar(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_' || c == '-' || c == '.';
}

static bool NameEqualsIgnoreCase(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b)
    {
        char ca = *a;
        char cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 'a' + 'A');
        if (ca != cb) return false;
        ++a; ++b;
    }
    return (*a == '\0' && *b == '\0');
}

template <typename Catalog>
static int32_t TryUploadPalettedTgaFromCatalogByName(const Catalog& catalog, const char* name)
{
    if (!name || name[0] == '\0') return -1;

    char norm[64]{};
    NormalizeTextureFileName(name, norm, sizeof(norm));
    if (norm[0] == '\0') return -1;

    for (const auto& t : catalog)
    {
        if (!t.cartPtr || t.size == 0) continue;
        if (!NameEqualsIgnoreCase(t.name, norm)) continue;
        DecodedTgaTexture decoded{};
        if (!DecodePalettedTgaMemory(static_cast<const uint8_t*>(t.cartPtr), t.size, decoded)) continue;
        return UploadDecodedTextureToVdp1(decoded);
    }

    return -1;
}

template <typename Catalog>
static int32_t TryUploadSeg1FamilyLodFromCatalog(const Catalog& catalog,
                                                 int familyId,
                                                 const char* sourceName,
                                                 int lodValue,
                                                 const RenTextureMap* renMap)
{
    if (!sourceName || sourceName[0] == '\0') return -1;

    char candA[64]{};
    char candB[64]{};
    char mapped[64]{};
    BuildLodTextureName(sourceName, lodValue, false, candA, sizeof(candA));
    BuildLodTextureName(sourceName, lodValue, true, candB, sizeof(candB));

    char famA[32]{};
    char famB[32]{};
    std::snprintf(famA, sizeof(famA), "F%03d_%d.TGA", familyId, lodValue);
    std::snprintf(famB, sizeof(famB), "F%03d%d.TGA", familyId, lodValue);

    int32_t slot = TryUploadPalettedTgaFromCatalogByName(catalog, famA);
    if (slot < 0) slot = TryUploadPalettedTgaFromCatalogByName(catalog, famB);

    if (slot < 0 && renMap && FindRenamedTarget(*renMap, candA, lodValue, mapped, sizeof(mapped)))
    {
        slot = TryUploadPalettedTgaFromCatalogByName(catalog, mapped);
    }
    if (slot < 0 && renMap && FindRenamedTarget(*renMap, candB, lodValue, mapped, sizeof(mapped)))
    {
        slot = TryUploadPalettedTgaFromCatalogByName(catalog, mapped);
    }

    return slot;
}

namespace
{
static void SetSeg1TgaDebugString(char* dst, size_t dstSize, const char* src)
{
    if (!dst || dstSize == 0) return;
    ::strncpy(dst, (src && src[0] != '\0') ? src : "none", dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static void ResetSeg1TgaPreloadDebugState(uint16_t& preloadCount,
                                          uint16_t& attemptCount,
                                          uint16_t& failCount,
                                          uint8_t& jsonOk)
{
    preloadCount = 0;
    attemptCount = 0;
    failCount = 0;
    jsonOk = 0;
    SetSeg1TgaDebugString(g_tgaLastTry, sizeof(g_tgaLastTry), "none");
    SetSeg1TgaDebugString(g_tgaLastResult, sizeof(g_tgaLastResult), "preload_start");
    SetSeg1TgaDebugString(g_tgaLastName, sizeof(g_tgaLastName), "none");
    g_smapBytes = 0;
    SetSeg1TgaDebugString(g_smapSig, sizeof(g_smapSig), "none");
    SetSeg1TgaDebugString(g_smapHead, sizeof(g_smapHead), "none");
}

static void SetSeg1TgaLastName(const char* name)
{
    SetSeg1TgaDebugString(g_tgaLastName, sizeof(g_tgaLastName), name);
}

static void CopyAsciiUpper(char* dst, size_t dstSize, const char* src)
{
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!src) return;
    ::strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
    for (size_t i = 0; dst[i] != '\0'; ++i)
    {
        if (dst[i] >= 'a' && dst[i] <= 'z') dst[i] = static_cast<char>(dst[i] - 'a' + 'A');
    }
}

static void BuildIso83UpperName(char* outName, size_t outSize, const char* inName)
{
    if (!outName || outSize == 0) return;
    outName[0] = '\0';
    if (!inName || inName[0] == '\0') return;

    char upper[80]{};
    CopyAsciiUpper(upper, sizeof(upper), inName);
    const char* dot = ::strrchr(upper, '.');
    if (!dot)
    {
        ::strncpy(outName, upper, outSize - 1);
        outName[outSize - 1] = '\0';
        return;
    }

    char base[16]{};
    size_t baseLen = static_cast<size_t>(dot - upper);
    if (baseLen > 8) baseLen = 8;
    for (size_t i = 0; i < baseLen; ++i) base[i] = upper[i];
    base[baseLen] = '\0';

    const char* ext = dot + 1;
    char ext3[8]{};
    size_t extLen = 0;
    while (ext[extLen] != '\0' && extLen < 3)
    {
        ext3[extLen] = ext[extLen];
        ++extLen;
    }
    ext3[extLen] = '\0';

    if (base[0] != '\0' && ext3[0] != '\0')
    {
        std::snprintf(outName, outSize, "%s.%s", base, ext3);
    }
}

template <typename Catalog>
static bool HasSeg1TgaCartEntry(const Catalog& catalog, const char* name)
{
    if (!name || name[0] == '\0') return false;
    for (const auto& entry : catalog)
    {
        if (NameEqualsIgnoreCase(entry.name, name)) return true;
    }
    return false;
}

static void UpdateSeg1SmapDebugSnapshot(const std::vector<char>& text, bool printHead)
{
    g_smapBytes = static_cast<uint32_t>(text.size());
    const uint8_t b0 = (text.size() > 0) ? static_cast<uint8_t>(text[0]) : 0;
    const uint8_t b1 = (text.size() > 1) ? static_cast<uint8_t>(text[1]) : 0;
    const uint8_t b2 = (text.size() > 2) ? static_cast<uint8_t>(text[2]) : 0;
    const uint8_t b3 = (text.size() > 3) ? static_cast<uint8_t>(text[3]) : 0;
    std::snprintf(g_smapSig, sizeof(g_smapSig), "%02X%02X%02X%02X", b0, b1, b2, b3);

    char head[48]{};
    const size_t headSize = (text.size() > 40) ? 40 : text.size();
    if (headSize > 0)
    {
        memcpy(head, text.data(), headSize);
        head[headSize] = '\0';
        for (size_t i = 0; head[i] != '\0'; ++i)
        {
            if (head[i] < 32 || head[i] > 126) head[i] = '.';
        }
    }

    SetSeg1TgaDebugString(g_smapHead, sizeof(g_smapHead), head);
    if (printHead)
    {
        SRL::Debug::Print(1, 24, "SMAP head:%s", g_smapHead);
    }
}

static size_t BuildSeg1TgaCandidatePaths(
    const char* normalizedName,
    std::array<std::array<char, 96>, 18>& storage,
    const char** outPaths)
{
    if (!normalizedName || normalizedName[0] == '\0' || !outPaths) return 0;

    char upper[64]{};
    CopyAsciiUpper(upper, sizeof(upper), normalizedName);

    char iso83[80]{};
    BuildIso83UpperName(iso83, sizeof(iso83), upper);

    size_t count = 0;
    auto addCandidate = [&](const char* fmt, const char* value)
    {
        if (!fmt || !value || value[0] == '\0' || count >= storage.size()) return;
        std::snprintf(storage[count].data(), storage[count].size(), fmt, value);
        outPaths[count] = storage[count].data();
        ++count;
    };

    addCandidate("CD/DATA/%s", normalizedName);
    addCandidate("CD/DATA/%s;1", normalizedName);
    addCandidate("DATA/%s", normalizedName);
    addCandidate("DATA/%s;1", normalizedName);
    addCandidate("%s", normalizedName);
    addCandidate("%s;1", normalizedName);

    addCandidate("CD/DATA/%s", upper);
    addCandidate("CD/DATA/%s;1", upper);
    addCandidate("DATA/%s", upper);
    addCandidate("DATA/%s;1", upper);
    addCandidate("%s", upper);
    addCandidate("%s;1", upper);

    addCandidate("CD/DATA/%s", iso83);
    addCandidate("CD/DATA/%s;1", iso83);
    addCandidate("DATA/%s", iso83);
    addCandidate("DATA/%s;1", iso83);
    addCandidate("%s", iso83);
    addCandidate("%s;1", iso83);

    return count;
}

static bool LoadCdFileToCart(const char* path, void*& outCartPtr, uint32_t& outSize)
{
    outCartPtr = nullptr;
    outSize = 0;
    if (!path || path[0] == '\0') return false;

    SRL::Cd::File file(path);
    if (!file.Exists() || file.Size.Bytes <= 0 || !file.Open()) return false;

    const uint32_t bytes = static_cast<uint32_t>(file.Size.Bytes);
    void* mem = SRL::Memory::CartRam::Malloc(bytes);
    if (!mem) return false;

    uint32_t readBytes = 0;
    if (!ReadCdFileFully(file, bytes, static_cast<uint8_t*>(mem), readBytes) || readBytes == 0)
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    outCartPtr = mem;
    outSize = readBytes;
    return true;
}

template <typename Catalog>
static bool FinalizeSeg1TgaPreloadCatalog(const Catalog& catalog, uint16_t& preloadCount)
{
    preloadCount = static_cast<uint16_t>(catalog.size());
    return !catalog.empty();
}

static void UpdatePackedAssetCacheTrackedBytes(PackedAssetCache& cache)
{
    const uint32_t current = VectorCapacityBytesSafe(cache.entries);
    if (current == cache.trackedEntryBytes) return;
    if (g_packedAssetCacheEntryBytesLwr >= cache.trackedEntryBytes)
    {
        g_packedAssetCacheEntryBytesLwr -= cache.trackedEntryBytes;
    }
    else
    {
        g_packedAssetCacheEntryBytesLwr = 0;
    }
    g_packedAssetCacheEntryBytesLwr += current;
    cache.trackedEntryBytes = current;
}

static void InvalidateTrackRuntimePackCache(TrackRuntimePackCache& cache)
{
    if (cache.cartPtr)
    {
        SRL::Memory::CartRam::Free(cache.cartPtr);
    }
    cache = {};
}

static void InvalidatePackedAssetCache(PackedAssetCache& cache)
{
    if (cache.cartPtr)
    {
        SRL::Memory::CartRam::Free(cache.cartPtr);
    }
    if (g_packedAssetCacheEntryBytesLwr >= cache.trackedEntryBytes)
    {
        g_packedAssetCacheEntryBytesLwr -= cache.trackedEntryBytes;
    }
    else
    {
        g_packedAssetCacheEntryBytesLwr = 0;
    }
    cache = {};
}

static bool RebuildPackedAssetEntries(PackedAssetCache& cache)
{
    cache.entries.clear();
    UpdatePackedAssetCacheTrackedBytes(cache);
    if (!cache.cartPtr || cache.size < 12) return false;

    const uint8_t* p = static_cast<const uint8_t*>(cache.cartPtr);
    const uint32_t magic = ReadLe32(p + 0);
    const uint32_t version = ReadLe32(p + 4);
    const uint32_t entryCount = ReadLe32(p + 8);
    if (magic != 0x314B4150 || version != 1) return false; // "PAK1"
    if (entryCount == 0) return false;

    const size_t entrySize = 64 + 4 + 4;
    const size_t tableBytes = static_cast<size_t>(entryCount) * entrySize;
    const size_t dataOffset = 12 + tableBytes;
    if (dataOffset > cache.size) return false;

    cache.entries.reserve(entryCount);
    uint32_t minOffset = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < entryCount; ++i)
    {
        const size_t off = 12 + static_cast<size_t>(i) * entrySize;
        if (off + entrySize > cache.size) return false;

        PackedAssetEntryMeta e{};
        ::memcpy(e.name, p + off, 64);
        e.name[64] = '\0';
        e.offset = ReadLe32(p + off + 64);
        e.size = ReadLe32(p + off + 68);
        if (e.size == 0) continue;
        if (e.offset < dataOffset) continue;
        if (static_cast<uint64_t>(e.offset) + static_cast<uint64_t>(e.size) > static_cast<uint64_t>(cache.size)) continue;
        if (e.offset < minOffset) minOffset = e.offset;
        cache.entries.push_back(e);
    }

    if (cache.entries.empty()) return false;
    // Header corruption guard: valid packs should start data immediately after table.
    // Allow a small slack for potential packer alignment/padding.
    if (minOffset > dataOffset)
    {
        const size_t gap = static_cast<size_t>(minOffset - static_cast<uint32_t>(dataOffset));
        if (gap > entrySize)
        {
            cache.entries.clear();
            UpdatePackedAssetCacheTrackedBytes(cache);
            return false;
        }
    }

    UpdatePackedAssetCacheTrackedBytes(cache);
    return !cache.entries.empty();
}

static bool ReadCdFileFully(SRL::Cd::File& file, uint32_t totalBytes, uint8_t* dst, uint32_t& outReadBytes)
{
    outReadBytes = 0;
    if (!dst || totalBytes == 0) return false;

    while (outReadBytes < totalBytes)
    {
        const int32_t toRead = static_cast<int32_t>(totalBytes - outReadBytes);
        if (toRead <= 0) break;
        int32_t got = file.Read(toRead, dst + outReadBytes);
        if (got <= 0) break;
        outReadBytes += static_cast<uint32_t>(got);
    }

    return outReadBytes == totalBytes;
}

static bool LoadTrackRuntimePackToCart(const char* const* candidates, size_t count, TrackRuntimePackCache& cache)
{
    char rememberedPath[96]{};
    if (cache.sourcePath[0] != '\0')
    {
        ::strncpy(rememberedPath, cache.sourcePath, sizeof(rememberedPath) - 1);
        rememberedPath[sizeof(rememberedPath) - 1] = '\0';
    }

    if (cache.cartPtr && cache.size > 0)
    {
        if (TrackRuntimePack::Loader::Parse(cache.cartPtr, cache.size, cache.view))
        {
            return true;
        }
        InvalidateTrackRuntimePackCache(cache);
    }

    const char* foundPath = nullptr;
    SRL::Cd::ChangeDir((const char*)0);
    if (rememberedPath[0] != '\0')
    {
        SRL::Cd::File remembered(rememberedPath);
        if (remembered.Exists() && remembered.Size.Bytes > 0)
        {
            foundPath = rememberedPath;
        }
    }
    for (size_t i = 0; i < count && !foundPath; ++i)
    {
        SRL::Cd::File probe(candidates[i]);
        if (probe.Exists() && probe.Size.Bytes > 0)
        {
            foundPath = candidates[i];
        }
    }
    if (!foundPath) return false;

    SRL::Cd::File file(foundPath);
    if (file.Size.Bytes <= 0) return false;
    if (!file.Open()) return false;

    const uint32_t bytes = static_cast<uint32_t>(file.Size.Bytes);
    void* mem = SRL::Memory::CartRam::Malloc(bytes);
    if (!mem) return false;

    uint32_t readBytes = 0;
    if (!ReadCdFileFully(file, bytes, static_cast<uint8_t*>(mem), readBytes))
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    cache.cartPtr = mem;
    cache.size = readBytes;
    if (!TrackRuntimePack::Loader::Parse(cache.cartPtr, cache.size, cache.view))
    {
        InvalidateTrackRuntimePackCache(cache);
        return false;
    }

    ::strncpy(cache.sourcePath, foundPath, sizeof(cache.sourcePath) - 1);
    cache.sourcePath[sizeof(cache.sourcePath) - 1] = '\0';
    return true;
}

static bool LoadPackedAssetIndexToCart(const char* const* candidates, size_t count, PackedAssetCache& cache)
{
    char rememberedPath[96]{};
    if (cache.sourcePath[0] != '\0')
    {
        ::strncpy(rememberedPath, cache.sourcePath, sizeof(rememberedPath) - 1);
        rememberedPath[sizeof(rememberedPath) - 1] = '\0';
    }

    if (cache.cartPtr && cache.size > 0)
    {
        // Keep previously validated in-memory index for runtime stability.
        if (!cache.entries.empty()) return true;
        if (RebuildPackedAssetEntries(cache)) return true;
        const uint8_t* p = static_cast<const uint8_t*>(cache.cartPtr);
        if (cache.size >= 12 &&
            ReadLe32(p + 0) == 0x314B4150 &&
            ReadLe32(p + 4) == 1u)
        {
            // Keep raw buffer alive even when vector index rebuild failed.
            // Callers with raw-table fallback can still resolve entries.
            return true;
        }

        // Cart-only recovery path: try to refresh the existing Cart RAM block
        // before freeing/reallocating, minimizing fragmentation and stalls.
        SRL::Cd::ChangeDir((const char*)0);
        if (cache.sourcePath[0] != '\0')
        {
            SRL::Cd::File rf(cache.sourcePath);
            if (rf.Exists() && rf.Size.Bytes > 0 &&
                static_cast<uint64_t>(rf.Size.Bytes) <= static_cast<uint64_t>(cache.size) &&
                rf.Open())
            {
                const uint32_t bytes = static_cast<uint32_t>(rf.Size.Bytes);
                uint32_t readBytes = 0;
                if (ReadCdFileFully(rf, bytes, static_cast<uint8_t*>(cache.cartPtr), readBytes))
                {
                    cache.size = readBytes;
                    if (RebuildPackedAssetEntries(cache)) return true;
                    const uint8_t* rp = static_cast<const uint8_t*>(cache.cartPtr);
                    if (cache.size >= 12 &&
                        ReadLe32(rp + 0) == 0x314B4150 &&
                        ReadLe32(rp + 4) == 1u)
                    {
                        return true;
                    }
                }
            }
        }
        for (size_t i = 0; i < count; ++i)
        {
            SRL::Cd::File rf(candidates[i]);
            if (!rf.Exists() || rf.Size.Bytes <= 0) continue;
            if (static_cast<uint64_t>(rf.Size.Bytes) > static_cast<uint64_t>(cache.size)) continue;
            if (!rf.Open()) continue;

            const uint32_t bytes = static_cast<uint32_t>(rf.Size.Bytes);
            uint32_t readBytes = 0;
            if (!ReadCdFileFully(rf, bytes, static_cast<uint8_t*>(cache.cartPtr), readBytes)) continue;
            cache.size = readBytes;
            if (RebuildPackedAssetEntries(cache))
            {
                ::strncpy(cache.sourcePath, candidates[i], sizeof(cache.sourcePath) - 1);
                cache.sourcePath[sizeof(cache.sourcePath) - 1] = '\0';
                return true;
            }

            const uint8_t* rp = static_cast<const uint8_t*>(cache.cartPtr);
            if (cache.size >= 12 &&
                ReadLe32(rp + 0) == 0x314B4150 &&
                ReadLe32(rp + 4) == 1u)
            {
                return true;
            }
        }
        InvalidatePackedAssetCache(cache);
    }

    char pinnedPath[96]{};
    if (rememberedPath[0] != '\0')
    {
        ::strncpy(pinnedPath, rememberedPath, sizeof(pinnedPath) - 1);
        pinnedPath[sizeof(pinnedPath) - 1] = '\0';
    }
    cache = {};
    if (pinnedPath[0] != '\0')
    {
        ::strncpy(cache.sourcePath, pinnedPath, sizeof(cache.sourcePath) - 1);
        cache.sourcePath[sizeof(cache.sourcePath) - 1] = '\0';
    }
    SRL::Cd::ChangeDir((const char*)0);
    const char* foundPath = nullptr;
    if (cache.sourcePath[0] != '\0')
    {
        SRL::Cd::File pinnedProbe(cache.sourcePath);
        if (pinnedProbe.Exists() && pinnedProbe.Size.Bytes > 0)
        {
            foundPath = cache.sourcePath;
        }
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (foundPath) break;
        SRL::Cd::File probe(candidates[i]);
        if (probe.Exists() && probe.Size.Bytes > 0)
        {
            foundPath = candidates[i];
            break;
        }
    }
    if (!foundPath) return false;

    SRL::Cd::File f(foundPath);
    if (f.Size.Bytes <= 0) return false;
    if (!f.Open()) return false;

    const uint32_t bytes = static_cast<uint32_t>(f.Size.Bytes);
    void* mem = SRL::Memory::CartRam::Malloc(bytes);
    if (!mem) return false;

    uint32_t readBytes = 0;
    if (!ReadCdFileFully(f, bytes, static_cast<uint8_t*>(mem), readBytes))
    {
        SRL::Debug::Print(1, 15, "PAK rd %s g:%u e:%u",
                          foundPath ? foundPath : "?",
                          static_cast<unsigned>(readBytes),
                          static_cast<unsigned>(bytes));
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    cache.cartPtr = mem;
    cache.size = readBytes;
    if (RebuildPackedAssetEntries(cache))
    {
        if (foundPath && foundPath[0] != '\0')
        {
            ::strncpy(cache.sourcePath, foundPath, sizeof(cache.sourcePath) - 1);
            cache.sourcePath[sizeof(cache.sourcePath) - 1] = '\0';
        }
        return true;
    }

    const uint8_t* p = static_cast<const uint8_t*>(cache.cartPtr);
    if (cache.size >= 12 &&
        ReadLe32(p + 0) == 0x314B4150 &&
        ReadLe32(p + 4) == 1u)
    {
        if (foundPath && foundPath[0] != '\0')
        {
            ::strncpy(cache.sourcePath, foundPath, sizeof(cache.sourcePath) - 1);
            cache.sourcePath[sizeof(cache.sourcePath) - 1] = '\0';
        }
        return true;
    }

    InvalidatePackedAssetCache(cache);
    return false;
}

static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentComponent::Blob& out)
{
    out.loaded = false;
    out.size = 0;
    out.bytes.clear();
    if (!cache.cartPtr || cache.size == 0 || cache.entries.empty() || !entryName || entryName[0] == '\0') return false;

    for (size_t i = 0; i < cache.entries.size(); ++i)
    {
        const auto& e = cache.entries[i];
        if (!NameEqualsIgnoreCase(e.name, entryName)) continue;

        const uint8_t* src = static_cast<const uint8_t*>(cache.cartPtr) + e.offset;
        out.bytes.resize(e.size);
        ::memcpy(out.bytes.data(), src, e.size);
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    return false;
}

static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentDrawReady::Blob& out)
{
    out.loaded = false;
    out.size = 0;
    out.bytes.clear();
    if (!cache.cartPtr || cache.size == 0 || !entryName || entryName[0] == '\0') return false;

    for (size_t i = 0; i < cache.entries.size(); ++i)
    {
        const auto& e = cache.entries[i];
        if (!NameEqualsIgnoreCase(e.name, entryName)) continue;

        const uint8_t* src = static_cast<const uint8_t*>(cache.cartPtr) + e.offset;
        out.bytes.resize(e.size);
        ::memcpy(out.bytes.data(), src, e.size);
        if (out.bytes.size() < sizeof(SegmentDrawReady::HeaderV1)) return false;
        if (ReadLe32(out.bytes.data()) != SegmentDrawReady::kMagicSdr1) return false;
        if (ReadLe16(out.bytes.data() + 4) != SegmentDrawReady::kVersion1) return false;
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    // Fallback path: scan raw PAK table directly.
    // This avoids runtime dependence on the in-memory vector index.
    const uint8_t* p = static_cast<const uint8_t*>(cache.cartPtr);
    if (cache.size < 12) return false;
    const uint32_t magic = ReadLe32(p + 0);
    const uint32_t version = ReadLe32(p + 4);
    const uint32_t entryCount = ReadLe32(p + 8);
    if (magic != 0x314B4150 || version != 1) return false; // "PAK1"

    const size_t entrySize = 64 + 4 + 4;
    const size_t tableBytes = static_cast<size_t>(entryCount) * entrySize;
    const size_t dataOffset = 12 + tableBytes;
    if (dataOffset > cache.size) return false;

    for (uint32_t i = 0; i < entryCount; ++i)
    {
        const size_t off = 12 + static_cast<size_t>(i) * entrySize;
        if (off + entrySize > cache.size) return false;

        char name[65]{};
        ::memcpy(name, p + off, 64);
        name[64] = '\0';
        if (!NameEqualsIgnoreCase(name, entryName)) continue;

        const uint32_t entryOffset = ReadLe32(p + off + 64);
        const uint32_t entrySizeBytes = ReadLe32(p + off + 68);
        if (entrySizeBytes == 0) return false;
        if (entryOffset < dataOffset) return false;
        if (static_cast<uint64_t>(entryOffset) + static_cast<uint64_t>(entrySizeBytes) > static_cast<uint64_t>(cache.size))
        {
            return false;
        }

        const uint8_t* src = p + entryOffset;
        out.bytes.resize(entrySizeBytes);
        ::memcpy(out.bytes.data(), src, entrySizeBytes);
        if (out.bytes.size() < sizeof(SegmentDrawReady::HeaderV1)) return false;
        if (ReadLe32(out.bytes.data()) != SegmentDrawReady::kMagicSdr1) return false;
        if (ReadLe16(out.bytes.data() + 4) != SegmentDrawReady::kVersion1) return false;
        out.loaded = true;
        out.size = out.bytes.size();
        SRL::Debug::Print(1, 15, "SDR raw idx hit %s", entryName);
        return true;
    }

    // Last-resort cart-only fallback: scan the table area by fixed record stride
    // and match the entry name directly, even when header entryCount is corrupted.
    const size_t bruteLimit = std::min<size_t>(cache.size, 256u * 1024u);
    for (size_t off = 12; (off + entrySize) <= bruteLimit; off += entrySize)
    {
        char name[65]{};
        ::memcpy(name, p + off, 64);
        name[64] = '\0';
        if (!NameEqualsIgnoreCase(name, entryName)) continue;

        const uint32_t entryOffset = ReadLe32(p + off + 64);
        const uint32_t entrySizeBytes = ReadLe32(p + off + 68);
        if (entrySizeBytes == 0) return false;
        if (entryOffset >= cache.size) return false;
        if (static_cast<uint64_t>(entryOffset) + static_cast<uint64_t>(entrySizeBytes) > static_cast<uint64_t>(cache.size))
        {
            return false;
        }

        const uint8_t* src = p + entryOffset;
        out.bytes.resize(entrySizeBytes);
        ::memcpy(out.bytes.data(), src, entrySizeBytes);
        if (out.bytes.size() < sizeof(SegmentDrawReady::HeaderV1)) return false;
        if (ReadLe32(out.bytes.data()) != SegmentDrawReady::kMagicSdr1) return false;
        if (ReadLe16(out.bytes.data() + 4) != SegmentDrawReady::kVersion1) return false;
        out.loaded = true;
        out.size = out.bytes.size();
        SRL::Debug::Print(1, 15, "SDR brute idx hit %s", entryName);
        return true;
    }

    return false;
}

static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, SegmentRuntimeDraw::Blob& out)
{
    out.loaded = false;
    out.size = 0;
    out.bytes.clear();
    if (!cache.cartPtr || cache.size == 0 || !entryName || entryName[0] == '\0') return false;

    for (size_t i = 0; i < cache.entries.size(); ++i)
    {
        const auto& e = cache.entries[i];
        if (!NameEqualsIgnoreCase(e.name, entryName)) continue;

        const uint8_t* src = static_cast<const uint8_t*>(cache.cartPtr) + e.offset;
        out.bytes.resize(e.size);
        ::memcpy(out.bytes.data(), src, e.size);
        if (out.bytes.size() < sizeof(SegmentRuntimeDraw::HeaderV1)) return false;
        if (ReadLe32(out.bytes.data()) != SegmentRuntimeDraw::kMagicRdr1) return false;
        if (ReadLe16(out.bytes.data() + 4) != SegmentRuntimeDraw::kVersion1) return false;
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    const uint8_t* p = static_cast<const uint8_t*>(cache.cartPtr);
    if (cache.size < 12) return false;
    const uint32_t magic = ReadLe32(p + 0);
    const uint32_t version = ReadLe32(p + 4);
    const uint32_t entryCount = ReadLe32(p + 8);
    if (magic != 0x314B4150 || version != 1u || entryCount == 0u) return false;

    const size_t entrySize = 64 + 4 + 4;
    const size_t tableBytes = static_cast<size_t>(entryCount) * entrySize;
    const size_t dataOffset = 12 + tableBytes;
    if (dataOffset > cache.size) return false;

    for (uint32_t i = 0; i < entryCount; ++i)
    {
        const size_t off = 12 + static_cast<size_t>(i) * entrySize;
        if (off + entrySize > cache.size) return false;

        char name[65]{};
        ::memcpy(name, p + off, 64);
        name[64] = '\0';
        if (!NameEqualsIgnoreCase(name, entryName)) continue;

        const uint32_t entryOffset = ReadLe32(p + off + 64);
        const uint32_t entrySizeBytes = ReadLe32(p + off + 68);
        if (entrySizeBytes == 0) return false;
        if (entryOffset < dataOffset) return false;
        if (static_cast<uint64_t>(entryOffset) + static_cast<uint64_t>(entrySizeBytes) > static_cast<uint64_t>(cache.size))
        {
            return false;
        }

        const uint8_t* src = p + entryOffset;
        out.bytes.resize(entrySizeBytes);
        ::memcpy(out.bytes.data(), src, entrySizeBytes);
        if (out.bytes.size() < sizeof(SegmentRuntimeDraw::HeaderV1)) return false;
        if (ReadLe32(out.bytes.data()) != SegmentRuntimeDraw::kMagicRdr1) return false;
        if (ReadLe16(out.bytes.data() + 4) != SegmentRuntimeDraw::kVersion1) return false;
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    return false;
}

static bool LoadPackedAssetEntryToBlob(PackedAssetCache& cache, const char* entryName, BatchDrawReady::Blob& out)
{
    out.loaded = false;
    out.size = 0;
    out.bytes.clear();
    if (!cache.cartPtr || cache.size == 0 || cache.entries.empty() || !entryName || entryName[0] == '\0') return false;

    for (size_t i = 0; i < cache.entries.size(); ++i)
    {
        const auto& e = cache.entries[i];
        if (!NameEqualsIgnoreCase(e.name, entryName)) continue;

        const uint8_t* src = static_cast<const uint8_t*>(cache.cartPtr) + e.offset;
        out.bytes.resize(e.size);
        ::memcpy(out.bytes.data(), src, e.size);
        out.loaded = true;
        out.size = out.bytes.size();
        return true;
    }

    return false;
}
} // namespace

bool TrackSystem::PreloadTgaCatalogFromSegmentsMap()
{
    ReleaseSeg1TgaCatalog();
    g_seg1MapCache = {};
    g_seg1MapCacheValid = false;
    ResetSeg1TgaPreloadDebugState(seg1TgaPreloadCount_,
                                  seg1TgaAttemptCount_,
                                  seg1TgaFailCount_,
                                  seg1TgaJsonOk_);

    auto loadNameToCart = [&](const char* inName) -> bool
    {
        if (!inName || inName[0] == '\0') return false;

        char name[64]{};
        NormalizeTextureFileName(inName, name, sizeof(name));
        if (name[0] == '\0') return false;
        SetSeg1TgaLastName(name);

        if (HasSeg1TgaCartEntry(seg1TgaCatalog_, name)) return true;

        ++seg1TgaAttemptCount_;
        std::array<std::array<char, 96>, 18> candidateStorage{};
        const char* paths[18]{};
        const size_t pathCount = BuildSeg1TgaCandidatePaths(name, candidateStorage, paths);
        const char* loadedPath = nullptr;
        for (size_t p = 0; p < pathCount; ++p)
        {
            ::strncpy(g_tgaLastTry, paths[p], sizeof(g_tgaLastTry) - 1);
            g_tgaLastTry[sizeof(g_tgaLastTry) - 1] = '\0';
            SRL::Debug::Print(1, 26, "TGA cart try:%s", paths[p]);
            void* mem = nullptr;
            uint32_t read = 0;
            if (!LoadCdFileToCart(paths[p], mem, read)) continue;
            Seg1TgaCartEntry e{};
            ::strncpy(e.name, name, sizeof(e.name) - 1);
            e.cartPtr = mem;
            e.size = read;
            seg1TgaCatalog_.push_back(e);
            loadedPath = paths[p];
            ::strncpy(g_tgaLastResult, "ok", sizeof(g_tgaLastResult) - 1);
            g_tgaLastResult[sizeof(g_tgaLastResult) - 1] = '\0';
            SRL::Debug::Print(1, 27, "TGA ok:%s p:%s s:%u", name, loadedPath, e.size);
            return true;
        }
        ++seg1TgaFailCount_;
        std::snprintf(g_tgaLastResult, sizeof(g_tgaLastResult), "fail:%s", name);
        SRL::Debug::Print(1, 27, "TGA cart fail:%s", name);
        return false;
    };

    // Preferred source: deterministic rename map used by runtime lookup.
    std::vector<char> renText{};
    RenTextureMap renMap{};
    if (LoadSeg1RenTextureCopyMapFromCd(renText, renMap))
    {
        seg1TgaJsonOk_ = 2;
        for (size_t i = 0; i < renMap.entries.size(); ++i)
        {
            (void)loadNameToCart(renMap.entries[i].targetName);
        }
        std::vector<char> smapText{};
        g_seg1MapCache = {};
        g_seg1MapCacheValid = LoadSeg1TextureJsonFromSmapCd(smapText, g_seg1MapCache);
        if (g_seg1MapCacheValid) UpdateSeg1SmapDebugSnapshot(smapText, false);
        return FinalizeSeg1TgaPreloadCatalog(seg1TgaCatalog_, seg1TgaPreloadCount_);
    }

    std::vector<char> jsonText{};
    const bool cdLoaded = ReadSeg1SmapText(jsonText);
#if TRACK_ENABLE_HOST_SEGMENTS_MAP_FALLBACK
    if (!cdLoaded && !ReadLocalSegmentsMap(jsonText))
#else
    if (!cdLoaded)
#endif
    {
        ::strncpy(g_tgaLastResult, "map_open_fail", sizeof(g_tgaLastResult) - 1);
        g_tgaLastResult[sizeof(g_tgaLastResult) - 1] = '\0';
        SRL::Debug::Print(1, 6, "TGA map json fail");
        return false;
    }
    if (!cdLoaded)
    {
        SRL::Debug::Print(1, 6, "TGA map json load fallback local");
    }
    seg1TgaJsonOk_ = cdLoaded ? 1 : 3;
    std::snprintf(g_tgaLastResult, sizeof(g_tgaLastResult), "map_ok bytes:%u", (unsigned)jsonText.size());
    UpdateSeg1SmapDebugSnapshot(jsonText, true);

    char variantNames[1024][64]{};
    const size_t variantCount = CollectAllVariantTextureNames(jsonText.data(), variantNames, 1024);
    g_seg1MapCacheValid = ParseSegment1TextureJson(jsonText.data(), g_seg1MapCache);
    size_t extracted = 0;
    if (variantCount > 0)
    {
        for (size_t i = 0; i < variantCount; ++i)
        {
            SetSeg1TgaLastName(variantNames[i]);
            if (loadNameToCart(variantNames[i])) ++extracted;
        }
        std::snprintf(g_tgaLastResult, sizeof(g_tgaLastResult), "variants:%u ok:%u",
                      (unsigned)variantCount, (unsigned)extracted);
        SRL::Debug::Print(1, 8, "TGA pre ok:%u", (unsigned)extracted);
        return FinalizeSeg1TgaPreloadCatalog(seg1TgaCatalog_, seg1TgaPreloadCount_);
    }

    size_t tokenHits = 0;
    char firstToken[64]{};
    extracted += ScanSeg1TgaTokensAndLoad(seg1TgaCatalog_,
                                          jsonText,
                                          loadNameToCart,
                                          tokenHits,
                                          firstToken,
                                          sizeof(firstToken));

    if (tokenHits == 0)
    {
        ::strncpy(g_tgaLastResult, "no_tga_tokens", sizeof(g_tgaLastResult) - 1);
        g_tgaLastResult[sizeof(g_tgaLastResult) - 1] = '\0';
    }
    else
    {
        std::snprintf(g_tgaLastResult, sizeof(g_tgaLastResult), "tokens:%u first:%s ok:%u",
                      (unsigned)tokenHits, firstToken, (unsigned)extracted);
    }
    SRL::Debug::Print(1, 8, "TGA pre ok:%u", (unsigned)extracted);
    return FinalizeSeg1TgaPreloadCatalog(seg1TgaCatalog_, seg1TgaPreloadCount_);
}

bool TrackSystem::BuildSeg1TexbankCandidatePaths(int lodValue,
                                                 std::array<std::array<char, 40>, 16>& storage,
                                                 const char** outCandidates,
                                                 size_t& outCount)
{
    if (!outCandidates) return false;

    outCount = 0;
    auto addCandidate = [&](const char* fmt)
    {
        if (outCount >= storage.size()) return;
        std::snprintf(storage[outCount].data(), storage[outCount].size(), fmt, lodValue);
        outCandidates[outCount] = storage[outCount].data();
        ++outCount;
    };

    addCandidate("CD/DATA/TEXBANK_%d.BIN");
    addCandidate("CD/DATA/TEXBANK_%d.BIN;1");
    addCandidate("DATA/TEXBANK_%d.BIN");
    addCandidate("DATA/TEXBANK_%d.BIN;1");
    addCandidate("TEXBANK_%d.BIN");
    addCandidate("TEXBANK_%d.BIN;1");
    addCandidate("texbank_%d.bin");
    addCandidate("texbank_%d.bin;1");
    addCandidate("CD/DATA/TBK%d.BIN");
    addCandidate("CD/DATA/TBK%d.BIN;1");
    addCandidate("DATA/TBK%d.BIN");
    addCandidate("DATA/TBK%d.BIN;1");
    addCandidate("TBK%d.BIN");
    addCandidate("TBK%d.BIN;1");
    addCandidate("tbk%d.bin");
    addCandidate("tbk%d.bin;1");
    return outCount > 0;
}

bool TrackSystem::LoadSeg1TexbankIndexToCart(size_t lodIndex, int lodValue)
{
    if (lodIndex >= seg1Texbanks_.size()) return false;
    auto& bank = seg1Texbanks_[lodIndex];
    if (bank.lod == lodValue && bank.cartPtr && bank.size > 0 && !bank.entries.empty()) return true;

    if (bank.cartPtr)
    {
        SRL::Memory::CartRam::Free(bank.cartPtr);
    }
    bank = {};
    bank.lod = lodValue;

    std::array<std::array<char, 40>, 16> candidateStorage{};
    const char* cands[16]{};
    size_t candCount = 0;
    if (!BuildSeg1TexbankCandidatePaths(bank.lod, candidateStorage, cands, candCount)) return false;

    const char* foundPath = nullptr;
    for (size_t i = 0; i < candCount; ++i)
    {
        SRL::Cd::File probe(cands[i]);
        if (probe.Exists() && probe.Size.Bytes > 0)
        {
            foundPath = cands[i];
            break;
        }
    }
    if (!foundPath) return false;
    SRL::Cd::File f(foundPath);
    if (f.Size.Bytes <= 0) return false;
    if (!f.Open()) return false;

    const uint32_t bytes = static_cast<uint32_t>(f.Size.Bytes);
    void* mem = SRL::Memory::CartRam::Malloc(bytes);
    if (!mem) return false;
    const int32_t read = f.Read(static_cast<int32_t>(bytes), mem);
    if (read <= 0 || static_cast<uint32_t>(read) > bytes)
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    const uint32_t readBytes = static_cast<uint32_t>(read);
    const uint8_t* p = static_cast<const uint8_t*>(mem);
    if (readBytes < 20)
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }
    const uint32_t magic = ReadLe32(p + 0);
    const uint16_t ver = ReadLe16(p + 4);
    const uint16_t lod = ReadLe16(p + 6);
    const uint32_t count = ReadLe32(p + 8);
    const uint32_t dataOff = ReadLe32(p + 12);
    (void)ver;
    if (magic != 0x314B4254 || lod != static_cast<uint16_t>(bank.lod))
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }
    const uint32_t entryBase = 20;
    const uint32_t entrySize = 16;
    if (readBytes < entryBase)
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }
    const uint32_t entrySpan = readBytes - entryBase;
    if (count > (entrySpan / entrySize))
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }
    if (dataOff > readBytes)
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    TrackLowWorkVector<Seg1TexbankEntry> parsedEntries{};
    parsedEntries.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t o = entryBase + (i * entrySize);
        Seg1TexbankEntry e{};
        e.familyId = static_cast<uint16_t>(ReadLe32(p + o + 0));
        e.offset = ReadLe32(p + o + 4);
        e.size = ReadLe32(p + o + 8);
        if (e.offset <= readBytes && e.size <= (readBytes - e.offset))
        {
            parsedEntries.push_back(e);
        }
    }
    if (parsedEntries.empty())
    {
        SRL::Memory::CartRam::Free(mem);
        return false;
    }

    bank.cartPtr = mem;
    bank.size = readBytes;
    bank.entries = std::move(parsedEntries);
    return true;
}

const TrackSegmentCopy* TrackSystem::FindRawSegmentCopyById(int id) const
{
    for (const auto& e : rawSegmentCatalog_)
    {
        if (e.id == id && e.copy.cartPtr && e.copy.size > 0) return &e.copy;
    }
    return nullptr;
}

const char* TrackSystem::FindExistingPath(const char* const* paths, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        SRL::Cd::File f(paths[i]);
        const bool exists = f.Exists() && f.Size.Bytes > 0;
        ::strncpy(lastSegmentPath_, paths[i], sizeof(lastSegmentPath_));
        lastSegmentPath_[sizeof(lastSegmentPath_) - 1] = '\0';
        SRL::Debug::Print(1, 6, "CD p:%s -> %d", paths[i], exists ? 1 : 0);
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

TrackSegmentCopy TrackSystem::CopySegmentById(size_t id)
{
    TrackSegmentCopy copy{};

    char upperName[32]{};
    char upperNameV[32]{};
    char lowerName[32]{};
    char lowerNameV[32]{};
    std::snprintf(upperName, sizeof(upperName), "SEG_%03u.NYA", unsigned(id));
    std::snprintf(upperNameV, sizeof(upperNameV), "SEG_%03u.NYA;1", unsigned(id));
    std::snprintf(lowerName, sizeof(lowerName), "seg_%03u.nya", unsigned(id));
    std::snprintf(lowerNameV, sizeof(lowerNameV), "seg_%03u.nya;1", unsigned(id));
    const char* names[] = { upperName, upperNameV, lowerName, lowerNameV };
    const size_t namesCount = sizeof(names) / sizeof(names[0]);
    struct DirChain { const char* a; const char* b; };
    const DirChain dirChains[] = {
        { "DATA", nullptr },
        { "data", nullptr },
        { nullptr, nullptr },
        { "DATA", "SETORES" },
        { "data", "setores" },
        { "SETORES", nullptr },
        { "setores", nullptr },
        { nullptr, nullptr }
    };

    for (const auto& chain : dirChains)
    {
        SRL::Cd::ChangeDir((const char*)0);
        if (chain.a) SRL::Cd::ChangeDir(chain.a);
        if (chain.b) SRL::Cd::ChangeDir(chain.b);

        for (size_t ni = 0; ni < namesCount; ++ni)
        {
            const char* name = names[ni];
            SRL::Cd::File f(name);
            const bool exists = f.Exists() && f.Size.Bytes > 0;
            if (!exists) continue;

            if (chain.a && chain.b)
            {
                std::snprintf(lastSegmentPath_, sizeof(lastSegmentPath_), "%s/%s/%s", chain.a, chain.b, name);
            }
            else if (chain.a)
            {
                std::snprintf(lastSegmentPath_, sizeof(lastSegmentPath_), "%s/%s", chain.a, name);
            }
            else
            {
                std::snprintf(lastSegmentPath_, sizeof(lastSegmentPath_), "%s", name);
            }
            copy = CopyTrackSegmentToCart(name);
            SRL::Cd::ChangeDir((const char*)0);
            return copy;
        }
    }

    SRL::Cd::ChangeDir((const char*)0);
    std::snprintf(lastSegmentPath_, sizeof(lastSegmentPath_), "SEG_%03u.NYA (not found via ChangeDir)", unsigned(id));
    return copy;
}

TrackSystem::SegmentEntryVector TrackSystem::CopyAllTrackSegments(size_t maxSegments)
{
    SegmentEntryVector segments;
    const size_t loadLimit = (maxSegments == 0) ? kTrackSegmentLimit : std::min(maxSegments, kTrackSegmentLimit);
    segments.reserve(loadLimit);
    for (size_t i = 1; i <= loadLimit; ++i)
    {
        TrackSegmentCopy copy = CopySegmentById(i);
        if (!copy.cartPtr || copy.size == 0)
        {
            SRL::Debug::Print(1, 12, "SEG%03u p miss (%u)", unsigned(i), unsigned(kSegmentPathTemplates_.size()));
            break;
        }
        segments.push_back({ static_cast<int32_t>(i), copy });
        if (copy.cartPtr)
        {
            SRL::Debug::Print(1, 11, "SEG%03u cpy (%u)", unsigned(i), unsigned(copy.size));
        }
        else
        {
            // log removido
            break;
        }
    }

    size_t valid = 0;
    for (const auto& segment : segments)
    {
        if (segment.copy.cartPtr && segment.copy.size > 0) ++valid;
    }
    SRL::Debug::Print(1, 13, "TRK seg cpy %u/%u", unsigned(valid), unsigned(segments.size()));
    return segments;
}

TrackLowWorkVector<TrackSystem::SegmentRenderEntry> TrackSystem::BuildSegmentRenderers(SegmentEntryVector& entries)
{
    TrackLowWorkVector<SegmentRenderEntry> renderers;
    if (entries.empty()) return renderers;
    renderers.reserve(1);

    // Single-segment package: use the runtime blob path first and keep SDR as
    // compatibility fallback during migration.
    if (entries.size() == 1)
    {
        const int segmentId = entries[0].id;
        auto renderer = MakeTrackObjectUnique<TrackRenderer, SRL::Memory::Zone::LWRam>();
        Vector3D center(0.0, 0.0, 0.0);
        TrackLowWorkU16Vector familyIds{};
        bool usedRdr = false;
        if (!BuildRendererFromRuntimeBlob(segmentId, *renderer, &center, &familyIds, &usedRdr))
        {
            SRL::Debug::Print(1, 15, "SDR init fail %03d", segmentId);
            return {};
        }
        if (usedRdr) ++runtimeRdrBuildsThisFrame_;
        else ++runtimeSdrBuildsThisFrame_;

        if (familyIds.empty())
        {
            SRL::Debug::Print(1, 15, "SDR fam fail %03d", segmentId);
            return {};
        }

        ConfigureStreamedRendererDefaults(*renderer);
        ApplyActiveRendererCapacityFloor(*renderer);

        SegmentRenderEntry item{};
        item.id = segmentId;
        item.logicalSegmentCount = 1;
        item.center = center;
        item.renderer = std::move(renderer);
        item.lodState.SetReady(true);
        item.lodState.SetHasPerFaceRankOffsets(false);
        item.lodState.currentLodIndex = 0xFF;
        item.lodState.currentBaseRank = -1;
        item.lodState.desiredLodIndex = 0xFF;
        item.lodState.desiredBaseRank = -1;
        item.lodState.faceFamilyIds = std::move(familyIds);
        item.lodState.faceRankOffsets.assign(item.lodState.faceFamilyIds.size(), 0);
        item.lodState.currentFaceSlots.assign(item.lodState.faceFamilyIds.size(), -1);
        renderers.push_back(std::move(item));
        return renderers;
    }

    // Pre-size batch buffers using exact SDR counts for this package.
    // This avoids repeated growth and lowers Work RAM fragmentation.
    size_t totalVerts = 0;
    size_t totalFaces = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        SegmentDrawReady::HeaderV1 hdr{};
        if (!LoadSdrHeaderForSegment(entries[i].id, hdr))
        {
            SRL::Debug::Print(1, 15, "SDR head fail %03d", entries[i].id);
            return {};
        }
        totalVerts += static_cast<size_t>(hdr.vertexCount);
        totalFaces += static_cast<size_t>(hdr.faceCount);
    }
    if (totalVerts >= static_cast<size_t>(0xFFFF))
    {
        SRL::Debug::Print(1, 15, "SDR pkg vtx ovf:%u", static_cast<unsigned>(totalVerts));
        return {};
    }
    SRL::Debug::Print(1, 12, "SDR pkg vf v:%u f:%u", (unsigned)totalVerts, (unsigned)totalFaces);

    TrackLowWorkVector<SRL::Math::Types::Vector3D> batchVerts{};
    TrackLowWorkVector<SRL::Types::Polygon> batchFaces{};
    TrackLowWorkVector<SRL::Types::Attribute> batchAttrs{};
    TrackLowWorkU16Vector batchFamilyIds{};
    TrackLowWorkU8Vector batchFaceRankOffsets{};
    batchVerts.reserve(totalVerts);
    batchFaces.reserve(totalFaces);
    batchAttrs.reserve(totalFaces);
    batchFamilyIds.reserve(totalFaces);
    batchFaceRankOffsets.reserve(totalFaces);

    Vector3D minv(SRL::Math::Types::Fxp::BuildRaw(32767 << 16),
                  SRL::Math::Types::Fxp::BuildRaw(32767 << 16),
                  SRL::Math::Types::Fxp::BuildRaw(32767 << 16));
    Vector3D maxv(SRL::Math::Types::Fxp::BuildRaw(-32768 << 16),
                  SRL::Math::Types::Fxp::BuildRaw(-32768 << 16),
                  SRL::Math::Types::Fxp::BuildRaw(-32768 << 16));

    // Build one runtime draw package from N contiguous SDR segments.
    // This keeps asset flexibility (segment-level SDR) while reducing draw count.
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (!AppendSdrSegmentToBatch(entries[i].id,
                                     static_cast<uint8_t>(i),
                                     batchVerts,
                                     batchFaces,
                                     batchAttrs,
                                     batchFamilyIds,
                                     batchFaceRankOffsets,
                                     minv,
                                     maxv))
        {
            SRL::Debug::Print(1, 15, "SDR batch fail %03d", entries[i].id);
            return {};
        }
    }

    auto renderer = MakeTrackObjectUnique<TrackRenderer, SRL::Memory::Zone::LWRam>();
    if (!renderer->InitializeFromComponentDataRecycled(batchVerts,
                                                       batchFaces,
                                                       batchAttrs))
    {
        SRL::Debug::Print(1, 15, "SDR pkg init fail %03d", entries.front().id);
        return {};
    }
    ConfigureStreamedRendererDefaults(*renderer);
    ApplyActiveRendererCapacityFloor(*renderer);

    SegmentRenderEntry item{};
    item.id = entries.front().id;
    item.logicalSegmentCount = static_cast<uint8_t>(std::min<size_t>(entries.size(), 255));
    item.center = (minv + maxv) / SRL::Math::Types::Fxp::BuildRaw(2 << 16);
    item.renderer = std::move(renderer);
    item.lodState.SetReady(true);
    item.lodState.currentLodIndex = 0xFF;
    item.lodState.currentBaseRank = -1;
    item.lodState.desiredLodIndex = 0xFF;
    item.lodState.desiredBaseRank = -1;
    item.lodState.faceFamilyIds = std::move(batchFamilyIds);
    item.lodState.faceRankOffsets = std::move(batchFaceRankOffsets);
    item.lodState.SetHasPerFaceRankOffsets(false);
    for (size_t fi = 0; fi < item.lodState.faceRankOffsets.size(); ++fi)
    {
        if (item.lodState.faceRankOffsets[fi] != 0)
        {
            item.lodState.SetHasPerFaceRankOffsets(true);
            break;
        }
    }
    item.lodState.currentFaceSlots.assign(item.lodState.faceFamilyIds.size(), -1);
    renderers.push_back(std::move(item));
    return renderers;
}

void TrackSystem::BuildSegmentHandleTable()
{
    LWR_PROBE_BEGIN();
    segmentPool_.Reset();
    segmentHandles_.clear();
    if (segmentRenderers_.empty()) return;
    if (segmentHandles_.capacity() < segmentRenderers_.size())
        segmentHandles_.reserve(segmentRenderers_.size());
    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
        segmentHandles_.push_back(segmentPool_.Add(&segmentRenderers_[i]));
    LWR_PROBE_END(g_lwrStageAccum.buildHandleTable);
}

void TrackSystem::InitializeFamilySlots(FamilySlotVector& outSlots,
                                        const int* familyIds,
                                        size_t count) const
{
    outSlots.clear();
    outSlots.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const int fam = familyIds ? familyIds[i] : 0;
        if (fam <= 0) continue;
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = static_cast<uint16_t>(fam);
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        outSlots.push_back(slotEntry);
    }
    if (&outSlots == &seg1FamilySlots_) InvalidateFamilySlotIndex();
}

void TrackSystem::InitializeFamilySlots(FamilySlotVector& outSlots,
                                        const FamilyIdCatalogVector& familyIds) const
{
    outSlots.clear();
    outSlots.reserve(familyIds.size());
    for (size_t i = 0; i < familyIds.size(); ++i)
    {
        const uint16_t fam = familyIds[i];
        if (fam == 0) continue;
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = fam;
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        outSlots.push_back(slotEntry);
    }
    if (&outSlots == &seg1FamilySlots_) InvalidateFamilySlotIndex();
}

void TrackSystem::InvalidateFamilySlotIndex() const
{
    SetFamilySlotIndexDirty(true);
}

void TrackSystem::ResetFamilyLookupTables()
{
    surfaceTypeByFamilyId_.clear();
    surfaceTypeByFamilyId_.resize(1u, 0u);
    familySlotIndex_.clear();
    familySlotIndex_.resize(1u, static_cast<int16_t>(-1));
}

void TrackSystem::EnsureSurfaceFamilyLookupCapacity(size_t requiredEntries)
{
    if (requiredEntries <= surfaceTypeByFamilyId_.size()) return;
    const size_t oldSize = surfaceTypeByFamilyId_.size();
    surfaceTypeByFamilyId_.resize(requiredEntries, 0u);
    if (surfaceTypeByFamilyId_.size() > oldSize)
    {
        std::fill(surfaceTypeByFamilyId_.begin() + static_cast<std::ptrdiff_t>(oldSize),
                  surfaceTypeByFamilyId_.end(),
                  0u);
    }
}

void TrackSystem::EnsureFamilySlotIndexCapacity(size_t requiredEntries) const
{
    if (requiredEntries <= familySlotIndex_.size()) return;
    const size_t oldSize = familySlotIndex_.size();
    familySlotIndex_.resize(requiredEntries, static_cast<int16_t>(-1));
    if (familySlotIndex_.size() > oldSize)
    {
        std::fill(familySlotIndex_.begin() + static_cast<std::ptrdiff_t>(oldSize),
                  familySlotIndex_.end(),
                  static_cast<int16_t>(-1));
    }
}

void TrackSystem::RebuildFamilySlotIndex() const
{
    if (!FamilySlotIndexDirty()) return;

    size_t requiredEntries = 1u;
    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        const size_t fam = static_cast<size_t>(seg1FamilySlots_[i].familyId);
        if ((fam + 1u) > requiredEntries) requiredEntries = fam + 1u;
    }
    EnsureFamilySlotIndexCapacity(requiredEntries);

    for (size_t i = 0; i < familySlotIndex_.size(); ++i)
    {
        familySlotIndex_[i] = -1;
    }
    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        const uint16_t fam = seg1FamilySlots_[i].familyId;
        if (fam < familySlotIndex_.size() && familySlotIndex_[fam] < 0)
        {
            familySlotIndex_[fam] = static_cast<int16_t>(i);
        }
    }
    SetFamilySlotIndexDirty(false);
}

TrackSystem::Seg1FamilySlotEntry* TrackSystem::FindFamilySlot(FamilySlotVector& familySlots, uint16_t familyId)
{
    if (&familySlots == &seg1FamilySlots_ && familyId < familySlotIndex_.size())
    {
        RebuildFamilySlotIndex();
        const int16_t idx = familySlotIndex_[familyId];
        if (idx >= 0)
        {
            const size_t uidx = static_cast<size_t>(idx);
            if (uidx < familySlots.size() && familySlots[uidx].familyId == familyId)
            {
                return &familySlots[uidx];
            }
        }
    }
    for (size_t i = 0; i < familySlots.size(); ++i)
    {
        if (familySlots[i].familyId == familyId) return &familySlots[i];
    }
    return nullptr;
}

const TrackSystem::Seg1FamilySlotEntry* TrackSystem::FindFamilySlot(const FamilySlotVector& familySlots, uint16_t familyId) const
{
    if (&familySlots == &seg1FamilySlots_ && familyId < familySlotIndex_.size())
    {
        RebuildFamilySlotIndex();
        const int16_t idx = familySlotIndex_[familyId];
        if (idx >= 0)
        {
            const size_t uidx = static_cast<size_t>(idx);
            if (uidx < familySlots.size() && familySlots[uidx].familyId == familyId)
            {
                return &familySlots[uidx];
            }
        }
    }
    for (size_t i = 0; i < familySlots.size(); ++i)
    {
        if (familySlots[i].familyId == familyId) return &familySlots[i];
    }
    return nullptr;
}

bool TrackSystem::TryGetFamilyLodSlot(const FamilySlotVector& familySlots,
                                      uint16_t familyId,
                                      uint8_t lodIndex,
                                      uint16_t& outSlot) const
{
    outSlot = No_Texture;
    if (lodIndex > 3) return false;
    lodIndex = NormalizeTrackTextureLodIndex(lodIndex);
    const auto* slotEntry = FindFamilySlot(familySlots, familyId);
    if (!slotEntry) return false;
    outSlot = slotEntry->lodSlots[lodIndex];
    return IsVdp1TextureSlotActiveAndOwned(outSlot);
}

const TrackSystem::Seg1TexbankEntry* TrackSystem::FindTexbankEntryByFamily(const Seg1TexbankCart& bank, uint16_t familyId) const
{
    for (size_t i = 0; i < bank.entries.size(); ++i)
    {
        if (bank.entries[i].familyId == familyId) return &bank.entries[i];
    }
    return nullptr;
}

bool TrackSystem::TryLoadFamilyLodSlot(Seg1FamilySlotEntry& slotEntry,
                                       uint8_t targetLodIndex,
                                       bool fallbackToLowerLods,
                                       bool fallbackToHigherLods,
                                       bool* outSawMissingFamily,
                                       bool* outSawDecodeFail,
                                       bool* outSawUploadFail,
                                       int* outLoadedFromLodValue)
{
    if (outSawMissingFamily) *outSawMissingFamily = false;
    if (outSawDecodeFail) *outSawDecodeFail = false;
    if (outSawUploadFail) *outSawUploadFail = false;
    if (outLoadedFromLodValue) *outLoadedFromLodValue = 0;

    if (targetLodIndex > 3) return false;
    targetLodIndex = NormalizeTrackTextureLodIndex(targetLodIndex);
    if (slotEntry.familyId == 0) return false;
    if (slotEntry.lodSlots[targetLodIndex] != No_Texture)
    {
        const uint16_t existingSlot = slotEntry.lodSlots[targetLodIndex];
        // Usar ActiveAndOwned: um slot aposentado/reutilizÃ¡vel pode ter sido realocado
        // para outra famÃ­lia â€” IsVdp1TextureSlotLive() retornaria true com textura errada.
        if (IsVdp1TextureSlotActiveAndOwned(existingSlot))
        {
            if (outLoadedFromLodValue)
            {
                *outLoadedFromLodValue = TrackTextureLodValue(targetLodIndex);
            }
            return true;
        }
        // Slot invÃ¡lido, aposentado ou reutilizado â€” limpar e recarregar.
        slotEntry.lodSlots[targetLodIndex] = No_Texture;
    }

    uint8_t searchOrder[2]{};
    size_t searchCount = 0;
    searchOrder[searchCount++] = targetLodIndex;
    if (fallbackToHigherLods)
    {
        if (targetLodIndex == kTrackLod32Index) searchOrder[searchCount++] = kTrackLod64Index;
    }
    if (fallbackToLowerLods)
    {
        if (targetLodIndex == kTrackLod64Index) searchOrder[searchCount++] = kTrackLod32Index;
    }

    for (size_t si = 0; si < searchCount; ++si)
    {
        const uint8_t sourceLodIndex = searchOrder[si];
        const int sourceLodValue = TrackTextureLodValue(sourceLodIndex);
        if (!LoadSeg1TexbankIndexToCart(static_cast<size_t>(sourceLodIndex), sourceLodValue)) continue;
        const auto& bank = seg1Texbanks_[sourceLodIndex];
        const uint8_t* bankBytes = static_cast<const uint8_t*>(bank.cartPtr);
        if (!bankBytes) continue;

        const Seg1TexbankEntry* bankEntry = FindTexbankEntryByFamily(bank, slotEntry.familyId);
        if (!bankEntry)
        {
            if (outSawMissingFamily) *outSawMissingFamily = true;
            continue;
        }

        const auto previousHwrTag = SRL::Memory::HighWorkRam::GetDebugTag();
        const auto previousLwrTag = SRL::Memory::LowWorkRam::GetDebugTag();
        SetTrackWorkRamDebugTag(SRL::Memory::DebugTag::TrackTexture);
        static DecodedTgaTexture sDecodedScratch{};
        DecodedTgaTexture& decoded = sDecodedScratch;
        const bool decodedOk =
            DecodePalettedTgaMemory(bankBytes + bankEntry->offset, bankEntry->size, decoded);
        if (!decodedOk)
        {
            NormalizeDecodedTextureScratch(decoded);
            SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
            SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
            if (outSawDecodeFail) *outSawDecodeFail = true;
            continue;
        }

        const int32_t slot = UploadDecodedTextureToVdp1(decoded);
        NormalizeDecodedTextureScratch(decoded);
        SRL::Memory::HighWorkRam::SetDebugTag(previousHwrTag);
        SRL::Memory::LowWorkRam::SetDebugTag(previousLwrTag);
        if (slot < 0)
        {
            if (outSawUploadFail) *outSawUploadFail = true;
            continue;
        }

        slotEntry.lodSlots[targetLodIndex] = static_cast<uint16_t>(slot);
        if (outLoadedFromLodValue) *outLoadedFromLodValue = sourceLodValue;
        return true;
    }

    return false;
}

bool TrackSystem::PreloadFullTrackFamilyLodCache()
{
    // Keep only 32x32 + 64x64 banks resident.
    const size_t loadBankStart = static_cast<size_t>(kTrackLod32Index);
    const size_t loadBankCount = seg1Texbanks_.size(); // [2..3]
    // Free/reset banks that fall before loadBankStart (indices 0 and 1).
    for (size_t li = 0; li < loadBankStart; ++li)
    {
        auto& bank = seg1Texbanks_[li];
        if (bank.cartPtr) { SRL::Memory::CartRam::Free(bank.cartPtr); bank.cartPtr = nullptr; }
        bank = {};
        bank.lod = TrackTextureLodValue(static_cast<uint8_t>(li));
    }
    for (size_t li = loadBankStart; li < loadBankCount; ++li)
    {
        if (!LoadSeg1TexbankIndexToCart(li, TrackTextureLodValue(static_cast<uint8_t>(li))))
        {
            SetFullTrackFamilyCacheReady(false);
            return false;
        }
    }
    for (size_t li = loadBankCount; li < seg1Texbanks_.size(); ++li)
    {
        auto& bank = seg1Texbanks_[li];
        if (bank.cartPtr)
        {
            SRL::Memory::CartRam::Free(bank.cartPtr);
        }
        bank = {};
        bank.lod = TrackTextureLodValue(static_cast<uint8_t>(li));
    }

    FamilyIdCatalogVector familyIdsUsed{};
    familyIdsUsed.reserve(128);
    std::array<uint8_t, 4096> seenFamilies{};
    for (size_t i = 0; i < seenFamilies.size(); ++i) seenFamilies[i] = 0u;

    for (size_t li = loadBankStart; li < loadBankCount; ++li)
    {
        const auto& bank = seg1Texbanks_[li];
        for (size_t i = 0; i < bank.entries.size(); ++i)
        {
            const uint16_t fam = bank.entries[i].familyId;
            if (fam == 0) continue;
            if (fam < seenFamilies.size())
            {
                if (seenFamilies[fam] != 0u) continue;
                seenFamilies[fam] = 1u;
            }
            else
            {
                bool exists = false;
                for (size_t fi = 0; fi < familyIdsUsed.size(); ++fi)
                {
                    if (familyIdsUsed[fi] == fam)
                    {
                        exists = true;
                        break;
                    }
                }
                if (exists) continue;
            }
            familyIdsUsed.push_back(fam);
        }
    }

    if (familyIdsUsed.empty())
    {
        SetFullTrackFamilyCacheReady(false);
        return false;
    }

    InitializeFamilySlots(seg1FamilySlots_, familyIdsUsed);
    InvalidateFamilySlotIndex();
    SetFullTrackFamilyCacheReady(!seg1FamilySlots_.empty());

    std::array<unsigned, 4> loadedLodCounts{};
    std::array<unsigned, 4> failedLodCounts{};
    if (FullTrackFamilyCacheReady() && kEnableTrackRuntimeStabilization)
    {
        const bool savedReady = ReadyFlag();
        const uint8_t savedTextureUploads = textureUploadsThisFrame_;
        SetReadyFlag(false);
        textureUploadsThisFrame_ = 0;
        const uint8_t preloadLodIndex = kTrackLod32Index;
        for (auto& family : seg1FamilySlots_)
        {
            bool sawMissingFamily = false;
            bool sawDecodeFail = false;
            bool sawUploadFail = false;
            if (TryLoadFamilyLodSlot(family,
                                     preloadLodIndex,
                                     /*fallbackToLowerLods*/false,
                                     /*fallbackToHigherLods*/false,
                                     &sawMissingFamily,
                                     &sawDecodeFail,
                                     &sawUploadFail,
                                     nullptr))
            {
                ++loadedLodCounts[preloadLodIndex];
            }
            else
            {
                ++failedLodCounts[preloadLodIndex];
            }
        }
        SetReadyFlag(savedReady);
        textureUploadsThisFrame_ = savedTextureUploads;
    }

    const auto cart = SRL::Memory::CartRam::GetReport();
    const auto hwr = SRL::Memory::HighWorkRam::GetReport();
    const auto lwr = SRL::Memory::LowWorkRam::GetReport();
    if constexpr (kEnableTrackOverlayRows16To22)
    {
        SRL::Debug::Print(1, 21, "TRK fam catalog fam:%u b:%u 32:%u/%u",
                          static_cast<unsigned>(seg1FamilySlots_.size()),
                          static_cast<unsigned>(loadBankCount - loadBankStart),
                          loadedLodCounts[2],
                          failedLodCounts[2]);
        SRL::Debug::Print(1, 22, "TRK fam lod64:%u/%u",
                          loadedLodCounts[3],
                          failedLodCounts[3]);
    }
    if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
    {
        SRL::Debug::Print(1, 23, "TRK mem hb:%u lb:%u cf:%u",
                          static_cast<unsigned>(EstimateWorkRamRetainedBytes()),
                          static_cast<unsigned>(EstimateLowWorkRamRetainedBytes()),
                          static_cast<unsigned>(cart.FreeSize));
    }
    constexpr bool kEnableTrackRamOverlay = false;
    if (kEnableTrackRamOverlay)
    {
        SRL::Debug::Print(1, 24, "TRK ram hf:%u lf:%u     ",
                          static_cast<unsigned>(hwr.FreeSize),
                          static_cast<unsigned>(lwr.FreeSize));
    }
    return FullTrackFamilyCacheReady();
}

// Build shared family ids used by the track renderer set. Texture slots are loaded lazily.

bool TrackSystem::BuildTrackFamilyLodSlots(FamilySlotVector& outSlots)
{
    outSlots.clear();
    const size_t familyFloor = std::max<size_t>(256u, static_cast<size_t>(familySlotCapacityFloor_));
    if (outSlots.capacity() < familyFloor) outSlots.reserve(familyFloor);
    std::array<uint8_t, 4096> seenSmallFamily{};
    for (size_t i = 0; i < seenSmallFamily.size(); ++i) seenSmallFamily[i] = 0;
    for (const auto& seg : segmentRenderers_)
    {
        if (!seg.renderer) continue;
        if (!seg.lodState.Ready()) continue;
        SegmentRenderEntry& mutableSeg = const_cast<SegmentRenderEntry&>(seg);
        const bool strictLeakIsolationFamilies = kEnableTrackLeakIsolationFixed64Pipeline;
        if (!strictLeakIsolationFamilies && mutableSeg.lodState.WorkingSetCacheDirty())
        {
            (void)RebuildEntryWorkingSetCache(mutableSeg);
        }

        const auto& familySource = strictLeakIsolationFamilies
            ? mutableSeg.lodState.faceFamilyIds
            : (!mutableSeg.lodState.workingSetFamilies.empty()
                ? mutableSeg.lodState.workingSetFamilies
                : mutableSeg.lodState.faceFamilyIds);
        for (size_t fi = 0; fi < familySource.size(); ++fi)
        {
            const uint16_t fam = familySource[fi];
            if (fam == 0) continue;
            if (fam < seenSmallFamily.size())
            {
                if (seenSmallFamily[fam]) continue;
                seenSmallFamily[fam] = 1;
            }
            else
            {
                bool exists = false;
                for (size_t i = 0; i < outSlots.size(); ++i)
                {
                    if (outSlots[i].familyId == fam)
                    {
                        exists = true;
                        break;
                    }
                }
                if (exists) continue;
            }
            Seg1FamilySlotEntry slotEntry{};
            slotEntry.familyId = fam;
            slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
            outSlots.push_back(slotEntry);
        }
    }

    return !outSlots.empty();
}

void TrackSystem::InvalidateEntryWorkingSetCache(SegmentRenderEntry& entry)
{
    entry.lodState.SetWorkingSetCacheDirty(true);
}

bool TrackSystem::RebuildEntryWorkingSetCache(SegmentRenderEntry& entry)
{
    entry.lodState.workingSetFamilies.clear();
    entry.lodState.workingSetLodIndices.clear();
    entry.lodState.workingSetSlots.clear();
    entry.lodState.SetWorkingSetCacheDirty(false);

    if (!entry.renderer || !entry.lodState.Ready()) return false;
    if (entry.lodState.faceFamilyIds.empty()) return false;

    const size_t faceCount = entry.lodState.faceFamilyIds.size();
    const bool hasRankOffsets = entry.lodState.HasPerFaceRankOffsets() &&
                                entry.lodState.faceRankOffsets.size() == faceCount;
    const uint8_t fallbackLodIndex = (entry.lodState.currentLodIndex <= 3u)
        ? NormalizeTrackTextureLodIndex(entry.lodState.currentLodIndex)
        : kTrackLod32Index;

    std::array<uint32_t, kSegmentFamilyDedupScratchCap> seenKeys{};
    std::array<int16_t, kSegmentFamilyDedupScratchCap> seenSlots{};
    size_t seenCount = 0u;
    constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
    auto findOverflowWorkingSetIndex = [&](uint16_t familyId, uint8_t lodIndex) -> size_t
    {
        for (size_t wi = 0; wi < entry.lodState.workingSetFamilies.size(); ++wi)
        {
            if (entry.lodState.workingSetFamilies[wi] != familyId) continue;
            if (wi >= entry.lodState.workingSetLodIndices.size()) continue;
            if (entry.lodState.workingSetLodIndices[wi] != lodIndex) continue;
            return wi;
        }
        return kInvalidIndex;
    };

    for (size_t fi = 0; fi < faceCount; ++fi)
    {
        const uint16_t fam = entry.lodState.faceFamilyIds[fi];
        if (fam == 0u) continue;

        uint8_t resolvedLod = fallbackLodIndex;
        const int32_t slotHint =
            (fi < entry.lodState.currentFaceSlots.size()) ? entry.lodState.currentFaceSlots[fi] : -1;
        if (hasRankOffsets && entry.lodState.currentBaseRank >= 0)
        {
            const size_t rank = static_cast<size_t>(entry.lodState.currentBaseRank) +
                                static_cast<size_t>(entry.lodState.faceRankOffsets[fi]);
            resolvedLod = ResolveSegmentLodIndexByRank(rank);
        }
        else if (slotHint >= 0 && slotHint < static_cast<int32_t>(SRL_MAX_TEXTURES))
        {
            const uint16_t slot = static_cast<uint16_t>(slotHint);
            const auto* family = FindFamilySlot(seg1FamilySlots_, fam);
            if (family)
            {
                for (uint8_t li = 0; li < 4u; ++li)
                {
                    if (family->lodSlots[li] != slot) continue;
                    // ActiveAndOwned: a reusable slot may match by number but
                    // is no longer owned by this family â€” use fallback LOD.
                    if (!IsVdp1TextureSlotActiveAndOwned(slot)) continue;
                    resolvedLod = NormalizeTrackTextureLodIndex(li);
                    break;
                }
            }
        }

        const uint32_t key =
            (static_cast<uint32_t>(resolvedLod) << 16) | static_cast<uint32_t>(fam);
        const size_t cached = FindScratchKeyIndex(
            seenKeys,
            std::min<size_t>(seenCount, kSegmentFamilyDedupScratchCap),
            key);
        if (cached != kSegmentFamilyDedupScratchCap)
        {
            if (seenSlots[cached] < 0 && slotHint >= 0)
            {
                seenSlots[cached] = static_cast<int16_t>(slotHint);
                if (cached < entry.lodState.workingSetSlots.size())
                {
                    entry.lodState.workingSetSlots[cached] = static_cast<int16_t>(slotHint);
                }
            }
            continue;
        }
        if (seenCount >= kSegmentFamilyDedupScratchCap)
        {
            // Overflow path: dedup against already-built working set entries.
            // Without this, segments with >kSegmentFamilyDedupScratchCap unique
            // families keep appending duplicate keys per face and inflate LWR.
            const size_t overflowCached = findOverflowWorkingSetIndex(fam, resolvedLod);
            if (overflowCached != kInvalidIndex)
            {
                if (slotHint >= 0 &&
                    overflowCached < entry.lodState.workingSetSlots.size() &&
                    entry.lodState.workingSetSlots[overflowCached] < 0)
                {
                    entry.lodState.workingSetSlots[overflowCached] =
                        static_cast<int16_t>(slotHint);
                }
                continue;
            }
        }

        if (seenCount < kSegmentFamilyDedupScratchCap)
        {
            seenKeys[seenCount] = key;
            seenSlots[seenCount] = static_cast<int16_t>(slotHint);
            ++seenCount;
        }
        entry.lodState.workingSetFamilies.push_back(fam);
        entry.lodState.workingSetLodIndices.push_back(resolvedLod);
        entry.lodState.workingSetSlots.push_back(static_cast<int16_t>(slotHint));
    }

    return !entry.lodState.workingSetFamilies.empty();
}

void TrackSystem::RebuildUsedTextureSlotFlagsFromWorkingRefs()
{
    ClearUsedTextureSlots(usedTextureSlotsThisFrame_);
    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        const auto& family = seg1FamilySlots_[i];
        for (uint8_t li = 0; li < 4u; ++li)
        {
            if (family.workingRefs[li] == 0u) continue;
            const uint16_t slot = family.lodSlots[li];
            if (!IsVdp1TextureSlotLive(slot)) continue;
            usedTextureSlotsThisFrame_[slot] = 1u;
        }
    }
}

void TrackSystem::RebuildUsedTextureSlotFlagsFromCurrentFaces()
{
    ClearUsedTextureSlots(usedTextureSlotsThisFrame_);
    if (seg1FamilySlots_.empty()) return;
    for (size_t ei = 0; ei < segmentRenderers_.size(); ++ei)
    {
        const auto& entry = segmentRenderers_[ei];
        if (!entry.renderer) continue;
        if (!entry.lodState.Ready()) continue;
        if (entry.lodState.faceFamilyIds.empty()) continue;
        if (kEnableTrackLeakIsolationFixed64Pipeline)
        {
            size_t logicalRank = 0u;
            if (!TryGetWindowLogicalRank(entry.id, logicalRank)) continue;
            (void)logicalRank;
        }
        for (size_t fi = 0; fi < entry.lodState.faceFamilyIds.size(); ++fi)
        {
            const uint16_t fam = entry.lodState.faceFamilyIds[fi];
            if (fam == 0u) continue;
            if (fi >= entry.lodState.currentFaceSlots.size()) continue;
            const int32_t slotHint = entry.lodState.currentFaceSlots[fi];
            if (slotHint < 0) continue;
            if (slotHint >= static_cast<int32_t>(usedTextureSlotsThisFrame_.size())) continue;
            const uint16_t slot = static_cast<uint16_t>(slotHint);
            const Seg1FamilySlotEntry* family = FindFamilySlot(seg1FamilySlots_, fam);
            if (!family) continue;
            bool ownsSlot = false;
            for (uint8_t li = 0u; li < 4u; ++li)
            {
                if (family->lodSlots[li] != slot) continue;
                if (!IsVdp1TextureSlotActiveAndOwned(slot)) continue;
                ownsSlot = true;
                break;
            }
            if (!ownsSlot) continue;
            usedTextureSlotsThisFrame_[slot] = 1u;
        }
    }
}

uint32_t TrackSystem::GetStrictPendingLodPriority(size_t logicalRank) const
{
    const bool reverse = (windowDirection_ < 0);
    const std::array<size_t, 4> boundaryRanks = reverse
        ? std::array<size_t, 4>{{0u, 4u, 9u, 14u}}
        : std::array<size_t, 4>{{3u, 8u, 13u, 19u}};
    for (size_t i = 0; i < boundaryRanks.size(); ++i)
    {
        if (logicalRank == boundaryRanks[i]) return static_cast<uint32_t>(i);
    }

    const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
    const uint32_t lodPriority = 3u - std::min<uint8_t>(desiredLodIndex, 3u);
    return 16u + (lodPriority * static_cast<uint32_t>(kTrackSegmentLimit)) +
           static_cast<uint32_t>(logicalRank);
}

// Build the per face family table for one segment renderer from SDR1.
bool TrackSystem::BuildSegmentLodState(SegmentRenderEntry& entry,
                                       const SegmentComponent::Blob& matBlob,
                                       const SegmentComponent::Loader::MatView& matView,
                                       FamilySlotVector& familySlots)
{
    if (!entry.renderer) return false;
    if (!entry.lodState.Ready()) return false;
    if (entry.lodState.faceFamilyIds.empty()) return false;
    entry.lodState.currentFaceSlots.assign(entry.lodState.faceFamilyIds.size(), -1);
    entry.lodState.SetHasPerFaceRankOffsets(false);
    for (size_t fi = 0; fi < entry.lodState.faceRankOffsets.size(); ++fi)
    {
        if (entry.lodState.faceRankOffsets[fi] != 0)
        {
            entry.lodState.SetHasPerFaceRankOffsets(true);
            break;
        }
    }

    (void)matBlob;
    (void)matView;

    entry.lodState.currentLodIndex = 0xFF;
    entry.lodState.currentBaseRank = -1;
    entry.lodState.desiredLodIndex = 0xFF;
    entry.lodState.desiredBaseRank = -1;
    return RebuildSegmentFaceSlotsForBaseRank(entry, 0, familySlots);
}

// Upload one family texture slot only when a lod band actually needs it.
bool TrackSystem::EnsureFamilyLodSlotLoaded(FamilySlotVector& familySlots,
                                            uint16_t familyId,
                                            uint8_t lodIndex,
                                            bool bypassUploadBudget)
{
    if (lodIndex > 3) return false;

    Seg1FamilySlotEntry* slotEntry = FindFamilySlot(familySlots, familyId);
    if (!slotEntry) return false;
    const uint16_t existing = slotEntry->lodSlots[lodIndex];
    if (existing != No_Texture && IsVdp1TextureSlotActiveAndOwned(existing)) return true;
    if (existing != No_Texture) slotEntry->lodSlots[lodIndex] = No_Texture; // limpar slot stale
    if (!bypassUploadBudget &&
        ReadyFlag() &&
        textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame()) return false;
    const bool requireExactLod =
        kEnableTrackRuntimeStabilization && kEnableTrackLodBandsInStabilization;
    const bool loaded = TryLoadFamilyLodSlot(*slotEntry,
                                             lodIndex,
                                             /*fallbackToLowerLods*/!requireExactLod,
                                             /*fallbackToHigherLods*/!requireExactLod,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr);
    if (loaded && ReadyFlag()) ++textureUploadsThisFrame_;
    return loaded;
}

bool TrackSystem::TryGetBestFamilyLodSlot(FamilySlotVector& familySlots,
                                          uint16_t familyId,
                                          uint8_t preferredLodIndex,
                                          uint16_t& outSlot,
                                          uint8_t* outResolvedLodIndex,
                                          bool tryLoadFallback,
                                          bool bypassUploadBudget)
{
    outSlot = No_Texture;
    if (outResolvedLodIndex) *outResolvedLodIndex = 0xFF;
    if (preferredLodIndex > 3u) return false;
    preferredLodIndex = NormalizeTrackTextureLodIndex(preferredLodIndex);

    Seg1FamilySlotEntry* slotEntry = FindFamilySlot(familySlots, familyId);
    if (!slotEntry) return false;

    auto tryExistingSlot = [&](uint8_t lodIndex) -> bool
    {
        if (lodIndex > 3u) return false;
        const uint16_t slot = slotEntry->lodSlots[lodIndex];
        if (!IsVdp1TextureSlotActiveAndOwned(slot)) return false;
        outSlot = slot;
        if (outResolvedLodIndex) *outResolvedLodIndex = lodIndex;
        return true;
    };

    if (tryExistingSlot(preferredLodIndex)) return true;
    if (preferredLodIndex == kTrackLod32Index && tryExistingSlot(kTrackLod64Index)) return true;
    if (preferredLodIndex == kTrackLod64Index && tryExistingSlot(kTrackLod32Index)) return true;

    if (!tryLoadFallback) return false;
    if (!bypassUploadBudget &&
        ReadyFlag() &&
        textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame())
    {
        return false;
    }

    const bool loaded = TryLoadFamilyLodSlot(*slotEntry,
                                             preferredLodIndex,
                                             /*fallbackToLowerLods*/true,
                                             /*fallbackToHigherLods*/true,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr);
    if (loaded && ReadyFlag()) ++textureUploadsThisFrame_;
    if (tryExistingSlot(preferredLodIndex)) return true;
    if (tryExistingSlot(kTrackLod32Index)) return true;
    if (tryExistingSlot(kTrackLod64Index)) return true;
    return false;
}

bool TrackSystem::ResolveBestEffortFaceSlots(const FamilyIdVector& faceFamilyIds,
                                             uint8_t preferredLodIndex,
                                             FamilySlotVector& familySlots,
                                             TrackLowWorkI16Vector& outFaceSlots,
                                             bool tryLoadFallback,
                                             bool bypassUploadBudget)
{
    if (preferredLodIndex > 3u) return false;
    preferredLodIndex = NormalizeTrackTextureLodIndex(preferredLodIndex);
    if (faceFamilyIds.empty()) return false;

    outFaceSlots.assign(faceFamilyIds.size(), -1);
    std::array<uint16_t, kSegmentFamilyDedupScratchCap> seenFamilies{};
    std::array<int16_t, kSegmentFamilyDedupScratchCap> seenSlots{};
    size_t seenCount = 0u;
    bool anyResolved = false;

    for (size_t fi = 0; fi < faceFamilyIds.size(); ++fi)
    {
        const uint16_t fam = faceFamilyIds[fi];
        if (fam == 0u) continue;

        const size_t cached = FindScratchKeyIndex(seenFamilies, seenCount, fam);
        if (cached != kSegmentFamilyDedupScratchCap)
        {
            outFaceSlots[fi] = seenSlots[cached];
            anyResolved = anyResolved || (seenSlots[cached] >= 0);
            continue;
        }

        int16_t resolved = -1;
        uint16_t slot = No_Texture;
        if (TryGetBestFamilyLodSlot(familySlots,
                                    fam,
                                    preferredLodIndex,
                                    slot,
                                    nullptr,
                                    tryLoadFallback,
                                    bypassUploadBudget))
        {
            resolved = static_cast<int16_t>(slot);
            outFaceSlots[fi] = resolved;
            anyResolved = true;
        }

        if (seenCount < kSegmentFamilyDedupScratchCap)
        {
            seenFamilies[seenCount] = fam;
            seenSlots[seenCount] = resolved;
            ++seenCount;
        }
    }

    return anyResolved;
}

// Rebuild one segment face slot table on demand for the selected lod band.
template <typename FaceFamilyVecT, typename FaceSlotsVecT>
bool TrackSystem::ResolveFaceSlotsForFixedLodFromFamilies(const FaceFamilyVecT& faceFamilyIds,
                                                          uint8_t lodIndex,
                                                          FamilySlotVector& familySlots,
                                                          FaceSlotsVecT& outFaceSlots,
                                                          bool bypassUploadBudget)
{
    if (lodIndex > 3) return false;
    const size_t faceCount = faceFamilyIds.size();
    if (faceCount == 0) return false;

    outFaceSlots.assign(faceCount, -1);
    std::array<uint16_t, kSegmentFamilyDedupScratchCap> seenFamilies{};
    std::array<int16_t, kSegmentFamilyDedupScratchCap> seenSlots{};
    size_t seenCount = 0;

    for (size_t fi = 0; fi < faceCount; ++fi)
    {
        const uint16_t fam = faceFamilyIds[fi];
        if (fam == 0) continue;

        const size_t cached = FindScratchKeyIndex(seenFamilies, seenCount, fam);
        if (cached != kSegmentFamilyDedupScratchCap)
        {
            outFaceSlots[fi] = seenSlots[cached];
            continue;
        }

        (void)EnsureFamilyLodSlotLoaded(familySlots, fam, lodIndex, bypassUploadBudget);
        int16_t resolved = -1;
        uint16_t slot = No_Texture;
        if (TryGetFamilyLodSlot(familySlots, fam, lodIndex, slot))
        {
            resolved = static_cast<int16_t>(slot);
            outFaceSlots[fi] = resolved;
        }
        if (seenCount < kSegmentFamilyDedupScratchCap)
        {
            seenFamilies[seenCount] = fam;
            seenSlots[seenCount] = resolved;
            ++seenCount;
        }
    }

    return true;
}

template <typename FaceFamilyVecT, typename RankOffsetVecT, typename FaceSlotsVecT>
bool TrackSystem::ResolveFaceSlotsForBaseRankFromFamilies(const FaceFamilyVecT& faceFamilyIds,
                                                          const RankOffsetVecT& faceRankOffsets,
                                                          size_t baseRank,
                                                          FamilySlotVector& familySlots,
                                                          FaceSlotsVecT& outFaceSlots,
                                                          bool bypassUploadBudget)
{
    const size_t faceCount = faceFamilyIds.size();
    if (faceCount == 0) return false;

    outFaceSlots.assign(faceCount, -1);
    std::array<uint16_t, kSegmentFamilyDedupScratchCap> seenFamilies{};
    std::array<int16_t, kSegmentFamilyDedupScratchCap> seenSlots{};
    size_t seenCount = 0;

    const bool hasRankOffsets = faceRankOffsets.size() == faceCount;
    for (size_t fi = 0; fi < faceCount; ++fi)
    {
        const uint16_t fam = faceFamilyIds[fi];
        if (fam == 0) continue;

        const size_t rank = baseRank + (hasRankOffsets ? static_cast<size_t>(faceRankOffsets[fi]) : 0u);
        const uint8_t lodIndex = ResolveSegmentLodIndexByRank(rank);
        const uint16_t key = static_cast<uint16_t>((static_cast<uint16_t>(lodIndex) << 12) | (fam & 0x0FFFu));
        const size_t cached = FindScratchKeyIndex(seenFamilies, seenCount, key);
        if (cached != kSegmentFamilyDedupScratchCap)
        {
            outFaceSlots[fi] = seenSlots[cached];
            continue;
        }

        (void)EnsureFamilyLodSlotLoaded(familySlots, fam, lodIndex, bypassUploadBudget);
        int16_t resolved = -1;
        uint16_t slot = No_Texture;
        if (TryGetFamilyLodSlot(familySlots, fam, lodIndex, slot))
        {
            resolved = static_cast<int16_t>(slot);
            outFaceSlots[fi] = resolved;
        }
        if (seenCount < kSegmentFamilyDedupScratchCap)
        {
            seenFamilies[seenCount] = key;
            seenSlots[seenCount] = resolved;
            ++seenCount;
        }
    }

    return true;
}

bool TrackSystem::RebuildSegmentFaceSlotsForLod(SegmentRenderEntry& entry,
                                                uint8_t lodIndex,
                                                FamilySlotVector& familySlots,
                                                bool bypassUploadBudget)
{
    if (!entry.renderer) return false;
    if (lodIndex > 3) return false;
    lodIndex = NormalizeTrackTextureLodIndex(lodIndex);
    return ResolveFaceSlotsForFixedLodFromFamilies(entry.lodState.faceFamilyIds,
                                                   lodIndex,
                                                   familySlots,
                                                   entry.lodState.currentFaceSlots,
                                                   bypassUploadBudget);
}

bool TrackSystem::RebuildSegmentFaceSlotsForBaseRank(SegmentRenderEntry& entry,
                                                     size_t baseRank,
                                                     FamilySlotVector& familySlots,
                                                     bool bypassUploadBudget)
{
    if (!entry.renderer) return false;
    return ResolveFaceSlotsForBaseRankFromFamilies(entry.lodState.faceFamilyIds,
                                                   entry.lodState.faceRankOffsets,
                                                   baseRank,
                                                   familySlots,
                                                   entry.lodState.currentFaceSlots,
                                                   bypassUploadBudget);
}

bool TrackSystem::RebuildSafeSegmentEntry(SegmentRenderEntry& entry)
{
    if (!entry.renderer) return false;
    if (entry.id <= 0) return false;

    uint8_t desiredLodIndex = kTrackLod32Index;
    if (kEnableTrackLodBandsInStabilization)
    {
        if (entry.lodState.desiredLodIndex <= 3u)
        {
            desiredLodIndex = NormalizeTrackTextureLodIndex(entry.lodState.desiredLodIndex);
        }
        else
        {
            size_t logicalRank = 0;
            if (TryGetWindowLogicalRank(entry.id, logicalRank))
            {
                desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
            }
            else if (entry.lodState.currentLodIndex <= 3u)
            {
                desiredLodIndex = NormalizeTrackTextureLodIndex(entry.lodState.currentLodIndex);
            }
        }
    }
    else if (entry.lodState.currentLodIndex <= 3u)
    {
        desiredLodIndex = NormalizeTrackTextureLodIndex(entry.lodState.currentLodIndex);
    }

    Vector3D rebuiltCenter(0.0, 0.0, 0.0);
    FamilyIdVector rebuiltFamilyIds{};
    if (!BuildSegmentIntoRenderer(entry.id, *entry.renderer, rebuiltCenter, rebuiltFamilyIds)) return false;
    if (rebuiltFamilyIds.empty()) return false;
    ApplyActiveRendererCapacityFloor(*entry.renderer);

    bool addedFamily = false;
    for (const uint16_t fam : rebuiltFamilyIds)
    {
        if (fam == 0) continue;
        if (FindFamilySlot(seg1FamilySlots_, fam)) continue;
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = fam;
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        seg1FamilySlots_.push_back(slotEntry);
        addedFamily = true;
    }
    if (addedFamily) InvalidateFamilySlotIndex();

    entry.center = rebuiltCenter;
    entry.logicalSegmentCount = 1;
    entry.lodState.SetReady(true);
    entry.lodState.SetHasPerFaceRankOffsets(false);
    entry.lodState.currentBaseRank = -1;
    entry.lodState.currentLodIndex = 0xFF;
    entry.lodState.desiredBaseRank = -1;
    entry.lodState.desiredLodIndex = desiredLodIndex;
    entry.lodState.faceFamilyIds = std::move(rebuiltFamilyIds);
    entry.lodState.faceRankOffsets.assign(entry.lodState.faceFamilyIds.size(), 0u);
    entry.lodState.currentFaceSlots.assign(entry.lodState.faceFamilyIds.size(), -1);
    EnsureVectorCapacityFloor(entry.lodState.faceFamilyIds, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(entry.lodState.faceRankOffsets, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(entry.lodState.currentFaceSlots, slotFaceCapacityFloor_);
    bool rebuilt = false;
    uint8_t appliedLodIndex = desiredLodIndex;
    const bool requireExactLod =
        kEnableTrackRuntimeStabilization && kEnableTrackLodBandsInStabilization;
    const int lowestLodAttempt = requireExactLod
        ? static_cast<int>(desiredLodIndex)
        : static_cast<int>(kTrackLod32Index);
    for (int lodIndex = static_cast<int>(desiredLodIndex); lodIndex >= lowestLodAttempt; --lodIndex)
    {
        entry.lodState.currentFaceSlots.assign(entry.lodState.faceFamilyIds.size(), -1);
        if (!RebuildSegmentFaceSlotsForLod(entry,
                                           static_cast<uint8_t>(lodIndex),
                                           seg1FamilySlots_,
                                           requireExactLod))
        {
            continue;
        }
        if (HasMissingRequiredFaceTextureSlots(entry.lodState.currentFaceSlots, &entry.lodState.faceFamilyIds))
        {
            continue;
        }
        rebuilt = true;
        appliedLodIndex = static_cast<uint8_t>(lodIndex);
        break;
    }
    if (!rebuilt)
    {
        // Restore to clean state (currentFaceSlots was already -1 before rebuild attempts)
        entry.lodState.currentFaceSlots.assign(entry.lodState.faceFamilyIds.size(), -1);
        return false;
    }
    (void)entry.renderer->ApplyFaceTextureSlotsGlobal(entry.lodState.currentFaceSlots);
    entry.lodState.currentLodIndex = appliedLodIndex;
    entry.lodState.currentBaseRank = -1;
    entry.lodState.desiredLodIndex = desiredLodIndex;
    entry.lodState.desiredBaseRank = -1;
    InvalidateEntryWorkingSetCache(entry);
    SetFamilyWorkingSetDirty(true);
    return IsRendererStateIntegral(*entry.renderer);
}

// Map a near to far rank into the current fixed lod bands for track rendering.
uint8_t TrackSystem::GetTextureUploadBudgetPerFrame() const
{
    if (kEnableTrackRuntimeStabilization && kEnableTrackLodBandsInStabilization)
    {
        // Keep stabilization responsive, but do not let a single 64x64 repair
        // monopolize the frame when a boundary segment carries many families.
        return 8u;
    }
    return kTextureUploadsBudgetPerFrame;
}

uint8_t TrackSystem::ResolveSegmentLodIndexByRank(size_t rank) const
{
    if (kEnableTrackLeakIsolationFixed64Pipeline)
    {
        if (kEnableLeakIsolationMixedLodProfile)
        {
            const size_t nearCount = std::min<size_t>(
                kLeakIsolationNearLodCount,
                kTrackLeakIsolationWindowSegments);
            return (rank < nearCount) ? kLeakIsolationNearLodIndex : kLeakIsolationFarLodIndex;
        }
        (void)rank;
        return 3u; // legacy fixed64 profile
    }
    if (kEnableTrackRuntimeStabilization && !kEnableTrackLodBandsInStabilization)
    {
        (void)rank;
        return kTrackLod32Index;
    }
    const size_t lod64End = static_cast<size_t>(kLodBand64Count);
    const size_t lod32End = lod64End + static_cast<size_t>(kLodBand32Count);
    if (rank < lod64End) return kTrackLod64Index;
    if (rank < lod32End) return kTrackLod32Index;
    return kTrackLod32Index;
}

bool TrackSystem::TryGetWindowLogicalRank(int32_t segmentId, size_t& outRank) const
{
    outRank = 0;
    if (segmentId <= 0 || totalSegmentCount_ == 0) return false;
    const_cast<TrackSystem*>(this)->RebuildActiveWindowLookupTables();
    if (static_cast<size_t>(segmentId) < windowLogicalRankBySegmentId_.size())
    {
        const int8_t rank = windowLogicalRankBySegmentId_[static_cast<size_t>(segmentId)];
        if (rank >= 0)
        {
            outRank = static_cast<size_t>(rank);
            return true;
        }
    }
    for (size_t i = 0; i < activeWindowLookupSegmentIds_.size(); ++i)
    {
        if (activeWindowLookupSegmentIds_[i] != segmentId) continue;
        if (i >= activeWindowLogicalRankBySegmentId_.size()) return false;
        const int8_t rank = activeWindowLogicalRankBySegmentId_[i];
        if (rank < 0) return false;
        outRank = static_cast<size_t>(rank);
        return true;
    }
    return false;
}

void TrackSystem::InvalidateActiveWindowLookupTables()
{
    SetActiveWindowLookupDirty(true);
}

size_t TrackSystem::LogicalToPhysicalWindowIndex(size_t logicalIndex, size_t windowCount) const
{
    if (windowCount == 0) return 0;
    const size_t safeHead = (activeWindowHead_ < windowCount)
        ? static_cast<size_t>(activeWindowHead_)
        : 0u;
    return (safeHead + (logicalIndex % windowCount)) % windowCount;
}

int32_t TrackSystem::ResolveWindowOutgoingSegmentId(int8_t direction, size_t windowCount) const
{
    if (windowCount == 0 || totalSegmentCount_ == 0) return -1;
    const int32_t dir = (direction < 0) ? -1 : 1;
    return (dir > 0)
        ? WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_)
        : WrapSegmentIdToRange(activeWindowStartId_ - (static_cast<int32_t>(windowCount) - 1),
                               totalSegmentCount_);
}

int32_t TrackSystem::ResolveWindowIncomingSegmentId(int8_t direction, size_t windowCount) const
{
    if (windowCount == 0 || totalSegmentCount_ == 0) return -1;
    const int32_t dir = (direction < 0) ? -1 : 1;
    return (dir > 0)
        ? WrapSegmentIdToRange(activeWindowStartId_ + static_cast<int32_t>(windowCount),
                               totalSegmentCount_)
        : WrapSegmentIdToRange(activeWindowStartId_ - static_cast<int32_t>(windowCount),
                               totalSegmentCount_);
}

bool TrackSystem::TryResolveWindowEntryIndexBySegmentId(int32_t segmentId, size_t& outIndex)
{
    outIndex = 0;
    if (segmentId <= 0 || segmentRenderers_.empty() || totalSegmentCount_ == 0) return false;
    RebuildActiveWindowLookupTables();
    if (static_cast<size_t>(segmentId) < windowEntryIndexBySegmentId_.size())
    {
        const int8_t idx = windowEntryIndexBySegmentId_[static_cast<size_t>(segmentId)];
        if (idx >= 0 && static_cast<size_t>(idx) < segmentRenderers_.size())
        {
            outIndex = static_cast<size_t>(idx);
            return true;
        }
    }
    for (size_t i = 0; i < activeWindowLookupSegmentIds_.size(); ++i)
    {
        if (activeWindowLookupSegmentIds_[i] != segmentId) continue;
        if (i >= activeWindowEntryIndexBySegmentId_.size()) return false;
        const int8_t idx = activeWindowEntryIndexBySegmentId_[i];
        if (idx < 0 || static_cast<size_t>(idx) >= segmentRenderers_.size()) return false;
        outIndex = static_cast<size_t>(idx);
        return true;
    }
    return false;
}

bool TrackSystem::TryResolveWindowEntryIndexBySegmentId(int32_t segmentId, size_t& outIndex) const
{
    return const_cast<TrackSystem*>(this)->TryResolveWindowEntryIndexBySegmentId(segmentId, outIndex);
}

bool TrackSystem::ResolveWindowDropIndexByDirection(int8_t direction,
                                                    size_t windowCount,
                                                    size_t& outDropIdx)
{
    outDropIdx = 0;
    if (windowCount == 0) return false;
    const int32_t outgoingId = ResolveWindowOutgoingSegmentId(direction, windowCount);
    size_t resolvedIdx = 0;
    if (TryResolveWindowEntryIndexBySegmentId(outgoingId, resolvedIdx))
    {
        outDropIdx = resolvedIdx;
        return true;
    }

    const int32_t dir = (direction < 0) ? -1 : 1;
    const size_t logicalDrop = (dir > 0) ? 0u : (windowCount - 1u);
    outDropIdx = LogicalToPhysicalWindowIndex(logicalDrop, windowCount);
    return true;
}

bool TrackSystem::ResolveWindowHeadByStartId(size_t fallbackIndex)
{
    if (segmentRenderers_.empty())
    {
        activeWindowHead_ = 0;
        return false;
    }

    size_t resolvedIdx = 0;
    if (TryResolveWindowEntryIndexBySegmentId(activeWindowStartId_, resolvedIdx))
    {
        activeWindowHead_ = static_cast<uint16_t>(resolvedIdx);
        return true;
    }

    activeWindowHead_ = static_cast<uint16_t>(fallbackIndex % segmentRenderers_.size());
    return false;
}

bool TrackSystem::AdvanceWindowHeadByDirection(int8_t direction, size_t windowCount)
{
    if (windowCount == 0)
    {
        activeWindowHead_ = 0;
        return false;
    }

    const int32_t dir = (direction < 0) ? -1 : 1;
    size_t head = (activeWindowHead_ < windowCount)
        ? static_cast<size_t>(activeWindowHead_)
        : 0u;
    if (dir > 0)
    {
        head = (head + 1u) % windowCount;
    }
    else
    {
        head = (head + windowCount - 1u) % windowCount;
    }
    activeWindowHead_ = static_cast<uint16_t>(head);
    return true;
}

void TrackSystem::RebuildActiveWindowLookupTables()
{
    if (!ActiveWindowLookupDirty()) return;

    windowEntryIndexBySegmentId_.fill(-1);
    windowLogicalRankBySegmentId_.fill(-1);
    activeWindowLookupSegmentIds_.clear();
    activeWindowEntryIndexBySegmentId_.clear();
    activeWindowLogicalRankBySegmentId_.clear();
    if (segmentRenderers_.empty() || totalSegmentCount_ == 0)
    {
        SetActiveWindowLookupDirty(false);
        return;
    }

    const size_t windowCount = segmentRenderers_.size();
    if (activeWindowLookupSegmentIds_.capacity() < windowCount)
    {
        activeWindowLookupSegmentIds_.reserve(windowCount);
    }
    if (activeWindowEntryIndexBySegmentId_.capacity() < windowCount)
    {
        activeWindowEntryIndexBySegmentId_.reserve(windowCount);
    }
    if (activeWindowLogicalRankBySegmentId_.capacity() < windowCount)
    {
        activeWindowLogicalRankBySegmentId_.reserve(windowCount);
    }

    for (size_t logicalRank = 0; logicalRank < windowCount; ++logicalRank)
    {
        const size_t physicalIdx = LogicalToPhysicalWindowIndex(logicalRank, windowCount);
        if (physicalIdx >= windowCount) continue;
        const int32_t segmentId =
            WrapSegmentIdToRange(segmentRenderers_[physicalIdx].id, totalSegmentCount_);
        if (segmentId <= 0) continue;

        const size_t segmentIdU = static_cast<size_t>(segmentId);
        bool alreadyAdded = false;
        if (segmentIdU < windowEntryIndexBySegmentId_.size())
        {
            alreadyAdded = (windowEntryIndexBySegmentId_[segmentIdU] >= 0);
        }
        else
        {
            for (size_t i = 0; i < activeWindowLookupSegmentIds_.size(); ++i)
            {
                if (activeWindowLookupSegmentIds_[i] != segmentId) continue;
                alreadyAdded = true;
                break;
            }
        }
        if (alreadyAdded) continue;

        if (segmentIdU < windowEntryIndexBySegmentId_.size())
        {
            windowEntryIndexBySegmentId_[segmentIdU] = static_cast<int8_t>(physicalIdx);
            windowLogicalRankBySegmentId_[segmentIdU] = static_cast<int8_t>(logicalRank);
        }
        activeWindowLookupSegmentIds_.push_back(static_cast<int16_t>(segmentId));
        activeWindowEntryIndexBySegmentId_.push_back(static_cast<int8_t>(physicalIdx));
        activeWindowLogicalRankBySegmentId_.push_back(static_cast<int8_t>(logicalRank));
    }

    SetActiveWindowLookupDirty(false);
}

TrackSystem::SegmentRenderEntry* TrackSystem::FindWindowEntryByIdFast(int32_t segmentId)
{
    size_t idx = 0;
    if (!TryResolveWindowEntryIndexBySegmentId(segmentId, idx)) return nullptr;
    return &segmentRenderers_[idx];
}

const TrackSystem::SegmentRenderEntry* TrackSystem::FindWindowEntryByIdFast(int32_t segmentId) const
{
    return const_cast<TrackSystem*>(this)->FindWindowEntryByIdFast(segmentId);
}

bool TrackSystem::EnsureWallSegmentCache(SegmentRenderEntry& entry) const
{
    const Vector3D* verts = nullptr;
    const SRL::Types::Polygon* faces = nullptr;
    size_t vertCount = 0u;
    size_t faceCount = 0u;

    if (!entry.renderer ||
        !entry.renderer->GetComponentGeometry(verts, vertCount, faces, faceCount) ||
        !verts || !faces || vertCount == 0u || faceCount == 0u)
    {
        entry.wallSegments2D.clear();
        entry.wallSegmentsCacheVertCount = static_cast<uint16_t>(vertCount);
        entry.wallSegmentsCacheFaceCount = static_cast<uint16_t>(faceCount);
        entry.wallSegmentsCacheFamilyCount = 0u;
        entry.wallSegmentsCacheSegmentId = static_cast<int16_t>(entry.id);
        entry.wallSegmentsCacheLodIndex = entry.lodState.currentLodIndex;
        entry.SetWallSegmentsCacheReady(true);
        return false;
    }

    const size_t familyCount = entry.lodState.faceFamilyIds.size();
    const uint16_t vertCountU32 = static_cast<uint16_t>(vertCount);
    const uint16_t faceCountU32 = static_cast<uint16_t>(faceCount);
    const uint16_t familyCountU32 = static_cast<uint16_t>(familyCount);

    if (entry.WallSegmentsCacheReady() &&
        entry.wallSegmentsCacheSegmentId == static_cast<int16_t>(entry.id) &&
        entry.wallSegmentsCacheLodIndex == entry.lodState.currentLodIndex &&
        entry.wallSegmentsCacheVertCount == vertCountU32 &&
        entry.wallSegmentsCacheFaceCount == faceCountU32 &&
        entry.wallSegmentsCacheFamilyCount == familyCountU32)
    {
        return !entry.wallSegments2D.empty();
    }

    entry.wallSegments2D.clear();
    entry.wallSegmentsCacheVertCount = vertCountU32;
    entry.wallSegmentsCacheFaceCount = faceCountU32;
    entry.wallSegmentsCacheFamilyCount = familyCountU32;
    entry.wallSegmentsCacheSegmentId = static_cast<int16_t>(entry.id);
    entry.wallSegmentsCacheLodIndex = entry.lodState.currentLodIndex;
    entry.SetWallSegmentsCacheReady(true);

    const size_t scanFaceCount =
        (familyCount > 0u) ? std::min(faceCount, familyCount) : faceCount;
    if (scanFaceCount == 0u)
    {
        return false;
    }

    entry.wallSegments2D.reserve(scanFaceCount);

    auto abs64 = [](int64_t v) -> int64_t { return (v < 0) ? -v : v; };
    auto normalizePlanar = [&](int64_t nxRaw, int64_t nzRaw, int32_t& outNxRaw, int32_t& outNzRaw) -> bool
    {
        const int64_t maxAxis = std::max(abs64(nxRaw), abs64(nzRaw));
        if (maxAxis <= 0) return false;
        outNxRaw = static_cast<int32_t>((nxRaw << 16) / maxAxis);
        outNzRaw = static_cast<int32_t>((nzRaw << 16) / maxAxis);
        return true;
    };

    auto endpointLess = [](int32_t ax, int32_t az, int32_t bx, int32_t bz) -> bool
    {
        if (ax != bx) return ax < bx;
        return az < bz;
    };

    auto isDuplicateSegment = [&](int32_t ax, int32_t az, int32_t bx, int32_t bz) -> bool
    {
        int32_t cax = ax;
        int32_t caz = az;
        int32_t cbx = bx;
        int32_t cbz = bz;
        if (endpointLess(cbx, cbz, cax, caz))
        {
            std::swap(cax, cbx);
            std::swap(caz, cbz);
        }

        for (size_t i = 0; i < entry.wallSegments2D.size(); ++i)
        {
            const auto& existing = entry.wallSegments2D[i];
            int32_t eax = existing.axRaw;
            int32_t eaz = existing.azRaw;
            int32_t ebx = existing.bxRaw;
            int32_t ebz = existing.bzRaw;
            if (endpointLess(ebx, ebz, eax, eaz))
            {
                std::swap(eax, ebx);
                std::swap(eaz, ebz);
            }
            if (eax == cax && eaz == caz && ebx == cbx && ebz == cbz)
            {
                return true;
            }
        }
        return false;
    };

    for (size_t fi = 0; fi < scanFaceCount; ++fi)
    {
        const bool hasFamilyId = (fi < familyCount);
        const uint16_t faceFamilyId = hasFamilyId ? entry.lodState.faceFamilyIds[fi] : 0u;
        // Include all faces with mostly-vertical normals regardless of surface type.
        // The planar-normal filter below is what distinguishes walls from floor.
        (void)faceFamilyId;
        (void)hasFamilyId;

        const SRL::Types::Polygon& face = faces[fi];

        const uint16_t i0 = face.Vertices[0];
        const uint16_t i1 = face.Vertices[1];
        const uint16_t i2 = face.Vertices[2];
        const uint16_t i3 = face.Vertices[3];
        if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount || i3 >= vertCount) continue;

        const uint16_t faceIndices[4] = { i0, i1, i2, i3 };
        int32_t px[4] = { 0, 0, 0, 0 };
        int32_t pz[4] = { 0, 0, 0, 0 };
        int32_t py[4] = { 0, 0, 0, 0 };
        for (size_t k = 0; k < 4; ++k)
        {
            const Vector3D& v = verts[faceIndices[k]];
            px[k] = v.X.RawValue();
            pz[k] = v.Z.RawValue();
            py[k] = v.Y.RawValue();
        }

        // Use stored face normal; fall back to vertex cross-product for GEO faces (normal == 0).
        int64_t nxRaw = static_cast<int64_t>(face.Normal.X.RawValue());
        int64_t nyRaw = static_cast<int64_t>(face.Normal.Y.RawValue());
        int64_t nzRaw = static_cast<int64_t>(face.Normal.Z.RawValue());
        if (nxRaw == 0 && nyRaw == 0 && nzRaw == 0)
        {
            const int64_t abx = static_cast<int64_t>(px[1]) - static_cast<int64_t>(px[0]);
            const int64_t aby = static_cast<int64_t>(py[1]) - static_cast<int64_t>(py[0]);
            const int64_t abz = static_cast<int64_t>(pz[1]) - static_cast<int64_t>(pz[0]);
            const int64_t acx = static_cast<int64_t>(px[2]) - static_cast<int64_t>(px[0]);
            const int64_t acy = static_cast<int64_t>(py[2]) - static_cast<int64_t>(py[0]);
            const int64_t acz = static_cast<int64_t>(pz[2]) - static_cast<int64_t>(pz[0]);
            nxRaw = ((aby * acz) - (abz * acy)) >> 16;
            nyRaw = ((abz * acx) - (abx * acz)) >> 16;
            nzRaw = ((abx * acy) - (aby * acx)) >> 16;
        }
        const int64_t planarNormalAbs = std::max(abs64(nxRaw), abs64(nzRaw));
        if (planarNormalAbs <= 0) continue;
        if ((planarNormalAbs * 2) < abs64(nyRaw)) continue;

        int32_t minXRaw = px[0];
        int32_t maxXRaw = px[0];
        int32_t minZRaw = pz[0];
        int32_t maxZRaw = pz[0];
        int32_t minYRaw = py[0];
        int32_t maxYRaw = py[0];
        for (size_t k = 1; k < 4; ++k)
        {
            if (px[k] < minXRaw) minXRaw = px[k];
            if (px[k] > maxXRaw) maxXRaw = px[k];
            if (pz[k] < minZRaw) minZRaw = pz[k];
            if (pz[k] > maxZRaw) maxZRaw = pz[k];
            if (py[k] < minYRaw) minYRaw = py[k];
            if (py[k] > maxYRaw) maxYRaw = py[k];
        }

        int bestA = 0;
        int bestB = 1;
        int64_t bestLenSq = 0;
        for (int a = 0; a < 4; ++a)
        {
            for (int b = a + 1; b < 4; ++b)
            {
                const int64_t dx = static_cast<int64_t>(px[b]) - static_cast<int64_t>(px[a]);
                const int64_t dz = static_cast<int64_t>(pz[b]) - static_cast<int64_t>(pz[a]);
                const int64_t lenSq = (dx * dx) + (dz * dz);
                if (lenSq > bestLenSq)
                {
                    bestLenSq = lenSq;
                    bestA = a;
                    bestB = b;
                }
            }
        }
        if (bestLenSq <= 0) continue;

        const int32_t axRaw = px[bestA];
        const int32_t azRaw = pz[bestA];
        const int32_t bxRaw = px[bestB];
        const int32_t bzRaw = pz[bestB];
        if (isDuplicateSegment(axRaw, azRaw, bxRaw, bzRaw))
        {
            continue;
        }

        int32_t normalizedNxRaw = 0;
        int32_t normalizedNzRaw = 0;
        if (!normalizePlanar(nxRaw, nzRaw, normalizedNxRaw, normalizedNzRaw))
        {
            continue;
        }

        SegmentRenderEntry::WallSegment2D segment2D{};
        segment2D.axRaw = axRaw;
        segment2D.azRaw = azRaw;
        segment2D.bxRaw = bxRaw;
        segment2D.bzRaw = bzRaw;
        segment2D.minXRaw = minXRaw;
        segment2D.maxXRaw = maxXRaw;
        segment2D.minZRaw = minZRaw;
        segment2D.maxZRaw = maxZRaw;
        segment2D.minYRaw = minYRaw;
        segment2D.maxYRaw = maxYRaw;
        segment2D.nxRaw = normalizedNxRaw;
        segment2D.nzRaw = normalizedNzRaw;
        entry.wallSegments2D.push_back(segment2D);
    }

    return !entry.wallSegments2D.empty();
}

void TrackSystem::UpdateDesiredStabilizedWindowLodTargets()
{
    if (!kEnableTrackRuntimeStabilization || segmentRenderers_.empty() || totalSegmentCount_ == 0) return;
    pendingLodCursor_ = 0;
    ResetPendingStabilizedLodRanks();

    for (auto& entry : segmentRenderers_)
    {
        entry.lodState.desiredLodIndex = 0xFF;
        entry.lodState.desiredBaseRank = -1;
    }

    const size_t windowCount = segmentRenderers_.size();
    for (size_t logicalRank = 0; logicalRank < windowCount; ++logicalRank)
    {
        const size_t physicalIdx = LogicalToPhysicalWindowIndex(logicalRank, windowCount);
        if (physicalIdx >= windowCount) continue;
        SegmentRenderEntry& entry = segmentRenderers_[physicalIdx];
        entry.lodState.desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        entry.lodState.desiredBaseRank = entry.lodState.HasPerFaceRankOffsets()
            ? static_cast<int16_t>(logicalRank)
            : -1;
        if (kEnableDeterministicStabilizedSlide) continue;
        const bool needsUpdate = entry.lodState.HasPerFaceRankOffsets()
            ? (entry.lodState.currentLodIndex != entry.lodState.desiredLodIndex ||
               entry.lodState.currentBaseRank != entry.lodState.desiredBaseRank ||
               HasMissingRequiredFaceTextureSlots(entry.lodState.currentFaceSlots,
                                                  &entry.lodState.faceFamilyIds))
            : (entry.lodState.currentLodIndex != entry.lodState.desiredLodIndex ||
               HasMissingRequiredFaceTextureSlots(entry.lodState.currentFaceSlots,
                                                  &entry.lodState.faceFamilyIds));
        if (needsUpdate) QueuePendingStabilizedLodRank(logicalRank);
    }
}

template <typename FaceSlotsVecT>
bool TrackSystem::ResolvePreparedFaceSlotsForLod(const SegmentRenderEntry& entry,
                                                 uint8_t lodIndex,
                                                 FamilySlotVector& familySlots,
                                                 FaceSlotsVecT& outFaceSlots,
                                                 bool bypassUploadBudget)
{
    if (!entry.renderer) return false;
    if (lodIndex > 3) return false;
    return ResolveFaceSlotsForFixedLodFromFamilies(entry.lodState.faceFamilyIds,
                                                   lodIndex,
                                                   familySlots,
                                                   outFaceSlots,
                                                   bypassUploadBudget);
}

template <typename FaceSlotsVecT>
bool TrackSystem::ResolvePreparedFaceSlotsForBaseRank(const SegmentRenderEntry& entry,
                                                      size_t baseRank,
                                                      FamilySlotVector& familySlots,
                                                      FaceSlotsVecT& outFaceSlots,
                                                      bool bypassUploadBudget)
{
    if (!entry.renderer) return false;
    return ResolveFaceSlotsForBaseRankFromFamilies(entry.lodState.faceFamilyIds,
                                                   entry.lodState.faceRankOffsets,
                                                   baseRank,
                                                   familySlots,
                                                   outFaceSlots,
                                                   bypassUploadBudget);
}

bool TrackSystem::HasPendingStabilizedWindowLodChanges() const
{
    return PendingLodWorkExists();
}

void TrackSystem::ResetPendingStabilizedLodRanks()
{
    pendingLodRankFlags_.fill(0u);
    pendingLodRetryCooldowns_.fill(0u);
    SetPendingLodWorkExists(false);
}

void TrackSystem::QueuePendingStabilizedLodRank(size_t logicalRank)
{
    if (logicalRank >= pendingLodRankFlags_.size()) return;
    pendingLodRankFlags_[logicalRank] = 1u;
    SetPendingLodWorkExists(true);
}

void TrackSystem::SeedPendingStabilizedLodRanksForWindow()
{
    ResetPendingStabilizedLodRanks();
    if (!kEnableTrackRuntimeStabilization || !kEnableTrackLodBandsInStabilization) return;
    if (segmentRenderers_.empty() || totalSegmentCount_ == 0) return;

    for (size_t logicalRank = 0; logicalRank < segmentRenderers_.size(); ++logicalRank)
    {
        const size_t physicalIdx = LogicalToPhysicalWindowIndex(logicalRank, segmentRenderers_.size());
        if (physicalIdx >= segmentRenderers_.size()) continue;
        SegmentRenderEntry& entry = segmentRenderers_[physicalIdx];
        if (!entry.renderer || !entry.lodState.Ready()) continue;

        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        const int16_t desiredBaseRank = entry.lodState.HasPerFaceRankOffsets()
            ? static_cast<int16_t>(logicalRank)
            : -1;
        const bool needsUpdate = entry.lodState.HasPerFaceRankOffsets()
            ? (entry.lodState.currentLodIndex != desiredLodIndex ||
               entry.lodState.currentBaseRank != desiredBaseRank ||
               HasMissingRequiredFaceTextureSlots(entry.lodState.currentFaceSlots,
                                                  &entry.lodState.faceFamilyIds))
            : (entry.lodState.currentLodIndex != desiredLodIndex ||
               HasMissingRequiredFaceTextureSlots(entry.lodState.currentFaceSlots,
                                                  &entry.lodState.faceFamilyIds));
        if (needsUpdate) QueuePendingStabilizedLodRank(logicalRank);
    }
}

bool TrackSystem::ApplyStabilizedLodForLogicalRank(size_t logicalRank)
{
    if (!kEnableTrackRuntimeStabilization || !kEnableTrackLodBandsInStabilization) return true;
    if (segmentRenderers_.empty() || totalSegmentCount_ == 0) return false;

    const size_t windowCount = segmentRenderers_.size();
    if (logicalRank >= windowCount) return true;

    FamilySlotVector& familySlots = seg1FamilySlots_;
    const size_t physicalIdx = LogicalToPhysicalWindowIndex(logicalRank, windowCount);
    if (physicalIdx >= windowCount) return false;
    SegmentRenderEntry* entry = &segmentRenderers_[physicalIdx];
    if (!entry->renderer || !entry->lodState.Ready()) return false;

    const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
    const int16_t desiredBaseRank = static_cast<int16_t>(logicalRank);
    entry->lodState.desiredLodIndex = desiredLodIndex;
    entry->lodState.desiredBaseRank = entry->lodState.HasPerFaceRankOffsets() ? static_cast<int8_t>(desiredBaseRank) : static_cast<int8_t>(-1);

    if (!entry->lodState.HasPerFaceRankOffsets())
    {
        if (entry->lodState.currentLodIndex == desiredLodIndex &&
            !HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                &entry->lodState.faceFamilyIds))
        {
            return true;
        }
        if (ReadyFlag() && textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame()) return false;

        CopyFaceSlotsToScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
        const uint8_t previousLodIndex = entry->lodState.currentLodIndex;
        const int16_t previousBaseRank = entry->lodState.currentBaseRank;
        if (!RebuildSegmentFaceSlotsForLod(*entry, desiredLodIndex, familySlots) ||
            HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                               &entry->lodState.faceFamilyIds))
        {
            RestoreFaceSlotsFromScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
            entry->lodState.currentLodIndex = previousLodIndex;
            entry->lodState.currentBaseRank = previousBaseRank;
            return false;
        }

        (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
        ++runtimeFaceRemapsThisFrame_;
        ++runtimeLodSegmentUpdatesThisFrame_;
        entry->lodState.currentLodIndex = desiredLodIndex;
        entry->lodState.currentBaseRank = -1;
        entry->lodState.desiredLodIndex = desiredLodIndex;
        entry->lodState.desiredBaseRank = -1;
        InvalidateEntryWorkingSetCache(*entry);
        SetFamilyWorkingSetDirty(true);
        return true;
    }

    if (entry->lodState.currentBaseRank == desiredBaseRank &&
        entry->lodState.currentLodIndex == desiredLodIndex &&
        !HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                            &entry->lodState.faceFamilyIds))
    {
        return true;
    }
    if (ReadyFlag() && textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame()) return false;

    CopyFaceSlotsToScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
    const uint8_t previousLodIndex = entry->lodState.currentLodIndex;
    const int16_t previousBaseRank = entry->lodState.currentBaseRank;
    if (!RebuildSegmentFaceSlotsForBaseRank(*entry, logicalRank, familySlots) ||
        HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                           &entry->lodState.faceFamilyIds))
    {
        RestoreFaceSlotsFromScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
        entry->lodState.currentLodIndex = previousLodIndex;
        entry->lodState.currentBaseRank = previousBaseRank;
        return false;
    }

    (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
    ++runtimeFaceRemapsThisFrame_;
    ++runtimeLodSegmentUpdatesThisFrame_;
    entry->lodState.currentBaseRank = static_cast<int8_t>(desiredBaseRank);
    entry->lodState.currentLodIndex = desiredLodIndex;
    entry->lodState.desiredBaseRank = static_cast<int8_t>(desiredBaseRank);
    entry->lodState.desiredLodIndex = desiredLodIndex;
    InvalidateEntryWorkingSetCache(*entry);
    SetFamilyWorkingSetDirty(true);
    return true;
}

void TrackSystem::UpdateStabilizedWindowLodBoundaries()
{
    if (!kEnableTrackRuntimeStabilization || !kEnableTrackLodBandsInStabilization) return;
    bool freeValid = false;
    const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    const bool allowMandatoryPromotions = !freeValid || freeBytes > kLodExactRecoveryFreeBytes;
    if (lodDegradeCooldown_ != 0 && !allowMandatoryPromotions) return;
    if (!allowMandatoryPromotions) return;
    if (segmentRenderers_.empty()) return;

    static constexpr std::array<size_t, 4> kForwardBoundaryRanks{{3u, 8u, 13u, 19u}};
    static constexpr std::array<size_t, 4> kBackwardBoundaryRanks{{0u, 4u, 9u, 14u}};
    const auto& boundaryRanks = (windowDirection_ >= 0) ? kForwardBoundaryRanks : kBackwardBoundaryRanks;
    // Promotions must close inside the slide event. If the first pass only
    // uploads some family slots, give the same four boundaries a couple of
    // extra passes before we leave the slide. This avoids per-frame churn.
    constexpr uint8_t kBoundaryPassCount = 1u;
    for (uint8_t pass = 0; pass < kBoundaryPassCount; ++pass)
    {
        bool anyApplied = false;
        for (size_t i = 0; i < boundaryRanks.size(); ++i)
        {
            if (ApplyStabilizedLodForLogicalRank(boundaryRanks[i]))
            {
                anyApplied = true;
            }
        }
        if (!anyApplied) break;
        if (ReadyFlag() && textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame()) break;
    }
}

void TrackSystem::ProcessPendingStabilizedWindowLodChanges(uint8_t maxUpdates)
{
    if (!kEnableTrackRuntimeStabilization || !kEnableTrackLodBandsInStabilization) return;
    bool freeValid = false;
    const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    const bool allowMandatoryPromotions = !freeValid || freeBytes > kLodExactRecoveryFreeBytes;
    if (lodDegradeCooldown_ != 0 && !allowMandatoryPromotions) return;
    if (!allowMandatoryPromotions) return;
    if (segmentRenderers_.empty() || totalSegmentCount_ == 0) return;
    if (maxUpdates == 0) return;
    if (!PendingLodWorkExists()) return;

    const size_t windowCount = segmentRenderers_.size();
    uint8_t attemptedUpdates = 0u;
    bool anyPending = false;
    auto pickNextPendingRank = [&]() -> size_t
    {
        size_t bestRank = windowCount;
        uint32_t bestScore = 0xFFFFFFFFu;
        for (size_t offset = 0; offset < windowCount; ++offset)
        {
            const size_t logicalRank =
                (static_cast<size_t>(pendingLodCursor_) + offset) % windowCount;
            if (logicalRank >= pendingLodRankFlags_.size()) continue;
            if (pendingLodRankFlags_[logicalRank] == 0u) continue;
            if (logicalRank < pendingLodRetryCooldowns_.size() &&
                pendingLodRetryCooldowns_[logicalRank] != 0u) continue;

            const uint32_t score = GetStrictPendingLodPriority(logicalRank);
            if (score < bestScore)
            {
                bestScore = score;
                bestRank = logicalRank;
            }
        }
        return bestRank;
    };

    while (attemptedUpdates < maxUpdates)
    {
        const size_t logicalRank = pickNextPendingRank();
        if (logicalRank >= windowCount) break;
        const int32_t segmentId = WrapSegmentIdToRange(
            activeWindowStartId_ + (windowDirection_ >= 0
                ? static_cast<int32_t>(logicalRank)
                : -static_cast<int32_t>(logicalRank)),
            totalSegmentCount_);
        if (segmentId <= 0)
        {
            pendingLodRankFlags_[logicalRank] = 0u;
            continue;
        }

        SegmentRenderEntry* entry = FindWindowEntryByIdFast(segmentId);
        if (!entry || !entry->renderer || !entry->lodState.Ready())
        {
            pendingLodRankFlags_[logicalRank] = 0u;
            continue;
        }

        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        const int16_t desiredBaseRank = static_cast<int16_t>(logicalRank);
        const bool needsUpdate = entry->lodState.HasPerFaceRankOffsets()
            ? (entry->lodState.currentLodIndex != desiredLodIndex ||
               entry->lodState.currentBaseRank != desiredBaseRank ||
               HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                  &entry->lodState.faceFamilyIds))
            : (entry->lodState.currentLodIndex != desiredLodIndex ||
               HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                  &entry->lodState.faceFamilyIds));
        if (!needsUpdate)
        {
            pendingLodRankFlags_[logicalRank] = 0u;
            continue;
        }

        ++attemptedUpdates;
        const bool applied = ApplyStabilizedLodForLogicalRank(logicalRank);
        SegmentRenderEntry* updatedEntry = FindWindowEntryByIdFast(segmentId);
        if (applied && updatedEntry && updatedEntry->renderer && updatedEntry->lodState.Ready())
        {
            const bool stillPending = updatedEntry->lodState.HasPerFaceRankOffsets()
                ? (updatedEntry->lodState.currentLodIndex != desiredLodIndex ||
                   updatedEntry->lodState.currentBaseRank != desiredBaseRank ||
                   HasMissingRequiredFaceTextureSlots(updatedEntry->lodState.currentFaceSlots,
                                                      &updatedEntry->lodState.faceFamilyIds))
                : (updatedEntry->lodState.currentLodIndex != desiredLodIndex ||
                   HasMissingRequiredFaceTextureSlots(updatedEntry->lodState.currentFaceSlots,
                                                      &updatedEntry->lodState.faceFamilyIds));
            pendingLodRankFlags_[logicalRank] = stillPending ? 1u : 0u;
            if (logicalRank < pendingLodRetryCooldowns_.size())
            {
                pendingLodRetryCooldowns_[logicalRank] = 0u;
            }
        }
        else
        {
            pendingLodRankFlags_[logicalRank] = 1u;
            if (logicalRank < pendingLodRetryCooldowns_.size())
            {
                bool freeValid = false;
                const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
                pendingLodRetryCooldowns_[logicalRank] =
                    (freeValid && freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u))) ? 4u : 2u;
            }
        }
        pendingLodCursor_ = static_cast<uint8_t>((logicalRank + 1u) % windowCount);

        if (ReadyFlag() && textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame()) break;
    }

    for (size_t i = 0; i < std::min(windowCount, pendingLodRankFlags_.size()); ++i)
    {
        if (pendingLodRankFlags_[i] == 0u) continue;
        anyPending = true;
        break;
    }
    SetPendingLodWorkExists(anyPending);
}

void TrackSystem::UpdateStabilizedWindowLodBands()
{
    if (!kEnableTrackRuntimeStabilization || !kEnableTrackLodBandsInStabilization) return;
    if (segmentRenderers_.empty() || totalSegmentCount_ == 0) return;

    FamilySlotVector& familySlots = seg1FamilySlots_;
    const size_t windowCount = segmentRenderers_.size();
    const int32_t dir = (windowDirection_ < 0) ? -1 : 1;
    const int32_t startId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
    if (startId <= 0) return;

    for (size_t logicalRank = 0; logicalRank < windowCount; ++logicalRank)
    {
        const int32_t segmentId = WrapSegmentIdToRange(
            startId + (dir > 0 ? static_cast<int32_t>(logicalRank)
                               : -static_cast<int32_t>(logicalRank)),
            totalSegmentCount_);
        if (segmentId <= 0) continue;

        SegmentRenderEntry* entry = FindWindowEntryByIdFast(segmentId);
        if (!entry || !entry->renderer || !entry->lodState.Ready()) continue;

        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        const int16_t desiredBaseRank = static_cast<int16_t>(logicalRank);

        if (!entry->lodState.HasPerFaceRankOffsets())
        {
            if (entry->lodState.currentLodIndex == desiredLodIndex &&
                !HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                    &entry->lodState.faceFamilyIds))
            {
                continue;
            }
            if (ReadyFlag() && textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame()) continue;

            CopyFaceSlotsToScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
            const uint8_t previousLodIndex = entry->lodState.currentLodIndex;
            const int16_t previousBaseRank = entry->lodState.currentBaseRank;
            if (!RebuildSegmentFaceSlotsForLod(*entry, desiredLodIndex, familySlots))
            {
                RestoreFaceSlotsFromScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
                entry->lodState.currentLodIndex = previousLodIndex;
                entry->lodState.currentBaseRank = previousBaseRank;
                continue;
            }
            if (HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                   &entry->lodState.faceFamilyIds))
            {
                RestoreFaceSlotsFromScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
                entry->lodState.currentLodIndex = previousLodIndex;
                entry->lodState.currentBaseRank = previousBaseRank;
                continue;
            }
            (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
            ++runtimeFaceRemapsThisFrame_;
            ++runtimeLodSegmentUpdatesThisFrame_;
            entry->lodState.currentLodIndex = desiredLodIndex;
            entry->lodState.currentBaseRank = -1;
            InvalidateEntryWorkingSetCache(*entry);
            continue;
        }

        if (entry->lodState.currentBaseRank == desiredBaseRank &&
            entry->lodState.currentLodIndex == desiredLodIndex &&
            !HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                &entry->lodState.faceFamilyIds))
        {
            continue;
        }
        if (ReadyFlag() && textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame()) continue;

        CopyFaceSlotsToScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
        const uint8_t previousLodIndex = entry->lodState.currentLodIndex;
        const int16_t previousBaseRank = entry->lodState.currentBaseRank;
        if (!RebuildSegmentFaceSlotsForBaseRank(*entry, logicalRank, familySlots))
        {
            RestoreFaceSlotsFromScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
            entry->lodState.currentLodIndex = previousLodIndex;
            entry->lodState.currentBaseRank = previousBaseRank;
            continue;
        }
        if (HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                               &entry->lodState.faceFamilyIds))
        {
            RestoreFaceSlotsFromScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
            entry->lodState.currentLodIndex = previousLodIndex;
            entry->lodState.currentBaseRank = previousBaseRank;
            continue;
        }
        (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
        ++runtimeFaceRemapsThisFrame_;
        ++runtimeLodSegmentUpdatesThisFrame_;
        entry->lodState.currentBaseRank = static_cast<int8_t>(desiredBaseRank);
        entry->lodState.currentLodIndex = desiredLodIndex;
        InvalidateEntryWorkingSetCache(*entry);
    }
}

// Apply lod changes only when a visible segment crosses a band boundary.
void TrackSystem::UpdateVisibleSegmentLods(const std::vector<SegmentHandle>& nearToFarHandles)
{
    if (kEnableTrackRuntimeStabilization && !kEnableTrackLodBandsInStabilization)
    {
        (void)nearToFarHandles;
        return;
    }

    FamilySlotVector& familySlots = seg1FamilySlots_;
    size_t logicalRank = 0;
    for (size_t rank = 0; rank < nearToFarHandles.size(); ++rank)
    {
        auto* entry = segmentPool_.Resolve(nearToFarHandles[rank]);
        if (!entry || !entry->renderer) continue;
        if (!entry->lodState.Ready()) continue;

        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        const int16_t desiredBaseRank = static_cast<int16_t>(logicalRank);
        if (!entry->lodState.HasPerFaceRankOffsets())
        {
            // Fast path for 1-segment packages: update only when band changes.
            if (entry->lodState.currentLodIndex == desiredLodIndex &&
                !HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                    &entry->lodState.faceFamilyIds))
            {
                logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
                continue;
            }
            if (ReadyFlag() && textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame())
            {
                // Keep current mapping this frame and retry when upload budget resets.
                logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
                continue;
            }

            CopyFaceSlotsToScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
            const uint8_t previousLodIndex = entry->lodState.currentLodIndex;
            const int16_t previousBaseRank = entry->lodState.currentBaseRank;
            if (!RebuildSegmentFaceSlotsForLod(*entry, desiredLodIndex, familySlots))
            {
                RestoreFaceSlotsFromScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
                entry->lodState.currentLodIndex = previousLodIndex;
                entry->lodState.currentBaseRank = previousBaseRank;
                logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
                continue;
            }
            const bool missing =
                HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                   &entry->lodState.faceFamilyIds);
            if (missing)
            {
                // Keep the previous mapping until the new lod is fully available.
                RestoreFaceSlotsFromScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
                entry->lodState.currentLodIndex = previousLodIndex;
                entry->lodState.currentBaseRank = previousBaseRank;
                logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
                continue;
            }
            (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
            ++runtimeFaceRemapsThisFrame_;
            ++runtimeLodSegmentUpdatesThisFrame_;
            entry->lodState.currentLodIndex = desiredLodIndex;
            entry->lodState.currentBaseRank = -1;
            InvalidateEntryWorkingSetCache(*entry);
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }

        // Fallback path for multi-segment batches with per-face rank offsets.
        if (entry->lodState.currentBaseRank == desiredBaseRank &&
            entry->lodState.currentLodIndex != 0xFF)
        {
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }
        if (ReadyFlag() && textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame())
        {
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }
        CopyFaceSlotsToScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
        const uint8_t previousLodIndex = entry->lodState.currentLodIndex;
        const int16_t previousBaseRank = entry->lodState.currentBaseRank;
        if (!RebuildSegmentFaceSlotsForBaseRank(*entry, logicalRank, familySlots))
        {
            RestoreFaceSlotsFromScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
            entry->lodState.currentLodIndex = previousLodIndex;
            entry->lodState.currentBaseRank = previousBaseRank;
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }
        const bool missing =
            HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                               &entry->lodState.faceFamilyIds);
        if (missing)
        {
            RestoreFaceSlotsFromScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
            entry->lodState.currentLodIndex = previousLodIndex;
            entry->lodState.currentBaseRank = previousBaseRank;
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }
        (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
        ++runtimeFaceRemapsThisFrame_;
        ++runtimeLodSegmentUpdatesThisFrame_;
        entry->lodState.currentBaseRank = static_cast<int8_t>(desiredBaseRank);
        entry->lodState.currentLodIndex = desiredLodIndex;
        InvalidateEntryWorkingSetCache(*entry);
        logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
    }
}

bool TrackSystem::BuildSegmentCenterCatalog()
{
    segmentCenterCatalog_.clear();

    // Catalog source prefers the runtime blob pack and falls back to SDR during migration.
    // Stop on first missing id to keep the id space contiguous.
    constexpr int32_t kCatalogHardLimit = 4096;
    for (int32_t id = 1; id <= kCatalogHardLimit; ++id)
    {
        int32_t centerX = 0;
        int32_t centerY = 0;
        int32_t centerZ = 0;
        SegmentRuntimeDraw::HeaderV1 rdrHeader{};
        if (LoadRdrHeaderForSegment(id, rdrHeader, true))
        {
            centerX = rdrHeader.centerX;
            centerY = rdrHeader.centerY;
            centerZ = rdrHeader.centerZ;
        }
        else
        {
            SegmentDrawReady::HeaderV1 sdrHeader{};
            if (!LoadSdrHeaderForSegment(id, sdrHeader, true))
            {
                break;
            }
            centerX = sdrHeader.centerX;
            centerY = sdrHeader.centerY;
            centerZ = sdrHeader.centerZ;
        }

        segmentCenterCatalog_.push_back(Vector3D(
            SRL::Math::Types::Fxp::BuildRaw(centerX),
            SRL::Math::Types::Fxp::BuildRaw(centerY),
            SRL::Math::Types::Fxp::BuildRaw(centerZ)));
    }

    totalSegmentCount_ = static_cast<uint16_t>(std::min<size_t>(
        segmentCenterCatalog_.size(),
        static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
    if (totalSegmentCount_ == 0)
    {
        activeWindowStartId_ = 1;
        return false;
    }

    activeWindowStartId_ = 1;
    SRL::Debug::Print(1, 27, "TRK cat segs:%u", static_cast<unsigned>(totalSegmentCount_));
    return true;
}

bool TrackSystem::RebuildActiveSegmentWindow(int32_t startSegmentId, size_t loadLimit, int8_t direction)
{
    direction = (direction < 0) ? -1 : 1;
    if (totalSegmentCount_ == 0 || loadLimit == 0)
    {
        segmentEntries_.clear();
        segmentRenderers_.clear();
        segmentHandles_.clear();
        segmentPool_.Reset();
        activeWindowHead_ = 0;
        windowDirection_ = direction;
        prewarmCooldown_ = 0;
        boundaryPrewarmCooldown_ = 0;
        slideScratchRenderer_.reset();
        ResetSlidePrefetchState();
        ResetSlideBackBuffer();
        familyMergeCooldown_ = 0;
        SetSegmentsReady(false);
        return false;
    }

    const int32_t wrappedStartId = WrapSegmentIdToRange(startSegmentId, totalSegmentCount_);
    if (wrappedStartId <= 0) return false;
    activeWindowStartId_ = wrappedStartId;
    windowDirection_ = direction;

    const size_t windowCount = std::min<size_t>(
        std::min<size_t>(loadLimit, kTrackSegmentLimit),
        static_cast<size_t>(totalSegmentCount_));
    const bool useFixedWindowStorage = kEnableTrackWindowFixedStorage;

    segmentEntries_.clear();
    if (!useFixedWindowStorage)
    {
        segmentRenderers_.clear();
    }
    segmentHandles_.clear();
    segmentPool_.Reset();
    activeWindowHead_ = 0;
    prewarmCooldown_ = 0;
    boundaryPrewarmCooldown_ = 0;
    slideScratchRenderer_.reset();
    ResetSlidePrefetchState();
    familyMergeCooldown_ = 0;
    // Reserve fixed capacities so runtime window rebuilds do not keep resizing
    // active storage as load limits vary.
    segmentEntries_.reserve(kTrackSegmentLimit);
    segmentRenderers_.reserve(kTrackSegmentLimit + 1u); // +1 keeps room for future staging slot
    if (useFixedWindowStorage)
    {
        segmentEntries_.resize(windowCount);
        segmentRenderers_.resize(windowCount);
        for (auto& slot : segmentRenderers_)
        {
            if (!slot.renderer) continue;
            ApplyActiveRendererCapacityFloor(*slot.renderer);
        }
    }

    // Runtime streaming path: keep package fixed at one segment to minimize
    // transient allocations and avoid heavy planner probes every window shift.
    const size_t segmentsPerDrawPackage = 1;
    size_t builtCount = 0;

    for (size_t logicalSid = 0; logicalSid < windowCount; )
    {
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        if (freeValid && freeBytes <= kWorkRamHardFloorBytes)
        {
            SRL::Debug::Print(1, 11, "PKG low HWR sid:%u free:%u ok:%u",
                              static_cast<unsigned>(logicalSid + 1),
                              static_cast<unsigned>(freeBytes),
                              freeValid ? 1u : 0u);
        }

        const size_t chosenCount = std::min(segmentsPerDrawPackage, (windowCount - logicalSid));
        SegmentEntryVector batchEntries{};
        batchEntries.reserve(chosenCount);
        for (size_t i = 0; i < chosenCount; ++i)
        {
            const int32_t sid = WrapSegmentIdToRange(
                wrappedStartId + (static_cast<int32_t>(logicalSid + i) * static_cast<int32_t>(direction)),
                totalSegmentCount_);
            if (sid <= 0) continue;
            batchEntries.push_back({ sid, {} });
        }

        if (batchEntries.empty())
        {
            logicalSid += chosenCount;
            continue;
        }

        const int32_t segmentId = batchEntries.front().id;
        if (useFixedWindowStorage)
        {
            if (builtCount >= segmentRenderers_.size())
            {
                break;
            }
            SegmentRenderEntry& builtEntry = segmentRenderers_[builtCount];
            if (!builtEntry.renderer)
            {
                builtEntry.renderer = MakeTrackObjectUnique<TrackRenderer, SRL::Memory::Zone::LWRam>();
                if (!builtEntry.renderer)
                {
                    SRL::Debug::Print(1, 11, "PKG slot alloc fail id:%d", segmentId);
                    break;
                }
            }
            ApplyActiveRendererCapacityFloor(*builtEntry.renderer);
            builtEntry.lodState.faceFamilyIds.clear();
            Vector3D builtCenter(0.0, 0.0, 0.0);
            if (!BuildSegmentIntoRenderer(segmentId,
                                          *builtEntry.renderer,
                                          builtCenter,
                                          builtEntry.lodState.faceFamilyIds) ||
                builtEntry.lodState.faceFamilyIds.empty())
            {
                SRL::Debug::Print(1, 11, "PKG build fail id:%d", segmentId);
                break;
            }

            builtEntry.id = segmentId;
            builtEntry.logicalSegmentCount = 1;
            builtEntry.center = builtCenter;
            builtEntry.lodState.SetReady(true);
            builtEntry.lodState.SetHasPerFaceRankOffsets(false);
            builtEntry.lodState.currentLodIndex = 0xFF;
            builtEntry.lodState.currentBaseRank = -1;
            builtEntry.lodState.desiredLodIndex = 0xFF;
            builtEntry.lodState.desiredBaseRank = -1;
            builtEntry.lodState.faceRankOffsets.assign(builtEntry.lodState.faceFamilyIds.size(), 0u);
            builtEntry.lodState.currentFaceSlots.assign(builtEntry.lodState.faceFamilyIds.size(), -1);
            EnsureVectorCapacityFloor(builtEntry.lodState.faceFamilyIds, slotFaceCapacityFloor_);
            EnsureVectorCapacityFloor(builtEntry.lodState.faceRankOffsets, slotFaceCapacityFloor_);
            EnsureVectorCapacityFloor(builtEntry.lodState.currentFaceSlots, slotFaceCapacityFloor_);
            EnsureVectorCapacityFloor(builtEntry.lodState.workingSetFamilies,
                                     static_cast<size_t>(slotFaceCapacityFloor_));
            EnsureVectorCapacityFloor(builtEntry.lodState.workingSetLodIndices,
                                     static_cast<size_t>(slotFaceCapacityFloor_));
            EnsureVectorCapacityFloor(builtEntry.lodState.workingSetSlots,
                                     static_cast<size_t>(slotFaceCapacityFloor_));

            if (kEnableTrackRuntimeStabilization && kEnableTrackLodBandsInStabilization)
            {
                const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalSid);
                const bool remapOk = !builtEntry.lodState.HasPerFaceRankOffsets()
                    ? RebuildSegmentFaceSlotsForLod(builtEntry, desiredLodIndex, seg1FamilySlots_)
                    : RebuildSegmentFaceSlotsForBaseRank(builtEntry, logicalSid, seg1FamilySlots_);
                if (remapOk &&
                    !HasMissingRequiredFaceTextureSlots(builtEntry.lodState.currentFaceSlots,
                                                       &builtEntry.lodState.faceFamilyIds))
                {
                    (void)builtEntry.renderer->ApplyFaceTextureSlotsGlobal(builtEntry.lodState.currentFaceSlots);
                    builtEntry.lodState.currentLodIndex = desiredLodIndex;
                    builtEntry.lodState.currentBaseRank = builtEntry.lodState.HasPerFaceRankOffsets()
                        ? static_cast<int16_t>(logicalSid)
                        : -1;
                    builtEntry.lodState.desiredLodIndex = desiredLodIndex;
                    builtEntry.lodState.desiredBaseRank = builtEntry.lodState.currentBaseRank;
                    InvalidateEntryWorkingSetCache(builtEntry);
                }
            }
            segmentEntries_[builtCount] = { segmentId, {} };
            ++builtCount;
            logicalSid += chosenCount;
            continue;
        }

        auto built = BuildSegmentRenderers(batchEntries);
        if (built.empty())
        {
            SRL::Debug::Print(1, 11, "PKG build fail id:%d", segmentId);
            break;
        }

        if (kEnableTrackRuntimeStabilization && kEnableTrackLodBandsInStabilization)
        {
            SegmentRenderEntry& builtEntry = built[0];
            const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalSid);
            const bool remapOk = !builtEntry.lodState.HasPerFaceRankOffsets()
                ? RebuildSegmentFaceSlotsForLod(builtEntry, desiredLodIndex, seg1FamilySlots_)
                : RebuildSegmentFaceSlotsForBaseRank(builtEntry, logicalSid, seg1FamilySlots_);
            if (remapOk &&
                !HasMissingRequiredFaceTextureSlots(builtEntry.lodState.currentFaceSlots,
                                                   &builtEntry.lodState.faceFamilyIds))
            {
                (void)builtEntry.renderer->ApplyFaceTextureSlotsGlobal(builtEntry.lodState.currentFaceSlots);
                builtEntry.lodState.currentLodIndex = desiredLodIndex;
                builtEntry.lodState.currentBaseRank = builtEntry.lodState.HasPerFaceRankOffsets()
                    ? static_cast<int16_t>(logicalSid)
                    : -1;
                builtEntry.lodState.desiredLodIndex = desiredLodIndex;
                builtEntry.lodState.desiredBaseRank = builtEntry.lodState.currentBaseRank;
                InvalidateEntryWorkingSetCache(builtEntry);
            }
        }

        // Keep metadata aligned with successfully built renderers.
        segmentEntries_.push_back({ batchEntries.front().id, {} });
        segmentRenderers_.push_back(std::move(built[0]));
        ++builtCount;
        logicalSid += chosenCount;
    }

    const bool fullWindowBuilt = (builtCount == windowCount);
    SetSegmentsReady(fullWindowBuilt);
    if (!SegmentsReady())
    {
        segmentEntries_.clear();
        segmentRenderers_.clear();
        segmentHandles_.clear();
        segmentPool_.Reset();
        activeWindowHead_ = 0;
        slideScratchRenderer_.reset();
        ResetSlidePrefetchState();
        if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
        {
            SRL::Debug::Print(1, 11, "PKG window incomplete built:%u need:%u",
                              static_cast<unsigned>(builtCount),
                              static_cast<unsigned>(windowCount));
        }
        return false;
    }
    activeWindowHead_ = 0;

    // slotPool_ nÃ£o Ã© alocado: a rotaÃ§Ã£o O(1) planejada nÃ£o foi implementada.
    // Alocar 21 SegmentRenderEntry+renderers+vetores em LWR sem uso Ã© ~210 KB perdidos.

    BuildSegmentHandleTable();
    if (kEnableTrackRuntimeStabilization && !slideScratchRenderer_)
    {
        slideScratchRenderer_ = MakeTrackObjectUnique<TrackRenderer, SRL::Memory::Zone::LWRam>();
        if (slideScratchRenderer_)
        {
            ConfigureStreamedRendererDefaults(*slideScratchRenderer_);
        }
    }

    if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
    {
        SRL::Debug::Print(1, 13, "TRK pkg %u s:%u st:%d t:%u",
                          static_cast<unsigned>(builtCount),
                          static_cast<unsigned>(segmentEntries_.size()),
                          activeWindowStartId_,
                          static_cast<unsigned>(totalSegmentCount_));
        SRL::Debug::Print(1, 14, "TRK sh2 md:%s p:%u s:%u lk:%u",
                          TrackSlaveModeRequestedFlag() ? "DUAL" : "SING",
                          TrackSlaveProducerRequestedFlag() ? 1u : 0u,
                          TrackSlaveDepthSortRequestedFlag() ? 1u : 0u,
                          TrackSlaveBarrierLockstepFlag() ? 1u : 0u);
        SRL::Debug::Print(1, 12, "TRK mem r:%u lwr:1 er:%u rr:%u",
                          static_cast<unsigned>(kTrackRuntimeMemRev),
                          static_cast<unsigned>(workRamEmergencyReserve_ ? workRamEmergencyReserveBytes_ : 0u),
                          static_cast<unsigned>(workRamMaintenance_.workRamEmergencyReserveReleases));
    }
    InvalidateActiveWindowLookupTables();
    UpdateDesiredStabilizedWindowLodTargets();
    SetFamilyWorkingSetDirty(true);
    if (kEnableTrackRuntimeStabilization && kEnableTrackLodBandsInStabilization)
    {
        (void)RebuildTrackTextureResidencyForWindow();
    }
    return SegmentsReady();
}

void TrackSystem::ResetSlidePrefetchState()
{
    LWR_PROBE_BEGIN();
    prefetchRetryCooldown_  = 0u;
    slidePrefetchSegmentId_ = -1;
    slidePrefetchCenter_    = Vector3D(0.0, 0.0, 0.0);

    // Clear for immediate reuse; oversized buffers are compacted back to floor
    // so prefetch does not keep a hidden backup across laps.
    slidePrefetchFamilyIds_.clear();
    slidePrefetchFaceSlots_.clear();
    slidePrefetchFamilySlotsScratch_.clear();

    const size_t lwrFreeHint =
        static_cast<size_t>(SRL::Memory::LowWorkRam::GetReport().FreeSize);
    const size_t faceFloor = std::max<size_t>(1u, static_cast<size_t>(slotFaceCapacityFloor_));
    const size_t familyFloor =
        std::max<size_t>(256u, static_cast<size_t>(familySlotCapacityFloor_));
    (void)CompactEmptyVectorForTarget(slidePrefetchFamilyIds_, faceFloor, lwrFreeHint);
    (void)CompactEmptyVectorForTarget(slidePrefetchFaceSlots_, faceFloor, lwrFreeHint);
    (void)CompactEmptyVectorForTarget(slidePrefetchFamilySlotsScratch_, familyFloor, lwrFreeHint);

    if (slidePrefetchRenderer_)
    {
        slidePrefetchRenderer_->RecycleRuntimeState();
        // Prevent outlier segments from leaving oversized component buffers alive.
        (void)slidePrefetchRenderer_->CompactRuntimeState(false);
    }

    SetSlidePrefetchRendererReady(false);
    SetSlidePrefetchLodReady(false);
    LWR_PROBE_END(g_lwrStageAccum.resetPrefetch);
}

void TrackSystem::ResetSlideBackBuffer()
{
    LWR_PROBE_BEGIN();
    slideBackBuffer_.SetReady(false);
    slideBackBuffer_.direction = 1;
    slideBackBuffer_.dropIdx = 0;
    slideBackBuffer_.incomingSegmentId = -1;
    slideBackBuffer_.outgoingSegmentId = -1;
    slideBackBuffer_.nextStartId = 1;
    slideBackBuffer_.incomingCenter = Vector3D(0.0, 0.0, 0.0);
    // Usar clear() em todos os casos â€” preserva capacity no LWR, sem swap-free
    slideBackBuffer_.incomingFamilyIds.clear();
    slideBackBuffer_.incomingFaceSlots.clear();
    slideBackBuffer_.incomingResidentLodIndex = 0xFF;
    slideBackBuffer_.incomingResidentBaseRank = static_cast<int8_t>(-1);
    for (auto& update : slideBackBuffer_.boundaryUpdates)
    {
        update.SetActive(false);
        update.segmentId       = -1;
        update.desiredLodIndex = 0xFF;
        update.desiredBaseRank = static_cast<int8_t>(-1);
        update.preparedFaceSlots.clear();
    }
    // In deterministic mode the backbuffer is inactive; keep it empty to avoid
    // retaining stale per-slide vectors as latent backups.
    if (kEnableDeterministicStabilizedSlide)
    {
        const size_t lwrFreeHint =
            static_cast<size_t>(SRL::Memory::LowWorkRam::GetReport().FreeSize);
        (void)CompactEmptyVectorForTarget(slideBackBuffer_.incomingFamilyIds, 0u, lwrFreeHint);
        (void)CompactEmptyVectorForTarget(slideBackBuffer_.incomingFaceSlots, 0u, lwrFreeHint);
        for (auto& update : slideBackBuffer_.boundaryUpdates)
        {
            (void)CompactEmptyVectorForTarget(update.preparedFaceSlots, 0u, lwrFreeHint);
        }
    }
    LWR_PROBE_END(g_lwrStageAccum.resetBackBuf);
}

bool TrackSystem::ExecuteDeterministicStabilizedSlide(size_t dropIdx,
                                                      int8_t direction,
                                                      int32_t nextId,
                                                      int32_t nextStartId)
{
    if (!kEnableTrackRuntimeStabilization) return false;
    if (segmentRenderers_.empty()) return false;
    if (dropIdx >= segmentRenderers_.size()) return false;

    // Sempre drenar aposentados pendentes antes de montar o novo tail para que
    // o upload prefira reuso de slot em vez de crescer o heap.
    workRamMaintenance_.releasedEndFrameSlotsThisFrame = static_cast<uint16_t>(
        std::min<uint32_t>(
            static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()),
            static_cast<uint32_t>(workRamMaintenance_.releasedEndFrameSlotsThisFrame) +
                static_cast<uint32_t>(FlushPendingRetiredTrackTextureSlots())));

    slideHwrTrace_.segmentId = nextId;

    auto prefetchMetadataReady = [&]() -> bool
    {
        return slidePrefetchSegmentId_ == nextId &&
               !slidePrefetchFamilyIds_.empty();
    };

    const bool hadPrefetchMetadata = prefetchMetadataReady();
    if (hadPrefetchMetadata)
    {
        ++runtimePrefetchHitsThisFrame_;
        slideHwrTrace_.afterBuildPrefetch = static_cast<uint32_t>(GetHighWorkRamFreeBytesSafe());
    }
    else
    {
        ++runtimePrefetchMissesThisFrame_;
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        // Estimativa detalhada de retenÃ§Ã£o Ã© custosa. SÃ³ calcular quando hÃ¡
        // pressÃ£o real de memÃ³ria para nÃ£o penalizar o caminho de slide.
        uint32_t trackOwnedHwrBytes = 0u;
        bool trackOwnedBypass = true;
        if (!freeValid || freeBytes <= (kWorkRamHardFloorBytes + (32u * 1024u)))
        {
            trackOwnedHwrBytes =
                ResolveTrackOwnedHighWorkBytesForPressure(
                    static_cast<uint32_t>(EstimateWorkRamRetainedBytes()));
            if (freeValid && freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u)))
            {
                trackOwnedHwrBytes = std::max<uint32_t>(trackOwnedHwrBytes,
                                                        GetTrackOwnedHighWorkBytesExact());
            }
            trackOwnedBypass =
                trackOwnedHwrBytes <= static_cast<uint32_t>(kWorkRamTrackOwnedBypassBytes);
        }
        if (!trackOwnedBypass &&
            freeValid &&
            freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u)))
        {
            if (slideScratchRenderer_ && slidePrefetchSegmentId_ != nextId)
            {
                slideScratchRenderer_->RecycleRuntimeState();
            }
            // Do NOT call TrimRuntimeBlobScratchCaches(true) here: it frees blob/verts/faces/attrs,
            // which BuildSegmentIntoPrefetch immediately reallocates â€” pure TLSF overhead per cycle.
            // The blob will be reused in-place via resize(); verts/faces/attrs are pre-primed.
            int32_t freeDelta = 0;
            (void)TrimWorkRamRetainedCapacities(true, &freeDelta);
        }
        (void)BuildSegmentIntoPrefetch(nextId, false);
        slideHwrTrace_.flags |= kSlideHwrTraceBuildPrefetchBit;
        slideHwrTrace_.afterBuildPrefetch = static_cast<uint32_t>(GetHighWorkRamFreeBytesSafe());
        if (!prefetchMetadataReady())
        {
            slideHwrTrace_.flags |= kSlideHwrTracePrepareFailBit;
            SRL::Debug::Print(1, 11, "PKG pf fail id:%d md:%u tx:%u p16:%u rs:%u",
                              nextId,
                              static_cast<unsigned>(prefetchMetadataReady() ? 1u : 0u),
                              static_cast<unsigned>(SRL::VDP1::GetTextureCount()),
                              static_cast<unsigned>(CountTrackedBanks(g_trackPaletteBanks.pal16)),
                              static_cast<unsigned>(CountReusableTrackTextureSlots()));
            return false;
        }
    }

    const size_t windowCount = segmentRenderers_.size();
    const size_t incomingLogicalRank =
        (direction >= 0) ? (windowCount - 1u) : 0u;

    Vector3D incomingCenter = slidePrefetchCenter_;
    // Usar scratch persistente â€” evita alloc/free de LWR por slide
    slideScratchEntry_.lodState.faceFamilyIds.clear();
    if (!BuildSegmentIntoSlideScratch(nextId, incomingCenter, slideScratchEntry_.lodState.faceFamilyIds) ||
        slideScratchEntry_.lodState.faceFamilyIds.empty())
    {
        slideHwrTrace_.flags |= kSlideHwrTracePrepareFailBit;
        SRL::Debug::Print(1, 11, "PKG tail build fail id:%d md:%u",
                          nextId,
                          static_cast<unsigned>(prefetchMetadataReady() ? 1u : 0u));
        return false;
    }

    bool addedIncomingFamily = false;
    const size_t familySlotsBeforeIncoming = seg1FamilySlots_.size();
    for (const uint16_t fam : slideScratchEntry_.lodState.faceFamilyIds)
    {
        if (fam == 0u) continue;
        if (FindFamilySlot(seg1FamilySlots_, fam)) continue;
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = fam;
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        seg1FamilySlots_.push_back(slotEntry);
        addedIncomingFamily = true;
    }
    if (addedIncomingFamily) InvalidateFamilySlotIndex();
    auto rollbackAddedIncomingFamilies = [&]()
    {
        if (seg1FamilySlots_.size() <= familySlotsBeforeIncoming) return;
        for (size_t fi = familySlotsBeforeIncoming; fi < seg1FamilySlots_.size(); ++fi)
        {
            for (uint8_t li = 0; li < 4u; ++li)
            {
                const uint16_t slotId = seg1FamilySlots_[fi].lodSlots[li];
                if (!IsVdp1TextureSlotLive(slotId)) continue;
                QueuePendingRetiredTrackTextureSlot(slotId);
            }
        }
        seg1FamilySlots_.resize(familySlotsBeforeIncoming);
        InvalidateFamilySlotIndex();
        SetFamilyWorkingSetDirty(true);
    };

    // Usar membro persistente para evitar alloc/free de LWR por slide.
    // faceFamilyIds jÃ¡ preenchido por BuildSegmentIntoSlideScratch acima â€” sem swap necessÃ¡rio.
    SegmentRenderEntry& incomingPrepared = slideScratchEntry_;
    incomingPrepared.id = nextId;
    incomingPrepared.logicalSegmentCount = 1;
    incomingPrepared.renderer = std::move(slideScratchRenderer_);
    incomingPrepared.center = incomingCenter;
    incomingPrepared.lodState.SetReady(true);
    incomingPrepared.lodState.SetHasPerFaceRankOffsets(false);
    incomingPrepared.lodState.currentLodIndex = ResolveSegmentLodIndexByRank(incomingLogicalRank);
    incomingPrepared.lodState.currentBaseRank = -1;
    incomingPrepared.lodState.desiredLodIndex = incomingPrepared.lodState.currentLodIndex;
    incomingPrepared.lodState.desiredBaseRank = -1;
    // faceFamilyIds jÃ¡ preenchido â€” sem swap; apenas preparar rank offsets e face slots
    incomingPrepared.lodState.faceRankOffsets.clear();
    incomingPrepared.lodState.faceRankOffsets.assign(incomingPrepared.lodState.faceFamilyIds.size(), 0u);
    incomingPrepared.lodState.currentFaceSlots.clear();
    incomingPrepared.lodState.currentFaceSlots.assign(incomingPrepared.lodState.faceFamilyIds.size(), -1);
    EnsureVectorCapacityFloor(incomingPrepared.lodState.faceFamilyIds, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(incomingPrepared.lodState.faceRankOffsets, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(incomingPrepared.lodState.currentFaceSlots, slotFaceCapacityFloor_);

    const bool incomingRebuilt =
        RebuildSegmentFaceSlotsForLod(incomingPrepared,
                                      incomingPrepared.lodState.currentLodIndex,
                                      seg1FamilySlots_,
                                      /*bypassUploadBudget*/true) &&
        !HasMissingRequiredFaceTextureSlots(incomingPrepared.lodState.currentFaceSlots,
                                            &incomingPrepared.lodState.faceFamilyIds);
    if (!incomingRebuilt)
    {
        slideHwrTrace_.flags |= kSlideHwrTracePrepareFailBit;
        const uint32_t missingIncoming = CountMissingOrDeadRequiredFaceTextureSlots(
            incomingPrepared.lodState.currentFaceSlots,
            &incomingPrepared.lodState.faceFamilyIds);
        SRL::Debug::Print(1, 11, "PKG tail miss id:%d md:%u ms:%u",
                          nextId,
                          static_cast<unsigned>(prefetchMetadataReady() ? 1u : 0u),
                          static_cast<unsigned>(missingIncoming));
        slideScratchRenderer_ = std::move(incomingPrepared.renderer);
        rollbackAddedIncomingFamilies();
        return false;
    }
    incomingCenter = incomingPrepared.center;
    // Devolver renderer ao scratch; dados permanecem em incomingPrepared.lodState.* (persistente)
    slideScratchRenderer_ = std::move(incomingPrepared.renderer);

    // Usar scratch persistente para boundary slots â€” evita alloc/free de LWR por slide
    struct BoundaryPrepared
    {
        SegmentRenderEntry* entry = nullptr;
        uint8_t desiredLodIndex = 0xFF;
        int16_t desiredBaseRank = -1;
        // faceSlots em slideScratchBoundarySlots_[preparedCount]
    };
    std::array<BoundaryPrepared, 3> preparedUpdates{};
    for (auto& s : slideScratchBoundarySlots_) s.clear();
    size_t preparedCount = 0u;
    uint8_t deferredBoundaryUpdates = 0u;
    static constexpr std::array<size_t, 3> kForwardBoundaryRanks{{3u, 8u, 13u}};
    static constexpr std::array<size_t, 3> kBackwardBoundaryRanks{{4u, 9u, 14u}};
    bool boundaryFreeValid = false;
    const size_t boundaryFreeBytes = GetHighWorkRamFreeBytesSafe(&boundaryFreeValid);
    const uint8_t boundaryBudget =
        (!boundaryFreeValid || boundaryFreeBytes <= (kWorkRamHardFloorBytes + (24u * 1024u))) ? 1u :
        (boundaryFreeBytes <= (kWorkRamHardFloorBytes + (64u * 1024u))) ? 2u :
        3u;
    const int32_t dir = (direction < 0) ? -1 : 1;
    const auto& boundaryRanks = (dir > 0) ? kForwardBoundaryRanks : kBackwardBoundaryRanks;
    for (size_t ri = 0; ri < boundaryRanks.size(); ++ri)
    {
        const size_t logicalRank = boundaryRanks[ri];
        if (logicalRank >= windowCount) continue;
        const int32_t segmentId = WrapSegmentIdToRange(
            nextStartId + (dir > 0 ? static_cast<int32_t>(logicalRank)
                                   : -static_cast<int32_t>(logicalRank)),
            totalSegmentCount_);
        if (segmentId <= 0)
        {
            rollbackAddedIncomingFamilies();
            return false;
        }

        SegmentRenderEntry* entry = FindWindowEntryByIdFast(segmentId);
        if (!entry || !entry->renderer || !entry->lodState.Ready())
        {
            rollbackAddedIncomingFamilies();
            return false;
        }

        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        const int16_t desiredBaseRank = entry->lodState.HasPerFaceRankOffsets()
            ? static_cast<int16_t>(logicalRank)
            : -1;
        const bool needsUpdate = entry->lodState.currentLodIndex != desiredLodIndex ||
                                 entry->lodState.currentBaseRank != desiredBaseRank ||
                                 HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                                    &entry->lodState.faceFamilyIds);
        if (!needsUpdate) continue;
        if (preparedCount >= boundaryBudget)
        {
            QueuePendingStabilizedLodRank(logicalRank);
            ++deferredBoundaryUpdates;
            continue;
        }

        BoundaryPrepared& prepared = preparedUpdates[preparedCount];
        prepared.entry = entry;
        prepared.desiredLodIndex = desiredLodIndex;
        prepared.desiredBaseRank = static_cast<int8_t>(desiredBaseRank);
        // Boundary transitions are important, but forcing all uploads in the
        // same slide causes frame spikes. Respect frame upload budget here and
        // defer unresolved boundaries to pending LOD recovery.
        TrackLowWorkI16Vector& boundaryScratch = slideScratchBoundarySlots_[preparedCount];
        const bool ok = entry->lodState.HasPerFaceRankOffsets()
            ? ResolvePreparedFaceSlotsForBaseRank(*entry,
                                                  logicalRank,
                                                  seg1FamilySlots_,
                                                  boundaryScratch,
                                                  /*bypassUploadBudget*/false)
            : ResolvePreparedFaceSlotsForLod(*entry,
                                             desiredLodIndex,
                                             seg1FamilySlots_,
                                             boundaryScratch,
                                             /*bypassUploadBudget*/false);
        if (!ok ||
            HasMissingRequiredFaceTextureSlots(boundaryScratch, &entry->lodState.faceFamilyIds))
        {
            QueuePendingStabilizedLodRank(logicalRank);
            slideHwrTrace_.flags |= kSlideHwrTraceDeferredBit;
            ++deferredBoundaryUpdates;
            continue;
        }
        ++preparedCount;
    }

    if (deferredBoundaryUpdates > 0u)
    {
        pendingLodFrameCooldown_ = 0u;
        if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
        {
            SRL::Debug::Print(1, 11, "PKG bd defer id:%d n:%u", nextId,
                              static_cast<unsigned>(deferredBoundaryUpdates));
        }
    }

    SegmentRenderEntry& slot = segmentRenderers_[dropIdx];
    TrackSegmentEntry& meta = segmentEntries_[dropIdx];
    std::swap(slot.renderer, slideScratchRenderer_);
    if (!slot.renderer)
    {
        slideHwrTrace_.flags |= kSlideHwrTraceCommitFailBit;
        rollbackAddedIncomingFamilies();
        return false;
    }
    if (slideScratchRenderer_)
    {
        slideScratchRenderer_->RecycleRuntimeState();
    }

    slot.id = nextId;
    slot.logicalSegmentCount = 1;
    slot.center = incomingCenter;
    slot.lodState.SetReady(true);
    slot.lodState.SetHasPerFaceRankOffsets(false);
    // Swap entre dois membros persistentes: old slot capacity vai para o scratch,
    // incoming data vai para o slot â€” nenhum free de LWR ocorre.
    slot.lodState.faceFamilyIds.swap(incomingPrepared.lodState.faceFamilyIds);
    slot.lodState.faceRankOffsets.swap(incomingPrepared.lodState.faceRankOffsets);
    slot.lodState.currentFaceSlots.swap(incomingPrepared.lodState.currentFaceSlots);
    EnsureVectorCapacityFloor(slot.lodState.faceFamilyIds, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(slot.lodState.faceRankOffsets, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(slot.lodState.currentFaceSlots, slotFaceCapacityFloor_);
    slot.lodState.currentLodIndex = ResolveSegmentLodIndexByRank(incomingLogicalRank);
    slot.lodState.currentBaseRank = -1;
    slot.lodState.desiredLodIndex = slot.lodState.currentLodIndex;
    slot.lodState.desiredBaseRank = -1;
    meta.id = nextId;
    ApplyActiveRendererCapacityFloor(*slot.renderer);
    (void)slot.renderer->ApplyFaceTextureSlotsGlobal(slot.lodState.currentFaceSlots);
    ++runtimeFaceRemapsThisFrame_;
    InvalidateEntryWorkingSetCache(slot);

    InvalidateActiveWindowLookupTables();
    activeWindowStartId_ = nextStartId;
    if (!AdvanceWindowHeadByDirection(direction, segmentRenderers_.size()))
    {
        (void)ResolveWindowHeadByStartId(dropIdx);
    }
    UpdateDesiredStabilizedWindowLodTargets();
    SetFamilyWorkingSetDirty(true);

    for (size_t i = 0; i < preparedCount; ++i)
    {
        BoundaryPrepared& prepared = preparedUpdates[i];
        if (!prepared.entry) continue;
        RestoreFaceSlotsFromScratch(prepared.entry->lodState.currentFaceSlots,
                                    slideScratchBoundarySlots_[i]);
        prepared.entry->lodState.currentLodIndex = prepared.desiredLodIndex;
        prepared.entry->lodState.currentBaseRank = prepared.desiredBaseRank;
        prepared.entry->lodState.desiredLodIndex = prepared.desiredLodIndex;
        prepared.entry->lodState.desiredBaseRank = prepared.desiredBaseRank;
        (void)prepared.entry->renderer->ApplyFaceTextureSlotsGlobal(prepared.entry->lodState.currentFaceSlots);
        ++runtimeFaceRemapsThisFrame_;
        ++runtimeLodSegmentUpdatesThisFrame_;
        InvalidateEntryWorkingSetCache(*prepared.entry);
    }

    BuildSegmentHandleTable();
    ResetSlidePrefetchState();
    if (familyMergeCooldown_ == 0u)
    {
        MergeCurrentWindowFamilies();
        familyMergeCooldown_ = ResolveFamilyMergeCooldownFrames(FullTrackFamilyCacheReady());
    }
    else
    {
        --familyMergeCooldown_;
    }
    ++runtimeSlidesThisFrame_;
    {
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        slideHwrTrace_.flags |= kSlideHwrTracePrepareOkBit | kSlideHwrTraceCommitOkBit;
        slideHwrTrace_.afterPrepare = static_cast<uint32_t>(freeBytes);
        slideHwrTrace_.afterCommit = static_cast<uint32_t>(freeBytes);
        const uint8_t cooldown =
            (!freeValid || freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u))) ? 4u :
            (freeBytes <= (kWorkRamHardFloorBytes + (32u * 1024u))) ? 2u :
            1u;
        pendingLodFrameCooldown_ = std::max<uint8_t>(pendingLodFrameCooldown_, cooldown);
    }
    return true;
}

bool TrackSystem::PrepareStabilizedSlideBackBuffer(size_t dropIdx,
                                                   int8_t direction,
                                                   int32_t nextId,
                                                   int32_t nextStartId)
{
    LWR_PROBE_BEGIN();
    slideHwrTrace_.segmentId = nextId;
    ResetSlideBackBuffer();
    if (!kEnableTrackRuntimeStabilization) return false;
    if (segmentRenderers_.empty()) return false;
    if (dropIdx >= segmentRenderers_.size()) return false;

    workRamMaintenance_.releasedEndFrameSlotsThisFrame = static_cast<uint16_t>(
        std::min<uint32_t>(
            static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()),
            static_cast<uint32_t>(workRamMaintenance_.releasedEndFrameSlotsThisFrame) +
                static_cast<uint32_t>(FlushPendingRetiredTrackTextureSlots())));

    auto prefetchMetadataReady = [&]() -> bool
    {
        return slidePrefetchSegmentId_ == nextId &&
               !slidePrefetchFamilyIds_.empty();
    };

    const bool hadPrefetchMetadata = prefetchMetadataReady();
    if (hadPrefetchMetadata)
    {
        ++runtimePrefetchHitsThisFrame_;
        slideHwrTrace_.afterBuildPrefetch = static_cast<uint32_t>(GetHighWorkRamFreeBytesSafe());
    }
    if (!hadPrefetchMetadata)
    {
        ++runtimePrefetchMissesThisFrame_;
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        uint32_t trackOwnedHwrBytes =
            ResolveTrackOwnedHighWorkBytesForPressure(
                static_cast<uint32_t>(EstimateWorkRamRetainedBytes()));
        if (freeValid && freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u)))
        {
            trackOwnedHwrBytes = std::max<uint32_t>(trackOwnedHwrBytes,
                                                    GetTrackOwnedHighWorkBytesExact());
        }
        const bool trackOwnedBypass =
            trackOwnedHwrBytes <= static_cast<uint32_t>(kWorkRamTrackOwnedBypassBytes);
        if (!trackOwnedBypass &&
            freeValid &&
            freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u)))
        {
            if (slideScratchRenderer_ && slidePrefetchSegmentId_ != nextId)
            {
                slideScratchRenderer_->RecycleRuntimeState();
            }
            // Do NOT call TrimRuntimeBlobScratchCaches(true) here: it frees blob/verts/faces/attrs,
            // which BuildSegmentIntoPrefetch immediately reallocates â€” pure TLSF overhead per cycle.
            // The blob will be reused in-place via resize(); verts/faces/attrs are pre-primed.
            int32_t freeDelta = 0;
            (void)TrimWorkRamRetainedCapacities(true, &freeDelta);
        }
        (void)BuildSegmentIntoPrefetch(nextId, false);
        slideHwrTrace_.flags |= kSlideHwrTraceBuildPrefetchBit;
        slideHwrTrace_.afterBuildPrefetch = static_cast<uint32_t>(GetHighWorkRamFreeBytesSafe());
        if (!prefetchMetadataReady())
        {
            slideHwrTrace_.flags |= kSlideHwrTracePrepareFailBit;
            SRL::Debug::Print(1, 11, "PKG pf fail id:%d md:%u tx:%u p16:%u rs:%u",
                              nextId,
                              static_cast<unsigned>(prefetchMetadataReady() ? 1u : 0u),
                              static_cast<unsigned>(SRL::VDP1::GetTextureCount()),
                              static_cast<unsigned>(CountTrackedBanks(g_trackPaletteBanks.pal16)),
                              static_cast<unsigned>(CountReusableTrackTextureSlots()));
            return false;
        }
    }

    slideBackBuffer_.direction = (direction < 0) ? -1 : 1;
    slideBackBuffer_.dropIdx = dropIdx;
    slideBackBuffer_.incomingSegmentId = nextId;
    slideBackBuffer_.outgoingSegmentId = segmentRenderers_[dropIdx].id;
    slideBackBuffer_.nextStartId = nextStartId;
    const size_t windowCount = segmentRenderers_.size();
    const size_t incomingLogicalRank =
        (slideBackBuffer_.direction >= 0) ? (windowCount - 1u) : 0u;

    Vector3D incomingCenter = slidePrefetchCenter_;
    // Usar scratch persistente â€” evita alloc/free de LWR por slide
    slideScratchEntry_.lodState.faceFamilyIds.clear();
    if (!BuildSegmentIntoSlideScratch(nextId, incomingCenter, slideScratchEntry_.lodState.faceFamilyIds) ||
        slideScratchEntry_.lodState.faceFamilyIds.empty())
    {
        slideHwrTrace_.flags |= kSlideHwrTracePrepareFailBit;
        SRL::Debug::Print(1, 11, "PKG tail build fail id:%d md:%u",
                          nextId,
                          static_cast<unsigned>(prefetchMetadataReady() ? 1u : 0u));
        return false;
    }

    bool addedIncomingFamily = false;
    const size_t familySlotsBeforeIncoming = seg1FamilySlots_.size();
    for (const uint16_t fam : slideScratchEntry_.lodState.faceFamilyIds)
    {
        if (fam == 0u) continue;
        if (FindFamilySlot(seg1FamilySlots_, fam)) continue;
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = fam;
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        seg1FamilySlots_.push_back(slotEntry);
        addedIncomingFamily = true;
    }
    if (addedIncomingFamily) InvalidateFamilySlotIndex();
    auto rollbackAddedIncomingFamilies = [&]()
    {
        if (seg1FamilySlots_.size() <= familySlotsBeforeIncoming) return;
        for (size_t fi = familySlotsBeforeIncoming; fi < seg1FamilySlots_.size(); ++fi)
        {
            for (uint8_t li = 0; li < 4u; ++li)
            {
                const uint16_t slotId = seg1FamilySlots_[fi].lodSlots[li];
                if (!IsVdp1TextureSlotLive(slotId)) continue;
                QueuePendingRetiredTrackTextureSlot(slotId);
            }
        }
        seg1FamilySlots_.resize(familySlotsBeforeIncoming);
        InvalidateFamilySlotIndex();
        SetFamilyWorkingSetDirty(true);
    };

    // Usar membro persistente para evitar alloc/free de LWR por slide.
    // faceFamilyIds jÃ¡ preenchido por BuildSegmentIntoSlideScratch acima â€” sem swap necessÃ¡rio.
    SegmentRenderEntry& incomingPrepared = slideScratchEntry_;
    incomingPrepared.id = nextId;
    incomingPrepared.logicalSegmentCount = 1;
    incomingPrepared.renderer = std::move(slideScratchRenderer_);
    incomingPrepared.center = incomingCenter;
    incomingPrepared.lodState.SetReady(true);
    incomingPrepared.lodState.SetHasPerFaceRankOffsets(false);
    incomingPrepared.lodState.currentLodIndex = ResolveSegmentLodIndexByRank(incomingLogicalRank);
    incomingPrepared.lodState.currentBaseRank = -1;
    incomingPrepared.lodState.desiredLodIndex = incomingPrepared.lodState.currentLodIndex;
    incomingPrepared.lodState.desiredBaseRank = -1;
    // faceFamilyIds jÃ¡ preenchido â€” sem swap; apenas preparar rank offsets e face slots
    incomingPrepared.lodState.faceRankOffsets.clear();
    incomingPrepared.lodState.faceRankOffsets.assign(incomingPrepared.lodState.faceFamilyIds.size(), 0u);
    incomingPrepared.lodState.currentFaceSlots.clear();
    incomingPrepared.lodState.currentFaceSlots.assign(incomingPrepared.lodState.faceFamilyIds.size(), -1);
    EnsureVectorCapacityFloor(incomingPrepared.lodState.faceFamilyIds, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(incomingPrepared.lodState.faceRankOffsets, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(incomingPrepared.lodState.currentFaceSlots, slotFaceCapacityFloor_);

    const bool incomingRebuilt =
        RebuildSegmentFaceSlotsForLod(incomingPrepared,
                                      incomingPrepared.lodState.currentLodIndex,
                                      seg1FamilySlots_,
                                      /*bypassUploadBudget*/true) &&
        !HasMissingRequiredFaceTextureSlots(incomingPrepared.lodState.currentFaceSlots,
                                            &incomingPrepared.lodState.faceFamilyIds);
    if (!incomingRebuilt)
    {
        slideHwrTrace_.flags |= kSlideHwrTracePrepareFailBit;
        const uint32_t missingIncoming = CountMissingOrDeadRequiredFaceTextureSlots(
            incomingPrepared.lodState.currentFaceSlots,
            &incomingPrepared.lodState.faceFamilyIds);
        SRL::Debug::Print(1, 11, "PKG tail miss id:%d md:%u ms:%u",
                          nextId,
                          static_cast<unsigned>(prefetchMetadataReady() ? 1u : 0u),
                          static_cast<unsigned>(missingIncoming));
        slideScratchRenderer_ = std::move(incomingPrepared.renderer);
        rollbackAddedIncomingFamilies();
        return false;
    }

    slideBackBuffer_.incomingCenter = incomingPrepared.center;
    // Swap entre membros persistentes: incomingPrepared data â†’ back buffer, old back buffer â†’ scratch
    slideBackBuffer_.incomingFamilyIds.swap(incomingPrepared.lodState.faceFamilyIds);
    slideBackBuffer_.incomingFaceSlots.swap(incomingPrepared.lodState.currentFaceSlots);
    slideScratchRenderer_ = std::move(incomingPrepared.renderer);
    slideBackBuffer_.incomingResidentLodIndex =
        ResolveSegmentLodIndexByRank(incomingLogicalRank);
    slideBackBuffer_.incomingResidentBaseRank = static_cast<int8_t>(-1);

    static constexpr std::array<size_t, 3> kForwardBoundaryRanks{{3u, 8u, 13u}};
    static constexpr std::array<size_t, 3> kBackwardBoundaryRanks{{4u, 9u, 14u}};
    const int32_t dir = (slideBackBuffer_.direction < 0) ? -1 : 1;
    size_t updateCount = 0;
    const auto& boundaryRanks = (dir > 0) ? kForwardBoundaryRanks : kBackwardBoundaryRanks;
    for (size_t ri = 0; ri < boundaryRanks.size(); ++ri)
    {
        const size_t logicalRank = boundaryRanks[ri];
        if (logicalRank >= windowCount) continue;
        const int32_t segmentId = WrapSegmentIdToRange(
            nextStartId + (dir > 0 ? static_cast<int32_t>(logicalRank)
                                   : -static_cast<int32_t>(logicalRank)),
            totalSegmentCount_);
        if (segmentId <= 0)
        {
            rollbackAddedIncomingFamilies();
            return false;
        }

        SegmentRenderEntry* entry = FindWindowEntryByIdFast(segmentId);
        if (!entry || !entry->renderer || !entry->lodState.Ready())
        {
            rollbackAddedIncomingFamilies();
            return false;
        }

        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        const int16_t desiredBaseRank = entry->lodState.HasPerFaceRankOffsets()
            ? static_cast<int16_t>(logicalRank)
            : -1;
        const bool needsUpdate = entry->lodState.currentLodIndex != desiredLodIndex ||
                                 entry->lodState.currentBaseRank != desiredBaseRank ||
                                 HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                                    &entry->lodState.faceFamilyIds);
        if (!needsUpdate) continue;
        if (updateCount >= slideBackBuffer_.boundaryUpdates.size())
        {
            slideHwrTrace_.flags |= kSlideHwrTracePrepareFailBit;
            return false;
        }

        auto& update = slideBackBuffer_.boundaryUpdates[updateCount];
        update.SetActive(true);
        update.segmentId = entry->id;
        update.desiredLodIndex = desiredLodIndex;
        update.desiredBaseRank = desiredBaseRank;
        const bool prepared = entry->lodState.HasPerFaceRankOffsets()
            ? ResolvePreparedFaceSlotsForBaseRank(*entry,
                                                  logicalRank,
                                                  seg1FamilySlots_,
                                                  update.preparedFaceSlots,
                                                  /*bypassUploadBudget*/true)
            : ResolvePreparedFaceSlotsForLod(*entry,
                                             desiredLodIndex,
                                             seg1FamilySlots_,
                                             update.preparedFaceSlots,
                                             /*bypassUploadBudget*/true);
        if (!prepared ||
            HasMissingRequiredFaceTextureSlots(update.preparedFaceSlots, &entry->lodState.faceFamilyIds))
        {
            update.SetActive(false);
            update.segmentId = -1;
            update.desiredLodIndex = 0xFF;
            update.desiredBaseRank = static_cast<int8_t>(-1);
            update.preparedFaceSlots.clear();
            slideHwrTrace_.flags |= kSlideHwrTracePrepareFailBit;
            SRL::Debug::Print(1, 11, "PKG bd fail id:%d r:%u l:%u",
                              entry->id,
                              static_cast<unsigned>(logicalRank),
                              static_cast<unsigned>(desiredLodIndex));
            rollbackAddedIncomingFamilies();
            return false;
        }
        ++updateCount;
    }

    slideBackBuffer_.SetReady(true);
    LWR_PROBE_END(g_lwrStageAccum.prepareSlide);
    slideHwrTrace_.flags |= kSlideHwrTracePrepareOkBit;
    slideHwrTrace_.afterPrepare = static_cast<uint32_t>(GetHighWorkRamFreeBytesSafe());
    return true;
}

bool TrackSystem::CommitStabilizedSlideBackBuffer()
{
    LWR_PROBE_BEGIN();
    if (!slideBackBuffer_.Ready()) return false;
    if (slideBackBuffer_.dropIdx >= segmentRenderers_.size()) return false;
    if (!slideScratchRenderer_)
    {
        slideHwrTrace_.flags |= kSlideHwrTraceCommitFailBit;
        return false;
    }

    SegmentRenderEntry& slot = segmentRenderers_[slideBackBuffer_.dropIdx];
    TrackSegmentEntry& meta = segmentEntries_[slideBackBuffer_.dropIdx];
    std::swap(slot.renderer, slideScratchRenderer_);
    if (!slot.renderer)
    {
        slideHwrTrace_.flags |= kSlideHwrTraceCommitFailBit;
        return false;
    }
    if (slideScratchRenderer_)
    {
        slideScratchRenderer_->RecycleRuntimeState();
    }

    slot.id = slideBackBuffer_.incomingSegmentId;
    slot.logicalSegmentCount = 1;
    slot.center = slideBackBuffer_.incomingCenter;
    slot.lodState.SetReady(true);
    slot.lodState.SetHasPerFaceRankOffsets(false);
    // Swap entre membros persistentes: old slot capacity vai para slideBackBuffer_ (preservado via clear()),
    // incoming data vai para o slot â€” nenhum free de LWR ocorre.
    slot.lodState.faceFamilyIds.swap(slideBackBuffer_.incomingFamilyIds);
    slot.lodState.faceRankOffsets.assign(slot.lodState.faceFamilyIds.size(), 0u);
    slot.lodState.currentFaceSlots.swap(slideBackBuffer_.incomingFaceSlots);
    EnsureVectorCapacityFloor(slot.lodState.faceFamilyIds, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(slot.lodState.faceRankOffsets, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(slot.lodState.currentFaceSlots, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(slot.lodState.workingSetFamilies, static_cast<size_t>(slotFaceCapacityFloor_));
    EnsureVectorCapacityFloor(slot.lodState.workingSetLodIndices, static_cast<size_t>(slotFaceCapacityFloor_));
    EnsureVectorCapacityFloor(slot.lodState.workingSetSlots, static_cast<size_t>(slotFaceCapacityFloor_));
    slot.lodState.currentLodIndex = slideBackBuffer_.incomingResidentLodIndex;
    slot.lodState.currentBaseRank = slideBackBuffer_.incomingResidentBaseRank;
    slot.lodState.desiredLodIndex = slideBackBuffer_.incomingResidentLodIndex;
    slot.lodState.desiredBaseRank = slideBackBuffer_.incomingResidentBaseRank;
    meta.id = slideBackBuffer_.incomingSegmentId;
    ApplyActiveRendererCapacityFloor(*slot.renderer);
    (void)slot.renderer->ApplyFaceTextureSlotsGlobal(slot.lodState.currentFaceSlots);
    ++runtimeFaceRemapsThisFrame_;
    InvalidateEntryWorkingSetCache(slot);

    InvalidateActiveWindowLookupTables();
    activeWindowStartId_ = slideBackBuffer_.nextStartId;
    if (!AdvanceWindowHeadByDirection(slideBackBuffer_.direction, segmentRenderers_.size()))
    {
        (void)ResolveWindowHeadByStartId(slideBackBuffer_.dropIdx);
    }

    for (auto& update : slideBackBuffer_.boundaryUpdates)
    {
        if (!update.Active()) continue;
        SegmentRenderEntry* entry = FindWindowEntryByIdFast(update.segmentId);
        if (!entry) continue;
        // Swap entre membros persistentes â€” nenhum free de LWR ocorre
        entry->lodState.currentFaceSlots.swap(update.preparedFaceSlots);
        entry->lodState.currentLodIndex = update.desiredLodIndex;
        entry->lodState.currentBaseRank = update.desiredBaseRank;
        entry->lodState.desiredLodIndex = update.desiredLodIndex;
        entry->lodState.desiredBaseRank = update.desiredBaseRank;
        EnsureVectorCapacityFloor(entry->lodState.workingSetFamilies, static_cast<size_t>(slotFaceCapacityFloor_));
        EnsureVectorCapacityFloor(entry->lodState.workingSetLodIndices, static_cast<size_t>(slotFaceCapacityFloor_));
        EnsureVectorCapacityFloor(entry->lodState.workingSetSlots, static_cast<size_t>(slotFaceCapacityFloor_));
        (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
        ++runtimeFaceRemapsThisFrame_;
        ++runtimeLodSegmentUpdatesThisFrame_;
        InvalidateEntryWorkingSetCache(*entry);
    }
    // === POST-SLIDE COST INSTRUMENTATION (Passo C do DUAL_SH2_OPTIMIZATION_PLAN) ===
    // MediÃ§Ã£o de ticks SH2 para as fases mais pesadas do commit.
    // Remover ou desabilitar quando o diagnÃ³stico estiver concluÃ­do.
    const uint16_t lodUpdateStart = Sh2FrtProfiler::Now();
    UpdateDesiredStabilizedWindowLodTargets();
    const uint16_t lodUpdateTicks = Sh2FrtProfiler::Elapsed(lodUpdateStart, Sh2FrtProfiler::Now());

    uint16_t mergeTicks = 0u;
    if (familyMergeCooldown_ == 0u)
    {
        const uint16_t mergeStart = Sh2FrtProfiler::Now();
        MergeCurrentWindowFamilies();
        mergeTicks = Sh2FrtProfiler::Elapsed(mergeStart, Sh2FrtProfiler::Now());
        familyMergeCooldown_ = ResolveFamilyMergeCooldownFrames(FullTrackFamilyCacheReady());
    }
    else
    {
        --familyMergeCooldown_;
    }
    SetFamilyWorkingSetDirty(true);
    const uint16_t handleStart = Sh2FrtProfiler::Now();
    BuildSegmentHandleTable();
    const uint16_t handleTicks = Sh2FrtProfiler::Elapsed(handleStart, Sh2FrtProfiler::Now());

    SRL::Debug::Print(1, 31, "SL tks lod:%u mrg:%u hdl:%u sid:%d",
                      static_cast<unsigned>(lodUpdateTicks),
                      static_cast<unsigned>(mergeTicks),
                      static_cast<unsigned>(handleTicks),
                      static_cast<int>(slideBackBuffer_.incomingSegmentId));
    // === FIM INSTRUMENTAÃ‡ÃƒO ===
    ResetSlidePrefetchState();
    ResetSlideBackBuffer();
    ++runtimeSlidesThisFrame_;
    {
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        LWR_PROBE_END(g_lwrStageAccum.commitSlide);
        slideHwrTrace_.flags |= kSlideHwrTraceCommitOkBit;
        slideHwrTrace_.afterCommit = static_cast<uint32_t>(freeBytes);
        const uint8_t cooldown =
            (!freeValid || freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u))) ? 4u :
            (freeBytes <= (kWorkRamHardFloorBytes + (32u * 1024u))) ? 2u :
            1u;
        pendingLodFrameCooldown_ = std::max<uint8_t>(pendingLodFrameCooldown_, cooldown);
    }
    return true;
}

bool TrackSystem::BuildSegmentIntoPrefetch(int32_t segmentId, bool allowSlotWarmup)
{
    LWR_PROBE_BEGIN();
    if (segmentId <= 0) return false;

    if (kEnableTrackRuntimeStabilization)
    {
        (void)allowSlotWarmup;
        const bool prefetchMetadataReady =
            slidePrefetchSegmentId_ == segmentId &&
            !slidePrefetchFamilyIds_.empty();
        // Retornar true somente quando metadata E renderer estiverem prontos
        if (prefetchMetadataReady && SlidePrefetchRendererReady())
        {
            return true;
        }

        if (slidePrefetchSegmentId_ > 0 && slidePrefetchSegmentId_ != segmentId)
        {
            ResetSlidePrefetchState();
        }

        if (prefetchBuildAttemptsThisFrame_ >= prefetchBuildBudgetThisFrame_)
        {
            if (prefetchBuildBudgetDropsThisFrame_ < std::numeric_limits<uint8_t>::max())
            {
                ++prefetchBuildBudgetDropsThisFrame_;
            }
            return false;
        }
        ++prefetchBuildAttemptsThisFrame_;

        // Fase 1: carregar metadata de famÃ­lia (reutilizar scratch â€” sem alloc LWR)
        if (!prefetchMetadataReady)
        {
            slideScratchEntry_.lodState.faceFamilyIds.clear();
            if (!LoadRuntimeFamilyIdsForSegment(segmentId, slideScratchEntry_.lodState.faceFamilyIds)
                || slideScratchEntry_.lodState.faceFamilyIds.empty())
            {
                return false;
            }
            slidePrefetchSegmentId_ = segmentId;
            if (segmentId > 0 &&
                static_cast<size_t>(segmentId) <= segmentCenterCatalog_.size())
            {
                slidePrefetchCenter_ = segmentCenterCatalog_[static_cast<size_t>(segmentId - 1)];
            }
            else
            {
                slidePrefetchCenter_ = Vector3D(0.0, 0.0, 0.0);
            }
            slidePrefetchFamilyIds_.assign(slideScratchEntry_.lodState.faceFamilyIds.begin(),
                                           slideScratchEntry_.lodState.faceFamilyIds.end());
            slidePrefetchFaceSlots_.assign(slidePrefetchFamilyIds_.size(), -1);
            EnsureVectorCapacityFloor(slidePrefetchFamilyIds_, slotFaceCapacityFloor_);
            EnsureVectorCapacityFloor(slidePrefetchFaceSlots_, slotFaceCapacityFloor_);
            SetSlidePrefetchRendererReady(false);
            SetSlidePrefetchLodReady(false);
        }

        // Fase 2: prÃ©-construir renderer no scratch para eliminar build sÃ­ncrono no frame do slide
        if (!SlidePrefetchRendererReady())
        {
            if (!slideScratchRenderer_)
                slideScratchRenderer_ = MakeTrackObjectUnique<TrackRenderer, SRL::Memory::Zone::LWRam>();
            if (slideScratchRenderer_)
            {
                ApplyActiveRendererCapacityFloor(*slideScratchRenderer_);
                slideScratchEntry_.lodState.faceFamilyIds.clear();
                Vector3D buildCenter{};
                if (BuildSegmentIntoRenderer(segmentId, *slideScratchRenderer_, buildCenter,
                                             slideScratchEntry_.lodState.faceFamilyIds))
                {
                    slidePrefetchCenter_ = buildCenter;
                    slidePrefetchFamilyIds_.assign(slideScratchEntry_.lodState.faceFamilyIds.begin(),
                                                   slideScratchEntry_.lodState.faceFamilyIds.end());
                    EnsureVectorCapacityFloor(slidePrefetchFamilyIds_, slotFaceCapacityFloor_);
                    SetSlidePrefetchRendererReady(true);
                    SetSlidePrefetchLodReady(false);
                }
            }
        }
        return slidePrefetchSegmentId_ == segmentId &&
               !slidePrefetchFamilyIds_.empty();
    }

    const bool needsRendererBuild =
        (slidePrefetchSegmentId_ != segmentId) ||
        !slidePrefetchRenderer_ ||
        slidePrefetchFamilyIds_.empty();

    if (needsRendererBuild)
    {
        if (slidePrefetchSegmentId_ > 0 && slidePrefetchSegmentId_ != segmentId)
        {
            ResetSlidePrefetchState();
        }
        if (prefetchBuildAttemptsThisFrame_ >= prefetchBuildBudgetThisFrame_)
        {
            if (prefetchBuildBudgetDropsThisFrame_ < std::numeric_limits<uint8_t>::max())
            {
                ++prefetchBuildBudgetDropsThisFrame_;
            }
            return false;
        }
        ++prefetchBuildAttemptsThisFrame_;
        if (!slidePrefetchRenderer_)
        {
            if (slideScratchRenderer_)
            {
                slidePrefetchRenderer_ = std::move(slideScratchRenderer_);
            }
            else
            {
                slidePrefetchRenderer_ = MakeTrackObjectUnique<TrackRenderer, SRL::Memory::Zone::LWRam>();
            }
        }
        if (!slidePrefetchRenderer_) return false;
        ApplyActiveRendererCapacityFloor(*slidePrefetchRenderer_);

        Vector3D center(0.0, 0.0, 0.0);
        bool usedRdr = false;
        slidePrefetchFamilyIds_.clear();
        if (!BuildRendererFromRuntimeBlob(segmentId,
                                          *slidePrefetchRenderer_,
                                          &center,
                                          &slidePrefetchFamilyIds_,
                                          &usedRdr) ||
            slidePrefetchFamilyIds_.empty())
        {
            ResetSlidePrefetchState();
            return false;
        }
        if (usedRdr) ++runtimeRdrBuildsThisFrame_;
        else ++runtimeSdrBuildsThisFrame_;

        ConfigureStreamedRendererDefaults(*slidePrefetchRenderer_);
        slidePrefetchSegmentId_ = segmentId;
        slidePrefetchCenter_ = center;
        slidePrefetchFaceSlots_.assign(slidePrefetchFamilyIds_.size(), -1);
        EnsureVectorCapacityFloor(slidePrefetchFamilyIds_, slotFaceCapacityFloor_);
        EnsureVectorCapacityFloor(slidePrefetchFaceSlots_, slotFaceCapacityFloor_);
        SetSlidePrefetchLodReady(false);
    }

    if (!slidePrefetchRenderer_ || slidePrefetchFamilyIds_.empty()) return false;

    if (!allowSlotWarmup)
    {
        SetFamilyWorkingSetDirty(true);
        return slidePrefetchRenderer_ &&
               slidePrefetchSegmentId_ == segmentId &&
               !slidePrefetchFamilyIds_.empty();
    }

    bool addedFamily = false;
    for (size_t fi = 0; fi < slidePrefetchFamilyIds_.size(); ++fi)
    {
        const uint16_t fam = slidePrefetchFamilyIds_[fi];
        if (fam == 0) continue;
        if (FindFamilySlot(seg1FamilySlots_, fam)) continue;
        Seg1FamilySlotEntry slotEntry{};
        slotEntry.familyId = fam;
        slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        seg1FamilySlots_.push_back(slotEntry);
        addedFamily = true;
    }
    if (addedFamily) InvalidateFamilySlotIndex();

    SegmentRenderEntry prefetched{};
    prefetched.id = segmentId;
    prefetched.logicalSegmentCount = 1;
    prefetched.renderer = std::move(slidePrefetchRenderer_);
    prefetched.center = slidePrefetchCenter_;
    prefetched.lodState.SetReady(true);
    prefetched.lodState.SetHasPerFaceRankOffsets(false);
    prefetched.lodState.currentLodIndex = 0xFF;
    prefetched.lodState.currentBaseRank = -1;
    prefetched.lodState.faceFamilyIds.swap(slidePrefetchFamilyIds_);
    prefetched.lodState.currentFaceSlots.swap(slidePrefetchFaceSlots_);

    // Same rule for the non-stabilized prefetch path: keep it budgeted and
    // let the slide boundary use the emergency synchronous warmup if needed.
    const bool rebuilt = RebuildSegmentFaceSlotsForLod(prefetched,
                                                       kTrackLod32Index,
                                                       seg1FamilySlots_,
                                                       /*bypassUploadBudget*/false);
    if (rebuilt)
    {
        (void)prefetched.renderer->ApplyFaceTextureSlotsGlobal(prefetched.lodState.currentFaceSlots);
        ++runtimeFaceRemapsThisFrame_;
    }

    slidePrefetchRenderer_ = std::move(prefetched.renderer);
    slidePrefetchFamilyIds_.swap(prefetched.lodState.faceFamilyIds);
    slidePrefetchFaceSlots_.swap(prefetched.lodState.currentFaceSlots);
    EnsureVectorCapacityFloor(slidePrefetchFamilyIds_, slotFaceCapacityFloor_);
    EnsureVectorCapacityFloor(slidePrefetchFaceSlots_, slotFaceCapacityFloor_);
    SetSlidePrefetchLodReady(rebuilt && !HasMissingRequiredFaceTextureSlots(slidePrefetchFaceSlots_, &slidePrefetchFamilyIds_));
    SetFamilyWorkingSetDirty(true);
    LWR_PROBE_END(g_lwrStageAccum.buildPrefetch);
    return rebuilt;
}

bool TrackSystem::BuildSegmentIntoRenderer(int32_t segmentId,
                                           TrackRenderer& renderer,
                                           Vector3D& outCenter,
                                           FamilyIdVector& outFamilyIds)
{
    if (segmentId <= 0) return false;

    outFamilyIds.clear();
    bool usedRdr = false;
    if (!BuildRendererFromRuntimeBlob(segmentId, renderer, &outCenter, &outFamilyIds, &usedRdr)) return false;
    if (outFamilyIds.empty()) return false;
    if (usedRdr) ++runtimeRdrBuildsThisFrame_;
    else ++runtimeSdrBuildsThisFrame_;
    ConfigureStreamedRendererDefaults(renderer);
    return true;
}

bool TrackSystem::BuildSegmentIntoSlideScratch(int32_t segmentId,
                                               Vector3D& outCenter,
                                               FamilyIdVector& outFamilyIds)
{
    if (segmentId <= 0) return false;
    // Se o renderer foi prÃ©-construÃ­do pelo prefetch para este segmento, reutilizÃ¡-lo diretamente.
    // O slideScratchRenderer_ jÃ¡ contÃ©m a geometria; apenas copiar os family IDs do cache.
    if (kEnableTrackRuntimeStabilization &&
        SlidePrefetchRendererReady() &&
        slidePrefetchSegmentId_ == segmentId &&
        slideScratchRenderer_)
    {
        outCenter = slidePrefetchCenter_;
        outFamilyIds.assign(slidePrefetchFamilyIds_.begin(), slidePrefetchFamilyIds_.end());
        // SlidePrefetchRendererReady() serÃ¡ limpo em ResetSlidePrefetchState() apÃ³s o slide
        return true;
    }
    // Build sÃ­ncrono (fallback: prefetch ainda nÃ£o construiu o renderer)
    if (!slideScratchRenderer_) slideScratchRenderer_ = MakeTrackObjectUnique<TrackRenderer, SRL::Memory::Zone::LWRam>();
    if (slideScratchRenderer_)
    {
        ApplyActiveRendererCapacityFloor(*slideScratchRenderer_);
    }
    if (!slideScratchRenderer_) return false;
    if (!BuildSegmentIntoRenderer(segmentId, *slideScratchRenderer_, outCenter, outFamilyIds)) return false;
    return true;
}

void TrackSystem::PrimeRuntimeScratchCapacities()
{
    static TrackRuntimePackCache sTrackScratchPackCache{};
    const char* packCandidates[] = {
        "/CD/DATA/TRKRDR.BIN",
        "/CD/DATA/TRKRDR.BIN;1",
        "/DATA/TRKRDR.BIN",
        "/DATA/TRKRDR.BIN;1",
        "CD/DATA/TRKRDR.BIN",
        "CD/DATA/TRKRDR.BIN;1",
        "DATA/TRKRDR.BIN",
        "DATA/TRKRDR.BIN;1",
        "cd/data/TRKRDR.BIN",
        "cd/data/TRKRDR.BIN;1",
        "cd/data/trkrdr.bin",
        "cd/data/trkrdr.bin;1",
        "data/TRKRDR.BIN",
        "data/TRKRDR.BIN;1",
        "data/trkrdr.bin",
        "data/trkrdr.bin;1",
        "TRKRDR.BIN",
        "TRKRDR.BIN;1",
        "trkrdr.bin",
        "trkrdr.bin;1"
    };
    if (!LoadTrackRuntimePackToCart(packCandidates,
                                    sizeof(packCandidates) / sizeof(packCandidates[0]),
                                    sTrackScratchPackCache))
    {
        return;
    }

    const size_t maxFaces = std::max<size_t>(1u, static_cast<size_t>(sTrackScratchPackCache.view.header.maxFaceCount));
    const size_t maxVerts = std::max<size_t>(1u, static_cast<size_t>(sTrackScratchPackCache.view.header.maxVertexCount));
    const size_t maxFamilies = std::max<size_t>(1u, static_cast<size_t>(sTrackScratchPackCache.view.header.maxFamilyCount));
    SRL::Debug::Print(1, 31, "TRK prm mxF:%u mxV:%u mxFam:%u",
                      static_cast<unsigned>(maxFaces),
                      static_cast<unsigned>(maxVerts),
                      static_cast<unsigned>(maxFamilies));
    // In fixed64 test mode the pack contains only 64x64 geometry; the hard floor
    // cap can be set lower than the full-production 768 ceiling.  Outlier segments
    // that exceed this cap are handled by the RecycleRuntimeState compact (capacity >
    // 2Ã—floor triggers a swap-to-floor), so we trade a rare one-time compact for
    // substantially lower steady-state LWR retention.
    // Calibration: set kFixed64FaceCapFloor / kFixed64VertCapFloor to
    //   maxFaces_printed_above * 1.2  (round up to nearest power of two).
    static constexpr size_t kFixed64FaceCapFloor = 256u;
    static constexpr size_t kFixed64VertCapFloor = 256u;
    // Family catalog is much smaller for 64x64-only packs.
    static constexpr size_t kFixed64FamilyFloorMin = 64u;
    const size_t familyFloorMin = kEnableTrackLeakIsolationFixed64Pipeline
        ? kFixed64FamilyFloorMin
        : 256u;
    const size_t familyFloor = std::max<size_t>(familyFloorMin, maxFamilies + 16u);
    familySlotCapacityFloor_ = static_cast<uint16_t>(std::min<size_t>(familyFloor, 0xFFFFu));
    const size_t effectiveFaceCap = kEnableTrackLeakIsolationFixed64Pipeline
        ? kFixed64FaceCapFloor
        : kStabilizedFaceCapacityFloorCap;
    const size_t effectiveVertCap = kEnableTrackLeakIsolationFixed64Pipeline
        ? kFixed64VertCapFloor
        : kStabilizedVertexCapacityFloorCap;
    const size_t faceReserveFloor =
        kEnableTrackRuntimeStabilization
            ? std::min<size_t>(maxFaces, effectiveFaceCap)
            : maxFaces;
    const size_t vertReserveFloor =
        kEnableTrackRuntimeStabilization
            ? std::min<size_t>(maxVerts, effectiveVertCap)
            : maxVerts;
    SRL::Debug::Print(1, 31, "TRK prm fF:%u vF:%u famF:%u",
                      static_cast<unsigned>(faceReserveFloor),
                      static_cast<unsigned>(vertReserveFloor),
                      static_cast<unsigned>(familyFloor));
    if (kEnableTrackRuntimeStabilization)
    {
        // Keep fixed floors in stabilized mode to avoid progressive growth and
        // reallocation churn while sliding through heavier sections.
        const size_t faceFloor = faceReserveFloor;
        const size_t vertFloor = vertReserveFloor;
        slotFaceCapacityFloor_ = static_cast<uint16_t>(std::min<size_t>(faceFloor, 0xFFFFu));
        rendererVertexCapacityFloor_ = static_cast<uint16_t>(std::min<size_t>(vertFloor, 0xFFFFu));
        rendererFaceCapacityFloor_ = static_cast<uint16_t>(std::min<size_t>(faceFloor, 0xFFFFu));
    }
    else
    {
        slotFaceCapacityFloor_ = static_cast<uint16_t>(std::min<size_t>(maxFaces, 0xFFFFu));
        rendererVertexCapacityFloor_ = static_cast<uint16_t>(std::min<size_t>(maxVerts, 0xFFFFu));
        rendererFaceCapacityFloor_ = static_cast<uint16_t>(std::min<size_t>(maxFaces, 0xFFFFu));
    }

    if (!kEnableTrackRuntimeStabilization)
    {
        if (g_rdrBuildScratch.verts.capacity() < vertReserveFloor) g_rdrBuildScratch.verts.reserve(vertReserveFloor);
        if (g_rdrBuildScratch.faces.capacity() < faceReserveFloor) g_rdrBuildScratch.faces.reserve(faceReserveFloor);
        if (g_rdrBuildScratch.attrs.capacity() < faceReserveFloor) g_rdrBuildScratch.attrs.reserve(faceReserveFloor);
    }
    else
    {
        // In stabilized mode reserve a bounded floor once and keep it.
        const size_t reserveVerts = vertReserveFloor;
        const size_t reserveFaces = faceReserveFloor;
        if (g_rdrBuildScratch.verts.capacity() < reserveVerts) g_rdrBuildScratch.verts.reserve(reserveVerts);
        if (g_rdrBuildScratch.faces.capacity() < reserveFaces) g_rdrBuildScratch.faces.reserve(reserveFaces);
        if (g_rdrBuildScratch.attrs.capacity() < reserveFaces) g_rdrBuildScratch.attrs.reserve(reserveFaces);
    }
    if (slideIncomingFamilyIdsScratch_.capacity() < faceReserveFloor) slideIncomingFamilyIdsScratch_.reserve(faceReserveFloor);
    if (slideIncomingFaceRankOffsetsScratch_.capacity() < faceReserveFloor) slideIncomingFaceRankOffsetsScratch_.reserve(faceReserveFloor);
    if (slideIncomingFaceSlotsScratch_.capacity() < faceReserveFloor) slideIncomingFaceSlotsScratch_.reserve(faceReserveFloor);
    if (slidePrefetchFamilyIds_.capacity() < faceReserveFloor) slidePrefetchFamilyIds_.reserve(faceReserveFloor);
    if (slidePrefetchFaceSlots_.capacity() < faceReserveFloor) slidePrefetchFaceSlots_.reserve(faceReserveFloor);
    if (seg1FamilySlots_.capacity() < familyFloor) seg1FamilySlots_.reserve(familyFloor);
    if (familyMergeCurrentWindowScratch_.capacity() < familyFloor) familyMergeCurrentWindowScratch_.reserve(familyFloor);
    if (slidePrefetchFamilySlotsScratch_.capacity() < familyFloor) slidePrefetchFamilySlotsScratch_.reserve(familyFloor);
    EnsureTrackTextureSlotQueueCapacityFloor(std::min<size_t>(static_cast<size_t>(SRL_MAX_TEXTURES), 256u));
    const size_t segmentCap = std::max<size_t>(1u, segmentRenderers_.size());
    if (stabilizedDepthItemsScratch_.capacity() < segmentCap)
    {
        stabilizedDepthItemsScratch_.reserve(segmentCap);
    }
    if (stabilizedSortedHandlesScratch_.capacity() < segmentCap)
    {
        stabilizedSortedHandlesScratch_.reserve(segmentCap);
    }
    if (stabilizedProducerInputScratch_.capacity() < segmentCap)
    {
        stabilizedProducerInputScratch_.reserve(segmentCap);
    }
    for (auto& entry : segmentRenderers_)
    {
        EnsureVectorCapacityFloor(entry.lodState.faceFamilyIds, faceReserveFloor);
        EnsureVectorCapacityFloor(entry.lodState.faceRankOffsets, faceReserveFloor);
        EnsureVectorCapacityFloor(entry.lodState.currentFaceSlots, faceReserveFloor);
        EnsureVectorCapacityFloor(entry.lodState.workingSetFamilies, faceReserveFloor);
        EnsureVectorCapacityFloor(entry.lodState.workingSetLodIndices, faceReserveFloor);
        EnsureVectorCapacityFloor(entry.lodState.workingSetSlots, faceReserveFloor);
        if (entry.renderer)
        {
            ApplyActiveRendererCapacityFloor(*entry.renderer);
        }
    }
    if (slideScratchRenderer_)
    {
        ApplyActiveRendererCapacityFloor(*slideScratchRenderer_);
    }
    if (slidePrefetchRenderer_)
    {
        ApplyActiveRendererCapacityFloor(*slidePrefetchRenderer_);
    }
    // Prime scratch vectors not covered by the segmentRenderers_ loop:
    // runtimeRenderFaceSlotsScratch_ grows lazily on first render per entry â€”
    // pre-floor it to avoid one-time alloc during the first rendered frame.
    if (runtimeRenderFaceSlotsScratch_.capacity() < faceReserveFloor)
        runtimeRenderFaceSlotsScratch_.reserve(faceReserveFloor);
    if (slideRollbackFaceSlotsScratch_.capacity() < faceReserveFloor)
        slideRollbackFaceSlotsScratch_.reserve(faceReserveFloor);
    // slideScratchEntry_ is a persistent entry used as staging â€” give it the
    // same floor as all segmentRenderers_ entries.
    EnsureVectorCapacityFloor(slideScratchEntry_.lodState.faceFamilyIds, faceReserveFloor);
    EnsureVectorCapacityFloor(slideScratchEntry_.lodState.faceRankOffsets, faceReserveFloor);
    EnsureVectorCapacityFloor(slideScratchEntry_.lodState.currentFaceSlots, faceReserveFloor);
    EnsureVectorCapacityFloor(slideScratchEntry_.lodState.workingSetFamilies, faceReserveFloor);
    EnsureVectorCapacityFloor(slideScratchEntry_.lodState.workingSetLodIndices, faceReserveFloor);
    EnsureVectorCapacityFloor(slideScratchEntry_.lodState.workingSetSlots, faceReserveFloor);
    // slideScratchBoundarySlots_ entries grow on first boundary prepare â€” floor them.
    for (auto& s : slideScratchBoundarySlots_)
    {
        if (s.capacity() < faceReserveFloor) s.reserve(faceReserveFloor);
    }
}

void TrackSystem::ApplyActiveRendererCapacityFloor(TrackRenderer& renderer)
{
    if (!kEnableTrackRuntimeStabilization) return;
    renderer.SetRuntimeCapacityFloor(static_cast<size_t>(rendererVertexCapacityFloor_),
                                     static_cast<size_t>(rendererFaceCapacityFloor_));
}

void TrackSystem::TryPrefetchUpcomingSegment()
{
    if (!SegmentsReady() || segmentRenderers_.empty() || totalSegmentCount_ == 0) return;

    const size_t windowCount = segmentRenderers_.size();
    const int32_t nextId = (windowDirection_ > 0)
        ? WrapSegmentIdToRange(activeWindowStartId_ + static_cast<int32_t>(windowCount),
                               totalSegmentCount_)
        : WrapSegmentIdToRange(activeWindowStartId_ - static_cast<int32_t>(windowCount),
                               totalSegmentCount_);
    if (nextId <= 0) return;
    const bool speedTier1 =
        PrefetchSpeedProxyValid() &&
        prefetchSpeedProxyRaw_ >= kPrefetchSpeedTier1UnitsPerFrame;
    const bool speedTier2 =
        PrefetchSpeedProxyValid() &&
        prefetchSpeedProxyRaw_ >= kPrefetchSpeedTier2UnitsPerFrame;

    if (kEnableTrackRuntimeStabilization)
    {
        if (slidePrefetchSegmentId_ == nextId && !slidePrefetchFamilyIds_.empty())
        {
            prefetchRetryCooldown_ = 0u;
            return;
        }
        if (speedTier2 && prefetchRetryCooldown_ > 1u)
        {
            prefetchRetryCooldown_ = 1u;
        }
        if (prefetchRetryCooldown_ > 0u)
        {
            --prefetchRetryCooldown_;
            return;
        }
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        const size_t floorBytes = 2u * 1024u;
        if (freeValid && freeBytes <= floorBytes)
        {
            prefetchRetryCooldown_ = speedTier2 ? 0u : 1u;
            return;
        }
        if (!BuildSegmentIntoPrefetch(nextId, false))
        {
            prefetchRetryCooldown_ = speedTier2 ? 0u : 1u;
            return;
        }
        // Reverse traversal suffers more from late prefetch misses.
        // Prime one extra segment in reverse direction when possible.
        if (windowDirection_ < 0 && speedTier1)
        {
            const int32_t reverseExtraId =
                WrapSegmentIdToRange(nextId - 1, totalSegmentCount_);
            if (reverseExtraId > 0 && reverseExtraId != nextId)
            {
                (void)BuildSegmentIntoPrefetch(reverseExtraId, false);
            }
        }
        prefetchRetryCooldown_ = 0u;
        return;
    }

    const bool prefetchRendererResident = static_cast<bool>(slidePrefetchRenderer_);
    if (slidePrefetchSegmentId_ == nextId && !slidePrefetchFamilyIds_.empty())
    {
        if (prefetchRendererResident &&
            SlidePrefetchLodReady() &&
            slidePrefetchFaceSlots_.size() == slidePrefetchFamilyIds_.size() &&
            !HasMissingRequiredFaceTextureSlots(slidePrefetchFaceSlots_, &slidePrefetchFamilyIds_))
        {
            prefetchRetryCooldown_ = 0u;
            return;
        }
        const bool uploadBudgetExhausted =
            textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame();
        bool retryFreeValid = false;
        const size_t retryFreeBytes = GetHighWorkRamFreeBytesSafe(&retryFreeValid);
        const bool lowRetryHeadroom =
            retryFreeValid &&
            retryFreeBytes <= (kWorkRamHardFloorBytes + (24u * 1024u));
        if (uploadBudgetExhausted || lowRetryHeadroom)
        {
            prefetchRetryCooldown_ = std::max<uint8_t>(
                prefetchRetryCooldown_,
                lowRetryHeadroom ? (speedTier2 ? 2u : 4u) : (speedTier1 ? 1u : 2u));
        }
        if (prefetchRetryCooldown_ > 0u)
        {
            --prefetchRetryCooldown_;
            return;
        }
        (void)BuildSegmentIntoPrefetch(nextId, false);
        const bool readyNow =
            SlidePrefetchLodReady() &&
            slidePrefetchFaceSlots_.size() == slidePrefetchFamilyIds_.size() &&
            !HasMissingRequiredFaceTextureSlots(slidePrefetchFaceSlots_, &slidePrefetchFamilyIds_);
        prefetchRetryCooldown_ = readyNow ? 0u : 1u;
        return;
    }

    prefetchRetryCooldown_ = 0u;

    bool freeValid = false;
    const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    // In safe mode the previous threshold effectively disabled prefetch for the
    // whole lap. The runtime now lives far below the conservative planning floor,
    // so waiting for +48 KB of HWR headroom meant the next tail was always built
    // at the slide boundary instead of ahead of time.
    const size_t floorBytes = (slideScratchRenderer_
        ? (kWorkRamHardFloorBytes + (12u * 1024u))
        : (kWorkRamHardFloorBytes + (32u * 1024u)));
    if (freeValid && freeBytes <= floorBytes)
    {
        TrimRuntimeBlobScratchCaches(true);
        bool freeValidAfter = false;
        const size_t freeAfter = GetHighWorkRamFreeBytesSafe(&freeValidAfter);
        if (freeValidAfter && freeAfter <= floorBytes) return;
    }

    if (!BuildSegmentIntoPrefetch(nextId, false))
    {
        TrackLowWorkU16Vector familyIds{};
        if (!LoadRuntimeFamilyIdsForSegment(nextId, familyIds) || familyIds.empty()) return;
        slidePrefetchSegmentId_ = nextId;
        slidePrefetchCenter_ = Vector3D(0.0, 0.0, 0.0);
        slidePrefetchFamilyIds_ = std::move(familyIds);
        slidePrefetchFaceSlots_.clear();
        slidePrefetchRenderer_.reset();
        SetSlidePrefetchLodReady(false);
    }
}

void TrackSystem::CaptureTrackTextureHeapBase()
{
    trackTextureHeapBase_ = SRL::VDP1::GetTextureCount();
    SetTrackTextureHeapBaseValid(true);
}

void TrackSystem::RebaseTrackTextureHeapBase()
{
    CaptureTrackTextureHeapBase();
}

bool TrackSystem::RebuildTrackTextureResidencyForWindow(bool forceStrongReset)
{
    if (segmentRenderers_.empty())
    {
        return false;
    }
    if (!TrackTextureHeapBaseValid())
    {
        CaptureTrackTextureHeapBase();
    }
    if (!TrackTextureHeapBaseValid())
    {
        return false;
    }
    MergeCurrentWindowFamilies();
    if (seg1FamilySlots_.empty())
    {
        if (!BuildTrackFamilyLodSlots(seg1FamilySlots_))
        {
            return false;
        }
        InvalidateFamilySlotIndex();
    }

    RebuildActiveWindowLookupTables();

    const bool savedReady = ReadyFlag();
    const uint8_t savedTextureUploads = textureUploadsThisFrame_;
    SetReadyFlag(false);
    textureUploadsThisFrame_ = 0;

    const bool forceStrongResidencyReset =
        forceStrongReset ||
        !kEnableTrackRuntimeStabilization ||
        (kEnableTrackRuntimeStabilization && kEnableTrackLodBandsInStabilization);
    if (forceStrongResidencyReset)
    {
        SRL::VDP1::ResetTextureHeap(trackTextureHeapBase_);
        ReleaseTrackedPaletteBanks();
        ResetReusableTrackTextureSlots();
        for (auto& family : seg1FamilySlots_)
        {
            family.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
            family.workingRefs = { 0, 0, 0, 0 };
            family.unusedFrames = { 0, 0, 0, 0 };
        }
    }

    bool structuralFailure = false;
    bool remapFailure = false;
    int32_t firstFailedSegmentId = -1;
    uint8_t firstFailedLodIndex = 0u;
    size_t logicalRank = 0;
    for (size_t rank = 0; rank < segmentRenderers_.size(); ++rank)
    {
        const int32_t segmentId = WrapSegmentIdToRange(
            activeWindowStartId_ + (windowDirection_ >= 0
                ? static_cast<int32_t>(rank)
                : -static_cast<int32_t>(rank)),
            totalSegmentCount_);
        if (segmentId <= 0)
        {
            structuralFailure = true;
            break;
        }

        SegmentRenderEntry* entry = FindWindowEntryByIdFast(segmentId);
        if (!entry || !entry->renderer || !entry->lodState.Ready() || entry->lodState.faceFamilyIds.empty())
        {
            structuralFailure = true;
            break;
        }

        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        const int16_t desiredBaseRank = entry->lodState.HasPerFaceRankOffsets()
            ? static_cast<int16_t>(logicalRank)
            : -1;
        CopyFaceSlotsToScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
        const uint8_t previousLodIndex = entry->lodState.currentLodIndex;
        const int16_t previousBaseRank = entry->lodState.currentBaseRank;
        const bool remapOk = !entry->lodState.HasPerFaceRankOffsets()
            ? RebuildSegmentFaceSlotsForLod(*entry, desiredLodIndex, seg1FamilySlots_)
            : RebuildSegmentFaceSlotsForBaseRank(*entry, logicalRank, seg1FamilySlots_);
        if (!remapOk ||
            HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                               &entry->lodState.faceFamilyIds))
        {
            RestoreFaceSlotsFromScratch(entry->lodState.currentFaceSlots, runtimeRenderFaceSlotsScratch_);
            entry->lodState.currentLodIndex = previousLodIndex;
            entry->lodState.currentBaseRank = previousBaseRank;
            if (!remapFailure)
            {
                remapFailure = true;
                firstFailedSegmentId = segmentId;
                firstFailedLodIndex = desiredLodIndex;
            }
            logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
            continue;
        }

        if (forceStrongResidencyReset)
        {
            (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
        }
        else
        {
            (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
        }
        ++runtimeFaceRemapsThisFrame_;
        ++runtimeLodSegmentUpdatesThisFrame_;
        entry->lodState.currentLodIndex = desiredLodIndex;
        entry->lodState.currentBaseRank = static_cast<int8_t>(desiredBaseRank);
        entry->lodState.desiredLodIndex = desiredLodIndex;
        entry->lodState.desiredBaseRank = static_cast<int8_t>(desiredBaseRank);
        InvalidateEntryWorkingSetCache(*entry);
        logicalRank += std::max<size_t>(1, static_cast<size_t>(entry->logicalSegmentCount));
    }

    SetReadyFlag(savedReady);
    textureUploadsThisFrame_ = savedTextureUploads;

    if (structuralFailure)
    {
        return false;
    }

    if (remapFailure)
    {
        SRL::Debug::Print(1, 22, "TRK lod fail id:%d l:%u",
                          firstFailedSegmentId,
                          static_cast<unsigned>(firstFailedLodIndex));
    }

    if (forceStrongResidencyReset)
    {
        ++trackTextureRecycleCount_;
    }
    SetFamilyWorkingSetDirty(true);
    return true;
}

bool TrackSystem::ShouldRecycleTrackTextureHeap() const
{
    if (kEnableTrackRuntimeStabilization) return false;
    if (!TrackTextureHeapBaseValid()) return false;
    const uint16_t texCount = SRL::VDP1::GetTextureCount();
    if (texCount <= trackTextureHeapBase_) return false;

    const uint16_t trackUsed = static_cast<uint16_t>(texCount - trackTextureHeapBase_);
    // Recycle only by VDP1 texture pressure. Tying this to HWR free bytes causes
    // unnecessary texture thrash and visible "red faces" while slots are repopulated.
    constexpr uint16_t kTrackTexBudgetBeforeRecycle = 920u;
    constexpr uint16_t kHeapGuardSlots = 24u;
    if (texCount >= static_cast<uint16_t>(SRL_MAX_TEXTURES - kHeapGuardSlots)) return true;
    return trackUsed >= kTrackTexBudgetBeforeRecycle;
}

bool TrackSystem::ShouldCompactTrackTextureHeapInStabilization() const
{
    if (!kEnableTrackRuntimeStabilization) return false;
    if (!TrackTextureHeapBaseValid()) return false;
    if (textureHeapCompactCooldown_ != 0u) return false;
    if (slideBackBuffer_.Ready()) return false;

    const uint16_t texCount = SRL::VDP1::GetTextureCount();
    if (texCount <= trackTextureHeapBase_) return false;
    const uint16_t trackTexUsed = static_cast<uint16_t>(texCount - trackTextureHeapBase_);

    uint16_t liveSlots = 0u;
    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        const auto& family = seg1FamilySlots_[i];
        for (size_t li = 0; li < family.lodSlots.size(); ++li)
        {
            if (IsVdp1TextureSlotLive(family.lodSlots[li]) &&
                liveSlots < std::numeric_limits<uint16_t>::max())
            {
                ++liveSlots;
            }
        }
    }

    const uint16_t slackSlots =
        (trackTexUsed > liveSlots) ? static_cast<uint16_t>(trackTexUsed - liveSlots) : 0u;
    const uint16_t reusableSlots = CountReusableTrackTextureSlots();
    const uint16_t pendingRetiredSlots = static_cast<uint16_t>(std::min<size_t>(
        g_trackPendingRetiredTextureSlots.size(),
        static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
    const uint16_t retiredSlots = static_cast<uint16_t>(std::min<uint32_t>(
        static_cast<uint32_t>(reusableSlots) + static_cast<uint32_t>(pendingRetiredSlots),
        static_cast<uint32_t>(std::numeric_limits<uint16_t>::max())));
    const size_t heapUsed = SRL::VDP1::GetUsedMemory();
    const size_t heapFree = SRL::VDP1::GetAvailableMemory();
    const size_t heapTotal = heapUsed + heapFree;
    const uint32_t heapPct =
        (heapTotal > 0u) ? static_cast<uint32_t>((heapUsed * 100u) / heapTotal) : 0u;

    constexpr uint32_t kHeapPctCompactThreshold = 46u;
    constexpr uint16_t kSlackSlotCompactThreshold = 8u;
    constexpr uint16_t kRetiredSlotCompactThreshold = 6u;
    constexpr uint16_t kTrackTexCompactThreshold = 72u;
    constexpr uint16_t kTrackTexHardCap = 112u;
    if (trackTexUsed >= kTrackTexHardCap)
    {
        return true;
    }
    if (heapPct >= kHeapPctCompactThreshold &&
        (slackSlots >= kSlackSlotCompactThreshold ||
         retiredSlots >= kRetiredSlotCompactThreshold))
    {
        return true;
    }
    return trackTexUsed >= kTrackTexCompactThreshold &&
           (slackSlots >= kSlackSlotCompactThreshold ||
            retiredSlots >= kRetiredSlotCompactThreshold);
}

void TrackSystem::RecycleTrackTextureHeap()
{
    if (!TrackTextureHeapBaseValid())
    {
        return;
    }
    const uint16_t texBefore = SRL::VDP1::GetTextureCount();
    const size_t freeBefore = GetHighWorkRamFreeBytesSafe();
    SRL::VDP1::ResetTextureHeap(trackTextureHeapBase_);
    ReleaseTrackedPaletteBanks();
    ResetReusableTrackTextureSlots();
    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        seg1FamilySlots_[i].lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
        seg1FamilySlots_[i].unusedFrames = { 0, 0, 0, 0 };
    }
    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        auto& lod = segmentRenderers_[i].lodState;
        lod.currentLodIndex = 0xFF;
        lod.currentBaseRank = -1;
        if (!lod.currentFaceSlots.empty())
        {
            lod.currentFaceSlots.assign(lod.currentFaceSlots.size(), -1);
        }
    }
    ++trackTextureRecycleCount_;
    SRL::Debug::Print(1, 23, "TRK tex recycle:%u base:%u tb:%u ta:%u fb:%u fa:%u",
                      static_cast<unsigned>(trackTextureRecycleCount_),
                      static_cast<unsigned>(trackTextureHeapBase_),
                      static_cast<unsigned>(texBefore),
                      static_cast<unsigned>(SRL::VDP1::GetTextureCount()),
                      static_cast<unsigned>(freeBefore),
                      static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
}

uint32_t TrackSystem::EstimateWorkRamRetainedBytes() const
{
    uint64_t bytes = 0;
    bytes += EstimateRuntimeBlobScratchBytesHigh();
    bytes += coordinator_.RetainedBytes();
    for (const auto& entry : segmentRenderers_)
    {
        if (!entry.renderer) continue;
        bytes += entry.renderer->RetainedBytes();
    }
    if (slideScratchRenderer_)
    {
        bytes += slideScratchRenderer_->RetainedBytes();
    }
    if (slidePrefetchRenderer_)
    {
        bytes += slidePrefetchRenderer_->RetainedBytes();
    }
    if (workRamEmergencyReserve_)
    {
        bytes += workRamEmergencyReserveBytes_;
    }

    return (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(bytes);
}

uint32_t TrackSystem::EstimateLowWorkRamRetainedBytes() const
{
    uint64_t bytes = 0;
    bytes += EstimateRuntimeBlobScratchBytesLow();
    bytes += VectorCapacityBytesSafe(g_textCache);
    bytes += VectorCapacityBytesSafe(g_trackReusableTextureSlots);
    bytes += VectorCapacityBytesSafe(g_trackPendingRetiredTextureSlots);
    bytes += coordinator_.RetainedBytesLowWork();
    bytes += VectorCapacityBytesSafe(segmentCenterCatalog_);
    bytes += VectorCapacityBytesSafe(activeWindowLookupSegmentIds_);
    bytes += VectorCapacityBytesSafe(activeWindowEntryIndexBySegmentId_);
    bytes += VectorCapacityBytesSafe(activeWindowLogicalRankBySegmentId_);
    bytes += VectorCapacityBytesSafe(segmentEntries_);
    bytes += VectorCapacityBytesSafe(segmentRenderers_);
    bytes += VectorCapacityBytesSafe(segmentHandles_);
    bytes += VectorCapacityBytesSafe(rawSegmentCatalog_);
    bytes += VectorCapacityBytesSafe(seg1FaceFamilyIds_);
    bytes += VectorCapacityBytesSafe(seg1FamilySlots_);
    bytes += VectorCapacityBytesSafe(familyMergeCurrentWindowScratch_);
    bytes += VectorCapacityBytesSafe(slidePrefetchFamilySlotsScratch_);
    bytes += VectorCapacityBytesSafe(slideIncomingFamilyIdsScratch_);
    bytes += VectorCapacityBytesSafe(slideIncomingFaceRankOffsetsScratch_);
    bytes += VectorCapacityBytesSafe(slideIncomingFaceSlotsScratch_);
    bytes += VectorCapacityBytesSafe(slidePrefetchFamilyIds_);
    bytes += VectorCapacityBytesSafe(slidePrefetchFaceSlots_);
    bytes += VectorCapacityBytesSafe(slideRollbackFaceSlotsScratch_);
    if (!kEnableDeterministicStabilizedSlide)
    {
        bytes += VectorCapacityBytesSafe(slideBackBuffer_.incomingFaceSlots);
    }
    bytes += VectorCapacityBytesSafe(g_seg1MapCache.faceFamily);
    bytes += VectorCapacityBytesSafe(seg1TgaCatalog_);
    bytes += VectorCapacityBytesSafe(seg1ComponentVerts_);
    bytes += VectorCapacityBytesSafe(seg1ComponentFaces_);
    bytes += VectorCapacityBytesSafe(seg1ComponentAttrs_);
    bytes += VectorCapacityBytesSafe(seg1SingleFaceSlots_);
    bytes += sizeof(TrackRenderer) * static_cast<uint64_t>(segmentRenderers_.size());
    bytes += sizeof(TrackRenderer) * (slideScratchRenderer_ ? 1u : 0u);
    bytes += sizeof(TrackRenderer) * (slidePrefetchRenderer_ ? 1u : 0u);
    for (size_t li = 0; li < seg1Texbanks_.size(); ++li)
    {
        bytes += VectorCapacityBytesSafe(seg1Texbanks_[li].entries);
    }
    for (size_t li = 0; li < seg1RendererFaceSlotsByLod_.size(); ++li)
    {
        bytes += VectorCapacityBytesSafe(seg1RendererFaceSlotsByLod_[li]);
    }
    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        const auto& entry = segmentRenderers_[i];
        bytes += VectorCapacityBytesSafe(entry.lodState.faceFamilyIds);
        bytes += VectorCapacityBytesSafe(entry.lodState.faceRankOffsets);
        bytes += VectorCapacityBytesSafe(entry.lodState.currentFaceSlots);
        bytes += VectorCapacityBytesSafe(entry.lodState.workingSetFamilies);
        bytes += VectorCapacityBytesSafe(entry.lodState.workingSetLodIndices);
        bytes += VectorCapacityBytesSafe(entry.lodState.workingSetSlots);
        if (entry.renderer)
        {
            bytes += entry.renderer->RetainedBytes();
        }
    }
    if (slideScratchRenderer_)
    {
        bytes += slideScratchRenderer_->RetainedBytes();
    }
    if (slidePrefetchRenderer_)
    {
        bytes += slidePrefetchRenderer_->RetainedBytes();
    }
    for (size_t i = 0; i < slideBackBuffer_.boundaryUpdates.size(); ++i)
    {
        if (kEnableDeterministicStabilizedSlide) break;
        bytes += VectorCapacityBytesSafe(slideBackBuffer_.boundaryUpdates[i].preparedFaceSlots);
    }
    bytes += g_packedAssetCacheEntryBytesLwr;
    bytes += VectorCapacityBytesSafe(runtimeRenderFaceSlotsScratch_);
    bytes += VectorCapacityBytesSafe(stabilizedDepthItemsScratch_);
    bytes += VectorCapacityBytesSafe(stabilizedSortedHandlesScratch_);

    return (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(bytes);
}

TrackSystem::LowWorkCategoryBreakdown TrackSystem::CaptureLowWorkBreakdown() const
{
    LowWorkCategoryBreakdown out{};

    out.transient += EstimateRuntimeBlobScratchBytesLow();
    out.metadata += VectorCapacityBytesSafe(g_textCache);
    out.metadata += VectorCapacityBytesSafe(g_trackReusableTextureSlots);
    out.metadata += VectorCapacityBytesSafe(g_trackPendingRetiredTextureSlots);
    out.metadata += coordinator_.RetainedBytesLowWork();
    out.metadata += VectorCapacityBytesSafe(segmentCenterCatalog_);
    out.metadata += VectorCapacityBytesSafe(activeWindowLookupSegmentIds_);
    out.metadata += VectorCapacityBytesSafe(activeWindowEntryIndexBySegmentId_);
    out.metadata += VectorCapacityBytesSafe(activeWindowLogicalRankBySegmentId_);
    out.metadata += VectorCapacityBytesSafe(segmentEntries_);
    out.metadata += VectorCapacityBytesSafe(segmentRenderers_);
    out.metadata += VectorCapacityBytesSafe(segmentHandles_);
    out.metadata += VectorCapacityBytesSafe(rawSegmentCatalog_);
    out.metadata += VectorCapacityBytesSafe(g_seg1MapCache.faceFamily);
    out.metadata += VectorCapacityBytesSafe(seg1TgaCatalog_);
    out.metadata += VectorCapacityBytesSafe(seg1ComponentVerts_);
    out.metadata += VectorCapacityBytesSafe(seg1ComponentFaces_);
    out.metadata += VectorCapacityBytesSafe(seg1ComponentAttrs_);
    out.metadata += VectorCapacityBytesSafe(seg1SingleFaceSlots_);
    out.metadata += g_packedAssetCacheEntryBytesLwr;

    out.familyCache += VectorCapacityBytesSafe(seg1FaceFamilyIds_);
    out.familyCache += VectorCapacityBytesSafe(seg1FamilySlots_);
    out.familyCache += VectorCapacityBytesSafe(familyMergeCurrentWindowScratch_);
    out.transient += VectorCapacityBytesSafe(slidePrefetchFamilySlotsScratch_);
    for (size_t li = 0; li < seg1Texbanks_.size(); ++li)
    {
        out.familyCache += VectorCapacityBytesSafe(seg1Texbanks_[li].entries);
    }
    for (size_t li = 0; li < seg1RendererFaceSlotsByLod_.size(); ++li)
    {
        out.familyCache += VectorCapacityBytesSafe(seg1RendererFaceSlotsByLod_[li]);
    }

    out.transient += VectorCapacityBytesSafe(slideIncomingFamilyIdsScratch_);
    out.transient += VectorCapacityBytesSafe(slideIncomingFaceRankOffsetsScratch_);
    out.transient += VectorCapacityBytesSafe(slideIncomingFaceSlotsScratch_);
    out.transient += VectorCapacityBytesSafe(slideRollbackFaceSlotsScratch_);
    out.transient += VectorCapacityBytesSafe(slidePrefetchFamilyIds_);
    out.transient += VectorCapacityBytesSafe(slidePrefetchFaceSlots_);
    out.transient += VectorCapacityBytesSafe(slideBackBuffer_.incomingFamilyIds);
    out.transient += VectorCapacityBytesSafe(slideBackBuffer_.incomingFaceSlots);
    for (size_t i = 0; i < slideBackBuffer_.boundaryUpdates.size(); ++i)
    {
        out.transient += VectorCapacityBytesSafe(slideBackBuffer_.boundaryUpdates[i].preparedFaceSlots);
    }

    out.renderers += sizeof(TrackRenderer) * static_cast<uint32_t>(segmentRenderers_.size());
    out.renderers += sizeof(TrackRenderer) * (slideScratchRenderer_ ? 1u : 0u);
    out.renderers += sizeof(TrackRenderer) * (slidePrefetchRenderer_ ? 1u : 0u);

    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        const auto& entry = segmentRenderers_[i];
        out.slotState += VectorCapacityBytesSafe(entry.lodState.faceFamilyIds);
        out.slotState += VectorCapacityBytesSafe(entry.lodState.faceRankOffsets);
        out.slotState += VectorCapacityBytesSafe(entry.lodState.currentFaceSlots);
        out.workingSet += VectorCapacityBytesSafe(entry.lodState.workingSetFamilies);
        out.workingSet += VectorCapacityBytesSafe(entry.lodState.workingSetLodIndices);
        out.workingSet += VectorCapacityBytesSafe(entry.lodState.workingSetSlots);
        if (entry.renderer)
        {
            out.renderers += entry.renderer->RetainedBytes();
        }
    }
    if (slideScratchRenderer_)
    {
        out.renderers += slideScratchRenderer_->RetainedBytes();
    }
    if (slidePrefetchRenderer_)
    {
        out.renderers += slidePrefetchRenderer_->RetainedBytes();
    }

    const uint64_t total =
        static_cast<uint64_t>(out.renderers) +
        static_cast<uint64_t>(out.slotState) +
        static_cast<uint64_t>(out.workingSet) +
        static_cast<uint64_t>(out.familyCache) +
        static_cast<uint64_t>(out.transient) +
        static_cast<uint64_t>(out.metadata);
    out.total = (total > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(total);
    return out;
}

void TrackSystem::AllocateWorkRamEmergencyReserve()
{
    if (workRamEmergencyReserve_) return;
    bool freeValid = false;
    const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    if (!freeValid) return;
    // Do not consume the emergency reserve while HWR is already tight.
    // Keeping these bytes free at boot prevents startup failures (e.g. BG/renderer init).
    if (freeBytes <= kTrackWorkRamEmergencyReserveReacquireFloorBytes) return;
    void* mem = SRL::Memory::HighWorkRam::Malloc(kTrackWorkRamEmergencyReserveBytes);
    if (!mem) return;
    workRamEmergencyReserve_ = mem;
    workRamEmergencyReserveBytes_ = static_cast<uint32_t>(kTrackWorkRamEmergencyReserveBytes);
}

void TrackSystem::ReleaseWorkRamEmergencyReserve()
{
    if (!workRamEmergencyReserve_) return;
    SRL::Memory::HighWorkRam::Free(workRamEmergencyReserve_);
    workRamEmergencyReserve_ = nullptr;
    workRamEmergencyReserveBytes_ = 0u;
    workRamMaintenance_.workRamEmergencyReserveReleases = static_cast<uint16_t>(
        std::min<uint32_t>(static_cast<uint32_t>(workRamMaintenance_.workRamEmergencyReserveReleases) + 1u,
                           static_cast<uint32_t>(std::numeric_limits<uint16_t>::max())));
}

void TrackSystem::ReacquireWorkRamEmergencyReserve()
{
    if (workRamEmergencyReserve_) return;
    bool freeValid = false;
    const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    if (!freeValid) return;
    if (freeBytes <= kTrackWorkRamEmergencyReserveReacquireFloorBytes) return;
    AllocateWorkRamEmergencyReserve();
}

bool TrackSystem::TrimWorkRamRetainedCapacities(bool aggressive, int32_t* outFreeDelta)
{
    bool freeValidBefore = false;
    const size_t freeBefore = GetHighWorkRamFreeBytesSafe(&freeValidBefore);
    bool trimmed = false;

    // Query LWR free once for the entire trim pass. Passing this hint to every
    // TrimVectorSlack call avoids O(n * free_blocks) TLSF scans â€” each scan walks
    // the entire free list, so 95+ calls per invocation was stalling the frame.
    const size_t lwrFreeHint =
        static_cast<size_t>(SRL::Memory::LowWorkRam::GetReport().FreeSize);

    const size_t windowTarget = std::max<size_t>(
        segmentRenderers_.size(),
        std::max<size_t>(1u, static_cast<size_t>(fixedVisibleSegmentCap_)));
    const size_t windowLookupTarget =
        std::max<size_t>(kTrackSegmentLimit + 2u, segmentRenderers_.size() + 2u);
    const size_t faceCapacityFloor = static_cast<size_t>(slotFaceCapacityFloor_);
    const size_t queueCapacityFloor = std::max<size_t>(64u, g_trackTextureSlotQueueCapacityFloor);
    const size_t familySlotVectorFloor =
        std::max<size_t>(256u, static_cast<size_t>(familySlotCapacityFloor_));

    if (aggressive)
    {
        if (!g_textCache.empty())
        {
            ReleaseTextCache();
            trimmed = true;
        }
    }
    else
    {
        trimmed |= TrimVectorSlack(g_textCache, g_textCache.size(), aggressive, lwrFreeHint);
    }
    trimmed |= TrimVectorSlack(g_trackReusableTextureSlots,
                               std::max<size_t>(
                                   queueCapacityFloor,
                                   aggressive ? g_trackReusableTextureSlots.size()
                                              : (g_trackReusableTextureSlots.size() + 16u)),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(g_trackPendingRetiredTextureSlots,
                               std::max<size_t>(
                                   queueCapacityFloor,
                                   aggressive ? g_trackPendingRetiredTextureSlots.size()
                                              : (g_trackPendingRetiredTextureSlots.size() + 16u)),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(activeWindowLookupSegmentIds_, windowLookupTarget, aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(activeWindowEntryIndexBySegmentId_, windowLookupTarget, aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(activeWindowLogicalRankBySegmentId_, windowLookupTarget, aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(segmentEntries_,
                               aggressive ? segmentEntries_.size() : (windowTarget + 2u),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(seg1FamilySlots_,
                               std::max<size_t>(familySlotVectorFloor,
                                                aggressive ? seg1FamilySlots_.size()
                                                           : (seg1FamilySlots_.size() + 16u)),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(familyMergeCurrentWindowScratch_,
                               std::max<size_t>(familySlotVectorFloor,
                                                aggressive ? familyMergeCurrentWindowScratch_.size()
                                                           : (familyMergeCurrentWindowScratch_.size() + 16u)),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(slidePrefetchFamilySlotsScratch_,
                               std::max<size_t>(familySlotVectorFloor,
                                                aggressive ? slidePrefetchFamilySlotsScratch_.size()
                                                           : (slidePrefetchFamilySlotsScratch_.size() + 16u)),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(stabilizedDepthItemsScratch_,
                               aggressive ? 0u : (stabilizedDepthItemsScratch_.size() + 8u),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(stabilizedSortedHandlesScratch_,
                               aggressive ? 0u : (stabilizedSortedHandlesScratch_.size() + 8u),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(slideIncomingFamilyIdsScratch_,
                               CapacityFloorTrimTarget(slideIncomingFamilyIdsScratch_.size(),
                                                      faceCapacityFloor,
                                                      aggressive,
                                                      16u),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(slideIncomingFaceRankOffsetsScratch_,
                               CapacityFloorTrimTarget(slideIncomingFaceRankOffsetsScratch_.size(),
                                                      faceCapacityFloor,
                                                      aggressive,
                                                      16u),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(slidePrefetchFamilyIds_,
                               CapacityFloorTrimTarget(slidePrefetchFamilyIds_.size(),
                                                      faceCapacityFloor,
                                                      aggressive,
                                                      16u),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(seg1SingleFaceSlots_,
                               aggressive ? seg1SingleFaceSlots_.size()
                                          : (seg1SingleFaceSlots_.size() + 8u),
                               aggressive, lwrFreeHint);

    trimmed |= TrimVectorSlack(segmentHandles_, windowTarget + 2u, aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(slideIncomingFaceSlotsScratch_,
                               CapacityFloorTrimTarget(slideIncomingFaceSlotsScratch_.size(),
                                                      faceCapacityFloor,
                                                      aggressive,
                                                      0u),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(slideRollbackFaceSlotsScratch_,
                               CapacityFloorTrimTarget(slideRollbackFaceSlotsScratch_.size(),
                                                      faceCapacityFloor,
                                                      aggressive,
                                                      0u),
                               aggressive, lwrFreeHint);
    trimmed |= TrimVectorSlack(slidePrefetchFaceSlots_,
                               CapacityFloorTrimTarget(slidePrefetchFaceSlots_.size(),
                                                      faceCapacityFloor,
                                                      aggressive,
                                                      0u),
                               aggressive, lwrFreeHint);
    if (!kEnableDeterministicStabilizedSlide)
    {
        trimmed |= TrimVectorSlack(slideBackBuffer_.incomingFaceSlots, aggressive ? 0u : 128u, aggressive, lwrFreeHint);
        for (auto& update : slideBackBuffer_.boundaryUpdates)
        {
            const size_t keep = aggressive ? 0u : (update.preparedFaceSlots.size() + 8u);
            trimmed |= TrimVectorSlack(update.preparedFaceSlots, keep, aggressive, lwrFreeHint);
        }
    }

    if (aggressive &&
        (slidePrefetchRenderer_ ||
         slidePrefetchSegmentId_ >= 0 ||
         !slidePrefetchFamilyIds_.empty()))
    {
        ResetSlidePrefetchState();
        ResetSlideBackBuffer();
        trimmed = true;
    }

    if (!Seg1ComponentEnabled())
    {
        trimmed |= TrimVectorSlack(seg1ComponentVerts_, 0u, aggressive, lwrFreeHint);
        trimmed |= TrimVectorSlack(seg1ComponentFaces_, 0u, aggressive, lwrFreeHint);
        trimmed |= TrimVectorSlack(seg1ComponentAttrs_, 0u, aggressive, lwrFreeHint);
        trimmed |= TrimVectorSlack(seg1FaceFamilyIds_, 0u, aggressive, lwrFreeHint);
    }

    trimmed |= TrimVectorSlack(runtimeRenderFaceSlotsScratch_,
                               CapacityFloorTrimTarget(runtimeRenderFaceSlotsScratch_.size(),
                                                      faceCapacityFloor,
                                                      aggressive,
                                                      0u),
                               aggressive, lwrFreeHint);

    for (size_t li = 0; li < seg1RendererFaceSlotsByLod_.size(); ++li)
    {
        const size_t keep = aggressive ? 0u : (seg1RendererFaceSlotsByLod_[li].size() + 8u);
        trimmed |= TrimVectorSlack(seg1RendererFaceSlotsByLod_[li], keep, aggressive, lwrFreeHint);
    }

    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        auto& lod = segmentRenderers_[i].lodState;
        trimmed |= TrimVectorSlack(lod.faceFamilyIds,
                                   CapacityFloorTrimTarget(lod.faceFamilyIds.size(),
                                                          faceCapacityFloor,
                                                          aggressive,
                                                          8u),
                                   aggressive, lwrFreeHint);
        trimmed |= TrimVectorSlack(lod.faceRankOffsets,
                                   CapacityFloorTrimTarget(lod.faceRankOffsets.size(),
                                                          faceCapacityFloor,
                                                          aggressive,
                                                          8u),
                                   aggressive, lwrFreeHint);
        trimmed |= TrimVectorSlack(lod.currentFaceSlots,
                                   CapacityFloorTrimTarget(lod.currentFaceSlots.size(),
                                                          faceCapacityFloor,
                                                          aggressive,
                                                          8u),
                                   aggressive, lwrFreeHint);
        // Working set vectors: never trim below the current capacity in non-aggressive
        // mode â€” the capacity IS the self-tracking high-water mark for this slot.
        // Trimming below it causes TLSF alloc/free oscillation every lap when a segment
        // with more unique (family, LOD) pairs enters the slot (e.g. segment 115 with
        // > kSegmentFamilyDedupScratchCap unique keys). In aggressive mode, floor at 64
        // (the dedup scratch cap) to allow emergency recovery without full collapse.
        trimmed |= TrimVectorSlack(lod.workingSetFamilies,
                                   aggressive ? std::max(lod.workingSetFamilies.size(),
                                                         kSegmentFamilyDedupScratchCap)
                                              : lod.workingSetFamilies.capacity(),
                                   aggressive, lwrFreeHint);
        trimmed |= TrimVectorSlack(lod.workingSetLodIndices,
                                   aggressive ? std::max(lod.workingSetLodIndices.size(),
                                                         kSegmentFamilyDedupScratchCap)
                                              : lod.workingSetLodIndices.capacity(),
                                   aggressive, lwrFreeHint);
        trimmed |= TrimVectorSlack(lod.workingSetSlots,
                                   aggressive ? std::max(lod.workingSetSlots.size(),
                                                         kSegmentFamilyDedupScratchCap)
                                              : lod.workingSetSlots.capacity(),
                                   aggressive, lwrFreeHint);
        if (segmentRenderers_[i].renderer)
        {
            trimmed |= segmentRenderers_[i].renderer->CompactRuntimeState(aggressive);
        }
    }

    if (slideScratchRenderer_)
    {
        trimmed |= slideScratchRenderer_->CompactRuntimeState(aggressive);
    }
    if (slidePrefetchRenderer_)
    {
        trimmed |= slidePrefetchRenderer_->CompactRuntimeState(aggressive);
    }

    const size_t desiredRendererCap = windowTarget + 2u;
    if (segmentRenderers_.capacity() > desiredRendererCap &&
        (aggressive || segmentRenderers_.capacity() > (desiredRendererCap + 8u)))
    {
        // Same double-alloc safety: compact.reserve() and old segmentRenderers_ block
        // coexist in LWR until the swap.
        const size_t rendererCompactBytes = desiredRendererCap * sizeof(SegmentRenderEntry);
        const bool lwrSafeToCompact =
            (rendererCompactBytes == 0u) ||
            (lwrFreeHint >= rendererCompactBytes + 2u * 1024u);
        if (lwrSafeToCompact)
        {
            TrackLowWorkVector<SegmentRenderEntry> compact{};
            compact.reserve(desiredRendererCap);
            for (size_t i = 0; i < segmentRenderers_.size(); ++i)
            {
                compact.push_back(std::move(segmentRenderers_[i]));
            }
            segmentRenderers_.swap(compact);
            trimmed = true;
        }
    }

    if (trimmed)
    {
        InvalidateFamilySlotIndex();
        BuildSegmentHandleTable();
    }

    if (outFreeDelta)
    {
        bool freeValidAfter = false;
        const size_t freeAfter = GetHighWorkRamFreeBytesSafe(&freeValidAfter);
        if (freeValidBefore && freeValidAfter)
        {
            *outFreeDelta = static_cast<int32_t>(freeAfter) - static_cast<int32_t>(freeBefore);
        }
        else
        {
            *outFreeDelta = 0;
        }
    }
    return trimmed;
}

TrackSystem::MemoryPressureLevel TrackSystem::ClassifyMemoryPressure(size_t freeBytes, bool freeValid) const
{
    if (!freeValid) return MemoryPressureLevel::Normal;
    if (freeBytes <= kWorkRamLodPendingMinBytes) return MemoryPressureLevel::Critical;
    if (freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u))) return MemoryPressureLevel::Pressure;
    return MemoryPressureLevel::Normal;
}

uint16_t TrackSystem::ReleaseTrackFamilyResourcesImmediate(MemoryPressureLevel level)
{
    if (seg1FamilySlots_.empty()) return 0u;
    if (FamilyWorkingSetDirty()) return 0u;
    if (level == MemoryPressureLevel::Normal) return 0u;

    uint16_t released = 0u;
    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        auto& family = seg1FamilySlots_[i];
        for (int li = 3; li >= 1; --li)
        {
            if (level == MemoryPressureLevel::Pressure && li < 2) continue;
            uint16_t& slot = family.lodSlots[static_cast<size_t>(li)];
            if (slot == No_Texture) continue;
            if (!IsVdp1TextureSlotLive(slot))
            {
                slot = No_Texture;
                family.unusedFrames[static_cast<size_t>(li)] = 0u;
                continue;
            }
            if (family.workingRefs[static_cast<size_t>(li)] != 0u) continue;
            QueuePendingRetiredTrackTextureSlot(slot);
            slot = No_Texture;
            family.unusedFrames[static_cast<size_t>(li)] = 0u;
            ++released;
        }
    }
    return released;
}

bool TrackSystem::ValidateAndRepairWindowState()
{
    if (segmentRenderers_.empty())
    {
        segmentHandles_.clear();
        segmentPool_.Reset();
        return false;
    }

    bool repaired = false;
    if (segmentEntries_.size() != segmentRenderers_.size())
    {
        const size_t n = std::min(segmentEntries_.size(), segmentRenderers_.size());
        segmentEntries_.resize(n);
        segmentRenderers_.resize(n);
        repaired = true;
    }

    if (segmentRenderers_.size() > kTrackSegmentLimit)
    {
        segmentRenderers_.resize(kTrackSegmentLimit);
        segmentEntries_.resize(std::min(segmentEntries_.size(), segmentRenderers_.size()));
        repaired = true;
    }

    if (activeWindowHead_ >= segmentRenderers_.size())
    {
        activeWindowHead_ = 0;
        repaired = true;
    }

    if (totalSegmentCount_ > 0 && !segmentRenderers_.empty())
    {
        static TrackLowWorkU8Vector seen{};
        static TrackLowWorkU8Vector expected{};
        const size_t markCount = static_cast<size_t>(totalSegmentCount_) + 1u;
        seen.assign(markCount, 0u);
        expected.assign(markCount, 0u);
        size_t duplicateCount = 0;
        size_t unexpectedCount = 0;
        size_t missingCount = 0;
        int32_t expectedId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
        for (size_t i = 0; i < segmentRenderers_.size(); ++i)
        {
            if (expectedId > 0) expected[static_cast<size_t>(expectedId)] = 1u;
            expectedId = WrapSegmentIdToRange(expectedId + (windowDirection_ >= 0 ? 1 : -1),
                                              totalSegmentCount_);
        }
        for (size_t i = 0; i < segmentRenderers_.size(); ++i)
        {
            const int32_t sid = WrapSegmentIdToRange(segmentRenderers_[i].id, totalSegmentCount_);
            if (sid <= 0) continue;
            uint8_t& mark = seen[static_cast<size_t>(sid)];
            if (mark != 0u) ++duplicateCount;
            mark = 1u;
            if (expected[static_cast<size_t>(sid)] == 0u) ++unexpectedCount;
        }
        expectedId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
        for (size_t i = 0; i < segmentRenderers_.size(); ++i)
        {
            if (expectedId > 0 && seen[static_cast<size_t>(expectedId)] == 0u) ++missingCount;
            expectedId = WrapSegmentIdToRange(expectedId + (windowDirection_ >= 0 ? 1 : -1),
                                              totalSegmentCount_);
        }
        if (duplicateCount > 0 || unexpectedCount > 0 || missingCount > 0)
        {
            if (!kEnableTrackRuntimeStabilization && kEnableRuntimeWindowRebuildRepairs &&
                RebuildActiveSegmentWindow(activeWindowStartId_, segmentRenderers_.size(), windowDirection_))
            {
                SRL::Debug::Print(1, 21, "TRK cov ms:%u dp:%u ux:%u",
                                  static_cast<unsigned>(missingCount),
                                  static_cast<unsigned>(duplicateCount),
                                  static_cast<unsigned>(unexpectedCount));
                repaired = true;
            }
        }
    }

    bool rebuildHandles = (segmentHandles_.size() != segmentRenderers_.size());
    if (!rebuildHandles)
    {
        for (size_t i = 0; i < segmentHandles_.size(); ++i)
        {
            if (segmentPool_.Resolve(segmentHandles_[i])) continue;
            rebuildHandles = true;
            break;
        }
    }
    if (rebuildHandles)
    {
        BuildSegmentHandleTable();
        repaired = true;
    }

    if (repaired)
    {
        ++workRamMaintenance_.workRamRepairCount;
        SRL::Debug::Print(1, 17, "WM repair:%u n:%u",
                          static_cast<unsigned>(workRamMaintenance_.workRamRepairCount),
                          static_cast<unsigned>(segmentRenderers_.size()));
    }
    return repaired;
}

void TrackSystem::EmitWorkRamLivePointersTelemetry()
{
    if (workRamTelemetryCooldown_ > 0)
    {
        --workRamTelemetryCooldown_;
        return;
    }
    workRamTelemetryCooldown_ = 20;

    uint32_t liveRenderers = 0;
    uint32_t validHandles = 0;
    uint32_t lodCapBytes = 0;
    uint32_t slotCapElements = 0;
    uintptr_t ptrHash = 0;

    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        const auto& entry = segmentRenderers_[i];
        if (entry.renderer)
        {
            ++liveRenderers;
            ptrHash ^= (reinterpret_cast<uintptr_t>(entry.renderer.get()) >> 4);
        }
        lodCapBytes += VectorCapacityBytesSafe(entry.lodState.faceFamilyIds);
        lodCapBytes += VectorCapacityBytesSafe(entry.lodState.faceRankOffsets);
        lodCapBytes += VectorCapacityBytesSafe(entry.lodState.currentFaceSlots);
        slotCapElements += VectorCapacityElementsSafe(entry.lodState.currentFaceSlots);
        if (!entry.lodState.faceFamilyIds.empty())
        {
            ptrHash ^= (reinterpret_cast<uintptr_t>(entry.lodState.faceFamilyIds.data()) >> 4);
        }
        if (!entry.lodState.currentFaceSlots.empty())
        {
            ptrHash ^= (reinterpret_cast<uintptr_t>(entry.lodState.currentFaceSlots.data()) >> 4);
        }
    }
    for (size_t i = 0; i < segmentHandles_.size(); ++i)
    {
        if (!segmentPool_.Resolve(segmentHandles_[i])) continue;
        ++validHandles;
    }
    if (slideScratchRenderer_)
    {
        ptrHash ^= (reinterpret_cast<uintptr_t>(slideScratchRenderer_.get()) >> 4);
    }

    slotCapElements += VectorCapacityElementsSafe(slideIncomingFaceSlotsScratch_);
    slotCapElements += VectorCapacityElementsSafe(slidePrefetchFaceSlots_);
    slotCapElements += VectorCapacityElementsSafe(seg1SingleFaceSlots_);
    for (size_t li = 0; li < seg1RendererFaceSlotsByLod_.size(); ++li)
    {
        slotCapElements += VectorCapacityElementsSafe(seg1RendererFaceSlotsByLod_[li]);
    }

    const uint32_t slotBytesNow = slotCapElements * static_cast<uint32_t>(sizeof(int16_t));
    const uint32_t slotBytesLegacy32 = slotCapElements * static_cast<uint32_t>(sizeof(int32_t));
    const uint32_t slotBytesSaved = slotBytesLegacy32 - slotBytesNow;

    const auto hwr = SRL::Memory::HighWorkRam::GetReport();
    const auto lwr = SRL::Memory::LowWorkRam::GetReport();
    const uint32_t hwrUsed = (hwr.TotalSize >= hwr.FreeSize)
        ? static_cast<uint32_t>(hwr.TotalSize - hwr.FreeSize)
        : 0u;
    const uint32_t lwrUsed = (lwr.TotalSize >= lwr.FreeSize)
        ? static_cast<uint32_t>(lwr.TotalSize - lwr.FreeSize)
        : 0u;

    if constexpr (kEnableTrackOverlayRows16To22)
    {
        SRL::Debug::Print(1, 17, "WM mem hf:%u hu:%u lf:%u lu:%u",
                          static_cast<unsigned>(hwr.FreeSize),
                          static_cast<unsigned>(hwrUsed),
                          static_cast<unsigned>(lwr.FreeSize),
                          static_cast<unsigned>(lwrUsed));
        SRL::Debug::Print(1, 18, "WM cap hb:%u lb:%u lc:%u     ",
                          static_cast<unsigned>(EstimateWorkRamRetainedBytes()),
                          static_cast<unsigned>(EstimateLowWorkRamRetainedBytes()),
                          static_cast<unsigned>(lodCapBytes));
        SRL::Debug::Print(1, 16, "WM slot e:%u b:%u sv:%u     ",
                          static_cast<unsigned>(slotCapElements),
                          static_cast<unsigned>(slotBytesNow),
                          static_cast<unsigned>(slotBytesSaved));
    }
}

void TrackSystem::RunWorkRamMaintenance(bool windowSlid)
{
    bool ranTrimThisCall = false;
    bool refreshedWindowRebuildCooldownThisCall = false;
    bool freeValid = false;
    size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    bool lowFreeValid = false;
    size_t lowFreeBytes = GetLowWorkRamFreeBytesSafe(&lowFreeValid);
    if (freeValid && freeBytes <= kWorkRamSlideSafeFloorBytes && workRamEmergencyReserve_)
    {
        ReleaseWorkRamEmergencyReserve();
        freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    }
    // Hot path: prefer exact tagged usage over deep retained-bytes walks.
    // The retained estimators traverse renderer/vector capacities and were
    // dominating maintenance ticks in long sessions.
    const uint32_t trackOwnedHwrBytes = GetTrackOwnedHighWorkBytesExact();
    const bool trackOwnsLittleHwr =
        trackOwnedHwrBytes <= static_cast<uint32_t>(kWorkRamTrackOwnedBypassBytes);
    const MemoryPressureLevel pressure = ClassifyMemoryPressure(freeBytes, freeValid);
    workRamMaintenance_.memoryPressureLevelThisFrame = std::max<uint8_t>(workRamMaintenance_.memoryPressureLevelThisFrame,
                                                      static_cast<uint8_t>(pressure));
    // Keep soft pressure conservative. In long sessions we often stabilize near
    // ~50 KiB free HWR without real risk; treating that as "soft low" triggers
    // unnecessary trims and hurts frame pacing.
    const bool softLowMemory = freeValid && freeBytes <= (kWorkRamHardFloorBytes + (24u * 1024u));
    const bool criticalLowMemory = freeValid && freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u));
    const bool catastrophicLowMemory = freeValid && freeBytes <= kWorkRamCatastrophicFloorBytes;
    const bool softLowLowWork = lowFreeValid && lowFreeBytes <= kLowWorkRamSoftFloorBytes;
    const bool criticalLowLowWork = lowFreeValid && lowFreeBytes <= kLowWorkRamHardFloorBytes;
    if (kEnableTrackRuntimeStabilization)
    {
        if (freeValid && freeBytes > kLodRecoveryFreeBytes)
        {
            lodDegradeCooldown_ = 0;
        }
        const bool bypassHwrRecoveryWork =
            trackOwnsLittleHwr &&
            !softLowLowWork &&
            !criticalLowLowWork;
        if (bypassHwrRecoveryWork)
        {
            if (workRamTrimCooldown_ > 0)
            {
                --workRamTrimCooldown_;
            }
            if (workRamWindowRebuildCooldown_ > 0)
            {
                --workRamWindowRebuildCooldown_;
            }
            return;
        }
        if (pressure != MemoryPressureLevel::Normal)
        {
            if (pressure == MemoryPressureLevel::Critical &&
                (slidePrefetchSegmentId_ >= 0 || !slidePrefetchFamilyIds_.empty()))
            {
                const int32_t upcomingId = (segmentRenderers_.empty() || totalSegmentCount_ == 0)
                    ? -1
                    : ((windowDirection_ > 0)
                        ? WrapSegmentIdToRange(activeWindowStartId_ + static_cast<int32_t>(segmentRenderers_.size()),
                                               totalSegmentCount_)
                        : WrapSegmentIdToRange(activeWindowStartId_ - static_cast<int32_t>(segmentRenderers_.size()),
                                               totalSegmentCount_));
                const bool keepUpcomingPrefetch =
                    slidePrefetchSegmentId_ == upcomingId &&
                    !slidePrefetchFamilyIds_.empty();
                if (!keepUpcomingPrefetch)
                {
                    ResetSlidePrefetchState();
                    ++workRamMaintenance_.releasedPrefetchNowThisFrame;
                }
            }
            // HighWorkRam pressure should not evict 32/64 track slots. Those live in
            // VDP1/CRAM and dropping them here only ratchets the visible window down to
            // 32x32 without materially recovering HWR. Texture-heap cleanup is handled by
            // the dedicated VDP1 compaction path.
        }
        const bool shouldTrimForWindowReuse =
            windowSlid &&
            workRamTrimCooldown_ == 0 &&
            !criticalLowMemory &&
            !criticalLowLowWork &&
            (softLowMemory || softLowLowWork);
        if ((criticalLowMemory || softLowMemory || criticalLowLowWork || softLowLowWork || shouldTrimForWindowReuse) &&
            workRamTrimCooldown_ == 0)
        {
            ranTrimThisCall = true;
            const bool aggressiveTrim =
                criticalLowMemory || softLowMemory || criticalLowLowWork;
            LWR_PROBE_BEGIN();
            TrimRuntimeBlobScratchCaches(aggressiveTrim || softLowLowWork || shouldTrimForWindowReuse);
            int32_t freeDelta = 0;
            const bool trimmed = TrimWorkRamRetainedCapacities(aggressiveTrim, &freeDelta);
            LWR_PROBE_END(g_lwrStageAccum.maintenanceTrim);
            if (trimmed)
            {
                if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
                {
                    SRL::Debug::Print(1, 17, "WM trim a:%u df:%d f:%u l:%u",
                                      aggressiveTrim ? 1u : 0u,
                                      static_cast<int>(freeDelta),
                                      static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()),
                                      static_cast<unsigned>(GetLowWorkRamFreeBytesSafe()));
                }
            }
            const bool ineffectiveAggressiveTrim =
                trimmed &&
                freeDelta <= 0 &&
                !criticalLowMemory &&
                !criticalLowLowWork;
            workRamTrimCooldown_ =
                ineffectiveAggressiveTrim ? 12u :
                (aggressiveTrim ? 1u : (shouldTrimForWindowReuse ? 2u : 6u));
        }
        if (!ranTrimThisCall && workRamTrimCooldown_ > 0)
        {
            --workRamTrimCooldown_;
        }

        if (pressure == MemoryPressureLevel::Normal)
        {
            ReacquireWorkRamEmergencyReserve();
        }

        if (catastrophicLowMemory &&
            workRamWindowRebuildCooldown_ == 0 &&
            !windowSlid &&
            runtimeSlidesThisFrame_ == 0u &&
            SegmentsReady() &&
            !segmentRenderers_.empty())
        {
            bool freeValidAfterTrim = false;
            const size_t freeAfterTrim = GetHighWorkRamFreeBytesSafe(&freeValidAfterTrim);
            if (freeValidAfterTrim && freeAfterTrim <= kWorkRamCatastrophicFloorBytes)
            {
                const size_t projectedFreeAfterRelease =
                    freeAfterTrim + static_cast<size_t>(trackOwnedHwrBytes);
                SRL::Debug::Print(1, 17, "WM rebuild skip e:%u f:%u   ",
                                  0u,
                                  static_cast<unsigned>(projectedFreeAfterRelease));
                workRamWindowRebuildCooldown_ = 6u;
                refreshedWindowRebuildCooldownThisCall = true;
            }
        }
        if (!refreshedWindowRebuildCooldownThisCall && workRamWindowRebuildCooldown_ > 0)
        {
            --workRamWindowRebuildCooldown_;
        }
        return;
    }

    if (criticalLowMemory || criticalLowLowWork)
    {
        ResetSlidePrefetchState();
        if (slideScratchRenderer_)
        {
            slideScratchRenderer_->RecycleRuntimeState();
        }
        TrimRuntimeBlobScratchCaches(true);
    }

    bool trimmed = false;
    if (criticalLowMemory ||
        criticalLowLowWork ||
        ((softLowMemory || softLowLowWork) && workRamTrimCooldown_ == 0) ||
        (windowSlid && workRamTrimCooldown_ == 0))
    {
        ranTrimThisCall = true;
        const bool aggressiveTrim = criticalLowMemory || softLowMemory || criticalLowLowWork;
        int32_t freeDelta = 0;
        trimmed = TrimWorkRamRetainedCapacities(aggressiveTrim, &freeDelta);
        TrimRuntimeBlobScratchCaches(aggressiveTrim || softLowLowWork);
        if (trimmed)
        {
            if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
            {
                SRL::Debug::Print(1, 17, "WM trim a:%u df:%d f:%u l:%u",
                                  aggressiveTrim ? 1u : 0u,
                                  static_cast<int>(freeDelta),
                                  static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()),
                                  static_cast<unsigned>(GetLowWorkRamFreeBytesSafe()));
            }
        }
        if (!kEnableTrackRuntimeStabilization && (trimmed || criticalLowMemory))
        {
            (void)ValidateAndRepairWindowState();
            EmitWorkRamLivePointersTelemetry();
        }
        workRamTrimCooldown_ = aggressiveTrim ? 1u : (windowSlid ? 2u : 6u);
    }
    else if (!ranTrimThisCall && workRamTrimCooldown_ > 0)
    {
        --workRamTrimCooldown_;
    }

    if (kEnableRuntimeTextureRecycleOnMaintenance &&
        (windowSlid || criticalLowMemory) &&
        ShouldRecycleTrackTextureHeap())
    {
        RecycleTrackTextureHeap();
    }
    if (!criticalLowMemory && !softLowMemory)
    {
        ReacquireWorkRamEmergencyReserve();
    }
}

bool TrackSystem::SlideActiveSegmentWindow(size_t stepCount, int8_t direction)
{
    if (stepCount == 0) return true;
    if (!SegmentsReady() || segmentRenderers_.empty()) return false;
    if (totalSegmentCount_ == 0) return false;
    direction = (direction < 0) ? -1 : 1;

    const size_t windowCount = segmentRenderers_.size();
    if (windowCount == 0) return false;
    if (activeWindowHead_ >= windowCount) activeWindowHead_ = 0;

    if (windowDirection_ != direction)
    {
        if (kEnableTrackRuntimeStabilization)
        {
            // Runtime estabilizado: evita rebuild de 20 segmentos quando
            // ocorre oscilacao de direcao no tracking do carro.
            direction = windowDirection_;
        }
        else
        {
            const int32_t newStartId = WrapSegmentIdToRange(
                activeWindowStartId_ + static_cast<int32_t>(direction),
                totalSegmentCount_);
            if (newStartId <= 0) return false;
            if (!RebuildActiveSegmentWindow(newStartId, windowCount, direction))
            {
                return false;
            }
            TryPrefetchUpcomingSegment();
            if (stepCount <= 1) return true;
            return SlideActiveSegmentWindow(stepCount - 1, direction);
        }
    }

    auto finalizeSlideWindow = [&](bool allowImmediatePrefetch) -> bool
    {
        SetSegmentsReady(!segmentRenderers_.empty());
        InvalidateActiveWindowLookupTables();
        bool handlesOk = !kEnableTrackRuntimeStabilization &&
                         (segmentHandles_.size() == segmentRenderers_.size());
        if (handlesOk)
        {
            for (size_t i = 0; i < segmentHandles_.size(); ++i)
            {
                if (segmentPool_.Resolve(segmentHandles_[i])) continue;
                handlesOk = false;
                break;
            }
        }
        if (!handlesOk || kEnableTrackRuntimeStabilization)
        {
            BuildSegmentHandleTable();
        }
        if (allowImmediatePrefetch)
        {
            TryPrefetchUpcomingSegment();
        }
        if constexpr (kEnableTrackWindowOverlayTelemetry)
        {
            const int32_t endId = WrapSegmentIdToRange(
                activeWindowStartId_ + (windowDirection_ > 0
                    ? static_cast<int32_t>(segmentRenderers_.size()) - 1
                    : -(static_cast<int32_t>(segmentRenderers_.size()) - 1)),
                totalSegmentCount_);
            bool winFreeValid = false;
            const size_t winFree = GetHighWorkRamFreeBytesSafe(&winFreeValid);
            SRL::Debug::Print(1, 14, "WIN %d..%d n:%u d:%d free:%u ok:%u",
                              activeWindowStartId_,
                              endId,
                              static_cast<unsigned>(segmentRenderers_.size()),
                              static_cast<int>(windowDirection_),
                              static_cast<unsigned>(winFree),
                              winFreeValid ? 1u : 0u);
            if (winFreeValid)
            {
                int32_t dFree = 0;
                if (LastWindowFreeValid())
                {
                    dFree = static_cast<int32_t>(winFree) - static_cast<int32_t>(lastWindowFreeBytes_);
                }
                SRL::Debug::Print(1, 15, "WIN dFree:%d", static_cast<int>(dFree));
                lastWindowFreeBytes_ = winFree;
                SetLastWindowFreeValid(true);
            }
            else
            {
                SetLastWindowFreeValid(false);
                lastWindowFreeBytes_ = 0;
            }
            uint32_t trkBytes = 0;
            for (size_t i = 0; i < segmentRenderers_.size(); ++i)
            {
                if (!segmentRenderers_[i].renderer) continue;
                trkBytes += segmentRenderers_[i].renderer->RetainedBytes();
            }
            if (slideScratchRenderer_) trkBytes += slideScratchRenderer_->RetainedBytes();
            const uint16_t texCount = SRL::VDP1::GetTextureCount();
            const uint16_t trackTexUsed = (TrackTextureHeapBaseValid() && texCount > trackTextureHeapBase_)
                ? static_cast<uint16_t>(texCount - trackTextureHeapBase_)
                : 0u;
            uint32_t slotCapElements = 0;
            for (size_t i = 0; i < segmentRenderers_.size(); ++i)
            {
                slotCapElements += VectorCapacityElementsSafe(segmentRenderers_[i].lodState.currentFaceSlots);
            }
            slotCapElements += VectorCapacityElementsSafe(slideIncomingFaceSlotsScratch_);
            slotCapElements += VectorCapacityElementsSafe(slidePrefetchFaceSlots_);
            slotCapElements += VectorCapacityElementsSafe(seg1SingleFaceSlots_);
            for (size_t li = 0; li < seg1RendererFaceSlotsByLod_.size(); ++li)
            {
                slotCapElements += VectorCapacityElementsSafe(seg1RendererFaceSlotsByLod_[li]);
            }
            const uint32_t slotBytesNow = slotCapElements * static_cast<uint32_t>(sizeof(int16_t));
            // sv: economia real vs. pior caso (slots usados Ã— sizeof vs. capacidade reservada)
            size_t slotUsedElements = 0;
            for (size_t i = 0; i < segmentRenderers_.size(); ++i)
                slotUsedElements += segmentRenderers_[i].lodState.currentFaceSlots.size();
            slotUsedElements += slideIncomingFaceSlotsScratch_.size();
            slotUsedElements += slidePrefetchFaceSlots_.size();
            slotUsedElements += seg1SingleFaceSlots_.size();
            for (size_t li = 0; li < seg1RendererFaceSlotsByLod_.size(); ++li)
                slotUsedElements += seg1RendererFaceSlotsByLod_[li].size();
            const uint32_t slotBytesSaved = (slotCapElements > slotUsedElements)
                ? static_cast<uint32_t>((slotCapElements - slotUsedElements) * sizeof(int16_t))
                : 0u;
            const auto hwr = SRL::Memory::HighWorkRam::GetReport();
            const auto lwr = SRL::Memory::LowWorkRam::GetReport();
            if constexpr (kEnableTrackOverlayRows16To22)
            {
                SRL::Debug::Print(1, 16, "WM trk:%u tx:%u hf:%u lf:%u",
                                  static_cast<unsigned>(trkBytes),
                                  static_cast<unsigned>(texCount),
                                  static_cast<unsigned>(hwr.FreeSize),
                                  static_cast<unsigned>(lwr.FreeSize));
            }
            if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
            {
                SRL::Debug::Print(1, 15, "WM tex tu:%u fs:%u sv:%u     ",
                                  static_cast<unsigned>(trackTexUsed),
                                  static_cast<unsigned>(slotBytesNow),
                                  static_cast<unsigned>(slotBytesSaved));
            }
        }
        return SegmentsReady();
    };

    if (kEnableTrackRuntimeStabilization)
    {
        for (size_t step = 0; step < stepCount; ++step)
        {
            const int32_t nextId = ResolveWindowIncomingSegmentId(direction, windowCount);
            if (nextId <= 0) return false;
            slideHwrTrace_.segmentId = nextId;
            const auto hasResidentPrefetchForNextId = [&]() -> bool
            {
                return slidePrefetchSegmentId_ == nextId &&
                       !slidePrefetchFamilyIds_.empty();
            };

            bool freeValid = false;
            size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
            if (freeValid && freeBytes <= kWorkRamSlideSafeFloorBytes && workRamEmergencyReserve_)
            {
                ReleaseWorkRamEmergencyReserve();
                freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
            }
            slideHwrTrace_.check = static_cast<uint32_t>(freeBytes);
            slideHwrTrace_.afterTrim = static_cast<uint32_t>(freeBytes);
            slideHwrTrace_.afterResetPrefetch = static_cast<uint32_t>(freeBytes);
            slideHwrTrace_.afterBuildPrefetch = 0u;
            slideHwrTrace_.afterPrepare = 0u;
            slideHwrTrace_.afterCommit = 0u;
            if (freeValid && freeBytes <= kWorkRamSlideSafeFloorBytes)
            {
                slideHwrTrace_.flags |= kSlideHwrTraceLowMemBit;
                if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
                {
                    SRL::Debug::Print(1, 11, "PKG slide low step:%u free:%u ok:%u",
                                      static_cast<unsigned>(step + 1),
                                      static_cast<unsigned>(freeBytes),
                                      freeValid ? 1u : 0u);
                }
                uint32_t trackOwnedHwrBytes =
                    ResolveTrackOwnedHighWorkBytesForPressure(
                        static_cast<uint32_t>(EstimateWorkRamRetainedBytes()));
                trackOwnedHwrBytes = std::max<uint32_t>(trackOwnedHwrBytes,
                                                        GetTrackOwnedHighWorkBytesExact());
                const bool trackOwnedBypass =
                    trackOwnedHwrBytes <= static_cast<uint32_t>(kWorkRamTrackOwnedBypassBytes);
                const MemoryPressureLevel pressure = ClassifyMemoryPressure(freeBytes, freeValid);
                workRamMaintenance_.memoryPressureLevelThisFrame = std::max<uint8_t>(workRamMaintenance_.memoryPressureLevelThisFrame,
                                                                  static_cast<uint8_t>(pressure));
                const bool keepResidentPrefetch = hasResidentPrefetchForNextId();
                if (!trackOwnedBypass &&
                    !keepResidentPrefetch &&
                    (slidePrefetchSegmentId_ >= 0 || !slidePrefetchFamilyIds_.empty()))
                {
                    ResetSlidePrefetchState();
                    slideHwrTrace_.flags |= kSlideHwrTraceResetPrefetchBit;
                    ++workRamMaintenance_.releasedPrefetchNowThisFrame;
                }
                slideHwrTrace_.afterResetPrefetch =
                    static_cast<uint32_t>(GetHighWorkRamFreeBytesSafe());
                if (!trackOwnedBypass && !keepResidentPrefetch && slideScratchRenderer_)
                {
                    slideScratchRenderer_->RecycleRuntimeState();
                }
                int32_t freeDelta = 0;
                if (!trackOwnedBypass)
                {
                    TrimRuntimeBlobScratchCaches(true);
                    if (TrimWorkRamRetainedCapacities(true, &freeDelta))
                    {
                        if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
                        {
                            SRL::Debug::Print(1, 12, "PKG slide trim df:%d free:%u",
                                              static_cast<int>(freeDelta),
                                              static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
                        }
                    }
                }
                bool freeValidAfterTrim = false;
                const size_t freeAfterTrim = GetHighWorkRamFreeBytesSafe(&freeValidAfterTrim);
                slideHwrTrace_.afterTrim = static_cast<uint32_t>(freeAfterTrim);
                if (freeValidAfterTrim)
                {
                    const uint32_t activeRendererBytes =
                        static_cast<uint32_t>(VectorCapacityBytesSafe(runtimeRenderFaceSlotsScratch_));

                    const uint32_t slideRendererBytes =
                        static_cast<uint32_t>(EstimateRuntimeBlobScratchBytesHigh());

                    const uint32_t transientBytes =
                        static_cast<uint32_t>(coordinator_.RetainedBytes());

                    if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
                    {
                        SRL::Debug::Print(1, 10, "WM hwr hb:%u ab:%u sb:%u tb:%u rb:%u",
                                          static_cast<unsigned>(trackOwnedHwrBytes),
                                          static_cast<unsigned>(activeRendererBytes),
                                          static_cast<unsigned>(slideRendererBytes),
                                          static_cast<unsigned>(transientBytes),
                                          static_cast<unsigned>(workRamEmergencyReserve_
                                              ? workRamEmergencyReserveBytes_
                                              : 0u));
                    }
                    if (freeAfterTrim <= kWorkRamLodDegradeBytes)
                    {
                        lodDegradeCooldown_ = kLodDegradeCooldownFrames;
                    }
                    else if (freeAfterTrim > kWorkRamHardFloorBytes)
                    {
                        lodDegradeCooldown_ = 0;
                    }
                    if (freeAfterTrim <= kWorkRamSlideSafeFloorBytes &&
                        !hasResidentPrefetchForNextId() &&
                        !trackOwnedBypass)
                    {
                        if (!kEnableDeterministicStabilizedSlide)
                        {
                            slideHwrTrace_.flags |= kSlideHwrTraceDeferredBit;
                            ++runtimeSlideStallsThisFrame_;
                            if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
                            {
                                SRL::Debug::Print(1, 11, "PKG slide defer id:%d free:%u",
                                                  nextId,
                                                  static_cast<unsigned>(freeAfterTrim));
                            }
                            return false;
                        }
                    }
                }
            }

            size_t dropIdx = 0;
            if (!ResolveWindowDropIndexByDirection(direction, windowCount, dropIdx)) return false;

            const int32_t nextStartId = WrapSegmentIdToRange(activeWindowStartId_ + static_cast<int32_t>(direction),
                                                             totalSegmentCount_);
            if (nextStartId <= 0) return false;
            if (kEnableDeterministicStabilizedSlide)
            {
                if (!ExecuteDeterministicStabilizedSlide(dropIdx, direction, nextId, nextStartId))
                {
                    lodDegradeCooldown_ = std::max<uint8_t>(lodDegradeCooldown_, kLodDegradeCooldownFrames);
                    ++runtimeSlideStallsThisFrame_;
                    if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
                    {
                        SRL::Debug::Print(1, 11, "PKG slide wait id:%d d:%d free:%u",
                                          nextId,
                                          static_cast<int>(direction),
                                          static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
                    }
                    return false;
                }
            }
            else
            {
                if (!PrepareStabilizedSlideBackBuffer(dropIdx, direction, nextId, nextStartId))
                {
                    lodDegradeCooldown_ = std::max<uint8_t>(lodDegradeCooldown_, kLodDegradeCooldownFrames);
                    ++runtimeSlideStallsThisFrame_;
                    if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
                    {
                        SRL::Debug::Print(1, 11, "PKG slide wait id:%d d:%d free:%u",
                                          nextId,
                                          static_cast<int>(direction),
                                          static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
                    }
                    return false;
                }
                if (!CommitStabilizedSlideBackBuffer())
                {
                    lodDegradeCooldown_ = std::max<uint8_t>(lodDegradeCooldown_, kLodDegradeCooldownFrames);
                    ResetSlideBackBuffer();
                    ++runtimeSlideStallsThisFrame_;
                    if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
                    {
                        SRL::Debug::Print(1, 11, "PKG slide commit fail id:%d free:%u",
                                          nextId,
                                          static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
                    }
                    return false;
                }
            }
        }

        bool allowImmediatePrefetch = false;
        if (kEnableTrackRuntimeStabilization)
        {
            const int32_t upcomingAfterSlideId =
                ResolveWindowIncomingSegmentId(windowDirection_, windowCount);
            const auto upcomingTailReady = [&]() -> bool
            {
                return slidePrefetchSegmentId_ == upcomingAfterSlideId &&
                       !slidePrefetchFamilyIds_.empty();
            };

            if (!upcomingTailReady() && upcomingAfterSlideId > 0)
            {
                bool freeValidAfterSlide = false;
                size_t freeAfterSlide = GetHighWorkRamFreeBytesSafe(&freeValidAfterSlide);
                if (freeValidAfterSlide && freeAfterSlide <= (kWorkRamHardFloorBytes + (8u * 1024u)))
                {
                    TrimRuntimeBlobScratchCaches(true);
                    int32_t freeDelta = 0;
                    (void)TrimWorkRamRetainedCapacities(true, &freeDelta);
                    freeAfterSlide = GetHighWorkRamFreeBytesSafe(&freeValidAfterSlide);
                }

                if (!freeValidAfterSlide || freeAfterSlide > (8u * 1024u))
                {
                    (void)BuildSegmentIntoPrefetch(upcomingAfterSlideId, false);
                }

                if (!upcomingTailReady())
                {
                    TryPrefetchUpcomingSegment();
                }
            }
        }

        return finalizeSlideWindow(allowImmediatePrefetch);
    }

    for (size_t step = 0; step < stepCount; ++step)
    {
        bool freeValid = false;
        size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        if (freeValid && freeBytes <= kWorkRamSlideSafeFloorBytes && workRamEmergencyReserve_)
        {
            ReleaseWorkRamEmergencyReserve();
            freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        }
        if (freeValid && freeBytes <= kWorkRamHardFloorBytes)
        {
            if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
            {
                SRL::Debug::Print(1, 11, "PKG slide low step:%u free:%u ok:%u",
                                  static_cast<unsigned>(step + 1),
                                  static_cast<unsigned>(freeBytes),
                                  freeValid ? 1u : 0u);
            }
        }
        if (freeValid && freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u)))
        {
            // In safe mode keep the incoming tail prepared and keep the scratch
            // renderer hot; otherwise we turn every near-floor frame into a
            // prefetch miss on the next slide.
            TrimRuntimeBlobScratchCaches(true);
            int32_t freeDelta = 0;
            if (TrimWorkRamRetainedCapacities(true, &freeDelta))
            {
                if (!kEnableTrackRuntimeStabilization)
                {
                    (void)ValidateAndRepairWindowState();
                }
                if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
                {
                    SRL::Debug::Print(1, 12, "PKG slide trim df:%d free:%u",
                                      static_cast<int>(freeDelta),
                                      static_cast<unsigned>(GetHighWorkRamFreeBytesSafe()));
                }
            }
            if (ShouldRecycleTrackTextureHeap())
            {
                RecycleTrackTextureHeap();
            }
        }

        const int32_t nextId = ResolveWindowIncomingSegmentId(direction, windowCount);
        if (nextId <= 0) return false;

        size_t dropIdx = 0;
        if (!ResolveWindowDropIndexByDirection(direction, windowCount, dropIdx)) return false;

        Vector3D center(0.0, 0.0, 0.0);
        slideIncomingFamilyIdsScratch_.clear();
        slideIncomingFaceSlotsScratch_.clear();
        auto prefetchReady = [&]() -> bool
        {
            if (slidePrefetchSegmentId_ != nextId) return false;
            if (kEnableTrackRuntimeStabilization)
            {
                if (!slideScratchRenderer_) return false;
            }
            else if (!slidePrefetchRenderer_) return false;
            if (slidePrefetchFamilyIds_.empty()) return false;
            if (!SlidePrefetchLodReady()) return false;
            if (slidePrefetchFaceSlots_.size() != slidePrefetchFamilyIds_.size()) return false;
            return !HasMissingRequiredFaceTextureSlots(slidePrefetchFaceSlots_, &slidePrefetchFamilyIds_);
        };

        auto tryResolvePrefetchFallbackSlots = [&]() -> bool
        {
            if (slidePrefetchSegmentId_ != nextId) return false;
            if (kEnableTrackRuntimeStabilization)
            {
                if (!slideScratchRenderer_) return false;
            }
            else if (!slidePrefetchRenderer_) return false;
            if (slidePrefetchFamilyIds_.empty()) return false;

            const size_t familySlotsBeforeWarmup = seg1FamilySlots_.size();
            auto rollbackWarmupFamilies = [&]()
            {
                if (seg1FamilySlots_.size() <= familySlotsBeforeWarmup) return;
                for (size_t fi = familySlotsBeforeWarmup; fi < seg1FamilySlots_.size(); ++fi)
                {
                    for (size_t li = 0; li < seg1FamilySlots_[fi].lodSlots.size(); ++li)
                    {
                        const uint16_t slotId = seg1FamilySlots_[fi].lodSlots[li];
                        if (IsVdp1TextureSlotLive(slotId)) QueuePendingRetiredTrackTextureSlot(slotId);
                    }
                }
                seg1FamilySlots_.resize(familySlotsBeforeWarmup);
                InvalidateFamilySlotIndex();
            };

            if (slidePrefetchFaceSlots_.size() != slidePrefetchFamilyIds_.size())
            {
                slidePrefetchFaceSlots_.assign(slidePrefetchFamilyIds_.size(), -1);
            }

            // Critical path: when the next segment is about to enter the window,
            // allow a short synchronous warmup for missing families so the slide
            // can progress instead of stalling indefinitely on id N+20.
            constexpr size_t kCriticalSlideWarmupCap = 8u;
            size_t warmedFamilies = 0;
            for (size_t fi = 0; fi < slidePrefetchFamilyIds_.size(); ++fi)
            {
                if (slidePrefetchFaceSlots_[fi] >= 0) continue;
                const uint16_t fam = slidePrefetchFamilyIds_[fi];
                if (fam == 0) continue;

                Seg1FamilySlotEntry* slotEntry = FindFamilySlot(seg1FamilySlots_, fam);
                if (!slotEntry)
                {
                    Seg1FamilySlotEntry init{};
                    init.familyId = fam;
                    init.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
                    seg1FamilySlots_.push_back(init);
                    InvalidateFamilySlotIndex();
                    slotEntry = &seg1FamilySlots_.back();
                }
                if (!slotEntry) continue;

                bool hasLiveSlot = false;
                for (int li = 0; li < 4; ++li)
                {
                    if (IsVdp1TextureSlotLive(slotEntry->lodSlots[static_cast<size_t>(li)]))
                    {
                        hasLiveSlot = true;
                        break;
                    }
                }
                if (!hasLiveSlot && warmedFamilies < kCriticalSlideWarmupCap)
                {
                    const bool loaded = TryLoadFamilyLodSlot(*slotEntry,
                                                             0,
                                                             /*fallbackToLowerLods*/true,
                                                             /*fallbackToHigherLods*/true,
                                                             nullptr,
                                                             nullptr,
                                                             nullptr,
                                                             nullptr);
                    if (loaded)
                    {
                        ++warmedFamilies;
                    }
                }

                int32_t resolved = -1;
                for (int li = 0; li < 4; ++li)
                {
                    const uint16_t slot = slotEntry->lodSlots[static_cast<size_t>(li)];
                    if (!IsVdp1TextureSlotLive(slot)) continue;
                    resolved = static_cast<int32_t>(slot);
                    break;
                }
                if (resolved >= 0)
                {
                    slidePrefetchFaceSlots_[fi] = resolved;
                }
            }

            if (HasMissingRequiredFaceTextureSlots(slidePrefetchFaceSlots_, &slidePrefetchFamilyIds_))
            {
                rollbackWarmupFamilies();
                return false;
            }
            if (kEnableTrackRuntimeStabilization)
            {
                (void)slideScratchRenderer_->ApplyFaceTextureSlotsGlobal(slidePrefetchFaceSlots_);
            }
            else
            {
                (void)slidePrefetchRenderer_->ApplyFaceTextureSlotsGlobal(slidePrefetchFaceSlots_);
            }
            ++runtimeFaceRemapsThisFrame_;
            return true;
        };

        bool usePrefetchedRenderer = prefetchReady();
        bool usedFallbackSlots = false;
        bool incomingLodReady = false;
        if (!usePrefetchedRenderer)
        {
            ++runtimePrefetchMissesThisFrame_;
            TryPrefetchUpcomingSegment();
            (void)BuildSegmentIntoPrefetch(nextId, false);
            PrewarmNextSegmentLod32();
            usePrefetchedRenderer = prefetchReady();
            if (!usePrefetchedRenderer)
            {
                if (kEnableTrackRuntimeStabilization)
                {
                    (void)BuildSegmentIntoPrefetch(nextId, false);
                    usePrefetchedRenderer = prefetchReady();
                }

                if (!tryResolvePrefetchFallbackSlots())
                {
                    ++runtimeSlideStallsThisFrame_;
                    const size_t stallFree = GetHighWorkRamFreeBytesSafe();
                    if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
                    {
                        SRL::Debug::Print(1, 11, "PKG slide wait id:%d d:%d free:%u",
                                          nextId,
                                          static_cast<int>(direction),
                                          static_cast<unsigned>(stallFree));
                    }
                    return false;
                }
                usedFallbackSlots = true;
            }
        }
        else
        {
            ++runtimePrefetchHitsThisFrame_;
        }

        center = slidePrefetchCenter_;
        slideIncomingFamilyIdsScratch_.swap(slidePrefetchFamilyIds_);
        slideIncomingFaceSlotsScratch_.swap(slidePrefetchFaceSlots_);
        incomingLodReady = !usedFallbackSlots &&
                           !HasMissingRequiredFaceTextureSlots(slideIncomingFaceSlotsScratch_,
                                                               &slideIncomingFamilyIdsScratch_);
        SegmentRenderEntry& slot = segmentRenderers_[dropIdx];
        TrackSegmentEntry& meta = segmentEntries_[dropIdx];

        TrackLowWorkUniquePtr<TrackRenderer> droppedRenderer = std::move(slot.renderer);
        if (kEnableTrackRuntimeStabilization)
        {
            slot.renderer = std::move(slideScratchRenderer_);
            slideScratchRenderer_ = std::move(droppedRenderer);
        }
        else
        {
            slot.renderer = std::move(slidePrefetchRenderer_);
            slideScratchRenderer_ = std::move(droppedRenderer);
        }
        if (slideScratchRenderer_)
        {
            slideScratchRenderer_->RecycleRuntimeState();
        }
        slot.id = nextId;
        slot.logicalSegmentCount = 1;
        slot.center = center;
        slot.lodState.SetReady(true);
        slot.lodState.SetHasPerFaceRankOffsets(false);
        slot.lodState.currentBaseRank = -1;
        const size_t incomingFaceCount = slideIncomingFamilyIdsScratch_.size();
        slideIncomingFaceRankOffsetsScratch_.assign(incomingFaceCount, 0);
        if (slideIncomingFaceSlotsScratch_.size() != incomingFaceCount)
        {
            slideIncomingFaceSlotsScratch_.assign(incomingFaceCount, -1);
            incomingLodReady = false;
        }
        EnsureVectorCapacityFloor(slideIncomingFamilyIdsScratch_, slotFaceCapacityFloor_);
        EnsureVectorCapacityFloor(slideIncomingFaceRankOffsetsScratch_, slotFaceCapacityFloor_);
        EnsureVectorCapacityFloor(slideIncomingFaceSlotsScratch_, slotFaceCapacityFloor_);

        // Recycle per-slot vectors by swapping with reusable scratch buffers.
        slot.lodState.faceFamilyIds.swap(slideIncomingFamilyIdsScratch_);
        slot.lodState.faceRankOffsets.swap(slideIncomingFaceRankOffsetsScratch_);
        slot.lodState.currentFaceSlots.swap(slideIncomingFaceSlotsScratch_);
        EnsureVectorCapacityFloor(slot.lodState.faceFamilyIds, slotFaceCapacityFloor_);
        EnsureVectorCapacityFloor(slot.lodState.faceRankOffsets, slotFaceCapacityFloor_);
        EnsureVectorCapacityFloor(slot.lodState.currentFaceSlots, slotFaceCapacityFloor_);
        slot.lodState.currentLodIndex = incomingLodReady ? kTrackLod32Index : 0xFF;
        slot.lodState.SetWorkingSetCacheDirty(true);  // slot reutilizado: invalidar cache stale do segmento anterior
        meta.id = nextId;

        // Incremental family cache update: avoid full-window rebuild scan every slide.
        bool addedFamily = false;
        for (size_t fi = 0; fi < slot.lodState.faceFamilyIds.size(); ++fi)
        {
            const uint16_t fam = slot.lodState.faceFamilyIds[fi];
            if (fam == 0) continue;
            if (FindFamilySlot(seg1FamilySlots_, fam)) continue;
            Seg1FamilySlotEntry slotEntry{};
            slotEntry.familyId = fam;
            slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
            seg1FamilySlots_.push_back(slotEntry);
            addedFamily = true;
        }
        if (addedFamily) InvalidateFamilySlotIndex();

        // Keep family/palette state bounded to the active window plus prefetch.
        MergeCurrentWindowFamilies();
        familyMergeCooldown_ = ResolveFamilyMergeCooldownFrames(FullTrackFamilyCacheReady());
        ResetSlidePrefetchState();
        InvalidateActiveWindowLookupTables();
        activeWindowStartId_ = WrapSegmentIdToRange(activeWindowStartId_ + static_cast<int32_t>(direction),
                                                    totalSegmentCount_);
        if (activeWindowStartId_ <= 0) return false;
        if (!AdvanceWindowHeadByDirection(direction, windowCount))
        {
            (void)ResolveWindowHeadByStartId(dropIdx);
        }
        if (prewarmCooldown_ == 0)
        {
            PrewarmNextSegmentLod32();
            prewarmCooldown_ = 2;
        }
        else
        {
            --prewarmCooldown_;
        }
        ++runtimeSlidesThisFrame_;
        {
            bool freeValid = false;
            const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
            const uint8_t cooldown =
                (!freeValid || freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u))) ? 4u :
                (freeBytes <= (kWorkRamHardFloorBytes + (32u * 1024u))) ? 2u :
                1u;
            pendingLodFrameCooldown_ = std::max<uint8_t>(pendingLodFrameCooldown_, cooldown);
        }
    }

    slideHwrTrace_.flags |= kSlideHwrTraceCommitOkBit;
    slideHwrTrace_.afterCommit = static_cast<uint32_t>(GetHighWorkRamFreeBytesSafe());
    return finalizeSlideWindow(true);
}

void TrackSystem::PrewarmNextSegmentLod32()
{
    if (kEnableTrackRuntimeStabilization) return;
    if (totalSegmentCount_ == 0 || segmentRenderers_.empty()) return;
    bool freeValid = false;
    const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    if (freeValid && freeBytes <= (kWorkRamHardFloorBytes + (16u * 1024u)))
    {
        TrimRuntimeBlobScratchCaches(true);
        bool freeValidAfter = false;
        const size_t freeAfter = GetHighWorkRamFreeBytesSafe(&freeValidAfter);
        if (freeValidAfter && freeAfter <= (kWorkRamHardFloorBytes + (16u * 1024u))) return;
    }
    const size_t windowCount = segmentRenderers_.size();
    const int32_t preloadId = ResolveWindowIncomingSegmentId(windowDirection_, windowCount);
    if (preloadId <= 0) return;

    if (slidePrefetchSegmentId_ == preloadId &&
        SlidePrefetchLodReady() &&
        slidePrefetchFaceSlots_.size() == slidePrefetchFamilyIds_.size() &&
        !HasMissingRequiredFaceTextureSlots(slidePrefetchFaceSlots_, &slidePrefetchFamilyIds_))
    {
        return;
    }

    static TrackLowWorkU16Vector sPrewarmFamilies{};
    sPrewarmFamilies.clear();
    if (slidePrefetchSegmentId_ == preloadId && !slidePrefetchFamilyIds_.empty())
    {
        sPrewarmFamilies = slidePrefetchFamilyIds_;
    }
    else if (!LoadRuntimeFamilyIdsForSegment(preloadId, sPrewarmFamilies) || sPrewarmFamilies.empty())
    {
        return;
    }

    // Keep prewarm bounded to reduce per-slide spikes.
    const uint16_t texCount = SRL::VDP1::GetTextureCount();
    const bool nearHeapLimit = texCount >= static_cast<uint16_t>(SRL_MAX_TEXTURES - 96);
    const size_t remainingUploadBudget =
        (textureUploadsThisFrame_ < GetTextureUploadBudgetPerFrame())
            ? static_cast<size_t>(GetTextureUploadBudgetPerFrame() - textureUploadsThisFrame_)
            : 0u;
    const size_t kPrewarmFamilyCapPerSlide = nearHeapLimit ? 1u : 2u;
    const size_t prewarmCap = std::min(kPrewarmFamilyCapPerSlide, remainingUploadBudget);
    if (prewarmCap == 0) return;
    size_t warmed = 0;
    for (size_t i = 0; i < sPrewarmFamilies.size(); ++i)
    {
        const uint16_t fam = sPrewarmFamilies[i];
        if (fam == 0) continue;

        Seg1FamilySlotEntry* slotEntry = FindFamilySlot(seg1FamilySlots_, fam);
        if (!slotEntry)
        {
            Seg1FamilySlotEntry init{};
            init.familyId = fam;
            init.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
            seg1FamilySlots_.push_back(init);
            InvalidateFamilySlotIndex();
            slotEntry = &seg1FamilySlots_.back();
        }
        if (!slotEntry) continue;
        // Use ActiveAndOwned: a slot in the reusable pool is still "live" but no
        // longer owned by this family â€” skipping the upload would leave a stale slot.
        if (slotEntry->lodSlots[kTrackLod32Index] != No_Texture &&
            IsVdp1TextureSlotActiveAndOwned(slotEntry->lodSlots[kTrackLod32Index])) continue;

        if (EnsureFamilyLodSlotLoaded(seg1FamilySlots_, fam, kTrackLod32Index))
        {
            ++warmed;
            if (warmed >= prewarmCap) break;
        }
    }

    const bool prefetchRendererResident = kEnableTrackRuntimeStabilization
        ? static_cast<bool>(slideScratchRenderer_)
        : static_cast<bool>(slidePrefetchRenderer_);
    if (slidePrefetchSegmentId_ == preloadId &&
        prefetchRendererResident &&
        !slidePrefetchFamilyIds_.empty())
    {
        (void)BuildSegmentIntoPrefetch(preloadId, false);
    }
}

void TrackSystem::PrewarmUpcomingBoundaryLods()
{
    if (kEnableDeterministicStabilizedSlide) return;
    if (!kEnableTrackRuntimeStabilization || !kEnableTrackLodBandsInStabilization) return;
    if (windowDirection_ < 0) return;
    if (segmentRenderers_.empty() || totalSegmentCount_ == 0) return;
    if (boundaryPrewarmCooldown_ > 0u)
    {
        --boundaryPrewarmCooldown_;
        return;
    }

    const size_t remainingUploadBudget =
        (textureUploadsThisFrame_ < GetTextureUploadBudgetPerFrame())
            ? static_cast<size_t>(GetTextureUploadBudgetPerFrame() - textureUploadsThisFrame_)
            : 0u;
    if (remainingUploadBudget == 0u) return;

    bool freeValid = false;
    const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    if (freeValid && freeBytes <= kLodExactRecoveryFreeBytes) return;
    const bool prefetchResident =
        slidePrefetchSegmentId_ > 0 &&
        SlidePrefetchRendererReady() &&
        slideScratchRenderer_ &&
        !slidePrefetchFamilyIds_.empty();
    if (!prefetchResident) return;

    struct BoundaryTarget
    {
        size_t logicalRank;
        uint8_t lodIndex;
    };
    static constexpr std::array<BoundaryTarget, 3> kForwardBoundaryTargets{{
        {4u, 3u},
        {9u, 2u},
        {14u, kTrackLod32Index},
    }};
    static constexpr size_t kBoundaryCapPerTarget = 1u;
    size_t uploadsRemaining = remainingUploadBudget;
    bool anyLoaded = false;
    const size_t windowCount = segmentRenderers_.size();
    for (size_t ti = 0; ti < kForwardBoundaryTargets.size(); ++ti)
    {
        if (uploadsRemaining == 0u) break;
        const size_t logicalRank = kForwardBoundaryTargets[ti].logicalRank;
        const uint8_t targetLodIndex = kForwardBoundaryTargets[ti].lodIndex;
        if (logicalRank >= windowCount) continue;

        const int32_t segmentId = WrapSegmentIdToRange(
            activeWindowStartId_ + static_cast<int32_t>(logicalRank),
            totalSegmentCount_);
        if (segmentId <= 0) continue;

        SegmentRenderEntry* entry = FindWindowEntryByIdFast(segmentId);
        if (!entry || !entry->renderer || !entry->lodState.Ready()) continue;
        if (entry->lodState.faceFamilyIds.empty()) continue;

        size_t boundaryLoads = 0u;
        std::array<uint16_t, kSegmentFamilyDedupScratchCap> seenFamilies{};
        size_t seenCount = 0u;
        for (size_t fi = 0; fi < entry->lodState.faceFamilyIds.size(); ++fi)
        {
            if (uploadsRemaining == 0u || boundaryLoads >= kBoundaryCapPerTarget) break;

            const uint16_t fam = entry->lodState.faceFamilyIds[fi];
            if (fam == 0u) continue;

            const size_t seenIndex = FindScratchKeyIndex(seenFamilies, seenCount, fam);
            if (seenIndex != kSegmentFamilyDedupScratchCap) continue;
            if (seenCount < kSegmentFamilyDedupScratchCap)
            {
                seenFamilies[seenCount++] = fam;
            }

            Seg1FamilySlotEntry* slotEntry = FindFamilySlot(seg1FamilySlots_, fam);
            if (!slotEntry)
            {
                Seg1FamilySlotEntry init{};
                init.familyId = fam;
                init.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
                seg1FamilySlots_.push_back(init);
                InvalidateFamilySlotIndex();
                slotEntry = &seg1FamilySlots_.back();
            }
            if (!slotEntry) continue;

            uint16_t existingSlot = No_Texture;
            if (TryGetFamilyLodSlot(seg1FamilySlots_, fam, targetLodIndex, existingSlot) &&
                IsVdp1TextureSlotActiveAndOwned(existingSlot))
            {
                continue;
            }

            if (EnsureFamilyLodSlotLoaded(seg1FamilySlots_, fam, targetLodIndex))
            {
                anyLoaded = true;
                ++boundaryLoads;
                --uploadsRemaining;
                continue;
            }

            if (ReadyFlag() && textureUploadsThisFrame_ >= GetTextureUploadBudgetPerFrame())
            {
                uploadsRemaining = 0u;
                break;
            }
        }
    }

    if (anyLoaded)
    {
        SetFamilyWorkingSetDirty(true);
        boundaryPrewarmCooldown_ = 1u;
    }
}

void TrackSystem::MergeCurrentWindowFamilies()
{
    LWR_PROBE_BEGIN();
    FamilySlotVector& currentWindowFamilies = familyMergeCurrentWindowScratch_;
    (void)BuildTrackFamilyLodSlots(currentWindowFamilies);

    if (currentWindowFamilies.empty()) return;
    auto adoptLiveWindowSlots = [&](FamilySlotVector& targetFamilies)
    {
        for (auto& entry : segmentRenderers_)
        {
            if (!entry.renderer) continue;
            if (!entry.lodState.Ready()) continue;

            const bool useWorkingSetCache = !kEnableTrackLeakIsolationFixed64Pipeline;
            if (useWorkingSetCache && entry.lodState.WorkingSetCacheDirty())
            {
                (void)RebuildEntryWorkingSetCache(entry);
            }

            auto adoptSlot = [&](uint16_t familyId, int32_t slotHint, uint8_t fallbackLodIndex)
            {
                if (familyId == 0u) return;
                if (slotHint < 0 || slotHint >= static_cast<int32_t>(SRL_MAX_TEXTURES)) return;

                const uint16_t liveSlot = static_cast<uint16_t>(slotHint);
                // Must use IsVdp1TextureSlotActiveAndOwned â€” not IsVdp1TextureSlotLive.
                // A slot in the reusable/retired queue is still "live" in VDP1 but is
                // no longer exclusively owned by this family. Re-adopting it would
                // re-insert a stale slot that is about to (or already has) been reused
                // for a different family, causing wrong textures on the next upload.
                if (!IsVdp1TextureSlotActiveAndOwned(liveSlot)) return;

                Seg1FamilySlotEntry* target = FindFamilySlot(targetFamilies, familyId);
                if (!target) return;

                uint8_t resolvedLodIndex = fallbackLodIndex;
                for (uint8_t li = 0; li < 4u; ++li)
                {
                    if (target->lodSlots[li] != liveSlot) continue;
                    if (!IsVdp1TextureSlotLive(liveSlot)) continue;
                    resolvedLodIndex = NormalizeTrackTextureLodIndex(li);
                    break;
                }

                if (resolvedLodIndex > 3u)
                {
                    resolvedLodIndex = (fallbackLodIndex <= 3u)
                        ? NormalizeTrackTextureLodIndex(fallbackLodIndex)
                        : kTrackLod32Index;
                }
                target->lodSlots[resolvedLodIndex] = liveSlot;
                target->unusedFrames[resolvedLodIndex] = 0u;
            };

            if (useWorkingSetCache && !entry.lodState.workingSetFamilies.empty())
            {
                for (size_t fi = 0; fi < entry.lodState.workingSetFamilies.size(); ++fi)
                {
                    const uint16_t fam = entry.lodState.workingSetFamilies[fi];
                    const int32_t slotHint =
                        (fi < entry.lodState.workingSetSlots.size())
                            ? entry.lodState.workingSetSlots[fi]
                            : -1;
                    const uint8_t lodIndex =
                        (fi < entry.lodState.workingSetLodIndices.size())
                            ? NormalizeTrackTextureLodIndex(entry.lodState.workingSetLodIndices[fi])
                            : ((entry.lodState.currentLodIndex <= 3u)
                                ? NormalizeTrackTextureLodIndex(entry.lodState.currentLodIndex)
                                : kTrackLod32Index);
                    adoptSlot(fam, slotHint, lodIndex);
                }
            }

            for (size_t fi = 0; fi < entry.lodState.faceFamilyIds.size(); ++fi)
            {
                const uint16_t fam = entry.lodState.faceFamilyIds[fi];
                const int32_t slotHint =
                    (fi < entry.lodState.currentFaceSlots.size())
                        ? entry.lodState.currentFaceSlots[fi]
                        : -1;
                const uint8_t lodIndex =
                    (entry.lodState.currentLodIndex <= 3u)
                        ? NormalizeTrackTextureLodIndex(entry.lodState.currentLodIndex)
                        : kTrackLod32Index;
                adoptSlot(fam, slotHint, lodIndex);
            }
        }
    };
    adoptLiveWindowSlots(currentWindowFamilies);

    if (seg1FamilySlots_.empty())
    {
        seg1FamilySlots_.swap(currentWindowFamilies);
        InvalidateFamilySlotIndex();
        return;
    }

    // Keep cache bounded to current window families while preserving live slots
    // from the previous frame. Merge in-place into currentWindowFamilies
    // (= familyMergeCurrentWindowScratch_) then swap directly with seg1FamilySlots_
    // â€” zero per-slide LWR allocation (familyMergeNextScratch_ eliminated).
    for (auto& family : currentWindowFamilies)
    {
        Seg1FamilySlotEntry* old = FindFamilySlot(seg1FamilySlots_, family.familyId);
        if (!old) continue;
        for (size_t li = 0; li < old->lodSlots.size(); ++li)
        {
            if (kEnableTrackLeakIsolationFixed64Pipeline)
            {
                // Leak-isolation mode: never inherit retired/reusable slots.
                const uint16_t oldSlot = old->lodSlots[li];
                const uint16_t newSlot = family.lodSlots[li];
                const bool oldOwned = IsVdp1TextureSlotActiveAndOwned(oldSlot);
                const bool newOwned = IsVdp1TextureSlotActiveAndOwned(newSlot);
                if (!newOwned && oldOwned)
                {
                    family.lodSlots[li] = oldSlot;
                }
            }
            else
            {
                if (!IsVdp1TextureSlotLive(old->lodSlots[li]) && IsVdp1TextureSlotLive(family.lodSlots[li]))
                {
                    old->lodSlots[li] = family.lodSlots[li];
                }
                if (IsVdp1TextureSlotLive(old->lodSlots[li]) && !IsVdp1TextureSlotLive(family.lodSlots[li]))
                {
                    family.lodSlots[li] = old->lodSlots[li];
                }
            }
        }
    }

    // Families that leave the active window must hand their slots back to the
    // reusable pool, but only after the frame finishes drawing. Recycling them
    // during the slide path can let a newly uploaded family overwrite a slot
    // that is still referenced by the current frame.
    for (const auto& oldFamily : seg1FamilySlots_)
    {
        if (oldFamily.familyId == 0) continue;
        if (FindFamilySlot(currentWindowFamilies, oldFamily.familyId)) continue;
        for (size_t li = 0; li < oldFamily.lodSlots.size(); ++li)
        {
            const uint16_t slot = oldFamily.lodSlots[li];
            if (!IsVdp1TextureSlotLive(slot)) continue;
            QueuePendingRetiredTrackTextureSlot(slot);
        }
    }

    // Direct swap â€” no per-slide LWR allocation. familyMergeCurrentWindowScratch_
    // (now holding old seg1FamilySlots_ data) is overwritten next slide by
    // BuildTrackFamilyLodSlots which calls .clear() before filling.
    seg1FamilySlots_.swap(familyMergeCurrentWindowScratch_);

    // Equalize capacities so the scratch buffer for the next call is never smaller
    // than the freshly-built result. Without this, a swap where the incoming buffer
    // grew (window gained families) leaves the scratch with the smaller old capacity,
    // causing a realloc on the very next merge call for the same segment.
    if (familyMergeCurrentWindowScratch_.capacity() < seg1FamilySlots_.capacity())
    {
        familyMergeCurrentWindowScratch_.reserve(seg1FamilySlots_.capacity());
    }

    InvalidateFamilySlotIndex();
    LWR_PROBE_END(g_lwrStageAccum.mergeFamilies);
}

void TrackSystem::RefreshFamilyWorkingSet(bool releaseUnused)
{
    (void)releaseUnused;
    if (seg1FamilySlots_.empty()) return;
    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        seg1FamilySlots_[i].workingRefs = { 0, 0, 0, 0 };
    }

    auto addRef = [&](uint16_t familyId, int32_t slotHint, uint8_t fallbackLodIndex)
    {
        if (familyId == 0) return;
        Seg1FamilySlotEntry* family = FindFamilySlot(seg1FamilySlots_, familyId);
        if (!family) return;

        uint8_t resolvedLod = 0xFF;
        if (slotHint >= 0 && slotHint < static_cast<int32_t>(SRL_MAX_TEXTURES))
        {
            const uint16_t slot = static_cast<uint16_t>(slotHint);
            for (uint8_t li = 0; li < 4; ++li)
            {
                if (family->lodSlots[li] != slot) continue;
                if (!IsVdp1TextureSlotLive(slot)) continue;
                resolvedLod = li;
                break;
            }
        }
        if (resolvedLod > 3)
        {
            resolvedLod = (fallbackLodIndex <= 3)
                ? NormalizeTrackTextureLodIndex(fallbackLodIndex)
                : kTrackLod32Index;
        }

        if (family->workingRefs[resolvedLod] < std::numeric_limits<uint16_t>::max())
        {
            ++family->workingRefs[resolvedLod];
        }
    };

    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        auto& entry = segmentRenderers_[i];
        if (!entry.renderer) continue;
        if (!entry.lodState.Ready()) continue;
        if (entry.lodState.faceFamilyIds.empty()) continue;

        const bool strictLeakIsolationRefs = kEnableTrackLeakIsolationFixed64Pipeline;
        if (strictLeakIsolationRefs)
        {
            size_t logicalRank = 0u;
            if (!TryGetWindowLogicalRank(entry.id, logicalRank)) continue;
            (void)logicalRank;
            const uint8_t fallbackLodIndex =
                (entry.lodState.currentLodIndex <= 3u)
                    ? NormalizeTrackTextureLodIndex(entry.lodState.currentLodIndex)
                    : kTrackLod32Index;
            for (size_t fi = 0; fi < entry.lodState.faceFamilyIds.size(); ++fi)
            {
                const uint16_t fam = entry.lodState.faceFamilyIds[fi];
                const int32_t slotHint =
                    (fi < entry.lodState.currentFaceSlots.size())
                        ? entry.lodState.currentFaceSlots[fi]
                        : -1;
                addRef(fam, slotHint, fallbackLodIndex);
            }
            continue;
        }

        if (entry.lodState.WorkingSetCacheDirty())
        {
            (void)RebuildEntryWorkingSetCache(entry);
        }

        if (entry.lodState.workingSetFamilies.empty()) continue;
        for (size_t fi = 0; fi < entry.lodState.workingSetFamilies.size(); ++fi)
        {
            const uint16_t fam = entry.lodState.workingSetFamilies[fi];
            const int32_t slotHint =
                (fi < entry.lodState.workingSetSlots.size()) ? entry.lodState.workingSetSlots[fi] : -1;
            const uint8_t lodIndex =
                (fi < entry.lodState.workingSetLodIndices.size())
                    ? NormalizeTrackTextureLodIndex(entry.lodState.workingSetLodIndices[fi])
                    : kTrackLod32Index;
            addRef(fam, slotHint, lodIndex);
        }
    }

    RebuildUsedTextureSlotFlagsFromWorkingRefs();
}

void TrackSystem::ReleaseUnusedFamilyResourcesEndFrame()
{
    if (seg1FamilySlots_.empty()) return;
    const bool strictWindowRecycling = kEnableTrackRuntimeStabilization;
    const uint8_t graceFrames = kEnableTrackLeakIsolationFixed64Pipeline
        ? 0u
        : strictWindowRecycling
        ? 1u
        : (workRamMaintenance_.memoryPressureLevelThisFrame >= static_cast<uint8_t>(MemoryPressureLevel::Critical)) ? 0u :
          (workRamMaintenance_.memoryPressureLevelThisFrame >= static_cast<uint8_t>(MemoryPressureLevel::Pressure)) ? 1u :
          2u;

    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        auto& family = seg1FamilySlots_[i];
        for (uint8_t li = 0; li < 4; ++li)
        {
            uint16_t& slot = family.lodSlots[li];
            uint8_t& unusedFrames = family.unusedFrames[li];
            // Stabilized mode still needs strict recycling. The far LOD used to be
            // pinned forever here, which let far-band slots accumulate lap after
            // lap even after their source segments left the 20-segment window.
            const uint8_t lodGraceFrames = strictWindowRecycling
                ? (kEnableTrackLeakIsolationFixed64Pipeline ? 0u : 1u)
                : (kEnableTrackRuntimeStabilization &&
                   kEnableTrackLodBandsInStabilization &&
                   li == kTrackLod32Index)
                      ? static_cast<uint8_t>(graceFrames + 1u)
                      : graceFrames;
            if (slot == No_Texture)
            {
                unusedFrames = 0u;
                continue;
            }
            if (!IsVdp1TextureSlotLive(slot))
            {
                slot = No_Texture;
                unusedFrames = 0u;
                continue;
            }

            const bool referencedByFace =
                (slot < usedTextureSlotsThisFrame_.size()) && (usedTextureSlotsThisFrame_[slot] != 0u);
            const bool referencedByFamily = family.workingRefs[li] != 0u;
            const bool keepSlot =
                strictWindowRecycling
                    ? referencedByFace
                    : (referencedByFace || referencedByFamily);
            if (keepSlot)
            {
                unusedFrames = 0u;
                continue;
            }

            if (unusedFrames < std::numeric_limits<uint8_t>::max())
            {
                ++unusedFrames;
            }
            if (unusedFrames < lodGraceFrames) continue;

            QueueReusableTrackTextureSlot(slot);
            slot = No_Texture;
            unusedFrames = 0u;
            if (workRamMaintenance_.releasedEndFrameSlotsThisFrame < std::numeric_limits<uint16_t>::max())
            {
                ++workRamMaintenance_.releasedEndFrameSlotsThisFrame;
            }
        }
    }
}

void TrackSystem::ValidateStabilizedWindowInvariants()
{
    if (!kEnableTrackRuntimeStabilization) return;
    static uint8_t sInvariantCadence = 0u;
    const uint8_t cadenceFrames =
        kEnableTrackLeakIsolationFixed64Pipeline
            ? (runtimeDiagnostics_.RuntimeStatsLogsEnabled() ? 180u : 240u)
            : (runtimeDiagnostics_.RuntimeStatsLogsEnabled() ? 24u : 32u);
    if (sInvariantCadence > 0u)
    {
        --sInvariantCadence;
        return;
    }
    sInvariantCadence = cadenceFrames;

    constexpr size_t kExpectedWindowSegments =
        static_cast<size_t>(kLodBand64Count + kLodBand32Count);

    std::array<uint16_t, 4> bandCounts{{0, 0, 0, 0}};
    size_t activeReadySegments = 0u;
    for (const auto& entry : segmentRenderers_)
    {
        if (!entry.renderer || !entry.lodState.Ready()) continue;
        ++activeReadySegments;
        if (entry.lodState.currentLodIndex > 3u) continue;
        ++bandCounts[entry.lodState.currentLodIndex];
    }

    // Reuse persistent scratch to avoid per-frame LWR alloc/free churn.
    static FamilySlotVector sExpectedFamiliesScratch{};
    FamilySlotVector& expectedFamilies = sExpectedFamiliesScratch;
    (void)BuildTrackFamilyLodSlots(expectedFamilies);
    // Prefetch families are legitimate out-of-window entries.
    if (slidePrefetchSegmentId_ > 0 && !slidePrefetchFamilyIds_.empty())
    {
        for (const uint16_t fam : slidePrefetchFamilyIds_)
        {
            if (fam == 0u) continue;
            if (FindFamilySlot(expectedFamilies, fam)) continue;
            Seg1FamilySlotEntry slotEntry{};
            slotEntry.familyId = fam;
            slotEntry.lodSlots = { No_Texture, No_Texture, No_Texture, No_Texture };
            expectedFamilies.push_back(slotEntry);
        }
    }
    uint16_t extraFamilies = 0u;
    for (const auto& family : seg1FamilySlots_)
    {
        if (family.familyId == 0u) continue;
        if (FindFamilySlot(expectedFamilies, family.familyId)) continue;
        ++extraFamilies;
    }

    const bool prefetchHasMetadata = slidePrefetchSegmentId_ > 0;
    const bool prefetchHasFamilies = !slidePrefetchFamilyIds_.empty();
    bool prefetchHasResolvedSlots = false;
    for (const int16_t slot : slidePrefetchFaceSlots_)
    {
        if (slot < 0) continue;
        prefetchHasResolvedSlots = true;
        break;
    }
    const bool prefetchHasLiveState =
        SlidePrefetchRendererReady() ||
        SlidePrefetchLodReady() ||
        prefetchHasResolvedSlots;
    const bool prefetchMetadataMismatch =
        (prefetchHasMetadata != prefetchHasFamilies) ||
        (!prefetchHasMetadata && !slidePrefetchFaceSlots_.empty()) ||
        (prefetchHasMetadata &&
         !slidePrefetchFaceSlots_.empty() &&
         slidePrefetchFaceSlots_.size() != slidePrefetchFamilyIds_.size());
    const bool badWindowCount = activeReadySegments != kExpectedWindowSegments;
    const bool badBands =
        bandCounts[3] != kLodBand64Count ||
        bandCounts[2] != kLodBand32Count;
    const bool badPrefetch = prefetchMetadataMismatch;
    const bool badFamilies = extraFamilies != 0u;

    if (badFamilies &&
        extraFamilies > 8u &&
        !slideBackBuffer_.Ready() &&
        !segmentRenderers_.empty())
    {
        MergeCurrentWindowFamilies();
        SetFamilyWorkingSetDirty(true);
        extraFamilies = 0u;
    }

    if (!badWindowCount && !badBands && !badPrefetch && !badFamilies) return;
    if (!runtimeDiagnostics_.RuntimeStatsLogsEnabled()) return;

    SRL::Debug::Print(1, 18, "TI n:%u pf:%u ex:%u ",
                      static_cast<unsigned>(activeReadySegments),
                      static_cast<unsigned>(prefetchHasMetadata ? 1u : 0u),
                      static_cast<unsigned>(extraFamilies));
    SRL::Debug::Print(1, 19, "TB 32:%u 64:%u",
                      static_cast<unsigned>(bandCounts[2]),
                      static_cast<unsigned>(bandCounts[3]));
    SRL::Debug::Print(1, 20, "TP m:%u l:%u s:%u",
                      static_cast<unsigned>(prefetchHasMetadata ? 1u : 0u),
                      static_cast<unsigned>(prefetchHasLiveState ? 1u : 0u),
                      static_cast<unsigned>(slidePrefetchFamilyIds_.size()));
}

void TrackSystem::EmitFamilyWorkingSetTelemetry() const
{
    if constexpr (!kEnableLegacyFamilyOverlayTelemetry)
    {
        return;
    }

    uint16_t activeFamilies = 0;
    uint16_t liveSlots = 0;
    std::array<uint16_t, 4> lodFamilies{{0, 0, 0, 0}};
    std::array<uint16_t, 4> lodRefs{{0, 0, 0, 0}};
    std::array<uint16_t, 4> segmentBands{{0, 0, 0, 0}};

    for (const auto& entry : segmentRenderers_)
    {
        if (!entry.renderer || !entry.lodState.Ready()) continue;
        if (entry.lodState.currentLodIndex > 3u) continue;
        ++segmentBands[entry.lodState.currentLodIndex];
    }

    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        const auto& family = seg1FamilySlots_[i];
        bool familyActive = false;
        for (uint8_t li = 0; li < 4; ++li)
        {
            if (IsVdp1TextureSlotLive(family.lodSlots[li])) ++liveSlots;
            if (family.workingRefs[li] == 0u) continue;
            familyActive = true;
            ++lodFamilies[li];
            lodRefs[li] = static_cast<uint16_t>(lodRefs[li] + family.workingRefs[li]);
        }
        if (familyActive) ++activeFamilies;
    }

    SRL::Debug::Print(1, 18, "TW f:%u s:%u 32:%u/%u 64:%u/%u",
                      static_cast<unsigned>(activeFamilies),
                      static_cast<unsigned>(liveSlots),
                      static_cast<unsigned>(lodFamilies[2]),
                      static_cast<unsigned>(lodRefs[2]),
                      static_cast<unsigned>(lodFamilies[3]),
                      static_cast<unsigned>(lodRefs[3]));
    SRL::Debug::Print(1, 16, "TB 32:%u 64:%u",
                      static_cast<unsigned>(segmentBands[2]),
                      static_cast<unsigned>(segmentBands[3]));
    SRL::Debug::Print(1, 17, "TP 16:%u 64:%u 128:%u 256:%u",
                      static_cast<unsigned>(CountTrackedBanks(g_trackPaletteBanks.pal16)),
                      static_cast<unsigned>(CountTrackedBanks(g_trackPaletteBanks.pal64)),
                      static_cast<unsigned>(CountTrackedBanks(g_trackPaletteBanks.pal128)),
                      static_cast<unsigned>(CountTrackedBanks(g_trackPaletteBanks.pal256)));
}

int8_t TrackSystem::ResolveCameraWindowDirection(const Vector3D& trackOffset,
                                                 const Vector3D& cameraLocation,
                                                 const Vector3D& cameraLookTarget) const
{
    const int8_t currentDirection = (cameraWindowDirection_ < 0) ? -1 : 1;
    if (totalSegmentCount_ == 0 || segmentCenterCatalog_.empty()) return currentDirection;

    int32_t anchorId = -1;
    if (observedCarSegmentId_ > 0)
    {
        anchorId = WrapSegmentIdToRange(observedCarSegmentId_, totalSegmentCount_);
    }
    else if (TrackedCarSegmentValid() && trackedCarSegmentId_ > 0)
    {
        anchorId = WrapSegmentIdToRange(trackedCarSegmentId_, totalSegmentCount_);
    }
    else
    {
        anchorId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
    }
    if (anchorId <= 0) return currentDirection;

    const int32_t nextId = WrapSegmentIdToRange(anchorId + 1, totalSegmentCount_);
    if (nextId <= 0) return currentDirection;
    if (static_cast<size_t>(anchorId) > segmentCenterCatalog_.size()) return currentDirection;
    if (static_cast<size_t>(nextId) > segmentCenterCatalog_.size()) return currentDirection;

    const Vector3D anchorCenter = segmentCenterCatalog_[static_cast<size_t>(anchorId - 1)] + trackOffset;
    const Vector3D nextCenter = segmentCenterCatalog_[static_cast<size_t>(nextId - 1)] + trackOffset;

    Vector3D cameraForward{};
    if (!BuildNormalizedFlatDirectionRaw(
            cameraLookTarget.X.RawValue() - cameraLocation.X.RawValue(),
            cameraLookTarget.Z.RawValue() - cameraLocation.Z.RawValue(),
            cameraForward))
    {
        return currentDirection;
    }

    Vector3D trackForward{};
    if (!BuildNormalizedFlatDirectionRaw(
            nextCenter.X.RawValue() - anchorCenter.X.RawValue(),
            nextCenter.Z.RawValue() - anchorCenter.Z.RawValue(),
            trackForward))
    {
        return currentDirection;
    }

    const int32_t dotRaw = ((cameraForward.X * trackForward.X) +
                            (cameraForward.Z * trackForward.Z)).RawValue();
    constexpr int32_t kSwitchToReverseDotRaw = -11380; // cos(100 deg)
    constexpr int32_t kSwitchToForwardDotRaw = 11380;  // cos(80 deg)
    if (currentDirection > 0)
    {
        return (dotRaw <= kSwitchToReverseDotRaw) ? -1 : +1;
    }
    return (dotRaw >= kSwitchToForwardDotRaw) ? +1 : -1;
}

void TrackSystem::UpdateCameraDrivenWindowDirection(const Vector3D& trackOffset,
                                                    const Vector3D& cameraLocation,
                                                    const Vector3D& cameraLookTarget)
{
    // Keep window direction synced with camera forward intent.
    // Use switch confirmation and cooldown to avoid transient direction thrash
    // during 360 turns that can cause one-frame window holes.
    if (!SegmentsReady() || totalSegmentCount_ == 0 || segmentRenderers_.empty()) return;

    const int8_t rawDesiredDirection =
        ResolveCameraWindowDirection(trackOffset, cameraLocation, cameraLookTarget);
    const int8_t currentDirection = (cameraWindowDirection_ < 0) ? -1 : 1;
    constexpr uint8_t kDirectionConfirmFrames = 3;
    constexpr uint8_t kDirectionFlipCooldownFrames = 8;
    if (cameraDirectionFlipCooldown_ > 0) --cameraDirectionFlipCooldown_;

    int8_t desiredDirection = currentDirection;
    if (rawDesiredDirection != currentDirection)
    {
        if (cameraDirectionPending_ != rawDesiredDirection)
        {
            cameraDirectionPending_ = rawDesiredDirection;
            cameraDirectionConfirmFrames_ = 1;
        }
        else if (cameraDirectionConfirmFrames_ < 255u)
        {
            ++cameraDirectionConfirmFrames_;
        }

        if (cameraDirectionConfirmFrames_ < kDirectionConfirmFrames ||
            cameraDirectionFlipCooldown_ > 0)
        {
            return;
        }

        desiredDirection = rawDesiredDirection;
        cameraWindowDirection_ = desiredDirection;
        cameraDirectionFlipCooldown_ = kDirectionFlipCooldownFrames;
        cameraDirectionConfirmFrames_ = 0;
    }
    else
    {
        cameraDirectionPending_ = currentDirection;
        cameraDirectionConfirmFrames_ = 0;
        desiredDirection = currentDirection;
        cameraWindowDirection_ = desiredDirection;
    }

    int32_t anchorId = -1;
    if (observedCarSegmentId_ > 0)
    {
        anchorId = WrapSegmentIdToRange(observedCarSegmentId_, totalSegmentCount_);
    }
    else if (TrackedCarSegmentValid() && trackedCarSegmentId_ > 0)
    {
        anchorId = WrapSegmentIdToRange(trackedCarSegmentId_, totalSegmentCount_);
    }
    else
    {
        anchorId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
    }
    if (anchorId <= 0) return;
    const int32_t desiredStartId =
        ResolveWindowStartFromCarSegment(anchorId, totalSegmentCount_, desiredDirection);
    if (desiredStartId <= 0) return;

    bool needsRebuild = false;
    if (desiredDirection < 0)
    {
        needsRebuild = (windowDirection_ >= 0);
    }
    else
    {
        needsRebuild = (windowDirection_ < 0);
    }
    if (!needsRebuild) return;

    const size_t windowCount = segmentRenderers_.size();
    const int32_t currentStartId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
    const int32_t forwardDistance = WrapDistanceForward(currentStartId, desiredStartId, totalSegmentCount_);
    const int32_t backwardDistance = WrapDistanceForward(desiredStartId, currentStartId, totalSegmentCount_);
    const int32_t half = static_cast<int32_t>(totalSegmentCount_) / 2;
    const int32_t alongDistance = (desiredDirection > 0) ? forwardDistance : backwardDistance;

    // If the new direction target is still close, avoid hard rebuild.
    // Let sliding converge to prevent transient full-window holes.
    if (alongDistance > 0 && alongDistance <= static_cast<int32_t>(windowCount))
    {
        targetWindowStartId_ = desiredStartId;
        trackedCarSegmentId_ = anchorId;
        SetTrackedCarSegmentValid(true);
        activeWindowSwitchCooldown_ = 0;
        windowDirection_ = desiredDirection;
        return;
    }

    if (!RebuildActiveSegmentWindow(desiredStartId, windowCount, desiredDirection)) return;

    targetWindowStartId_ = desiredStartId;
    trackedCarSegmentId_ = anchorId;
    SetTrackedCarSegmentValid(true);
    activeWindowSwitchCooldown_ = 0;
    if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
    {
        SRL::Debug::Print(1, 13, "TRK cam dir:%d anchor:%d start:%d",
                          static_cast<int>(desiredDirection),
                          static_cast<int>(anchorId),
                          static_cast<int>(desiredStartId));
    }
}

bool TrackSystem::UpdateActiveSegmentWindowForPosition(const Vector3D& worldPosition,
                                                       const Vector3D& trackOffset)
{
    // Advance the active window incrementally in both directions.
    // This avoids full window rebuild churn when driving in reverse.
    if (!SegmentsReady() || totalSegmentCount_ == 0) return false;
    if (segmentCenterCatalog_.empty()) return false;
    const int8_t desiredDirection = (cameraWindowDirection_ < 0) ? -1 : 1;
    if (activeWindowSwitchCooldown_ > 0)
    {
        --activeWindowSwitchCooldown_;
        return false;
    }

    const int32_t startId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
    if (startId <= 0) return false;
    const size_t windowCount = segmentRenderers_.size();
    if (windowCount == 0) return false;
    const int32_t currentAnchorId =
        WrapSegmentIdToRange(startId + (desiredDirection > 0 ? 1 : -1), totalSegmentCount_);
    if (currentAnchorId <= 0) return false;

    auto scoreToId = [&](int32_t id) -> SRL::Math::Types::Fxp
    {
        if (id <= 0 || static_cast<size_t>(id) > segmentCenterCatalog_.size())
        {
            return SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        }
        const Vector3D c = segmentCenterCatalog_[static_cast<size_t>(id - 1)] + trackOffset;
        return (c.X - worldPosition.X).Abs() + (c.Z - worldPosition.Z).Abs();
    };

    auto wrapDistanceForward = [&](int32_t fromId, int32_t toId) -> int32_t
    {
        const int32_t total = static_cast<int32_t>(totalSegmentCount_);
        int32_t d = (toId - fromId) % total;
        if (d < 0) d += total;
        return d;
    };

    auto distanceAlongDirection = [&](int32_t fromId, int32_t toId) -> int32_t
    {
        return (desiredDirection > 0)
            ? wrapDistanceForward(fromId, toId)
            : wrapDistanceForward(toId, fromId);
    };

    auto queueDeferredSlide = [&](int8_t slideDirection, int32_t desiredStartId) -> bool
    {
        desiredStartId = WrapSegmentIdToRange(desiredStartId, totalSegmentCount_);
        if (desiredStartId <= 0) return false;
        if (!kEnableTrackRuntimeStabilization)
        {
            const uint8_t stallBefore = runtimeSlideStallsThisFrame_;
            if (!SlideActiveSegmentWindow(1, slideDirection))
            {
                activeWindowSwitchCooldown_ = (runtimeSlideStallsThisFrame_ != stallBefore) ? 1 : 12;
                return false;
            }
            activeWindowSwitchCooldown_ = 0;
            return true;
        }

        const int32_t currentTargetId = WrapSegmentIdToRange(targetWindowStartId_, totalSegmentCount_);
        if (currentTargetId <= 0)
        {
            targetWindowStartId_ = desiredStartId;
        }
        else
        {
            const int32_t total = static_cast<int32_t>(totalSegmentCount_);
            const int32_t deltaFromTarget = (slideDirection > 0)
                ? wrapDistanceForward(currentTargetId, desiredStartId)
                : wrapDistanceForward(desiredStartId, currentTargetId);
            if (deltaFromTarget > 0 && deltaFromTarget < (total / 2))
            {
                targetWindowStartId_ = desiredStartId;
            }
        }
        activeWindowSwitchCooldown_ = 0;
        return false;
    };

    auto resolveProgressiveLocalSegment = [&](int32_t seedId,
                                              int32_t& outId,
                                              SRL::Math::Types::Fxp& outScore) -> bool
    {
        if (seedId <= 0) return false;
        bool found = false;
        int32_t bestId = -1;
        SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);

        // Keep search narrow and biased to movement direction.
        // Reverse traversal needs a slightly wider local search to avoid
        // anchor oscillation when observed segment reports arrive late.
        const int32_t kBackSearch = (desiredDirection > 0) ? 1 : 4;
        const int32_t kForwardSearch = (desiredDirection > 0) ? 3 : 2;
        for (int32_t delta = -kBackSearch; delta <= kForwardSearch; ++delta)
        {
            const int32_t candidateId = WrapSegmentIdToRange(seedId + delta, totalSegmentCount_);
            const SRL::Math::Types::Fxp score = scoreToId(candidateId);
            if (!found || score < bestScore)
            {
                found = true;
                bestId = candidateId;
                bestScore = score;
            }
        }

        if (!found || bestId <= 0) return false;
        outId = bestId;
        outScore = bestScore;
        return true;
    };

    const int32_t seedId = TrackedCarSegmentValid()
        ? WrapSegmentIdToRange(trackedCarSegmentId_, totalSegmentCount_)
        : startId;

    const int32_t observedId = WrapSegmentIdToRange(observedCarSegmentId_, totalSegmentCount_);
    if (observedCarSegmentId_ > 0 && observedId > 0)
    {
        const int32_t observedStartId =
            ResolveWindowStartFromCarSegment(observedId, totalSegmentCount_, desiredDirection);
        if (observedStartId > 0)
        {
            const int32_t observedForwardDistance = distanceAlongDirection(startId, observedStartId);
            const int32_t observedBackwardDistance = wrapDistanceForward(observedStartId, startId);
            if (observedForwardDistance > 0 &&
                observedForwardDistance < (static_cast<int32_t>(totalSegmentCount_) / 2))
            {
                trackedCarSegmentId_ = observedId;
                SetTrackedCarSegmentValid(true);
                return queueDeferredSlide(desiredDirection, observedStartId);
            }
            if (observedForwardDistance == 0)
            {
                trackedCarSegmentId_ = observedId;
                SetTrackedCarSegmentValid(true);
                return false;
            }
            // Reverse mode may report behind by a few ids during wrap/turn.
            // Accept short backward deltas as valid incremental slides.
            const int32_t backwardTolerance = (desiredDirection < 0) ? 3 : 1;
            if (observedBackwardDistance <= backwardTolerance)
            {
                trackedCarSegmentId_ = observedId;
                SetTrackedCarSegmentValid(true);
                return queueDeferredSlide(-desiredDirection, observedStartId);
            }
        }

        const int32_t observedForwardDistance = distanceAlongDirection(startId, observedId);
        if (observedForwardDistance > 0 && observedForwardDistance < (static_cast<int32_t>(totalSegmentCount_) / 2))
        {
            trackedCarSegmentId_ = observedId;
            SetTrackedCarSegmentValid(true);
            return queueDeferredSlide(
                desiredDirection,
                ResolveWindowStartFromCarSegment(observedId, totalSegmentCount_, desiredDirection));
        }
        if (observedForwardDistance == 0)
        {
            trackedCarSegmentId_ = observedId;
            SetTrackedCarSegmentValid(true);
            return false;
        }
        if (runtimeDiagnostics_.RuntimeStatsLogsEnabled() && ((frameIdThisFrame_ & 0x0Fu) == 0u))
        {
            SRL::Debug::Print(1, 13, "TRK obs back s:%d o:%d t:%d",
                              startId,
                              observedId,
                              TrackedCarSegmentValid() ? trackedCarSegmentId_ : -1);
        }
        // If gameplay temporarily reports a segment behind the active window,
        // do not freeze the slide state. Fall through to the local progressive
        // heuristic so the window can continue following the car.
    }

    int32_t localSegmentId = -1;
    SRL::Math::Types::Fxp localScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
    if (resolveProgressiveLocalSegment(seedId, localSegmentId, localScore))
    {
        trackedCarSegmentId_ = localSegmentId;
        SetTrackedCarSegmentValid(true);

        const int32_t localStartId =
            ResolveWindowStartFromCarSegment(localSegmentId, totalSegmentCount_, desiredDirection);
        const int32_t forwardDistance = (localStartId > 0) ? distanceAlongDirection(startId, localStartId) : 0;
        if (localStartId > 0 && forwardDistance > 0)
        {
            return queueDeferredSlide(desiredDirection, localStartId);
        }
    }

    const int32_t nextAnchorId =
        WrapSegmentIdToRange(currentAnchorId + (desiredDirection > 0 ? 1 : -1), totalSegmentCount_);
    if (nextAnchorId <= 0) return false;
    const SRL::Math::Types::Fxp currentScore = scoreToId(currentAnchorId);
    const SRL::Math::Types::Fxp nextScore = scoreToId(nextAnchorId);
    const SRL::Math::Types::Fxp crossingHysteresis = SRL::Math::Types::Fxp::BuildRaw(8 << 16);
    if ((nextScore + crossingHysteresis) < currentScore)
    {
        trackedCarSegmentId_ = nextAnchorId;
        SetTrackedCarSegmentValid(true);
        return queueDeferredSlide(
            desiredDirection,
            WrapSegmentIdToRange(startId + (desiredDirection > 0 ? 1 : -1), totalSegmentCount_));
    }

    return false;
}

void TrackSystem::ResetInitializationState()
{
    ReleaseWorkRamEmergencyReserve();
    SetReadyFlag(false);
    SetSegmentsReady(false);
    SetCoordinatorReady(false);
    fixedVisibleSegmentCap_ = 1;
    totalSegmentCount_ = 0;
    activeWindowStartId_ = 1;
    activeWindowHead_ = 0;
    windowDirection_ = 1;
    cameraWindowDirection_ = 1;
    cameraDirectionPending_ = 1;
    cameraDirectionConfirmFrames_ = 0;
    cameraDirectionFlipCooldown_ = 0;
    activeWindowSwitchCooldown_ = 0;
    targetWindowStartId_ = 1;
    trackedCarSegmentId_ = 1;
    SetTrackedCarSegmentValid(false);
    observedCarSegmentId_ = -1;
    lastLapWrapProbeSegmentId_ = -1;
    lapWrapScrubCooldown_ = 0;
    slotFaceCapacityFloor_ = 0;
    familySlotCapacityFloor_ = 0;
    rendererVertexCapacityFloor_ = 0;
    rendererFaceCapacityFloor_ = 0;
    segmentCenterCatalog_.clear();
    segmentSurfaceFlagsById_.clear();
    ResetFamilyLookupTables();
    SetSurfaceFamilyMapReady(false);
    SetSegmentCollisionMapReady(false);
    SetSeg1ComponentEnabled(false);
    seg1ComponentVerts_.clear();
    seg1ComponentFaces_.clear();
    seg1ComponentAttrs_.clear();
    seg1FaceFamilyIds_.clear();
    seg1FamilySlots_.clear();
    familyMergeCurrentWindowScratch_.clear();
    slidePrefetchFamilySlotsScratch_.clear();
    InvalidateFamilySlotIndex();
    familyMergeCooldown_ = 0;
    for (auto& v : seg1RendererFaceSlotsByLod_) v.clear();
    SetSeg1RendererLodReady(false);
    SetSeg1SingleFaceSwapReady(false);
    SetSeg1SingleFaceSwapUseAlt(false);
    seg1SingleFaceSwapCounter_ = 0;
    seg1SingleFaceSwapFrames_ = 180;
    seg1SingleFaceSwapFace_ = -1;
    seg1SingleFaceSwapBaseSlot_ = -1;
    seg1SingleFaceSwapAltSlot_ = -1;
    seg1SingleFaceSlots_.clear();
    seg1CurrentLodIndex_ = 0;
    seg1LodFrameCounter_ = 0;
    seg1LodSwapFrames_ = 60;
    seg1ComponentCenter_ = Vector3D(0.0, 0.0, 0.0);
    ReleaseRawSegmentCatalog();
    ReleaseSeg1Texbanks();
    ReleaseSeg1TgaCatalog();
    ReleaseTextCache();
    segmentEntries_.clear();
    segmentRenderers_.clear();
    segmentHandles_.clear();
    stabilizedDepthItemsScratch_.clear();
    stabilizedSortedHandlesScratch_.clear();
    stabilizedProducerInputScratch_.clear();
    stabilizedDepthStats_ = {};
    slideScratchRenderer_.reset();
    slideIncomingFamilyIdsScratch_.clear();
    slideIncomingFaceRankOffsetsScratch_.clear();
    slideIncomingFaceSlotsScratch_.clear();
    runtimeRenderFaceSlotsScratch_.clear();
    ResetSlidePrefetchState();
    ResetSlideBackBuffer();
    ReleaseTrackedPaletteBanks();
    ResetReusableTrackTextureSlots();
    trackTextureHeapBase_ = 0;
    SetTrackTextureHeapBaseValid(false);
    trackTextureRecycleCount_ = 0;
    textureUploadsThisFrame_ = 0;
    slideHwrTrace_.segmentId = -1;
    slideHwrTrace_.flags = 0u;
    slideHwrTrace_.check = 0u;
    slideHwrTrace_.afterTrim = 0u;
    slideHwrTrace_.afterResetPrefetch = 0u;
    slideHwrTrace_.afterBuildPrefetch = 0u;
    slideHwrTrace_.afterPrepare = 0u;
    slideHwrTrace_.afterCommit = 0u;
    prewarmCooldown_ = 0;
    boundaryPrewarmCooldown_ = 0;
    prefetchRetryCooldown_ = 0;
    prefetchBuildAttemptsThisFrame_ = 0;
    prefetchBuildBudgetThisFrame_ = 1u;
    prefetchBuildBudgetDropsThisFrame_ = 0;
    surfaceQueryScmapSkipsThisFrame_ = 0u;
    surfaceQueryScmapSkipsLastFrame_ = 0u;
    surfaceQuerySegmentsScannedThisFrame_ = 0u;
    surfaceQueryFacesScannedThisFrame_ = 0u;
    surfaceQuerySegmentsScannedLastFrame_ = 0u;
    surfaceQueryFacesScannedLastFrame_ = 0u;
    surfaceQueryCacheHitsThisFrame_ = 0u;
    surfaceQueryCacheMissesThisFrame_ = 0u;
    surfaceQueryCacheHitsLastFrame_ = 0u;
    surfaceQueryCacheMissesLastFrame_ = 0u;
    wallQueryCallsThisFrame_ = 0u;
    wallQueryHitsThisFrame_ = 0u;
    wallQuerySegmentsScannedThisFrame_ = 0u;
    wallQueryFacesScannedThisFrame_ = 0u;
    wallQueryCallsLastFrame_ = 0u;
    wallQueryHitsLastFrame_ = 0u;
    wallQuerySegmentsScannedLastFrame_ = 0u;
    wallQueryFacesScannedLastFrame_ = 0u;
    wallQueryPrevWorldPosition_ = Vector3D(0.0, 0.0, 0.0);
    SetWallQueryPrevWorldPositionValid(false);
    surfaceQueryLastInsideSegmentId_ = -1;
    surfaceQueryLastInsideFaceIndex_ = -1;
    surfaceQueryLastInsideFamilyId_ = 0u;
    surfaceQueryLastInsideType_ = 0u;
    SetSurfaceQueryLastInsideValid(false);
    prefetchSpeedProxyRaw_ = 0u;
    SetPrefetchSpeedProxyValid(false);
    prefetchLastCarWorldPosition_ = Vector3D(0.0, 0.0, 0.0);
    textureHeapCompactCooldown_ = 0;
    workRamTrimCooldown_ = 0;
    workRamWindowRebuildCooldown_ = 0;
    workRamTelemetryCooldown_ = 0;
    workRamMaintenance_.workRamRepairCount = 0;
    workRamMaintenance_.workRamEmergencyReserveReleases = 0;
    runtimeDiagnostics_.leakProbeSlidesObserved = 0;
    runtimeDiagnostics_.SetLeakProbePrevValid(false);
    runtimeDiagnostics_.leakProbePrevHwrFree = 0;
    runtimeDiagnostics_.leakProbePrevLwrFree = 0;
    runtimeDiagnostics_.leakProbePrevRetainedHwr = 0;
    runtimeDiagnostics_.leakProbePrevRetainedLwr = 0;
    runtimeDiagnostics_.lowWorkBaselineFree = 0;
    SetFullTrackFamilyCacheReady(false);
    lastWindowFreeBytes_ = 0;
    SetLastWindowFreeValid(false);
    SetActiveWindowLookupDirty(true);
    SetFamilyWorkingSetDirty(true);
    windowEntryIndexBySegmentId_.fill(-1);
    windowLogicalRankBySegmentId_.fill(-1);
    activeWindowLookupSegmentIds_.clear();
    activeWindowEntryIndexBySegmentId_.clear();
    activeWindowLogicalRankBySegmentId_.clear();
    ClearUsedTextureSlots(usedTextureSlotsThisFrame_);
    sh2SlaveSortTicksThisFrame_ = 0;
    sh2ProducerListUsedThisFrame_ = 0;
    sh2ProducerListFallbacksThisFrame_ = 0;
    soakMonitor_.Reset();
}

size_t TrackSystem::ResolveInitialLoadLimit(const Config& config) const
{
    if (kEnableTrackLeakIsolationFixed64Pipeline)
    {
        return std::min<size_t>(kTrackLeakIsolationWindowSegments, kTrackSegmentLimit);
    }
    // Test mode: keep the loaded catalog aligned with the configured visible segment budget
    // so we can isolate experiments on SEG_001 only. The full-catalog path stays available
    // behind this switch for future broader texture-mapping validation.
    constexpr bool kBuildFullCatalogForTextureMapping = false;
    if (kBuildFullCatalogForTextureMapping) return kTrackSegmentLimit;
    if (config.initialSegments == 0) return kTrackSegmentLimit;
    return std::min<size_t>(config.initialSegments, kTrackSegmentLimit);
}

void TrackSystem::PrepareInitialSegmentPackages(size_t loadLimit)
{
    const uint32_t desiredCap = std::max<uint32_t>(1u, static_cast<uint32_t>(loadLimit));
    fixedVisibleSegmentCap_ =
        std::min<uint32_t>(desiredCap,
                           std::max<uint32_t>(1u, static_cast<uint32_t>(totalSegmentCount_)));
    (void)RebuildActiveSegmentWindow(1, fixedVisibleSegmentCap_, +1);
    trackedCarSegmentId_ = 1;
    SetTrackedCarSegmentValid(true);
    targetWindowStartId_ = 1;
    TryPrefetchUpcomingSegment();
}

void TrackSystem::ApplyTrackSlaveMode()
{
    const bool effectiveUseSlave =
        TrackSlaveModeRequestedFlag() &&
        (!kEnableTrackRuntimeStabilization || kEnableStabilizedProducerOnSlave);
    const bool useSlaveDepthSort =
        TrackSlaveModeRequestedFlag() &&
        kEnableTrackRuntimeStabilization &&
        kEnableStabilizedDepthSortOnSlave;
    SetTrackSlaveProducerRequestedFlag(effectiveUseSlave);
    SetTrackSlaveDepthSortRequestedFlag(useSlaveDepthSort);
    producer_.SetUseSlave(effectiveUseSlave);
    stabilizedDepthSorter_.SetUseSlave(useSlaveDepthSort);
}

void TrackSystem::SetTrackSlaveMode(bool enabled)
{
    SetTrackSlaveModeRequestedFlag(enabled);
    ApplyTrackSlaveMode();
}

void TrackSystem::ConfigureCoordinatorAndBudget(const Config& config)
{
    // Safety guard: cap draws by configured visible segments.
    // In leak-isolation mode we keep a strict fixed 20-segment window
    // (10x64x64 near + 10x32x32 far) regardless of external config.
    const uint32_t kSegmentCap = static_cast<uint32_t>(kTrackSegmentLimit);
    const uint32_t fixedLeakIsolationSegments = kEnableTrackLeakIsolationFixed64Pipeline
        ? std::min<uint32_t>(
            static_cast<uint32_t>(kTrackLeakIsolationWindowSegments),
            kSegmentCap)
        : 0u;
    const uint32_t kSafeTrackDrawsPerFrame = kEnableTrackLeakIsolationFixed64Pipeline
        ? fixedLeakIsolationSegments
        : std::min<uint32_t>(
            std::max<uint32_t>(1u, config.initialSegments),
            kSegmentCap);
    TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::Config coordinatorConfig{};
    coordinatorConfig.budget.maxTrackSegments = kSafeTrackDrawsPerFrame;
    coordinatorConfig.budget.maxTrackMeshes = config.initialMeshes;
    coordinatorConfig.budget.maxTrackFaces = config.initialFaces;
    coordinatorConfig.chunkCapacity =
        std::max<size_t>(1u, static_cast<size_t>(coordinatorConfig.budget.maxTrackSegments));

    SetCoordinatorReady(coordinator_.Initialize(coordinatorConfig));
    if (!CoordinatorReady())
    {
        SRL::Debug::Print(1, 31, "TRK HWR alloc fail");
    }
    SetTrackSlaveModeRequestedFlag(config.useSlave);
    ApplyTrackSlaveMode();
    producer_.SetMaxFramesInFlight(3);
    producer_.SetRecoveryFrames(120);
    producer_.SetSafeModeStallThreshold(6);
    producer_.SetSafeModeCooldownFrames(45);
    SetTrackSlaveBarrierLockstepFlag(kEnableTrackSlaveBarrierLockstep);
    producer_.SetBlockUntilDone(TrackSlaveBarrierLockstepFlag());
    stabilizedDepthSorter_.SetMaxFramesInFlight(2);
    stabilizedDepthSorter_.SetRecoveryFrames(120);
    stabilizedDepthSorter_.SetSafeModeStallThreshold(5);
    stabilizedDepthSorter_.SetSafeModeCooldownFrames(45);
    stabilizedDepthSorter_.SetBlockUntilDone(TrackSlaveBarrierLockstepFlag());
    SRL::Debug::Print(1, 31, "TRK slv d:%u s:%u",
                      TrackSlaveProducerRequestedFlag() ? 1u : 0u,
                      TrackSlaveDepthSortRequestedFlag() ? 1u : 0u);
    if (kEnableTrackLeakIsolationFixed64Pipeline && kEnableLeakIsolationMixedLodProfile)
    {
        const uint32_t nearCount = std::min<uint32_t>(
            static_cast<uint32_t>(kLeakIsolationNearLodCount),
            kSafeTrackDrawsPerFrame);
        const uint32_t farCount =
            (kSafeTrackDrawsPerFrame > nearCount)
            ? (kSafeTrackDrawsPerFrame - nearCount)
            : 0u;
        SRL::Debug::Print(1, 30, "TRK win:%u 64:%u 32:%u",
                          static_cast<unsigned>(kSafeTrackDrawsPerFrame),
                          static_cast<unsigned>(nearCount),
                          static_cast<unsigned>(farCount));
    }

    AdaptiveTrackBudgetController::Limits adaptiveBudgetLimits{};
    if (kEnableTrackLeakIsolationFixed64Pipeline)
    {
        adaptiveBudgetLimits.minSegments = fixedLeakIsolationSegments;
        adaptiveBudgetLimits.maxSegments = fixedLeakIsolationSegments;
    }
    else
    {
        const uint32_t minSegmentsRequested =
            std::min<uint32_t>(std::max<uint32_t>(1u, config.minSegments), kSafeTrackDrawsPerFrame);
        const uint32_t maxSegmentsRequested =
            std::min<uint32_t>(std::max<uint32_t>(minSegmentsRequested, config.initialSegments), kSafeTrackDrawsPerFrame);
        const uint32_t maxSegmentsCap = kSegmentCap;
        adaptiveBudgetLimits.minSegments = std::min<uint32_t>(minSegmentsRequested, maxSegmentsCap);
        adaptiveBudgetLimits.maxSegments = std::min<uint32_t>(maxSegmentsRequested, maxSegmentsCap);
    }
    // Lock mesh/face budget to configured startup values to avoid runtime shrink.
    adaptiveBudgetLimits.minMeshes = std::max<uint32_t>(1u, config.initialMeshes);
    adaptiveBudgetLimits.maxMeshes = adaptiveBudgetLimits.minMeshes;
    adaptiveBudgetLimits.minFaces = std::max<uint32_t>(1000u, config.initialFaces);
    adaptiveBudgetLimits.maxFaces = adaptiveBudgetLimits.minFaces;
    budgetController_ = AdaptiveTrackBudgetController(adaptiveBudgetLimits);
}

void TrackSystem::LoadSurfaceCollisionMaps()
{
    ResetFamilyLookupTables();
    SetSurfaceFamilyMapReady(false);
    SetSegmentCollisionMapReady(false);
    segmentSurfaceFlagsById_.clear();

    if (totalSegmentCount_ == 0) return;
    if (!Game::PhysicsFeatureFlags::kEnableScmapRuntime) return;

    const char* sfMapCandidates[] = {
        "/CD/DATA/SFMAP.BIN", "/CD/DATA/SFMAP.BIN;1",
        "/DATA/SFMAP.BIN", "/DATA/SFMAP.BIN;1",
        "CD/DATA/SFMAP.BIN", "CD/DATA/SFMAP.BIN;1",
        "DATA/SFMAP.BIN", "DATA/SFMAP.BIN;1",
        "cd/data/SFMAP.BIN", "cd/data/SFMAP.BIN;1",
        "SFMAP.BIN", "SFMAP.BIN;1"
    };
    const char* scMapCandidates[] = {
        "/CD/DATA/SCMAP.BIN", "/CD/DATA/SCMAP.BIN;1",
        "/DATA/SCMAP.BIN", "/DATA/SCMAP.BIN;1",
        "CD/DATA/SCMAP.BIN", "CD/DATA/SCMAP.BIN;1",
        "DATA/SCMAP.BIN", "DATA/SCMAP.BIN;1",
        "cd/data/SCMAP.BIN", "cd/data/SCMAP.BIN;1",
        "SCMAP.BIN", "SCMAP.BIN;1"
    };

    std::vector<uint8_t> sfBlob{};
    if (ReadCdFileBinary(sfMapCandidates, sizeof(sfMapCandidates) / sizeof(sfMapCandidates[0]), sfBlob) &&
        sfBlob.size() >= 12)
    {
        const uint32_t magic = ReadLe32(sfBlob.data() + 0);
        const uint16_t version = ReadLe16(sfBlob.data() + 4);
        const uint32_t entryCount = ReadLe32(sfBlob.data() + 8);
        const size_t needBytes = 12u + (static_cast<size_t>(entryCount) * 4u);
        if (magic == 0x314D4653u && version == 1u && needBytes <= sfBlob.size())
        {
            EnsureSurfaceFamilyLookupCapacity(static_cast<size_t>(entryCount) + 1u);
            size_t off = 12u;
            for (uint32_t i = 0; i < entryCount; ++i, off += 4u)
            {
                const uint16_t familyId = ReadLe16(sfBlob.data() + off + 0u);
                const uint8_t surfaceTypeId = sfBlob[off + 2u];
                if (static_cast<size_t>(familyId) >= surfaceTypeByFamilyId_.size())
                {
                    EnsureSurfaceFamilyLookupCapacity(static_cast<size_t>(familyId) + 1u);
                }
                if (familyId < surfaceTypeByFamilyId_.size())
                {
                    surfaceTypeByFamilyId_[familyId] = surfaceTypeId;
                }
            }
            SetSurfaceFamilyMapReady(true);
        }
    }

    std::vector<uint8_t> scBlob{};
    if (ReadCdFileBinary(scMapCandidates, sizeof(scMapCandidates) / sizeof(scMapCandidates[0]), scBlob) &&
        scBlob.size() >= 12)
    {
        const uint32_t magic = ReadLe32(scBlob.data() + 0);
        const uint16_t version = ReadLe16(scBlob.data() + 4);
        const uint32_t segmentCount = ReadLe32(scBlob.data() + 8);
        if (magic == 0x314D4353u && version == 1u)
        {
            TrackLowWorkU8Vector flagsById;
            flagsById.resize(static_cast<size_t>(totalSegmentCount_) + 1u, 0u);
            size_t off = 12u;
            bool parseOk = true;
            for (uint32_t si = 0; si < segmentCount; ++si)
            {
                if (off + 8u > scBlob.size())
                {
                    parseOk = false;
                    break;
                }
                const uint16_t segmentId = ReadLe16(scBlob.data() + off + 0u);
                const uint32_t runCount = ReadLe32(scBlob.data() + off + 4u);
                off += 8u;
                if (off + (static_cast<size_t>(runCount) * 8u) > scBlob.size())
                {
                    parseOk = false;
                    break;
                }
                uint8_t segmentFlags = 0u;
                for (uint32_t ri = 0; ri < runCount; ++ri)
                {
                    const size_t ro = off + (static_cast<size_t>(ri) * 8u);
                    const uint8_t runFlags = scBlob[ro + 5u];
                    segmentFlags = static_cast<uint8_t>(segmentFlags | runFlags);
                }
                off += static_cast<size_t>(runCount) * 8u;
                if (segmentId > 0 && static_cast<size_t>(segmentId) < flagsById.size())
                {
                    flagsById[segmentId] = segmentFlags;
                }
            }
            if (parseOk)
            {
                segmentSurfaceFlagsById_.swap(flagsById);
                SetSegmentCollisionMapReady(!segmentSurfaceFlagsById_.empty());
            }
        }
    }

    SRL::Debug::Print(1, 22, "SCM sf:%u sc:%u seg:%u",
                      SurfaceFamilyMapReady() ? 1u : 0u,
                      SegmentCollisionMapReady() ? 1u : 0u,
                      static_cast<unsigned>(totalSegmentCount_));
}

void TrackSystem::LogInitialSegmentDiagnostics() const
{
    if (!SegmentsReady())
    {
        SRL::Debug::Print(1, 28, "TRK skip: seg miss");
        if (lastSegmentPath_[0] != '\0')
        {
            SRL::Debug::Print(1, 29, "TRK last:%s", lastSegmentPath_);
        }
    }

    if (segmentRenderers_.empty()) return;

}

void TrackSystem::ApplyInitialSdrFamilySlots()
{
    // Build the initial per batch face slots from SDR family ids.
    constexpr bool kEnableMat8FixedIntegration = true;
    if (!kEnableMat8FixedIntegration) return;

    if (kEnableTrackRuntimeStabilization)
    {
        // Stabilized streaming must start from the visible-window cache only.
        // Carrying a boot-time full-track family catalog defeats the intended
        // 20-segment residency model and keeps extra runtime state alive.
        if (!seg1FamilySlots_.empty())
        {
            FamilySlotVector{}.swap(seg1FamilySlots_);
            InvalidateFamilySlotIndex();
        }
        SetFullTrackFamilyCacheReady(false);
        if (!RebuildTrackTextureResidencyForWindow())
        {
            SRL::Debug::Print(1, 19, "SDR lod fail");
            return;
        }
        SRL::Debug::Print(1, 20, "SDR ok:%u fl:%u fam:%u fu:%u",
                          static_cast<unsigned>(segmentRenderers_.size()),
                          0u,
                          static_cast<unsigned>(seg1FamilySlots_.size()),
                          FullTrackFamilyCacheReady() ? 1u : 0u);
        return;
    }

    FamilySlotVector familyLodSlots{};
    if (!BuildTrackFamilyLodSlots(familyLodSlots))
    {
        SRL::Debug::Print(1, 19, "SDR lod fail");
        return;
    }

    unsigned matOk = 0;
    unsigned matFail = 0;

    // Preserve already uploaded slot ids when the active segment window slides.
    if (!seg1FamilySlots_.empty())
    {
        for (auto& target : familyLodSlots)
        {
            for (const auto& current : seg1FamilySlots_)
            {
                if (current.familyId != target.familyId) continue;
                for (size_t li = 0; li < target.lodSlots.size(); ++li)
                {
                    if (target.lodSlots[li] == No_Texture && current.lodSlots[li] != No_Texture)
                    {
                        target.lodSlots[li] = current.lodSlots[li];
                    }
                }
                break;
            }
        }
    }

    size_t logicalRank = 0;
    for (size_t i = 0; i < segmentRenderers_.size(); ++i)
    {
        auto& seg = segmentRenderers_[i];
        if (!seg.renderer || !seg.lodState.Ready() || seg.lodState.faceFamilyIds.empty())
        {
            ++matFail;
            continue;
        }

        seg.lodState.currentFaceSlots.assign(seg.lodState.faceFamilyIds.size(), -1);
        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        const bool lodOk = !seg.lodState.HasPerFaceRankOffsets()
            ? RebuildSegmentFaceSlotsForLod(seg, desiredLodIndex, familyLodSlots)
            : RebuildSegmentFaceSlotsForBaseRank(seg, logicalRank, familyLodSlots);
        if (!lodOk)
        {
            ++matFail;
            logicalRank += std::max<size_t>(1, static_cast<size_t>(seg.logicalSegmentCount));
            continue;
        }
        (void)seg.renderer->ApplyFaceTextureSlotsGlobal(seg.lodState.currentFaceSlots);
        const bool missing =
            HasMissingRequiredFaceTextureSlots(seg.lodState.currentFaceSlots,
                                               &seg.lodState.faceFamilyIds);
        seg.lodState.currentBaseRank =
            (seg.lodState.HasPerFaceRankOffsets() && !missing)
                ? static_cast<int16_t>(logicalRank)
                : -1;
        seg.lodState.currentLodIndex = missing ? 0xFF : desiredLodIndex;
        logicalRank += std::max<size_t>(1, static_cast<size_t>(seg.logicalSegmentCount));
        ++matOk;
    }

    // Persist resolved VDP1 slots back to the shared family cache. When the full
    // track family catalog is active, discarding this state causes the runtime to
    // re-upload textures continuously after the first slide.
    seg1FamilySlots_ = familyLodSlots;
    InvalidateFamilySlotIndex();
    RefreshFamilyWorkingSet(false);
    SRL::Debug::Print(1, 20, "SDR ok:%u fl:%u fam:%u fu:%u",
                      matOk,
                      matFail,
                      static_cast<unsigned>(familyLodSlots.size()),
                      FullTrackFamilyCacheReady() ? 1u : 0u);
}

bool TrackSystem::Initialize(const Config& config)
{
    auto printInitRam = [](int row, const char* tag)
    {
        const auto hwr = SRL::Memory::HighWorkRam::GetReport();
        const auto lwr = SRL::Memory::LowWorkRam::GetReport();
        const auto crt = SRL::Memory::CartRam::GetReport();
        SRL::Debug::Print(1, row, "%s hf:%u lf:%u cf:%u",
                          tag,
                          static_cast<unsigned>(hwr.FreeSize),
                          static_cast<unsigned>(lwr.FreeSize),
                          static_cast<unsigned>(crt.FreeSize));
    };

    ResetInitializationState();
    if (!BuildSegmentCenterCatalog())
    {
        SRL::Debug::Print(1, 28, "TRK cat miss");
        return false;
    }
    LoadSurfaceCollisionMaps();
    // Boot-time init RAM snapshots disabled to keep the on-screen diagnostics
    // focused on slide/runtime behavior.
    const size_t loadLimit = ResolveInitialLoadLimit(config);
    PrepareInitialSegmentPackages(loadLimit);
    if (kEnableTrackLeakIsolationFixed64Pipeline)
    {
        SRL::Debug::Print(1, 28, "TRK iso64 on n:%u slide:0 pf:0",
                          static_cast<unsigned>(loadLimit));
    }
    // disabled
    CaptureTrackTextureHeapBase();
    SegmentRuntimeDraw::HeaderV1 warmRdrHeader{};
    (void)LoadRdrHeaderForSegment(1, warmRdrHeader);
    if (totalSegmentCount_ > 1)
    {
        (void)LoadRdrHeaderForSegment(static_cast<int>(totalSegmentCount_), warmRdrHeader);
    }
    TrimRuntimeBlobScratchCaches(true);
    PrimeRuntimeScratchCapacities();
    if (!kEnableTrackRuntimeStabilization)
    {
        (void)PreloadFullTrackFamilyLodCache();
    }
    else
    {
        SetFullTrackFamilyCacheReady(false);
    }
    // disabled

    ConfigureCoordinatorAndBudget(config);
    LogInitialSegmentDiagnostics();
    ApplyInitialSdrFamilySlots();
    // Keep emergency reserve lazy. Startup is the most memory-sensitive phase
    // (track + car + background init), so reserve bytes are reacquired only
    // when runtime free HWR is comfortably above the configured floor.
    // This avoids boot-time starvation and emulator startup failures.
    // disabled

    // Keep the normal multi segment render path active even when only one segment
    // is visible, so single segment tests match the production flow.
    const bool enableSeg1Diagnostics = false;

    // Preload all referenced TGA files into Cart 4MB (diagnostic + fast path source cache).
    if (enableSeg1Diagnostics)
    {
        (void)PreloadTgaCatalogFromSegmentsMap();
        int32_t sCd = -1;
        int32_t sCart = -1;
        if (!seg1TgaCatalog_.empty() && seg1TgaCatalog_[0].cartPtr && seg1TgaCatalog_[0].size > 0)
        {
            DecodedTgaTexture d{};
            if (DecodePalettedTgaMemory(static_cast<const uint8_t*>(seg1TgaCatalog_[0].cartPtr), seg1TgaCatalog_[0].size, d))
            {
                sCart = UploadDecodedTextureToVdp1(d);
            }
        }
        SRL::Debug::Print(1, 5, "TXT cd:%d ct:%d c:%u j:%u",
                          (int)sCd, (int)sCart, (unsigned)seg1TgaPreloadCount_, (unsigned)seg1TgaJsonOk_);
    }

    // Minimal forced texture test for SEG_001 (diagnostic):
    // Apply one known texture slot to all faces to validate renderer texture path.
    if (enableSeg1Diagnostics)
    {
        constexpr bool kEnableSeg1ForcedTextureTest = false;
        if (kEnableSeg1ForcedTextureTest)
        {
            int32_t slot = -1;
            bool asfaltoFromCart = false;
            slot = TryUploadPalettedTgaFromCatalogByName(seg1TgaCatalog_, "asfalto_32.tga");
            asfaltoFromCart = (slot > 0);
            SRL::Debug::Print(1, 19, "S1 FORCE cart hit:%u cat:%u", asfaltoFromCart ? 1u : 0u, (unsigned)seg1TgaCatalog_.size());
            if (slot < 0) slot = TryLoadTextureFromCd("asfalto_32.tga");
            if (slot < 0) slot = TryLoadTextureFromCd("ASFALTO_32.TGA");
            if (slot > 0)
            {
                for (auto& seg : segmentRenderers_)
                {
                    if (seg.id != 1 || !seg.renderer) continue;
                    const size_t faces = static_cast<size_t>(seg.renderer->FaceCount());
                    if (faces == 0) break;
                    const size_t applied = seg.renderer->ForceTextureAll(static_cast<uint16_t>(slot));
                    TrackLowWorkI16Vector probe{};
                    seg.renderer->CollectFaceTextureSlotsGlobal(probe);
                    const int32_t first = probe.empty() ? -1 : probe[0];
                    SRL::Debug::Print(1, 19, "S1 FORCE tex:%d ap:%u f:%u p0:%d", slot, (unsigned)applied, (unsigned)faces, (int)first);
                    break;
                }
            }
            else
            {
                SRL::Debug::Print(1, 19, "S1 FORCE tex load fail cat:%u", (unsigned)seg1TgaCatalog_.size());
            }
        }
    }

    // Single-face overwrite probe (disabled - unstable in current runtime path).
    if (enableSeg1Diagnostics)
    {
        constexpr bool kEnableSeg1SingleFaceSwapProbe = false;
        if (kEnableSeg1SingleFaceSwapProbe)
        {
            TrackRenderer* seg1Renderer = nullptr;
            for (auto& seg : segmentRenderers_)
            {
                if (seg.id == 1 && seg.renderer) { seg1Renderer = seg.renderer.get(); break; }
            }
            if (seg1Renderer)
            {
                seg1Renderer->CollectFaceTextureSlotsGlobal(seg1SingleFaceSlots_);
                if (!seg1SingleFaceSlots_.empty())
                {
                    int16_t chosenFace = -1;
                    int16_t chosenBase = -1;
                    for (size_t i = 0; i < seg1SingleFaceSlots_.size(); ++i)
                    {
                        if (seg1SingleFaceSlots_[i] > 0)
                        {
                            chosenFace = static_cast<int16_t>(i);
                            chosenBase = seg1SingleFaceSlots_[i];
                            break;
                        }
                    }

                    if (chosenFace >= 0 && chosenBase > 0)
                    {
                        auto overwriteWithCandidates = [&](int32_t slot, const char* a, const char* b) -> bool
                        {
                            if (TryOverwriteTextureSlotFromCd(slot, a)) return true;
                            if (TryOverwriteTextureSlotFromCd(slot, b)) return true;
                            return false;
                        };

                        // Prepare/validate compatibility by writing base then alt then base again.
                        const bool baseOk0 = overwriteWithCandidates(chosenBase, "asfalto_32.tga", "ASFALTO_32.TGA");
                        const bool altOk = overwriteWithCandidates(chosenBase, "area_escape_32.tga", "AREA_ESCAPE_32.TGA");
                        const bool baseOk1 = overwriteWithCandidates(chosenBase, "asfalto_32.tga", "ASFALTO_32.TGA");
                        if (baseOk0 && altOk && baseOk1)
                        {
                            seg1SingleFaceSwapFace_ = chosenFace;
                            seg1SingleFaceSwapBaseSlot_ = chosenBase;
                            seg1SingleFaceSwapAltSlot_ = chosenBase; // same slot overwrite mode
                            SetSeg1SingleFaceSwapReady(true);
                            SRL::Debug::Print(1, 20, "S1O ok f:%u s:%u",
                                              (unsigned)seg1SingleFaceSwapFace_,
                                              (unsigned)seg1SingleFaceSwapBaseSlot_);
                        }
                        else
                        {
                            SRL::Debug::Print(1, 20, "S1O skip");
                        }
                    }
                    else
                    {
                        SRL::Debug::Print(1, 20, "S1O noface");
                    }
                }
            }
        }
    }

    // Componentized pipeline probe (phase 1):
    // Validate and optionally render SEG_001 from GEO/MAT component files.
    if (enableSeg1Diagnostics)
    {
        constexpr bool kUseSeg1ComponentRenderer = false; // usar renderer normal da pista para LOD swap
        SegmentComponent::Blob geoBlob{};
        SegmentComponent::Blob matBlob{};
        SegmentComponent::Loader::GeoView geoView{};
        SegmentComponent::Loader::MatView matView{};
        const bool geoParsed = LoadGeoForSegment(1, geoBlob, geoView);
        const bool matParsed = LoadMat8ForSegment(1, matBlob, matView);
        const bool geoOk = geoParsed;
        const bool matOk = matParsed;
        constexpr bool kShowSeg1ComponentProbeLogs = false;
        if (kShowSeg1ComponentProbeLogs)
        {
            SRL::Debug::Print(1, 24, "C1 G:%d(%u) M:%d(%u)",
                              geoOk ? 1 : 0, (unsigned)geoBlob.size,
                              matOk ? 1 : 0, (unsigned)matBlob.size);
        }
        const bool pairOk = SegmentComponent::Loader::ValidateGeoMatPair(geoView, matView);
        if (kShowSeg1ComponentProbeLogs)
        {
            SRL::Debug::Print(1, 25, "S1 P g:%d m:%d p:%d",
                              geoParsed ? 1 : 0, matParsed ? 1 : 0, pairOk ? 1 : 0);
        }
        if (geoParsed)
        {
            if (kShowSeg1ComponentProbeLogs)
            {
                SRL::Debug::Print(1, 26, "CMP GEO sid:%u v:%u f:%u",
                                  (unsigned)geoView.file.segmentId,
                                  (unsigned)geoView.header.vertexCount,
                                  (unsigned)geoView.header.faceCount);
            }
        }
        if (matParsed)
        {
            if (kShowSeg1ComponentProbeLogs)
            {
                SRL::Debug::Print(1, 27, "CMP MAT sid:%u f:%u",
                                  (unsigned)matView.file.segmentId,
                                  (unsigned)matView.header.faceCount);
            }
        }
        if (pairOk)
        {
            TrackRenderer* seg1Renderer = nullptr;
            SegmentRenderEntry* seg1Entry = nullptr;
            for (auto& seg : segmentRenderers_)
            {
                if (seg.id == 1 && seg.renderer) { seg1Renderer = seg.renderer.get(); seg1Entry = &seg; break; }
            }
            if (seg1Renderer)
            {
                const uint32_t rv = seg1Renderer->VertexCount();
                const uint32_t rf = seg1Renderer->FaceCount();
                const int vOk = (rv == geoView.header.vertexCount) ? 1 : 0;
                const int fOk = (rf == geoView.header.faceCount) ? 1 : 0;
                if (kShowSeg1ComponentProbeLogs)
                {
                    SRL::Debug::Print(1, 28, "C1 N v:%u/%u(%d) f:%u/%u(%d)",
                                      (unsigned)geoView.header.vertexCount, (unsigned)rv, vOk,
                                      (unsigned)geoView.header.faceCount, (unsigned)rf, fOk);
                }
            }

            if (seg1Renderer && seg1Entry)
            {
                seg1ComponentVerts_.clear();
                seg1ComponentFaces_.clear();
                seg1ComponentAttrs_.clear();
                seg1ComponentVerts_.reserve(static_cast<size_t>(geoView.header.vertexCount));
                seg1ComponentFaces_.reserve(static_cast<size_t>(geoView.header.faceCount));
                seg1ComponentAttrs_.reserve(static_cast<size_t>(geoView.header.faceCount));

                SRL::Math::Types::Vector3D minv(32767, 32767, 32767);
                SRL::Math::Types::Vector3D maxv(-32768, -32768, -32768);

                for (uint32_t vi = 0; vi < geoView.header.vertexCount; ++vi)
                {
                    SegmentComponent::GeoVertex gv{};
                    const size_t off = geoView.vertexOffset + static_cast<size_t>(vi) * sizeof(SegmentComponent::GeoVertex);
                    if (!SegmentComponent::Loader::ReadGeoVertexLeAt(geoBlob.bytes, off, gv))
                    {
                        seg1ComponentVerts_.clear();
                        break;
                    }
                    Vector3D v(
                        SRL::Math::Types::Fxp::BuildRaw(gv.x),
                        SRL::Math::Types::Fxp::BuildRaw(gv.y),
                        SRL::Math::Types::Fxp::BuildRaw(gv.z));
                    minv.X = SRL::Math::Min(minv.X, v.X);
                    minv.Y = SRL::Math::Min(minv.Y, v.Y);
                    minv.Z = SRL::Math::Min(minv.Z, v.Z);
                    maxv.X = SRL::Math::Max(maxv.X, v.X);
                    maxv.Y = SRL::Math::Max(maxv.Y, v.Y);
                    maxv.Z = SRL::Math::Max(maxv.Z, v.Z);
                    seg1ComponentVerts_.push_back(v);
                }

                // Load texture catalogs from TEXBANK_{8,16,32,64}.BIN into cart RAM and upload to VDP1.
                int familyIdsUsed[512]{};
                size_t familyIdsUsedCount = 0;

                for (uint32_t fi = 0; fi < matView.header.faceCount; ++fi)
                {
                    SegmentComponent::MatFaceBinding mb{};
                    const size_t moff = matView.bindingOffset + static_cast<size_t>(fi) * sizeof(SegmentComponent::MatFaceBinding);
                    if (!SegmentComponent::Loader::ReadMatFaceBindingLeAt(matBlob.bytes, moff, mb)) continue;
                    const int fam = static_cast<int>(mb.materialId);
                    if (fam <= 0) continue;
                    bool exists = false;
                    for (size_t u = 0; u < familyIdsUsedCount; ++u)
                    {
                        if (familyIdsUsed[u] == fam) { exists = true; break; }
                    }
                    if (!exists && familyIdsUsedCount < 512)
                    {
                        familyIdsUsed[familyIdsUsedCount++] = fam;
                    }
                }

                InitializeFamilySlots(seg1FamilySlots_, familyIdsUsed, familyIdsUsedCount);
                size_t texLoaded = 0;
                size_t texFail = 0;
                size_t texMissFamily = 0;
                size_t texDecodeFail = 0;
                size_t texUploadFail = 0;
                size_t texFallbackRecovered = 0;
                uint16_t texLastUnresolvedFam = 0;
                int texLastUnresolvedLod = 0;
                int texLastRecoveredDstLod = 0;
                int texLastRecoveredSrcLod = 0;

                auto loadTexbankToCart = [&](size_t li) -> bool
                {
                    return LoadSeg1TexbankIndexToCart(li, kSeg1FamilyLodValues[li]);
                };

                for (size_t li = 0; li < 4; ++li)
                {
                    if (!loadTexbankToCart(li))
                    {
                        texFail += familyIdsUsedCount;
                        continue;
                    }

                    for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                    {
                        const uint16_t fam = seg1FamilySlots_[u].familyId;
                        bool sawMissingFamily = false;
                        bool sawDecodeFail = false;
                        bool sawUploadFail = false;
                        int loadedFromLodValue = 0;
                        const bool loadedForRequestedLod = TryLoadFamilyLodSlot(seg1FamilySlots_[u],
                                                                                 static_cast<uint8_t>(li),
                                                                                 /*fallbackToLowerLods*/true,
                                                                                 /*fallbackToHigherLods*/false,
                                                                                 &sawMissingFamily,
                                                                                 &sawDecodeFail,
                                                                                 &sawUploadFail,
                                                                                 &loadedFromLodValue);

                        if (loadedForRequestedLod)
                        {
                            ++texLoaded;
                            if (loadedFromLodValue != kSeg1FamilyLodValues[li])
                            {
                                ++texFallbackRecovered;
                                texLastRecoveredDstLod = kSeg1FamilyLodValues[li];
                                texLastRecoveredSrcLod = loadedFromLodValue;
                            }
                        }
                        else
                        {
                            ++texFail;
                            texLastUnresolvedFam = fam;
                            texLastUnresolvedLod = kSeg1FamilyLodValues[li];
                            if (sawDecodeFail) ++texDecodeFail;
                            else if (sawUploadFail) ++texUploadFail;
                            else if (sawMissingFamily) ++texMissFamily;
                            else ++texMissFamily;
                        }
                    }
                }
                SRL::Debug::Print(1, 17, "S1 FB ok:%u u:%u l:%d %d>%d",
                                  (unsigned)texFallbackRecovered,
                                  (unsigned)texLastUnresolvedFam,
                                  texLastUnresolvedLod,
                                  texLastRecoveredDstLod,
                                  texLastRecoveredSrcLod);

                // Fallback path from preloaded cart catalog only (no direct CD reads).
                if (texLoaded == 0 && !seg1FamilySlots_.empty())
                {
                    std::vector<char> jsonText{};
                    Segment1TextureJson map1{};
                    const bool jsonOk = LoadSeg1TextureJsonFromLegacyMapCd(jsonText, map1);
                    std::vector<char> renText{};
                    RenTextureMap renMap{};
                    const bool renOk = LoadSeg1RenTextureCopyMapFromCd(renText, renMap);
                    if (jsonOk)
                    {
                        char fileNorm[64]{};
                        bool firstMapMissLogged = false;
                        for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                        {
                            const int fam = static_cast<int>(seg1FamilySlots_[u].familyId);
                            const int fi = FindFamilyIndex(map1, fam);
                            if (fi < 0) continue;

                            NormalizeTextureFileName(map1.familyTex64[fi], fileNorm, sizeof(fileNorm));
                            bool anyLoaded = false;
                            for (size_t li = 0; li < 4; ++li)
                            {
                                const int32_t slot =
                                    TryUploadSeg1FamilyLodFromCatalog(seg1TgaCatalog_,
                                                                      fam,
                                                                      fileNorm,
                                                                      kSeg1FamilyLodValues[li],
                                                                      renOk ? &renMap : nullptr);
                                if (slot >= 0)
                                {
                                    seg1FamilySlots_[u].lodSlots[li] = static_cast<uint16_t>(slot);
                                    anyLoaded = true;
                                }
                                else if (!firstMapMissLogged)
                                {
                                    firstMapMissLogged = true;
                                    SRL::Debug::Print(1, 16, "S1MM f:%d l:%d", fam, kSeg1FamilyLodValues[li]);
                                }
                            }
                            if (anyLoaded) ++texLoaded; else ++texFail;
                        }
                        SRL::Debug::Print(1, 28, "S1R ok:%u c:%u", renOk ? 1u : 0u, (unsigned)renMap.entries.size());
                        if (!renOk)
                        {
                            SRL::Debug::Print(1, 18, "S1R miss");
                        }
                    }
                }
                SRL::Debug::Print(1, 30, "S1T ok:%u fl:%u fm:%u",
                                  (unsigned)texLoaded, (unsigned)texFail, (unsigned)familyIdsUsedCount);
                SRL::Debug::Print(1, 18, "S1T ms:%u de:%u up:%u",
                                  (unsigned)texMissFamily, (unsigned)texDecodeFail, (unsigned)texUploadFail);

                // Build fallback remap tables for TrackRenderer path (global face order).
                // This allows LOD swap even when component renderer is disabled.
                SetSeg1RendererLodReady(false);
                for (auto& v : seg1RendererFaceSlotsByLod_) v.clear();
                if (seg1Renderer)
                {
                    Segment1TextureJson map1ForSeg{};
                    bool map1ForSegOk = false;
                    if (g_seg1MapCacheValid && !g_seg1MapCache.faceFamily.empty())
                    {
                        map1ForSeg = g_seg1MapCache;
                        map1ForSegOk = true;
                    }
                    else
                    {
                        map1ForSegOk = LoadSeg1FaceFamilyMapFromCd(map1ForSeg);
                    }

                    if (!map1ForSegOk)
                    {
                        std::vector<char> jsonText{};
                        map1ForSegOk = LoadSeg1TextureJsonFromAnyMapCd(jsonText, map1ForSeg) &&
                                       !map1ForSeg.faceFamily.empty();
                    }

                    const size_t rendererFaces = static_cast<size_t>(seg1Renderer->FaceCount());
                    const size_t mapFaces = static_cast<size_t>(matView.header.faceCount);
                    const size_t nFaces = std::min(rendererFaces, mapFaces);
                    for (size_t li = 0; li < 4; ++li)
                    {
                        auto& slots = seg1RendererFaceSlotsByLod_[li];
                        slots.assign(rendererFaces, -1);
                        size_t mappedFaces = 0;

                        // Prefer face order from MAT because it is generated from the same OBJ/MTL
                        // used to define per-face material assignment. Use S001FAM/SMAP only to
                        // fill any remaining unmapped faces.
                        for (size_t fi = 0; fi < nFaces; ++fi)
                        {
                            SegmentComponent::MatFaceBinding mb{};
                            const size_t moff = matView.bindingOffset + fi * sizeof(SegmentComponent::MatFaceBinding);
                            if (!SegmentComponent::Loader::ReadMatFaceBindingLeAt(matBlob.bytes, moff, mb)) continue;
                            const uint16_t fam = static_cast<uint16_t>(mb.materialId);
                            if (fam == 0) continue;
                            uint16_t slot = No_Texture;
                            if (TryGetFamilyLodSlot(seg1FamilySlots_, fam, static_cast<uint8_t>(li), slot))
                            {
                                slots[fi] = static_cast<int32_t>(slot);
                                ++mappedFaces;
                            }
                        }

                        if (map1ForSegOk)
                        {
                            const size_t faceCount = std::min(rendererFaces, map1ForSeg.faceFamily.size());
                            for (size_t fi = 0; fi < faceCount; ++fi)
                            {
                                if (slots[fi] >= 0) continue;
                                const uint16_t fam = static_cast<uint16_t>(map1ForSeg.faceFamily[fi]);
                                if (fam == 0) continue;
                                uint16_t slot = No_Texture;
                                if (TryGetFamilyLodSlot(seg1FamilySlots_, fam, static_cast<uint8_t>(li), slot))
                                {
                                    slots[fi] = static_cast<int32_t>(slot);
                                    ++mappedFaces;
                                }
                            }
                        }
                        // Safety fallback: if no face was mapped for this LOD but we have at least
                        // one valid slot, force-apply that slot to all faces to validate path.
                        if (mappedFaces == 0 && !seg1FamilySlots_.empty())
                        {
                            const uint16_t fallbackSlot = seg1FamilySlots_[0].lodSlots[li];
                            if (fallbackSlot != No_Texture)
                            {
                                for (size_t fi = 0; fi < rendererFaces; ++fi)
                                {
                                    slots[fi] = static_cast<int32_t>(fallbackSlot);
                                }
                                mappedFaces = rendererFaces;
                            }
                        }
                        SRL::Debug::Print(1, 18, "S1M%d:%u mp:%u", kSeg1FamilyLodValues[li], (unsigned)mappedFaces, map1ForSegOk ? 1u : 0u);
                    }
                    SetSeg1RendererLodReady((rendererFaces > 0));
                    if (Seg1RendererLodReady())
                    {
                        // Apply initial LOD map immediately.
                        (void)seg1Renderer->ApplyFaceTextureSlotsGlobal(seg1RendererFaceSlotsByLod_[seg1CurrentLodIndex_]);
                    }
                    SRL::Debug::Print(1, 20, "S1Rdy:%d f:%u fm:%u",
                                      Seg1RendererLodReady() ? 1 : 0,
                                      (unsigned)rendererFaces,
                                      (unsigned)familyIdsUsedCount);
                    if (texLoaded == 0)
                    {
                        SRL::Debug::Print(1, 18, "S1Ms c:%u u:%u", (unsigned)texLoaded, (unsigned)texUploadFail);
                    }
                }
                else
                {
                    SRL::Debug::Print(1, 20, "S1Rdy:0 f:0 fm:%u", (unsigned)familyIdsUsedCount);
                }

                seg1FaceFamilyIds_.clear();
                seg1FaceFamilyIds_.reserve(static_cast<size_t>(geoView.header.faceCount));
                for (uint32_t fi = 0; fi < geoView.header.faceCount && !seg1ComponentVerts_.empty(); ++fi)
                {
                    SegmentComponent::GeoFace gf{};
                    const size_t off = geoView.faceOffset + static_cast<size_t>(fi) * sizeof(SegmentComponent::GeoFace);
                    if (!SegmentComponent::Loader::ReadGeoFaceLeAt(geoBlob.bytes, off, gf))
                    {
                        seg1ComponentFaces_.clear();
                        seg1ComponentAttrs_.clear();
                        break;
                    }
                    SRL::Types::Polygon p{};
                    for (size_t c = 0; c < 4; ++c)
                    {
                        p.Vertices[c] = gf.vertex[c];
                    }
                    if (gf.kind == static_cast<uint8_t>(SegmentComponent::FaceKind::Triangle))
                    {
                        p.Vertices[3] = p.Vertices[2];
                    }
                    p.Normal = BuildFaceNormalFromVerts(seg1ComponentVerts_, p.Vertices);
                    seg1ComponentFaces_.push_back(p);

                    uint16_t texIndex = No_Texture;
                    uint16_t baseColor = static_cast<uint16_t>(0x841F);
                    uint16_t drawMode = CL32KRGB;
                    uint32_t directionFlags = sprPolygon;
                    uint16_t shading = UseLight;

                    SegmentComponent::MatFaceBinding mb{};
                    const size_t moff = matView.bindingOffset + static_cast<size_t>(fi) * sizeof(SegmentComponent::MatFaceBinding);
                    uint16_t faceFamilyId = 0;
                    if (SegmentComponent::Loader::ReadMatFaceBindingLeAt(matBlob.bytes, moff, mb))
                    {
                        const int fam = static_cast<int>(mb.materialId);
                        const uint16_t m = static_cast<uint16_t>(mb.materialId & 0x1F);
                        baseColor = static_cast<uint16_t>(0x8400 | (m ? m : 0x1F));
                        faceFamilyId = static_cast<uint16_t>((fam > 0) ? fam : 0);
                        if (fam > 0)
                        {
                            uint16_t slot = No_Texture;
                            (void)TryGetFamilyLodSlot(seg1FamilySlots_,
                                                      static_cast<uint16_t>(fam),
                                                      seg1CurrentLodIndex_,
                                                      slot);
                            if (slot != No_Texture)
                            {
                                texIndex = slot;
                                baseColor = No_Palet;
                                drawMode = static_cast<uint16_t>(CL32KRGB | CL_Gouraud);
                                directionFlags =
                                    static_cast<uint32_t>(sprNoflip) |
                                    (static_cast<uint32_t>(ECdis) << 24);
                                shading = UseLight;
                            }
                        }
                    }
                    seg1FaceFamilyIds_.push_back(faceFamilyId);

                    seg1ComponentAttrs_.push_back(SRL::Types::Attribute(
                        SRL::Types::Attribute::FaceVisibility::DoubleSided,
                        SRL::Types::Attribute::SortMode::Center,
                        texIndex,
                        baseColor,
                        CL32KRGB,
                        drawMode,
                        directionFlags,
                        shading));
                }

                if (kUseSeg1ComponentRenderer &&
                    !seg1ComponentVerts_.empty() &&
                    seg1ComponentFaces_.size() == static_cast<size_t>(geoView.header.faceCount) &&
                    seg1ComponentAttrs_.size() == seg1ComponentFaces_.size())
                {
                    SetSeg1ComponentEnabled(true);
                    seg1ComponentCenter_ = (minv + maxv) / SRL::Math::Types::Fxp::BuildRaw(2 << 16);
                    seg1Entry->center = seg1ComponentCenter_;
                    if (kShowSeg1ComponentProbeLogs)
                    {
                        SRL::Debug::Print(1, 29, "C1 rdr:on v:%u f:%u",
                                          (unsigned)seg1ComponentVerts_.size(),
                                          (unsigned)seg1ComponentFaces_.size());
                    }
                }
                else
                {
                    SetSeg1ComponentEnabled(false);
                    if (kShowSeg1ComponentProbeLogs)
                    {
                        SRL::Debug::Print(1, 29, "C1 rdr:off");
                    }
                }
            }
        }

        // Fallback path (temporarily disabled for stability).
        constexpr bool kEnableSeg1JsonFallback = false;
        if (kEnableSeg1JsonFallback && !Seg1RendererLodReady())
        {
            TrackRenderer* seg1Renderer = nullptr;
            for (auto& seg : segmentRenderers_)
            {
                if (seg.id == 1 && seg.renderer) { seg1Renderer = seg.renderer.get(); break; }
            }
            if (seg1Renderer)
            {
                std::vector<char> jsonText{};
                Segment1TextureJson map1{};
                const bool jsonOk = LoadSeg1FaceFamilyArrayFromAnyMapCd(jsonText, map1);

                if (jsonOk && !map1.faceFamily.empty())
                {
                    int familyIdsUsed[512]{};
                    const size_t familyIdsUsedCount = BuildUniqueUsedFamilies(map1.faceFamily, familyIdsUsed, 512);

                    InitializeFamilySlots(seg1FamilySlots_, familyIdsUsed, familyIdsUsedCount);

                    auto loadTexbankToCart = [&](size_t li) -> bool
                    {
                        return LoadSeg1TexbankIndexToCart(li, kSeg1FamilyLodValues[li]);
                    };

                    size_t texLoaded = 0;
                    size_t texFail = 0;
                    size_t texMissFamily = 0;
                    size_t texDecodeFail = 0;
                    size_t texUploadFail = 0;
                    for (size_t li = 0; li < 4; ++li)
                    {
                        if (!loadTexbankToCart(li))
                        {
                            texFail += familyIdsUsedCount;
                            continue;
                        }

                        for (size_t u = 0; u < seg1FamilySlots_.size(); ++u)
                        {
                            bool sawMissingFamily = false;
                            bool sawDecodeFail = false;
                            bool sawUploadFail = false;
                            const bool loaded = TryLoadFamilyLodSlot(seg1FamilySlots_[u],
                                                                      static_cast<uint8_t>(li),
                                                                      /*fallbackToLowerLods*/false,
                                                                      /*fallbackToHigherLods*/false,
                                                                      &sawMissingFamily,
                                                                      &sawDecodeFail,
                                                                      &sawUploadFail,
                                                                      nullptr);
                            if (loaded)
                            {
                                ++texLoaded;
                            }
                            else
                            {
                                ++texFail;
                                if (sawDecodeFail) ++texDecodeFail;
                                else if (sawUploadFail) ++texUploadFail;
                                else ++texMissFamily;
                            }
                        }
                    }

                    for (auto& v : seg1RendererFaceSlotsByLod_) v.clear();
                    const size_t rendererFaces = static_cast<size_t>(seg1Renderer->FaceCount());
                    for (size_t li = 0; li < 4; ++li)
                    {
                        auto& slots = seg1RendererFaceSlotsByLod_[li];
                        slots.assign(rendererFaces, -1);
                        const size_t nFaces = std::min(rendererFaces, map1.faceFamily.size());
                        for (size_t fi = 0; fi < nFaces; ++fi)
                        {
                            const uint16_t fam = static_cast<uint16_t>(map1.faceFamily[fi] < 0 ? 0 : map1.faceFamily[fi]);
                            if (fam == 0) continue;
                            uint16_t slot = No_Texture;
                            if (TryGetFamilyLodSlot(seg1FamilySlots_, fam, static_cast<uint8_t>(li), slot))
                            {
                                slots[fi] = static_cast<int32_t>(slot);
                            }
                        }
                    }
                    SetSeg1RendererLodReady((rendererFaces > 0 && !seg1FamilySlots_.empty()));
                    if (Seg1RendererLodReady())
                    {
                        (void)seg1Renderer->ApplyFaceTextureSlotsGlobal(seg1RendererFaceSlotsByLod_[seg1CurrentLodIndex_]);
                    }
                    SRL::Debug::Print(1, 30, "S1 TEX ok:%u fl:%u fm:%u",
                                      (unsigned)texLoaded, (unsigned)texFail, (unsigned)familyIdsUsedCount);
                    SRL::Debug::Print(1, 18, "S1 TEX miss:%u dec:%u up:%u",
                                      (unsigned)texMissFamily, (unsigned)texDecodeFail, (unsigned)texUploadFail);
                    SRL::Debug::Print(1, 20, "S1 RDY:%d f:%u fm:%u",
                                      Seg1RendererLodReady() ? 1 : 0,
                                      (unsigned)rendererFaces,
                                      (unsigned)familyIdsUsedCount);
                }
            }
        }
    }

    // Dynamic texture upgrade test (phase 1): only SEG_001 uses 64x64 textures from JSON map.
    // Disabled for stability.
    constexpr bool kEnableSeg1JsonTextureUpgrade = false;
    if (kEnableSeg1JsonTextureUpgrade && !segmentRenderers_.empty())
    {
        std::vector<char> jsonText;
        if (ReadSeg1LegacySegmentsMapText(jsonText))
        {
            Segment1TextureJson map1{};
            if (ParseSegment1TextureJson(jsonText.data(), map1))
            {
                TrackRenderer* seg1Renderer = nullptr;
                for (auto& seg : segmentRenderers_)
                {
                    if (seg.id == 1 && seg.renderer) { seg1Renderer = seg.renderer.get(); break; }
                }
                if (!seg1Renderer)
                {
                    SRL::Debug::Print(1, 23, "S1 no rdr");
                }
                else
                {
                    const size_t seg1FaceCount = static_cast<size_t>(seg1Renderer->FaceCount());
                    if (seg1FaceCount == 0)
                    {
                        SRL::Debug::Print(1, 23, "S1 face 0");
                    }
                    else
                    {
                        if (map1.faceFamily.size() > seg1FaceCount)
                        {
                            // Strict bound to avoid any mismatch-induced overwrite risk.
                            map1.faceFamily.resize(seg1FaceCount);
                        }

                        TrackLowWorkI16Vector faceSlots;
                        seg1Renderer->CollectFaceTextureSlotsGlobal(faceSlots);
                        const size_t mapCount = std::min(map1.faceFamily.size(), std::min(faceSlots.size(), seg1FaceCount));
                        if (mapCount == 0)
                        {
                            SRL::Debug::Print(1, 23, "S1 no fmap");
                        }
                        else
                        {
                            int famIds[256]{};
                            int32_t famSlots[256]{};
                            size_t famCount = 0;
                            for (size_t fi = 0; fi < mapCount; ++fi)
                            {
                                const int fam = map1.faceFamily[fi];
                                const int32_t slot = faceSlots[fi];
                                if (fam < 0 || slot <= 0) continue;
                                bool exists = false;
                                for (size_t i = 0; i < famCount; ++i)
                                {
                                    if (famIds[i] == fam) { exists = true; break; }
                                }
                                if (!exists && famCount < 256)
                                {
                                    famIds[famCount] = fam;
                                    famSlots[famCount] = slot;
                                    ++famCount;
                                }
                            }

                            size_t okCount = 0;
                            size_t failCount = 0;
                            constexpr size_t kFamilyUpgradeCap = 24;
                            size_t upgraded = 0;
                            for (size_t i = 0; i < famCount; ++i)
                            {
                                if (upgraded >= kFamilyUpgradeCap) break;
                                const int fam = famIds[i];
                                const int fi = FindFamilyIndex(map1, fam);
                                if (fi < 0) continue;

                                char fileNorm[64]{};
                                NormalizeTextureFileName(map1.familyTex64[fi], fileNorm, sizeof(fileNorm));
                                if (TryOverwriteTextureSlotFromCd(famSlots[i], fileNorm))
                                {
                                    ++okCount;
                                    ++upgraded;
                                }
                                else
                                {
                                    ++failCount;
                                }
                            }
                            SRL::Debug::Print(1, 23, "S1 up ok:%u fl:%u fam:%u",
                                              (unsigned)okCount, (unsigned)failCount, (unsigned)famCount);
                        }
                    }
                }
            }
            else
            {
                SRL::Debug::Print(1, 23, "segmap parse S1");
            }
        }
        else
        {
            SRL::Debug::Print(1, 23, "segmap miss");
        }
    }

    // Phase A/B warmup (disabled for runtime stability on SH2).
    constexpr bool kEnableSeg1Warmup = false;
    constexpr bool kEnableSeg1WarmupTexbank = false;
    constexpr size_t kSeg1WarmupTexbankCount = 1; // incremental: 8 only
    if (kEnableSeg1Warmup)
    {
        Segment1TextureJson warmupMap{};
        const bool mapOk = LoadSeg1FaceFamilyMapFromCd(warmupMap);
        const auto& faceFamily = warmupMap.faceFamily;

        size_t famCount = 0;
        if (mapOk && !faceFamily.empty())
        {
            int familyIdsUsed[512]{};
            famCount = BuildUniqueUsedFamilies(faceFamily, familyIdsUsed, 512);
            if (seg1FamilySlots_.empty() && famCount > 0)
            {
                InitializeFamilySlots(seg1FamilySlots_, familyIdsUsed, famCount);
            }
        }

        size_t banksOk = 0;
        if (kEnableSeg1WarmupTexbank)
        {
            const size_t loadCount = std::min<size_t>(kSeg1WarmupTexbankCount, 4);
            for (size_t li = 0; li < loadCount; ++li)
            {
                if (LoadSeg1TexbankIndexToCart(li, kSeg1FamilyLodValues[li])) ++banksOk;
            }
        }

        SRL::Debug::Print(1, 20, "S1Map ok:%u f:%u fm:%u",
                          mapOk ? 1u : 0u,
                          mapOk ? (unsigned)faceFamily.size() : 0u,
                          (unsigned)famCount);
        SRL::Debug::Print(1, 21, "S1Bk ok:%u/4", (unsigned)banksOk);
    }

    // Keep track rendering available even when coordinator allocation fails.
    // RenderFrame will use a direct fallback path when coordinator is unavailable.
    SetReadyFlag(SegmentsReady());
    return ReadyFlag();
}

void TrackSystem::BeginFrame(uint32_t frameId)
{
    surfaceQueryCallsLastFrame_ = surfaceQueryCallsThisFrame_;
    surfaceQueryFallbackHitsLastFrame_ = surfaceQueryFallbackHitsThisFrame_;
    surfaceQueryGlobalPassesLastFrame_ = surfaceQueryGlobalPassesThisFrame_;
    surfaceQueryLocalOnlyMissesLastFrame_ = surfaceQueryLocalOnlyMissesThisFrame_;
    surfaceQueryScmapSkipsLastFrame_ = surfaceQueryScmapSkipsThisFrame_;
    surfaceQuerySegmentsScannedLastFrame_ = surfaceQuerySegmentsScannedThisFrame_;
    surfaceQueryFacesScannedLastFrame_ = surfaceQueryFacesScannedThisFrame_;
    surfaceQueryCacheHitsLastFrame_ = surfaceQueryCacheHitsThisFrame_;
    surfaceQueryCacheMissesLastFrame_ = surfaceQueryCacheMissesThisFrame_;
    wallQueryCallsLastFrame_ = wallQueryCallsThisFrame_;
    wallQueryHitsLastFrame_ = wallQueryHitsThisFrame_;
    wallQuerySegmentsScannedLastFrame_ = wallQuerySegmentsScannedThisFrame_;
    wallQueryFacesScannedLastFrame_ = wallQueryFacesScannedThisFrame_;
    surfaceQueryCallsThisFrame_ = 0u;
    surfaceQueryFallbackHitsThisFrame_ = 0u;
    surfaceQueryGlobalPassesThisFrame_ = 0u;
    surfaceQueryLocalOnlyMissesThisFrame_ = 0u;
    surfaceQueryScmapSkipsThisFrame_ = 0u;
    surfaceQuerySegmentsScannedThisFrame_ = 0u;
    surfaceQueryFacesScannedThisFrame_ = 0u;
    surfaceQueryCacheHitsThisFrame_ = 0u;
    surfaceQueryCacheMissesThisFrame_ = 0u;
    wallQueryCallsThisFrame_ = 0u;
    wallQueryHitsThisFrame_ = 0u;
    wallQuerySegmentsScannedThisFrame_ = 0u;
    wallQueryFacesScannedThisFrame_ = 0u;
    frameIdThisFrame_ = frameId;
    AdvanceReusableTrackTextureSlotCooldowns();
    textureUploadsThisFrame_ = 0;
    ClearUsedTextureSlots(usedTextureSlotsThisFrame_);
    runtimeRdrBuildsThisFrame_ = 0;
    runtimeSdrBuildsThisFrame_ = 0;
    runtimeFaceRemapsThisFrame_ = 0;
    runtimeSlidesThisFrame_ = 0;
    runtimeSlideStallsThisFrame_ = 0;
    runtimePrefetchHitsThisFrame_ = 0;
    runtimePrefetchMissesThisFrame_ = 0;
    runtimeLodSegmentUpdatesThisFrame_ = 0;
    runtimeSafeRenderedThisFrame_ = 0;
    runtimeSafeSkippedThisFrame_ = 0;
    runtimeSafeNoDrawThisFrame_ = 0;
    runtimeSafeReappliedThisFrame_ = 0;
    sh2MasterStreamTicksThisFrame_ = 0;
    sh2MasterDrawTicksThisFrame_ = 0;
    sh2MasterFrameTicksThisFrame_ = 0;
    sh2MasterMaintenanceTicksThisFrame_ = 0;
    sh2MasterWindowTicksThisFrame_ = 0;
    sh2MasterPrefetchTicksThisFrame_ = 0;
    sh2MasterPlanTicksThisFrame_ = 0;
    sh2MasterLodTicksThisFrame_ = 0;
    sh2MasterWorkingSetTicksThisFrame_ = 0;
    sh2SlavePlanTicksThisFrame_ = 0;
    sh2SlaveSortTicksThisFrame_ = 0;
    sh2ProducerListUsedThisFrame_ = 0;
    sh2ProducerListFallbacksThisFrame_ = 0;
    workRamMaintenance_.releasedNowSlotsThisFrame = 0;
    workRamMaintenance_.releasedEndFrameSlotsThisFrame = 0;
    workRamMaintenance_.releasedPrefetchNowThisFrame = 0;
    g_trackUploadsFreshThisFrame = 0u;
    g_trackUploadsReusedThisFrame = 0u;
    g_trackRetiredQueuedThisFrame = 0u;
    g_trackRetiredFlushedThisFrame = 0u;
    workRamMaintenance_.memoryPressureLevelThisFrame = 0;
    frameMemoryTelemetry_.phaseHwrBeforeStream = 0;
    frameMemoryTelemetry_.phaseHwrAfterStream = 0;
    frameMemoryTelemetry_.phaseHwrAfterDraw = 0;
    frameMemoryTelemetry_.phaseHwrEnd = 0;
    frameMemoryTelemetry_.phaseLwrBeforeStream = 0;
    frameMemoryTelemetry_.phaseLwrAfterStream = 0;
    frameMemoryTelemetry_.phaseLwrAfterDraw = 0;
    frameMemoryTelemetry_.phaseLwrEnd = 0;
    frameMemoryTelemetry_.drawPrepareDeltaThisFrame = 0;
    frameMemoryTelemetry_.drawExecuteDeltaThisFrame = 0;
    frameMemoryTelemetry_.drawOtherDeltaThisFrame = 0;
    frameMemoryTelemetry_.drawFrameDeltaThisFrame = 0;
    slideHwrTrace_.segmentId = -1;
    slideHwrTrace_.flags = 0u;
    slideHwrTrace_.check = 0u;
    slideHwrTrace_.afterTrim = 0u;
    slideHwrTrace_.afterResetPrefetch = 0u;
    slideHwrTrace_.afterBuildPrefetch = 0u;
    slideHwrTrace_.afterPrepare = 0u;
    slideHwrTrace_.afterCommit = 0u;
    prefetchBuildAttemptsThisFrame_ = 0u;
    prefetchBuildBudgetDropsThisFrame_ = 0u;
    {
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        if (kEnableTrackRuntimeStabilization)
        {
            int32_t backlog = 0;
            const int8_t slideDirection = (windowDirection_ < 0) ? -1 : 1;
            if (SegmentsReady() && totalSegmentCount_ > 0)
            {
                const int32_t currentStartId =
                    WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
                const int32_t targetStartId =
                    WrapSegmentIdToRange(targetWindowStartId_, totalSegmentCount_);
                if (currentStartId > 0 && targetStartId > 0)
                {
                    const int32_t total = static_cast<int32_t>(totalSegmentCount_);
                    // Direction aware backlog:
                    // +1 uses forward wrap distance current -> target.
                    // -1 uses backward wrap distance current -> target.
                    backlog = (slideDirection > 0)
                        ? ((targetStartId - currentStartId) % total)
                        : ((currentStartId - targetStartId) % total);
                    if (backlog < 0) backlog += total;
                    if (backlog >= (total / 2)) backlog = 0;
                }
            }

            const bool freeCritical =
                freeValid && freeBytes <= (kWorkRamHardFloorBytes + (24u * 1024u));
            const bool freeVeryHealthy =
                freeValid && freeBytes > (kWorkRamHardFloorBytes + (128u * 1024u));
            const bool speedTier1 =
                PrefetchSpeedProxyValid() &&
                prefetchSpeedProxyRaw_ >= kPrefetchSpeedTier1UnitsPerFrame;
            const bool speedTier2 =
                PrefetchSpeedProxyValid() &&
                prefetchSpeedProxyRaw_ >= kPrefetchSpeedTier2UnitsPerFrame;

            uint8_t budget = freeVeryHealthy ? 2u : 1u;
            if (backlog >= 2) budget = std::max<uint8_t>(budget, 2u);
            if (backlog >= 3 && freeVeryHealthy) budget = 3u;
            if (!freeCritical && speedTier1) budget = std::max<uint8_t>(budget, 2u);
            if (!freeCritical && speedTier2 && freeVeryHealthy)
            {
                budget = std::max<uint8_t>(budget, 3u);
            }
            if (freeCritical) budget = 1u;
            prefetchBuildBudgetThisFrame_ = budget;
        }
        else
        {
            prefetchBuildBudgetThisFrame_ =
                (freeValid && freeBytes <= (kWorkRamHardFloorBytes + (32u * 1024u))) ? 1u : 2u;
        }
    }
    stabilizedDepthStats_ = {};
    ResetFramePlan(framePlanCurrent_);
    framePlanSortedHandles_.clear();
    {
        LWR_PROBE_BEGIN();
        if (kEnableTrackRuntimeStabilization &&
            SegmentsReady() &&
            totalSegmentCount_ > 0)
        {
            const int8_t slideDirection = (windowDirection_ < 0) ? -1 : 1;
            const int32_t currentStartId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
            const int32_t targetStartId = WrapSegmentIdToRange(targetWindowStartId_, totalSegmentCount_);
            if (currentStartId > 0 && targetStartId > 0)
            {
                const int32_t total = static_cast<int32_t>(totalSegmentCount_);
                // Direction aware catch-up for both forward and reverse travel.
                int32_t backlog = (slideDirection > 0)
                    ? ((targetStartId - currentStartId) % total)
                    : ((currentStartId - targetStartId) % total);
                if (backlog < 0) backlog += total;
                if (backlog > 0 && backlog < (total / 2))
                {
                    // Safe mode must still behave as a strict sliding window, but
                    // when the car moves faster than one segment per frame we need
                    // limited catch-up. Otherwise the car outruns the 20-segment
                    // window even without an explicit slide stall.
                    // Avoid 2-slide bursts in normal pressure; they create visible
                    // frame spikes exactly at segment boundaries.
                    const uint8_t maxCatchupSlides = 1u;
                    const uint8_t slideBudget = static_cast<uint8_t>(
                        std::min<int32_t>(backlog, static_cast<int32_t>(maxCatchupSlides)));
                    for (uint8_t i = 0; i < slideBudget; ++i)
                    {
                        TryPrefetchUpcomingSegment();
                        if (!SlideActiveSegmentWindow(1, slideDirection))
                        {
                            bool freeValid = false;
                            const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
                            const uint8_t stallCooldown =
                                (freeValid && freeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u))) ? 6u :
                                (freeValid && freeBytes <= (kWorkRamHardFloorBytes + (32u * 1024u))) ? 3u :
                                2u;
                            activeWindowSwitchCooldown_ =
                                std::max<uint8_t>(activeWindowSwitchCooldown_, stallCooldown);
                            prefetchRetryCooldown_ =
                                std::max<uint8_t>(prefetchRetryCooldown_, stallCooldown);
                            break;
                        }

                        const int32_t progressedStartId =
                            WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
                        if (progressedStartId == targetStartId) break;
                    }
                }
            }
        }
        LWR_PROBE_END(g_lwrStageAccum.beginFrameOps);
    }
    stabilizedDepthSorter_.BeginFrame(frameId);
    coordinator_.BeginFrame(frameId);
}

void TrackSystem::TickRuntimeFrameCooldowns()
{
    if (lodDegradeCooldown_ > 0)
    {
        --lodDegradeCooldown_;
    }
    if (textureHeapCompactCooldown_ > 0)
    {
        --textureHeapCompactCooldown_;
    }
    if (pendingLodFrameCooldown_ > 0)
    {
        --pendingLodFrameCooldown_;
    }
    for (size_t i = 0; i < pendingLodRetryCooldowns_.size(); ++i)
    {
        if (pendingLodRetryCooldowns_[i] > 0)
        {
            --pendingLodRetryCooldowns_[i];
        }
    }
}

bool TrackSystem::ShouldRunPostSlideMaintenance(bool slidThisFrame) const
{
    if (!kEnableTrackRuntimeStabilization || !slidThisFrame)
    {
        return false;
    }

    // Keep post-slide maintenance bounded. Running full maintenance every
    // slide in moderate pressure creates frame-time spikes exactly at segment
    // transitions. We still react immediately under critical pressure.
    static uint8_t sPostSlideMaintenanceCooldown = 0u;

    bool postSlideFreeValid = false;
    const size_t postSlideFreeBytes = GetHighWorkRamFreeBytesSafe(&postSlideFreeValid);
    bool postSlideLowFreeValid = false;
    const size_t postSlideLowFreeBytes = GetLowWorkRamFreeBytesSafe(&postSlideLowFreeValid);
    if (kEnableTrackLeakIsolationFixed64Pipeline)
    {
        // Leak-isolation mode focuses on long-run behavior. Running full
        // maintenance every slide with chronic low HWR hurts frame time more
        // than it helps memory. Keep only true emergencies immediate.
        static uint8_t sLeakIsoPostSlideCooldown = 0u;
        const bool emergencyHighPressure =
            postSlideFreeValid &&
            postSlideFreeBytes <= kWorkRamCatastrophicFloorBytes;
        const bool emergencyLowPressure =
            postSlideLowFreeValid &&
            postSlideLowFreeBytes <= kLowWorkRamHardFloorBytes;
        if (emergencyHighPressure || emergencyLowPressure)
        {
            sLeakIsoPostSlideCooldown = 0u;
            return true;
        }
        if (sLeakIsoPostSlideCooldown > 0u)
        {
            --sLeakIsoPostSlideCooldown;
            return false;
        }
        sLeakIsoPostSlideCooldown = kLeakIsolationPostSlideMaintenanceCadenceFrames;
        return true;
    }
    const bool postSlideHighPressure =
        postSlideFreeValid &&
        postSlideFreeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u));
    const bool postSlideLowPressure =
        postSlideLowFreeValid &&
        postSlideLowFreeBytes <= (kLowWorkRamHardFloorBytes + (24u * 1024u));
    if (postSlideHighPressure || postSlideLowPressure)
    {
        sPostSlideMaintenanceCooldown = 0u;
        return true;
    }

    const bool moderatePressure =
        workRamMaintenance_.memoryPressureLevelThisFrame != static_cast<uint8_t>(MemoryPressureLevel::Normal);
    if (!moderatePressure)
    {
        if (sPostSlideMaintenanceCooldown > 0u)
        {
            --sPostSlideMaintenanceCooldown;
        }
        return false;
    }

    if (sPostSlideMaintenanceCooldown > 0u)
    {
        --sPostSlideMaintenanceCooldown;
        return false;
    }

    sPostSlideMaintenanceCooldown = 3u;
    return true;
}

bool TrackSystem::RunPendingLodRecoveryStage(bool slidThisFrame)
{
    if (kEnableTrackLeakIsolationFixed64Pipeline)
    {
        (void)slidThisFrame;
        return false;
    }
    bool mandatoryBandPending = false;
    if (kEnableTrackRuntimeStabilization &&
        kEnableTrackLodBandsInStabilization &&
        !segmentRenderers_.empty() &&
        totalSegmentCount_ > 0)
    {
        const size_t windowCount = segmentRenderers_.size();
        for (size_t logicalRank = 0; logicalRank < windowCount; ++logicalRank)
        {
            const int32_t segmentId = WrapSegmentIdToRange(
                activeWindowStartId_ + (windowDirection_ >= 0
                    ? static_cast<int32_t>(logicalRank)
                    : -static_cast<int32_t>(logicalRank)),
                totalSegmentCount_);
            if (segmentId <= 0) continue;
            SegmentRenderEntry* entry = FindWindowEntryByIdFast(segmentId);
            if (!entry || !entry->renderer || !entry->lodState.Ready()) continue;
            uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
            int16_t desiredBaseRank = entry->lodState.HasPerFaceRankOffsets()
                ? static_cast<int16_t>(logicalRank)
                : static_cast<int16_t>(-1);
            if (framePlanCurrent_.Valid() &&
                framePlanCurrent_.frameId == frameIdThisFrame_ &&
                logicalRank < framePlanCurrent_.desiredLodByLogicalRank.size())
            {
                const uint8_t plannedLod =
                    framePlanCurrent_.desiredLodByLogicalRank[logicalRank];
                if (plannedLod <= 3u)
                {
                    desiredLodIndex = plannedLod;
                    desiredBaseRank = entry->lodState.HasPerFaceRankOffsets()
                        ? static_cast<int16_t>(framePlanCurrent_.desiredBaseRankByLogicalRank[logicalRank])
                        : static_cast<int16_t>(-1);
                }
            }
            const bool needsUpdate = entry->lodState.HasPerFaceRankOffsets()
                ? (entry->lodState.currentLodIndex != desiredLodIndex ||
                   entry->lodState.currentBaseRank != desiredBaseRank ||
                   HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                      &entry->lodState.faceFamilyIds))
                : (entry->lodState.currentLodIndex != desiredLodIndex ||
                   HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                      &entry->lodState.faceFamilyIds));
            if (!needsUpdate) continue;
            QueuePendingStabilizedLodRank(logicalRank);
            if (logicalRank < static_cast<size_t>(kLodMandatoryBandSegmentCount))
            {
                mandatoryBandPending = true;
            }
        }
    }

    const bool prefetchTailResidentForRecovery =
        !kEnableTrackRuntimeStabilization ||
        (slidePrefetchSegmentId_ > 0 &&
         slideScratchRenderer_ &&
         !slidePrefetchFamilyIds_.empty());
    bool freeValidForRecovery = false;
    const size_t freeBytesForRecovery = GetHighWorkRamFreeBytesSafe(&freeValidForRecovery);
    const bool allowMandatoryLodRecovery =
        runtimeSlideStallsThisFrame_ == 0u &&
        (!freeValidForRecovery || freeBytesForRecovery > kLodExactRecoveryFreeBytes);
    const bool allowPendingLodRecovery =
        allowMandatoryLodRecovery &&
        (prefetchTailResidentForRecovery || !slidThisFrame);

    if (kEnableTrackRuntimeStabilization &&
        kEnableTrackLodBandsInStabilization &&
        allowPendingLodRecovery &&
        (!slidThisFrame || mandatoryBandPending) &&
        (pendingLodFrameCooldown_ == 0 || mandatoryBandPending) &&
        HasPendingStabilizedWindowLodChanges())
    {
        const uint16_t lodTicksStart = Sh2FrtProfiler::Now();
        bool freeValid = false;
        const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
        const size_t preferredPendingFloor = kWorkRamHardFloorBytes + (32u * 1024u);
        const size_t aggressivePendingFloor = kWorkRamHardFloorBytes + (64u * 1024u);
        const size_t minimumPendingFloor = kWorkRamLodDegradeBytes;
        if (!freeValid || freeBytes > minimumPendingFloor)
        {
            const bool aggressiveFree = (!freeValid || freeBytes > aggressivePendingFloor);
            const uint8_t pendingBudget =
                slidThisFrame
                    ? (mandatoryBandPending ? (aggressiveFree ? 2u : 1u) : 0u)
                    : ((mandatoryBandPending && aggressiveFree) ? 4u :
                       (aggressiveFree ? 2u : 1u));
            if (pendingBudget > 0u)
            {
                ProcessPendingStabilizedWindowLodChanges(pendingBudget);
                if (HasPendingStabilizedWindowLodChanges())
                {
                    const uint8_t pendingCooldown =
                        slidThisFrame ? 1u :
                        ((mandatoryBandPending && aggressiveFree) ? 1u :
                         (aggressiveFree ? 1u :
                          ((freeValid && freeBytes <= preferredPendingFloor) ? 4u : 2u)));
                    pendingLodFrameCooldown_ = std::max<uint8_t>(
                        pendingLodFrameCooldown_,
                        pendingCooldown);
                }
                else
                {
                    pendingLodFrameCooldown_ = std::max<uint8_t>(
                        pendingLodFrameCooldown_,
                        slidThisFrame ? 1u :
                        ((mandatoryBandPending && aggressiveFree) ? 1u :
                         (aggressiveFree ? 1u : 2u)));
                }
            }
        }
        sh2MasterLodTicksThisFrame_ =
            Sh2FrtProfiler::Elapsed(lodTicksStart, Sh2FrtProfiler::Now());
        return true;
    }

    return false;
}

const TrackLowWorkVector<TrackSystem::SegmentHandle>& TrackSystem::BuildStabilizedSortedHandles(
    const Vector3D& trackOffset,
    const Vector3D& cameraLocation)
{
    stabilizedDepthItemsScratch_.clear();
    stabilizedSortedHandlesScratch_.clear();
    if (segmentRenderers_.empty()) return stabilizedSortedHandlesScratch_;

    const size_t windowCount = segmentRenderers_.size();
    if (segmentHandles_.empty() || segmentHandles_.size() != windowCount)
    {
        BuildSegmentHandleTable();
    }
    if (segmentHandles_.empty()) return stabilizedSortedHandlesScratch_;

    stabilizedDepthItemsScratch_.reserve(segmentHandles_.size());
    auto depthMetricRaw = [&](const SegmentRenderEntry* e) -> int32_t
    {
        if (!e) return std::numeric_limits<int32_t>::max();
        const Vector3D c = e->center + trackOffset;
        const auto dx = (c.X - cameraLocation.X).Abs();
        const auto dz = (c.Z - cameraLocation.Z).Abs();
        const auto major = (dx > dz) ? dx : dz;
        const auto minor = (dx > dz) ? dz : dx;
        return (major + minor).RawValue();
    };
    auto windowRank = [&](const SegmentRenderEntry* e) -> int32_t
    {
        if (!e || totalSegmentCount_ == 0) return 0;
        const int32_t startId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
        if (startId <= 0) return 0;
        const int32_t dir = (windowDirection_ < 0) ? -1 : 1;
        int32_t rank = (dir > 0) ? (e->id - startId) : (startId - e->id);
        if (rank < 0) rank += static_cast<int32_t>(totalSegmentCount_);
        return (rank < 0) ? 0 : rank;
    };
    auto depthKey = [&](const SegmentRenderEntry* e) -> int64_t
    {
        const int64_t d = static_cast<int64_t>(depthMetricRaw(e));
        const int64_t r = static_cast<int64_t>(windowRank(e));
        // Far first; when depths tie, keep deterministic order by window rank.
        return (d << 16) - r;
    };

    for (size_t i = 0; i < segmentHandles_.size(); ++i)
    {
        const SegmentHandle handle = segmentHandles_[i];
        auto* entry = segmentPool_.Resolve(handle);
        if (!entry || !entry->renderer) continue;
        TrackDepthSortItem<SegmentHandle, int64_t> item{};
        item.handle = handle;
        item.depthKey = depthKey(entry);
        stabilizedDepthItemsScratch_.push_back(item);
    }
    if (stabilizedDepthItemsScratch_.empty()) return stabilizedSortedHandlesScratch_;

    if (kTestRenderSegmentsAscendingById)
    {
        std::sort(stabilizedDepthItemsScratch_.begin(),
                  stabilizedDepthItemsScratch_.end(),
                  [&](const TrackDepthSortItem<SegmentHandle, int64_t>& a,
                      const TrackDepthSortItem<SegmentHandle, int64_t>& b)
                  {
                      const auto* ea = segmentPool_.Resolve(a.handle);
                      const auto* eb = segmentPool_.Resolve(b.handle);
                      if (!ea && !eb) return false;
                      if (!ea) return false;
                      if (!eb) return true;
                      if (ea->id == eb->id) return a.handle.slot < b.handle.slot;
                      return ea->id < eb->id;
                  });
        const size_t maxVisible = std::min<size_t>(
            stabilizedDepthItemsScratch_.size(),
            std::max<size_t>(1u, static_cast<size_t>(fixedVisibleSegmentCap_)));
        stabilizedSortedHandlesScratch_.reserve(maxVisible);
        for (size_t i = 0; i < maxVisible; ++i)
        {
            stabilizedSortedHandlesScratch_.push_back(stabilizedDepthItemsScratch_[i].handle);
        }
        stabilizedDepthStats_ = {};
        sh2SlaveSortTicksThisFrame_ = 0u;
        return stabilizedSortedHandlesScratch_;
    }

    const size_t maxVisible = std::min<size_t>(
        stabilizedDepthItemsScratch_.size(),
        std::max<size_t>(1u, static_cast<size_t>(fixedVisibleSegmentCap_)));
    stabilizedDepthSorter_.Build(stabilizedDepthItemsScratch_, maxVisible);
    stabilizedDepthStats_ = stabilizedDepthSorter_.Stats();
    sh2SlaveSortTicksThisFrame_ = stabilizedDepthStats_.slaveLastJobTicks;

    const auto& sorted = stabilizedDepthSorter_.Consume();
    if (sorted.count == maxVisible)
    {
        auto sameHandle = [](const SegmentHandle& a, const SegmentHandle& b) -> bool
        {
            return (a.slot == b.slot) && (a.generation == b.generation);
        };
        bool sortedListValid = true;
        for (uint16_t i = 0; i < sorted.count; ++i)
        {
            const SegmentHandle handle = sorted.items[i];
            auto* entry = segmentPool_.Resolve(handle);
            if (!entry || !entry->renderer)
            {
                sortedListValid = false;
                break;
            }

            bool foundInCurrentFrame = false;
            for (size_t k = 0; k < stabilizedDepthItemsScratch_.size(); ++k)
            {
                if (sameHandle(stabilizedDepthItemsScratch_[k].handle, handle))
                {
                    foundInCurrentFrame = true;
                    break;
                }
            }
            if (!foundInCurrentFrame)
            {
                sortedListValid = false;
                break;
            }
        }

        if (sortedListValid)
        {
            stabilizedSortedHandlesScratch_.reserve(sorted.count);
            for (uint16_t i = 0; i < sorted.count; ++i)
            {
                stabilizedSortedHandlesScratch_.push_back(sorted.items[i]);
            }
            return stabilizedSortedHandlesScratch_;
        }
    }

    for (size_t i = 1; i < stabilizedDepthItemsScratch_.size(); ++i)
    {
        const auto current = stabilizedDepthItemsScratch_[i];
        size_t j = i;
        while (j > 0 && stabilizedDepthItemsScratch_[j - 1].depthKey < current.depthKey)
        {
            stabilizedDepthItemsScratch_[j] = stabilizedDepthItemsScratch_[j - 1];
            --j;
        }
        stabilizedDepthItemsScratch_[j] = current;
    }
    stabilizedSortedHandlesScratch_.reserve(maxVisible);
    for (size_t i = 0; i < maxVisible; ++i)
    {
        stabilizedSortedHandlesScratch_.push_back(stabilizedDepthItemsScratch_[i].handle);
    }
    return stabilizedSortedHandlesScratch_;
}

void TrackSystem::SetObservedCarSegmentId(int32_t segmentId)
{
    observedCarSegmentId_ = segmentId;
    if (!kEnableTrackRuntimeStabilization) return;
    if (segmentId <= 0 || totalSegmentCount_ == 0) return;

    const int32_t observedId = WrapSegmentIdToRange(segmentId, totalSegmentCount_);
    if (observedId <= 0) return;
    const int8_t desiredDirection = (cameraWindowDirection_ < 0) ? -1 : 1;
    const int32_t desiredStartId =
        ResolveWindowStartFromCarSegment(observedId, totalSegmentCount_, desiredDirection);
    if (desiredStartId <= 0) return;

    const int32_t currentStartId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
    const int32_t currentTargetId = WrapSegmentIdToRange(targetWindowStartId_, totalSegmentCount_);
    const size_t windowCount = segmentRenderers_.size();
    const int32_t windowForwardDistance =
        WrapDistanceForward(currentStartId, desiredStartId, totalSegmentCount_);
    const int32_t windowBackwardDistance =
        WrapDistanceForward(desiredStartId, currentStartId, totalSegmentCount_);
    const int32_t windowDirectionalDistance = (desiredDirection > 0)
        ? windowForwardDistance
        : windowBackwardDistance;
    const bool bootstrapWindow =
        currentStartId == 1 &&
        currentTargetId == 1 &&
        TrackedCarSegmentValid() &&
        trackedCarSegmentId_ == 1 &&
        activeWindowHead_ == 0;
    if (bootstrapWindow &&
        windowCount > 0 &&
        (windowDirectionalDistance >= static_cast<int32_t>(windowCount) || windowBackwardDistance == 1))
    {
        // Avoid full 20-segment rebuild on runtime bootstrap alignment.
        // Keep the deterministic sliding window and converge through normal
        // incremental slides to prevent frame spikes and transient memory churn.
        targetWindowStartId_ = desiredStartId;
        trackedCarSegmentId_ = observedId;
        SetTrackedCarSegmentValid(true);
        activeWindowSwitchCooldown_ = 0;
        if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
        {
            SRL::Debug::Print(1, 13, "TRK align defer s:%d o:%d ws:%d",
                              currentStartId,
                              observedId,
                              desiredStartId);
        }
        return;
    }

    const int32_t anchorId = (currentTargetId > 0) ? currentTargetId : currentStartId;
    const int32_t forwardDistance = WrapDistanceForward(anchorId, desiredStartId, totalSegmentCount_);
    const int32_t backwardDistance = WrapDistanceForward(desiredStartId, anchorId, totalSegmentCount_);
    const int32_t directionalDistance = (desiredDirection > 0) ? forwardDistance : backwardDistance;
    const int32_t reverseDistance = (desiredDirection > 0) ? backwardDistance : forwardDistance;
    // Guard against large target jumps during tight circles / camera flips.
    // Accept only near-forward targets in current direction.
    const int32_t maxAcceptedDirectionalDistance =
        std::max<int32_t>(2, std::min<int32_t>(static_cast<int32_t>(windowCount) + 2, 8));
    if (directionalDistance == 0 ||
        (directionalDistance > 0 && directionalDistance <= maxAcceptedDirectionalDistance) ||
        reverseDistance == 1)
    {
        targetWindowStartId_ = desiredStartId;
        trackedCarSegmentId_ = observedId;
        SetTrackedCarSegmentValid(true);
    }
}

std::vector<TrackSystem::SegmentHandle> TrackSystem::BuildVisibleSegmentOrder(
    const Vector3D& trackOffset,
    const Vector3D& cameraLocation)
{
    std::vector<SegmentHandle> orderedHandles{};
    if (segmentRenderers_.empty()) return orderedHandles;
    const size_t windowCount = segmentRenderers_.size();
    if (kEnableTrackRuntimeStabilization ||
        segmentHandles_.empty() ||
        segmentHandles_.size() != windowCount)
    {
        BuildSegmentHandleTable();
    }
    if (segmentHandles_.empty()) return orderedHandles;
    orderedHandles.reserve(windowCount);
    size_t invalidHandleCount = 0;
    for (size_t i = 0; i < windowCount; ++i)
    {
        const SegmentHandle h = segmentHandles_[i];
        if (!segmentPool_.Resolve(h))
        {
            ++invalidHandleCount;
            continue;
        }
        orderedHandles.push_back(h);
    }
    if (invalidHandleCount > 0)
    {
        BuildSegmentHandleTable();
        orderedHandles.clear();
        for (size_t i = 0; i < windowCount; ++i)
        {
            const SegmentHandle h = segmentHandles_[i % windowCount];
            if (!segmentPool_.Resolve(h)) continue;
            orderedHandles.push_back(h);
        }
        if (orderedHandles.size() != windowCount)
        {
            SRL::Debug::Print(1, 23, "TRK hole st:%d h:%u n:%u ok:%u",
                              activeWindowStartId_,
                              static_cast<unsigned>(activeWindowHead_),
                              static_cast<unsigned>(windowCount),
                              static_cast<unsigned>(orderedHandles.size()));
        }
        if (orderedHandles.empty())
        {
            return orderedHandles;
        }
    }
    if (orderedHandles.size() > 1 && totalSegmentCount_ > 0)
    {
        const int32_t startId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
        const int32_t dir = (windowDirection_ < 0) ? -1 : 1;
        std::sort(orderedHandles.begin(), orderedHandles.end(),
            [&](const SegmentHandle& a, const SegmentHandle& b)
            {
                const auto* ea = segmentPool_.Resolve(a);
                const auto* eb = segmentPool_.Resolve(b);
                if (!ea && !eb) return false;
                if (!ea) return false;
                if (!eb) return true;
                int32_t da = (dir > 0) ? (ea->id - startId) : (startId - ea->id);
                int32_t db = (dir > 0) ? (eb->id - startId) : (startId - eb->id);
                if (da < 0) da += static_cast<int32_t>(totalSegmentCount_);
                if (db < 0) db += static_cast<int32_t>(totalSegmentCount_);
                if (da == db) return ea->id < eb->id;
                return da < db;
            });
    }

    auto depthMetricToCamera = [&](const SegmentRenderEntry* e) -> SRL::Math::Types::Fxp
    {
        if (!e) return SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        const Vector3D c = e->center + trackOffset;
        const auto dx = (c.X - cameraLocation.X).Abs();
        const auto dz = (c.Z - cameraLocation.Z).Abs();
        const auto major = (dx > dz) ? dx : dz;
        const auto minor = (dx > dz) ? dz : dx;
        return major + minor;
    };

    const size_t keepCount =
        std::min<size_t>(
            orderedHandles.size(),
            static_cast<size_t>(fixedVisibleSegmentCap_));
    orderedHandles.resize(keepCount);

    if (kTestRenderSegmentsAscendingById)
    {
        std::sort(orderedHandles.begin(), orderedHandles.end(),
            [&](const SegmentHandle& a, const SegmentHandle& b)
            {
                const auto* ea = segmentPool_.Resolve(a);
                const auto* eb = segmentPool_.Resolve(b);
                if (!ea && !eb) return false;
                if (!ea) return false;
                if (!eb) return true;
                if (ea->id == eb->id) return a.slot < b.slot;
                return ea->id < eb->id;
            });
        return orderedHandles;
    }

    // Draw in camera-space painter order (far -> near).
    // VDP1 has no Z-buffer for this path, so camera-relative order is required
    // to avoid distortion when camera rotates around the car.
    if (kEnableTrackRuntimeStabilization)
    {
        std::sort(orderedHandles.begin(), orderedHandles.end(),
            [&](const SegmentHandle& a, const SegmentHandle& b)
            {
                const auto* ea = segmentPool_.Resolve(a);
                const auto* eb = segmentPool_.Resolve(b);
                if (!ea && !eb) return false;
                if (!ea) return false;
                if (!eb) return true;
                const auto da = depthMetricToCamera(ea);
                const auto db = depthMetricToCamera(eb);
                if (da == db)
                {
                    const int32_t startId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
                    const int32_t dir = (windowDirection_ < 0) ? -1 : 1;
                    int32_t wa = (dir > 0) ? (ea->id - startId) : (startId - ea->id);
                    int32_t wb = (dir > 0) ? (eb->id - startId) : (startId - eb->id);
                    if (wa < 0) wa += static_cast<int32_t>(totalSegmentCount_);
                    if (wb < 0) wb += static_cast<int32_t>(totalSegmentCount_);
                    return wa < wb;
                }
                return da > db; // far first
            });
        return orderedHandles;
    }

    std::sort(orderedHandles.begin(), orderedHandles.end(),
        [&](const SegmentHandle& a, const SegmentHandle& b)
        {
            const auto* ea = segmentPool_.Resolve(a);
            const auto* eb = segmentPool_.Resolve(b);
            if (!ea && !eb) return false;
            if (!ea) return false;
            if (!eb) return true;
            const auto da = depthMetricToCamera(ea);
            const auto db = depthMetricToCamera(eb);
            if (da == db) return ea->id < eb->id;
            return da > db; // far first
        });
    return orderedHandles;
}

void TrackSystem::RunSeg1DiagnosticsForFrame()
{
    // Single-face overwrite probe disabled.
    if (false && Seg1SingleFaceSwapReady() && seg1SingleFaceSwapBaseSlot_ > 0)
    {
        if (seg1SingleFaceSwapCounter_ >= seg1SingleFaceSwapFrames_)
        {
            seg1SingleFaceSwapCounter_ = 0;
            SetSeg1SingleFaceSwapUseAlt(!Seg1SingleFaceSwapUseAlt());

            bool ok = false;
            if (Seg1SingleFaceSwapUseAlt())
            {
                ok = TryOverwriteTextureSlotFromCd(seg1SingleFaceSwapBaseSlot_, "area_escape_32.tga") ||
                     TryOverwriteTextureSlotFromCd(seg1SingleFaceSwapBaseSlot_, "AREA_ESCAPE_32.TGA");
            }
            else
            {
                ok = TryOverwriteTextureSlotFromCd(seg1SingleFaceSwapBaseSlot_, "asfalto_32.tga") ||
                     TryOverwriteTextureSlotFromCd(seg1SingleFaceSwapBaseSlot_, "ASFALTO_32.TGA");
            }
            SRL::Debug::Print(1, 21, "S1O sw:%u ok:%u",
                              Seg1SingleFaceSwapUseAlt() ? 1u : 0u,
                              ok ? 1u : 0u);
        }
        else
        {
            ++seg1SingleFaceSwapCounter_;
        }
    }

    // SEG_001 LOD cycle test (experimental).
    // Disabled by default to keep texture assignment deterministic in gameplay.
    constexpr bool kEnableSeg1LodCycleTest = false;
    constexpr bool kEnableSeg1LodCycleLogs = false;
    const bool isTrueSingleSegmentScene =
        (segmentRenderers_.size() == 1) &&
        (segmentRenderers_[0].logicalSegmentCount == 1) &&
        (segmentRenderers_[0].id == 1);
    if (kEnableSeg1LodCycleTest &&
        isTrueSingleSegmentScene &&
        (Seg1ComponentEnabled() || Seg1RendererLodReady()) &&
        !seg1FamilySlots_.empty())
    {
        if (seg1LodFrameCounter_ >= seg1LodSwapFrames_)
        {
            seg1LodFrameCounter_ = 0;
            seg1CurrentLodIndex_ = static_cast<uint8_t>((seg1CurrentLodIndex_ + 1) & 0x03);

            if (Seg1ComponentEnabled() &&
                !seg1ComponentAttrs_.empty() &&
                seg1ComponentAttrs_.size() == seg1FaceFamilyIds_.size())
            {
                for (size_t fi = 0; fi < seg1ComponentAttrs_.size(); ++fi)
                {
                    const uint16_t fam = seg1FaceFamilyIds_[fi];
                    uint16_t slot = No_Texture;
                    if (fam != 0)
                    {
                        (void)TryGetFamilyLodSlot(seg1FamilySlots_, fam, seg1CurrentLodIndex_, slot);
                    }

                    auto& attr = seg1ComponentAttrs_[fi];
                    if (slot != No_Texture)
                    {
                        attr.Texture = slot;
                        attr.ColorMode = No_Palet;
                    }
                }
            }

            if (Seg1RendererLodReady())
            {
                size_t appliedRenderer = 0;
                for (auto& entry : segmentRenderers_)
                {
                    if (entry.id == 1 && entry.renderer)
                    {
                        appliedRenderer = entry.renderer->ApplyFaceTextureSlotsGlobal(
                            seg1RendererFaceSlotsByLod_[seg1CurrentLodIndex_]);
                        break;
                    }
                }
                if (kEnableSeg1LodCycleLogs)
                {
                    SRL::Debug::Print(1, 21, "S1A rdr:%u", (unsigned)appliedRenderer);
                }
            }
            else if (kEnableSeg1LodCycleLogs)
            {
                SRL::Debug::Print(1, 21, "S1A rdr:off");
            }
            if (kEnableSeg1LodCycleLogs)
            {
                const unsigned rdrFaces = Seg1RendererLodReady() ? (unsigned)seg1RendererFaceSlotsByLod_[seg1CurrentLodIndex_].size() : 0u;
                SRL::Debug::Print(1, 22, "S1L c:%u j:%u l:%d r:%u",
                                  (unsigned)seg1TgaPreloadCount_,
                                  (unsigned)seg1TgaJsonOk_,
                                  kSeg1FamilyLodValues[seg1CurrentLodIndex_], rdrFaces);
            }
        }
        else
        {
            ++seg1LodFrameCounter_;
        }
    }
}

void TrackSystem::RenderVisibleSegmentOrderStabilized(
    const Vector3D& trackOffset,
    const Vector3D& lightDirection,
    const Vector3D& cameraLocation,
    std::array<uint8_t, kTrackSegmentLimit + 1>& preparedCountById,
    std::array<uint8_t, kTrackSegmentLimit + 1>& renderedCountById,
    bool& segment01Prepared)
{
    // Two pass stabilized draw:
    // pass 1 draws segments outside the local car neighborhood,
    // pass 2 draws segments near the car segment window rank.
    // This reduces seam overdraw flicker near the car during segment transitions.
    std::array<SegmentRenderEntry*, kTrackSegmentLimit> preparedEntries{};
    size_t preparedCount = 0;
    auto backupRuntimeFaceSlots = [&](const SegmentRenderEntry& entry)
    {
        runtimeRenderFaceSlotsScratch_.assign(entry.lodState.currentFaceSlots.begin(),
                                              entry.lodState.currentFaceSlots.end());
    };
    auto restoreRuntimeFaceSlots = [&](SegmentRenderEntry& entry)
    {
        entry.lodState.currentFaceSlots.assign(runtimeRenderFaceSlotsScratch_.begin(),
                                               runtimeRenderFaceSlotsScratch_.end());
    };

    const TrackLowWorkVector<SegmentHandle>* plannedHandles = nullptr;
    if (framePlanCurrent_.Valid() &&
        framePlanCurrent_.frameId == frameIdThisFrame_ &&
        !framePlanSortedHandles_.empty())
    {
        plannedHandles = &framePlanSortedHandles_;
    }
    else if (kEnableTrackLeakIsolationFixed64Pipeline &&
             runtimeSlidesThisFrame_ == 0u &&
             !framePlanSortedHandles_.empty())
    {
        // Leak-isolation fixed64 mode: when the window didn't slide, reusing the
        // last sorted plan avoids re-sorting every frame and keeps pacing stable.
        plannedHandles = &framePlanSortedHandles_;
    }
    const auto& orderedHandles = plannedHandles
        ? *plannedHandles
        : BuildStabilizedSortedHandles(trackOffset, cameraLocation);
    auto sameHandle = [](const SegmentHandle& a, const SegmentHandle& b) -> bool
    {
        return (a.slot == b.slot) && (a.generation == b.generation);
    };
    const SegmentHandle* drawHandles =
        orderedHandles.empty() ? nullptr : orderedHandles.data();
    size_t drawHandleCount = orderedHandles.size();
    if (kEnableStabilizedProducerOnSlave)
    {
        stabilizedProducerInputScratch_.clear();
        stabilizedProducerInputScratch_.reserve(orderedHandles.size());
        for (size_t i = 0; i < orderedHandles.size(); ++i)
        {
            stabilizedProducerInputScratch_.push_back(orderedHandles[i]);
        }
        producer_.Build(stabilizedProducerInputScratch_, stabilizedProducerInputScratch_.size());
        const auto& produced = producer_.Consume();
        bool producerListValid =
            produced.count > 0u &&
            produced.count <= orderedHandles.size();
        if (producerListValid)
        {
            for (uint16_t i = 0; i < produced.count; ++i)
            {
                const SegmentHandle handle = produced.items[i];
                auto* producedEntry = segmentPool_.Resolve(handle);
                if (!producedEntry || !producedEntry->renderer)
                {
                    producerListValid = false;
                    break;
                }
                bool foundInOrderedHandles = false;
                for (size_t k = 0; k < orderedHandles.size(); ++k)
                {
                    if (sameHandle(orderedHandles[k], handle))
                    {
                        foundInOrderedHandles = true;
                        break;
                    }
                }
                if (!foundInOrderedHandles)
                {
                    producerListValid = false;
                    break;
                }
            }
        }
        if (producerListValid)
        {
            drawHandles = produced.items.data();
            drawHandleCount = produced.count;
            sh2ProducerListUsedThisFrame_ = 1;
        }
        else if (!orderedHandles.empty())
        {
            ++sh2ProducerListFallbacksThisFrame_;
        }
        coordinator_.SetProducerStats(producer_.Stats());
    }
    for (size_t i = 0; i < drawHandleCount; ++i)
    {
        auto* entry = segmentPool_.Resolve(drawHandles[i]);
        if (!entry || !entry->renderer) continue;
        if (!IsRendererStateIntegral(*entry->renderer))
        {
            if (!TryRepairRendererState(*entry->renderer) &&
                !RebuildSafeSegmentEntry(*entry))
            {
                if (runtimeDiagnostics_.RuntimeStatsLogsEnabled() && ((frameIdThisFrame_ & 0x0Fu) == 0u))
                {
                    SRL::Debug::Print(1, 21, "TRK inv id:%d m:%u f:%u v:%u d:%u",
                                      entry->id,
                                      static_cast<unsigned>(entry->renderer->MeshCount()),
                                      static_cast<unsigned>(entry->renderer->FaceCount()),
                                      static_cast<unsigned>(entry->renderer->VertexCount()),
                                      static_cast<unsigned>(entry->renderer->DrawLimit()));
                }
                ++runtimeSafeSkippedThisFrame_;
                continue;
            }
        }
        const uint32_t missingSlots = CountMissingOrDeadRequiredFaceTextureSlots(
            entry->lodState.currentFaceSlots,
            &entry->lodState.faceFamilyIds);
        if (missingSlots > 0)
        {
            const bool deterministicTextureState =
                kEnableTrackRuntimeStabilization &&
                kEnableTrackLodBandsInStabilization &&
                kEnableDeterministicStabilizedSlide;
            if (deterministicTextureState)
            {
                size_t logicalRank = 0u;
                const bool hasLogicalRank = TryGetWindowLogicalRank(entry->id, logicalRank);
                if (runtimeDiagnostics_.RuntimeStatsLogsEnabled() && ((frameIdThisFrame_ & 0x0Fu) == 0u))
                {
                    SRL::Debug::Print(1, 21, "TRK det miss id:%d r:%u l:%u m:%u",
                                      entry->id,
                                      static_cast<unsigned>(hasLogicalRank ? logicalRank : 0u),
                                      static_cast<unsigned>(entry->lodState.desiredLodIndex <= 3u
                                          ? entry->lodState.desiredLodIndex
                                          : entry->lodState.currentLodIndex),
                                      static_cast<unsigned>(missingSlots));
                }
            }
            else
            {
                uint8_t repairLodIndex = 0u;
                int16_t repairBaseRank = -1;
                size_t logicalRank = 0;
                const bool requireExactRepair =
                    kEnableTrackRuntimeStabilization && kEnableTrackLodBandsInStabilization;
                const bool hasLogicalRank =
                    kEnableTrackLodBandsInStabilization &&
                    TryGetWindowLogicalRank(entry->id, logicalRank);
                if (kEnableTrackLodBandsInStabilization)
                {
                    if (entry->lodState.desiredLodIndex <= 3u)
                    {
                        repairLodIndex = entry->lodState.desiredLodIndex;
                        repairBaseRank = entry->lodState.desiredBaseRank;
                    }
                    else if (hasLogicalRank)
                    {
                        repairLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
                        repairBaseRank = static_cast<int16_t>(logicalRank);
                    }
                    else if (entry->lodState.currentLodIndex <= 3u)
                    {
                        repairLodIndex = entry->lodState.currentLodIndex;
                    }
                }
                backupRuntimeFaceSlots(*entry);
                const uint8_t previousLodIndex = entry->lodState.currentLodIndex;
                const int16_t previousBaseRank = entry->lodState.currentBaseRank;

                bool remapped = false;
                uint8_t appliedLodIndex = repairLodIndex;
                int16_t appliedBaseRank = repairBaseRank;
                if (entry->lodState.HasPerFaceRankOffsets())
                {
                    if (hasLogicalRank)
                    {
                        remapped = RebuildSegmentFaceSlotsForBaseRank(*entry,
                                                                      logicalRank,
                                                                      seg1FamilySlots_,
                                                                      requireExactRepair) &&
                                   !HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                                      &entry->lodState.faceFamilyIds);
                        if (!remapped)
                        {
                            restoreRuntimeFaceSlots(*entry);
                            entry->lodState.currentLodIndex = previousLodIndex;
                            entry->lodState.currentBaseRank = previousBaseRank;
                        }
                    }
                }
                else
                {
                    const int lowestLodAttempt = requireExactRepair ? static_cast<int>(repairLodIndex) : 0;
                    for (int lodAttempt = static_cast<int>(repairLodIndex);
                         lodAttempt >= lowestLodAttempt;
                         --lodAttempt)
                    {
                        entry->lodState.currentFaceSlots.assign(entry->lodState.faceFamilyIds.size(), -1);
                        if (!RebuildSegmentFaceSlotsForLod(*entry,
                                                           static_cast<uint8_t>(lodAttempt),
                                                           seg1FamilySlots_,
                                                           requireExactRepair))
                        {
                            continue;
                        }
                        if (HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                               &entry->lodState.faceFamilyIds))
                        {
                            continue;
                        }
                        remapped = true;
                        appliedLodIndex = static_cast<uint8_t>(lodAttempt);
                        appliedBaseRank = -1;
                        break;
                    }
                    if (!remapped)
                    {
                        restoreRuntimeFaceSlots(*entry);
                        entry->lodState.currentLodIndex = previousLodIndex;
                        entry->lodState.currentBaseRank = previousBaseRank;
                    }
                }
                if (remapped)
                {
                    (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
                    entry->lodState.currentLodIndex = appliedLodIndex;
                    entry->lodState.currentBaseRank = appliedBaseRank;
                    entry->lodState.desiredLodIndex = repairLodIndex;
                    entry->lodState.desiredBaseRank = repairBaseRank;
                    InvalidateEntryWorkingSetCache(*entry);
                    if (kEnableTrackRuntimeStabilization &&
                        kEnableTrackLodBandsInStabilization &&
                        hasLogicalRank)
                    {
                        const bool stillNeedsPromotion =
                            (entry->lodState.currentLodIndex != entry->lodState.desiredLodIndex) ||
                            (entry->lodState.HasPerFaceRankOffsets() &&
                             entry->lodState.currentBaseRank != entry->lodState.desiredBaseRank);
                        if (stillNeedsPromotion)
                        {
                            QueuePendingStabilizedLodRank(logicalRank);
                        }
                    }
                    ++runtimeSafeReappliedThisFrame_;
                }
                else if (requireExactRepair)
                {
                    if (runtimeDiagnostics_.RuntimeStatsLogsEnabled() && ((frameIdThisFrame_ & 0x0Fu) == 0u))
                    {
                        SRL::Debug::Print(1, 21, "TRK exact miss id:%d r:%u l:%u m:%u",
                                          entry->id,
                                          static_cast<unsigned>(hasLogicalRank ? logicalRank : 0u),
                                          static_cast<unsigned>(repairLodIndex),
                                          static_cast<unsigned>(missingSlots));
                    }
                }
                if (!remapped ||
                    CountMissingOrDeadRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                               &entry->lodState.faceFamilyIds) > 0)
                {
                    const bool previousStillUsable =
                        !HasMissingRequiredFaceTextureSlots(entry->lodState.currentFaceSlots,
                                                            &entry->lodState.faceFamilyIds);
                    if (!previousStillUsable && !RebuildSafeSegmentEntry(*entry))
                    {
                        SRL::Debug::Print(1, 21, "TRK slot miss id:%d m:%u",
                                          entry->id,
                                          static_cast<unsigned>(missingSlots));
                        ++runtimeSafeSkippedThisFrame_;
                        continue;
                    }
                }
            }
        }

        if (preparedCount < preparedEntries.size())
        {
            // Skip duplicated segment ids inside the same frame prepare list.
            // Duplicate draw of the same segment can cause overdraw artifacts.
            if (entry->id > 0 && entry->id <= static_cast<int>(kTrackSegmentLimit))
            {
                const size_t idx = static_cast<size_t>(entry->id);
                if (preparedCountById[idx] > 0)
                {
                    continue;
                }
            }
            preparedEntries[preparedCount++] = entry;
            if (entry->id > 0 && entry->id <= static_cast<int>(kTrackSegmentLimit))
            {
                const size_t idx = static_cast<size_t>(entry->id);
                if (preparedCountById[idx] < 255)
                {
                    ++preparedCountById[idx];
                }
            }
            if (entry->id == 1)
            {
                segment01Prepared = true;
            }
        }
    }

    auto renderPreparedEntry = [&](SegmentRenderEntry* entry)
    {
        if (!entry || !entry->renderer) return;
        if (kEnableLeakABSkipTrackRenderSubmit)
        {
            ++runtimeSafeNoDrawThisFrame_;
            return;
        }
        entry->renderer->SetOffset(trackOffset);
        SetTrackWorkRamDebugTag(SRL::Memory::DebugTag::TrackBackend);
        entry->renderer->Render(lightDirection, cameraLocation);
        SetTrackWorkRamDebugTag(SRL::Memory::DebugTag::TrackPrepare);
        if (entry->renderer->LastDrawnMeshes() == 0)
        {
            if (!entry->lodState.currentFaceSlots.empty() &&
                entry->renderer->FaceCount() == entry->lodState.currentFaceSlots.size())
            {
                (void)entry->renderer->ApplyFaceTextureSlotsGlobal(entry->lodState.currentFaceSlots);
                SetTrackWorkRamDebugTag(SRL::Memory::DebugTag::TrackBackend);
                entry->renderer->Render(lightDirection, cameraLocation);
                SetTrackWorkRamDebugTag(SRL::Memory::DebugTag::TrackPrepare);
                ++runtimeSafeReappliedThisFrame_;
            }
            if (entry->renderer->LastDrawnMeshes() == 0)
            {
                ++runtimeSafeNoDrawThisFrame_;
                return;
            }
        }
        ++runtimeSafeRenderedThisFrame_;
        const int sid = entry->id;
        if (sid > 0 && sid <= static_cast<int>(kTrackSegmentLimit))
        {
            if (renderedCountById[static_cast<size_t>(sid)] < 255)
            {
                ++renderedCountById[static_cast<size_t>(sid)];
            }
        }
    };

    std::array<uint8_t, kTrackSegmentLimit> farOrder{};
    std::array<uint8_t, kTrackSegmentLimit> nearOrder{};
    size_t farCount = 0u;
    size_t nearCount = 0u;

    size_t windowCount = segmentRenderers_.size();
    if (windowCount == 0u) windowCount = preparedCount;
    const int32_t carRefSegmentId =
        (observedCarSegmentId_ > 0)
            ? WrapSegmentIdToRange(observedCarSegmentId_, totalSegmentCount_)
            : (TrackedCarSegmentValid() ? WrapSegmentIdToRange(trackedCarSegmentId_, totalSegmentCount_) : -1);
    size_t carRank = 0u;
    const bool hasCarRank =
        (carRefSegmentId > 0) &&
        TryGetWindowLogicalRank(carRefSegmentId, carRank);

    // Build deterministic two pass order by local window rank distance to car.
    for (size_t i = 0; i < preparedCount; ++i)
    {
        SegmentRenderEntry* entry = preparedEntries[i];
        if (!entry)
        {
            if (farCount < farOrder.size()) farOrder[farCount++] = static_cast<uint8_t>(i);
            continue;
        }

        bool isNearCar = false;
        if (hasCarRank)
        {
            size_t entryRank = 0u;
            if (TryGetWindowLogicalRank(entry->id, entryRank) && windowCount > 0u)
            {
                const size_t linearDist =
                    (entryRank > carRank) ? (entryRank - carRank) : (carRank - entryRank);
                const size_t wrapDist = (linearDist <= windowCount)
                    ? std::min(linearDist, windowCount - std::min(linearDist, windowCount))
                    : linearDist;
                isNearCar = (wrapDist <= 1u);
            }
        }

        if (isNearCar)
        {
            if (nearCount < nearOrder.size()) nearOrder[nearCount++] = static_cast<uint8_t>(i);
        }
        else
        {
            if (farCount < farOrder.size()) farOrder[farCount++] = static_cast<uint8_t>(i);
        }
    }

    // Pass 1: draw far from car.
    for (size_t i = 0; i < farCount; ++i)
    {
        const size_t idx = static_cast<size_t>(farOrder[i]);
        if (idx >= preparedCount) continue;
        renderPreparedEntry(preparedEntries[idx]);
    }
    // Pass 2: draw local seam neighborhood around car.
    for (size_t i = 0; i < nearCount; ++i)
    {
        const size_t idx = static_cast<size_t>(nearOrder[i]);
        if (idx >= preparedCount) continue;
        renderPreparedEntry(preparedEntries[idx]);
    }
}

void TrackSystem::RenderVisibleSegmentOrder(
    const std::vector<SegmentHandle>& orderedHandles,
    const Vector3D& trackOffset,
    const Vector3D& lightDirection,
    const Vector3D& cameraLocation,
    std::array<uint8_t, kTrackSegmentLimit + 1>& preparedCountById,
    std::array<uint8_t, kTrackSegmentLimit + 1>& renderedCountById,
    bool& segment01Logged,
    bool& segment01Prepared)
{
    LowWorkRamStageSample lowWorkDrawStart{};
    LowWorkRamStageSample lowWorkDrawCursor{};
    if constexpr (kEnableLowWorkDrawStageTelemetry)
    {
        lowWorkDrawStart = CaptureLowWorkRamStageSample();
        lowWorkDrawCursor = lowWorkDrawStart;
    }
    auto captureLowWorkDrawDelta = [&](int32_t& outDelta)
    {
        if constexpr (!kEnableLowWorkDrawStageTelemetry)
        {
            outDelta = 0;
            return;
        }
        const LowWorkRamStageSample after = CaptureLowWorkRamStageSample();
        outDelta = SignedLowWorkRamDelta(lowWorkDrawCursor.freeBytes, after.freeBytes);
        lowWorkDrawCursor = after;
    };
    auto finalizeLowWorkDrawDeltas = [&]()
    {
        if constexpr (!kEnableLowWorkDrawStageTelemetry)
        {
            frameMemoryTelemetry_.drawFrameDeltaThisFrame = 0;
            frameMemoryTelemetry_.drawOtherDeltaThisFrame = 0;
            return;
        }
        const int32_t totalDelta = SignedLowWorkRamDelta(lowWorkDrawStart.freeBytes,
                                                         lowWorkDrawCursor.freeBytes);
        frameMemoryTelemetry_.drawFrameDeltaThisFrame = totalDelta;
        frameMemoryTelemetry_.drawOtherDeltaThisFrame =
            totalDelta -
            frameMemoryTelemetry_.drawPrepareDeltaThisFrame -
            frameMemoryTelemetry_.drawExecuteDeltaThisFrame;
    };
    auto releaseRuntimeFaceSlotsScratch = [&]()
    {
        const size_t keep =
            (slotFaceCapacityFloor_ > 0u)
                ? static_cast<size_t>(slotFaceCapacityFloor_)
                : 0u;
        (void)TrimVectorSlack(runtimeRenderFaceSlotsScratch_, keep, true);
    };

    if (kEnableTrackRuntimeStabilization)
    {
        RenderVisibleSegmentOrderStabilized(trackOffset,
                                            lightDirection,
                                            cameraLocation,
                                            preparedCountById,
                                            renderedCountById,
                                            segment01Prepared);
        frameMemoryTelemetry_.drawPrepareDeltaThisFrame = 0;
        captureLowWorkDrawDelta(frameMemoryTelemetry_.drawExecuteDeltaThisFrame);
        finalizeLowWorkDrawDeltas();
        releaseRuntimeFaceSlotsScratch();
        return;
    }

    if (!CoordinatorReady())
    {
        // Fallback render path when coordinator is unavailable.
        for (size_t i = 0; i < orderedHandles.size(); ++i)
        {
            auto* entry = segmentPool_.Resolve(orderedHandles[i]);
            if (!entry || !entry->renderer) continue;
            if (kEnableLeakABSkipTrackRenderSubmit)
            {
                ++runtimeSafeNoDrawThisFrame_;
                continue;
            }
            entry->renderer->SetOffset(trackOffset);
            SetTrackWorkRamDebugTag(SRL::Memory::DebugTag::TrackBackend);
            entry->renderer->Render(lightDirection, cameraLocation);
            SetTrackWorkRamDebugTag(SRL::Memory::DebugTag::TrackPrepare);
            const int sid = entry->id;
            if (sid > 0 && sid <= static_cast<int>(kTrackSegmentLimit))
            {
                if (renderedCountById[static_cast<size_t>(sid)] < 255)
                {
                    ++renderedCountById[static_cast<size_t>(sid)];
                }
            }
        }
        captureLowWorkDrawDelta(frameMemoryTelemetry_.drawOtherDeltaThisFrame);
        finalizeLowWorkDrawDeltas();
        releaseRuntimeFaceSlotsScratch();
        return;
    }

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
            if (Seg1ComponentEnabled() && entry.id == 1)
            {
                estimate.rendered = true;
                estimate.meshes = 1;
                estimate.faces = static_cast<uint32_t>(seg1ComponentFaces_.size());
                return estimate;
            }
            auto* renderer = entry.renderer.get();
            if (!renderer)
            {
                return estimate;
            }
            // Do not budget corrupted renderers for this frame.
            if (!TryRepairRendererState(*renderer))
            {
                if (runtimeDiagnostics_.RuntimeStatsLogsEnabled() && ((frameIdThisFrame_ & 0x0Fu) == 0u))
                {
                    SRL::Debug::Print(1, 23, "TRK prep skip seg:%d", entry.id);
                }
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
    captureLowWorkDrawDelta(frameMemoryTelemetry_.drawPrepareDeltaThisFrame);

    coordinator_.SetProducerStats(producer_.Stats());
    SetTrackWorkRamDebugTag(SRL::Memory::DebugTag::TrackBackend);
    coordinator_.Execute(
        [&](const SegmentHandle& handle) -> SegmentRenderEntry*
        {
            return segmentPool_.Resolve(handle);
        },
        [&](SegmentRenderEntry& entry,
            const TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::PreparedChunk& chunk)
            -> TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult
        {
            if (Seg1ComponentEnabled() && chunk.segmentId == 1 &&
                !seg1ComponentVerts_.empty() && !seg1ComponentFaces_.empty() &&
                seg1ComponentAttrs_.size() == seg1ComponentFaces_.size())
            {
                SRL::Scene3D::PushMatrix();
                SRL::Scene3D::Translate(trackOffset);
                SRL::Types::Mesh mesh{};
                mesh.Vertices = seg1ComponentVerts_.data();
                mesh.VertexCount = seg1ComponentVerts_.size();
                mesh.Faces = seg1ComponentFaces_.data();
                mesh.FaceCount = seg1ComponentFaces_.size();
                mesh.Attributes = seg1ComponentAttrs_.data();
                SRL::Scene3D::DrawMesh(mesh);
                SRL::Scene3D::PopMatrix();

                if (!segment01Logged)
                {
                    segment01Logged = true;
                }

                TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult result{};
                result.rendered = true;
                result.meshes = 1;
                result.faces = static_cast<uint32_t>(seg1ComponentFaces_.size());
                return result;
            }

            auto* renderer = entry.renderer.get();
            if (!renderer)
            {
                return {};
            }
            // Guard draw path and retry one repair before issuing commands.
            if (!IsRendererStateIntegral(*renderer))
            {
                if (!TryRepairRendererState(*renderer))
                {
                    if (runtimeDiagnostics_.RuntimeStatsLogsEnabled() && ((frameIdThisFrame_ & 0x0Fu) == 0u))
                    {
                        SRL::Debug::Print(1, 23, "TRK draw skip seg:%d", chunk.segmentId);
                    }
                    return {};
                }
            }

            renderer->SetOffset(trackOffset);
            if (chunk.segmentId > 0 && chunk.segmentId <= static_cast<int>(kTrackSegmentLimit))
            {
                if (renderedCountById[static_cast<size_t>(chunk.segmentId)] < 255)
                {
                    ++renderedCountById[static_cast<size_t>(chunk.segmentId)];
                }
            }
            if (kEnableLeakABSkipTrackRenderSubmit)
            {
                ++runtimeSafeNoDrawThisFrame_;
                return {};
            }
            renderer->Render(lightDirection, cameraLocation);

            if (!segment01Logged && chunk.segmentId == 1)
            {
                segment01Logged = true;
            }

            TrackRenderCoordinator<SegmentHandle, kTrackSegmentLimit>::RenderResult result{};
            result.rendered = renderer->LastDrawnMeshes() > 0;
            result.meshes = renderer->LastDrawnMeshes();
            result.faces = renderer->LastDrawnFaces();
            return result;
        });
    captureLowWorkDrawDelta(frameMemoryTelemetry_.drawExecuteDeltaThisFrame);
    finalizeLowWorkDrawDeltas();
    releaseRuntimeFaceSlotsScratch();
    SetTrackWorkRamDebugTag(SRL::Memory::DebugTag::TrackCore);
}

bool TrackSystem::RunInitialMaintenanceStage()
{
    if (!kEnableTrackRuntimeStabilization)
    {
        return false;
    }
    bool freeValid = false;
    const size_t freeBytes = GetHighWorkRamFreeBytesSafe(&freeValid);
    bool lowFreeValid = false;
    const size_t lowFreeBytes = GetLowWorkRamFreeBytesSafe(&lowFreeValid);
    const size_t initialHighPressureThreshold =
        kEnableTrackLeakIsolationFixed64Pipeline
            ? kWorkRamHardFloorBytes
            : (kWorkRamHardFloorBytes + (16u * 1024u));
    const size_t initialCriticalPressureThreshold =
        kEnableTrackLeakIsolationFixed64Pipeline
            ? kWorkRamCatastrophicFloorBytes
            : (kWorkRamHardFloorBytes + (8u * 1024u));
    const bool highPressure =
        freeValid && freeBytes <= initialHighPressureThreshold;
    const bool lowPressure =
        lowFreeValid && lowFreeBytes <= (kLowWorkRamHardFloorBytes + (48u * 1024u));
    const bool criticalPressure =
        (freeValid && freeBytes <= initialCriticalPressureThreshold) ||
        (lowFreeValid && lowFreeBytes <= (kLowWorkRamHardFloorBytes + (24u * 1024u)));
    static uint8_t sInitialMaintenanceCooldown = 0u;
    const bool moderatePressure =
        highPressure ||
        lowPressure ||
        (workRamMaintenance_.memoryPressureLevelThisFrame != static_cast<uint8_t>(MemoryPressureLevel::Normal));

    if (kEnableTrackLeakIsolationFixed64Pipeline)
    {
        // Fixed64 leak-isolation mode favors frame pacing: run heavy
        // maintenance periodically and react immediately only to true emergency.
        if (!criticalPressure)
        {
            if (sInitialMaintenanceCooldown > 0u)
            {
                --sInitialMaintenanceCooldown;
                sh2MasterMaintenanceTicksThisFrame_ = 0u;
                return false;
            }
            sInitialMaintenanceCooldown = kLeakIsolationInitialMaintenanceCadenceFrames;
        }
        else
        {
            sInitialMaintenanceCooldown = 0u;
        }

        const uint16_t maintenanceTicksStart = Sh2FrtProfiler::Now();
        RunWorkRamMaintenance(false);
        sh2MasterMaintenanceTicksThisFrame_ =
            Sh2FrtProfiler::Elapsed(maintenanceTicksStart, Sh2FrtProfiler::Now());
        return true;
    }

    if (!moderatePressure)
    {
        if (sInitialMaintenanceCooldown > 0u)
        {
            --sInitialMaintenanceCooldown;
            sh2MasterMaintenanceTicksThisFrame_ = 0u;
            return false;
        }
        // Under stable memory, run initial maintenance at a lower cadence.
        const bool steadyHeadroomHighWork =
            freeValid && freeBytes > (kWorkRamHardFloorBytes + (20u * 1024u));
        const bool veryHealthyLowWork =
            lowFreeValid && lowFreeBytes > (kLowWorkRamSoftFloorBytes + (128u * 1024u));
        sInitialMaintenanceCooldown =
            (steadyHeadroomHighWork && veryHealthyLowWork) ? 40u :
            steadyHeadroomHighWork ? 24u :
            12u;
    }
    else if (!criticalPressure)
    {
        // In moderate pressure, keep maintenance periodic to avoid heavy
        // per-slide spikes while still trimming regularly.
        if (sInitialMaintenanceCooldown > 0u)
        {
            --sInitialMaintenanceCooldown;
            sh2MasterMaintenanceTicksThisFrame_ = 0u;
            return false;
        }
        sInitialMaintenanceCooldown = 2u;
    }
    else
    {
        sInitialMaintenanceCooldown = 0u;
    }

    const uint16_t maintenanceTicksStart = Sh2FrtProfiler::Now();
    RunWorkRamMaintenance(false);
    sh2MasterMaintenanceTicksThisFrame_ =
        Sh2FrtProfiler::Elapsed(maintenanceTicksStart, Sh2FrtProfiler::Now());
    return true;
}

bool TrackSystem::RunWindowStage(const Vector3D& carWorldPosition,
                                 const Vector3D& trackOffset)
{
    const uint16_t windowTicksStart = Sh2FrtProfiler::Now();
    const bool windowSlid = UpdateActiveSegmentWindowForPosition(carWorldPosition, trackOffset);
    sh2MasterWindowTicksThisFrame_ =
        Sh2FrtProfiler::Elapsed(windowTicksStart, Sh2FrtProfiler::Now());
    return windowSlid;
}

void TrackSystem::RunTextureCompactionStage(bool windowSlid)
{
    const bool leakIsolationCompactionEnabled =
        kEnableTrackLeakIsolationFixed64Pipeline && kEnableLeakIsolationTextureCompaction;
    if (kEnableTrackLeakIsolationFixed64Pipeline && !leakIsolationCompactionEnabled)
    {
        (void)windowSlid;
        return;
    }
    if (!kEnableTrackRuntimeStabilization)
    {
        return;
    }
    if (!kEnableRuntimeTextureCompaction && !leakIsolationCompactionEnabled)
    {
        return;
    }

    const bool idleWindowFrame = !windowSlid && runtimeSlidesThisFrame_ == 0u;
    const bool compactByIdleSlack =
        idleWindowFrame && ShouldCompactTrackTextureHeapInStabilization();

    const uint16_t texCount = SRL::VDP1::GetTextureCount();
    const uint16_t trackTexUsed =
        (TrackTextureHeapBaseValid() && texCount > trackTextureHeapBase_)
            ? static_cast<uint16_t>(texCount - trackTextureHeapBase_)
            : 0u;
    uint16_t liveSlots = 0u;
    for (size_t i = 0; i < seg1FamilySlots_.size(); ++i)
    {
        const auto& family = seg1FamilySlots_[i];
        for (size_t li = 0; li < family.lodSlots.size(); ++li)
        {
            if (!IsVdp1TextureSlotLive(family.lodSlots[li])) continue;
            if (liveSlots < std::numeric_limits<uint16_t>::max()) ++liveSlots;
        }
    }
    const uint16_t slackSlots =
        (trackTexUsed > liveSlots) ? static_cast<uint16_t>(trackTexUsed - liveSlots) : 0u;
    const uint16_t reusableSlots = CountReusableTrackTextureSlots();
    const uint16_t pendingRetiredSlots = static_cast<uint16_t>(std::min<size_t>(
        g_trackPendingRetiredTextureSlots.size(),
        static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
    const uint16_t retiredSlots = static_cast<uint16_t>(std::min<uint32_t>(
        static_cast<uint32_t>(reusableSlots) + static_cast<uint32_t>(pendingRetiredSlots),
        static_cast<uint32_t>(std::numeric_limits<uint16_t>::max())));
    bool lwrValidNow = false;
    const size_t lwrFreeNow = GetLowWorkRamFreeBytesSafe(&lwrValidNow);
    const uint32_t lwrFreeNow32 = lwrValidNow
        ? static_cast<uint32_t>(lwrFreeNow)
        : 0u;
    const uint32_t baselineLwr = runtimeDiagnostics_.lowWorkBaselineFree;
    const uint32_t lwrDrop = (lwrValidNow && baselineLwr > lwrFreeNow32)
        ? static_cast<uint32_t>(baselineLwr - lwrFreeNow32)
        : 0u;
    const bool compactByHardCap =
        textureHeapCompactCooldown_ == 0u &&
        TrackTextureHeapBaseValid() &&
        !slideBackBuffer_.Ready() &&
        trackTexUsed >= 112u;
    const bool compactByRetireBacklog =
        textureHeapCompactCooldown_ == 0u &&
        TrackTextureHeapBaseValid() &&
        !slideBackBuffer_.Ready() &&
        trackTexUsed >= 64u &&
        retiredSlots >= 32u;
    const bool compactByLowWorkEmergency =
        textureHeapCompactCooldown_ == 0u &&
        TrackTextureHeapBaseValid() &&
        !slideBackBuffer_.Ready() &&
        lwrValidNow &&
        lwrFreeNow32 <= static_cast<uint32_t>(kLowWorkRamHardFloorBytes + (32u * 1024u)) &&
        trackTexUsed >= 96u;
    const bool compactByLeakIsolationBudget =
        leakIsolationCompactionEnabled &&
        textureHeapCompactCooldown_ == 0u &&
        TrackTextureHeapBaseValid() &&
        !slideBackBuffer_.Ready() &&
        (trackTexUsed >= 48u ||
         retiredSlots >= 12u ||
         (lwrValidNow && lwrDrop >= static_cast<uint32_t>(16u * 1024u)));

    bool compactByPressure = false;
    if (!compactByIdleSlack &&
        !idleWindowFrame &&
        textureHeapCompactCooldown_ == 0u &&
        TrackTextureHeapBaseValid() &&
        !slideBackBuffer_.Ready())
    {
        bool hwrValid = false;
        const size_t hwrFreeBytes = GetHighWorkRamFreeBytesSafe(&hwrValid);
        bool lwrValid = false;
        const size_t lwrFreeBytes = GetLowWorkRamFreeBytesSafe(&lwrValid);
        const bool highWorkPressure =
            hwrValid && hwrFreeBytes <= (kWorkRamHardFloorBytes + (8u * 1024u));
        const bool lowWorkPressure =
            lwrValid && lwrFreeBytes <= kLowWorkRamHardFloorBytes;

        constexpr uint16_t kPressureCompactTrackTexFloor = 72u;
        constexpr uint16_t kPressureRetiredSlotFloor = 12u;
        compactByPressure =
            (highWorkPressure || lowWorkPressure) &&
            (trackTexUsed >= kPressureCompactTrackTexFloor ||
             retiredSlots >= kPressureRetiredSlotFloor);
    }

    const bool compactByBaselineDrop =
        kEnableTrackCompactByBaselineDrop &&
        textureHeapCompactCooldown_ == 0u &&
        TrackTextureHeapBaseValid() &&
        !slideBackBuffer_.Ready() &&
        lwrValidNow &&
        baselineLwr > 0u &&
        lwrDrop >= static_cast<uint32_t>(kLowWorkCompactDropBytes);

    bool compactByStaleResidency = false;
    if (!compactByIdleSlack &&
        !compactByBaselineDrop &&
        textureHeapCompactCooldown_ == 0u &&
        TrackTextureHeapBaseValid() &&
        !slideBackBuffer_.Ready())
    {
        constexpr uint16_t kStaleCompactTrackTexFloor = 64u;
        constexpr uint16_t kStaleCompactSlackFloor = 8u;
        constexpr uint16_t kStaleCompactRetiredFloor = 6u;
        compactByStaleResidency =
            trackTexUsed >= kStaleCompactTrackTexFloor &&
            (slackSlots >= kStaleCompactSlackFloor ||
             retiredSlots >= kStaleCompactRetiredFloor);
    }

    if (!compactByIdleSlack &&
        !compactByHardCap &&
        !compactByRetireBacklog &&
        !compactByLowWorkEmergency &&
        !compactByLeakIsolationBudget &&
        !compactByPressure &&
        !compactByBaselineDrop &&
        !compactByStaleResidency)
    {
        return;
    }

    if (compactByBaselineDrop)
    {
        // If LWR drops far from startup baseline, force immediate cache trims
        // before rebuilding residency to avoid runaway growth during long laps.
        TrimRuntimeBlobScratchCaches(true);
        int32_t freeDelta = 0;
        (void)TrimWorkRamRetainedCapacities(true, &freeDelta);
    }

    const uint16_t texBefore = texCount;
    const size_t heapUsedBefore = SRL::VDP1::GetUsedMemory();
    const size_t heapTotalBefore = heapUsedBefore + SRL::VDP1::GetAvailableMemory();
    ResetSlidePrefetchState();
    if (!RebuildTrackTextureResidencyForWindow(true))
    {
        return;
    }

    if ((compactByBaselineDrop || compactByLowWorkEmergency) &&
        lwrDrop >= static_cast<uint32_t>(kLowWorkCompactCriticalDropBytes))
    {
        textureHeapCompactCooldown_ = 4u;
    }
    else
    {
        textureHeapCompactCooldown_ =
            compactByHardCap ? 24u :
            compactByRetireBacklog ? 10u :
            compactByLeakIsolationBudget ? 8u :
            compactByStaleResidency ? 12u : 20u;
    }
    const size_t heapUsedAfter = SRL::VDP1::GetUsedMemory();
    const size_t heapTotalAfter = heapUsedAfter + SRL::VDP1::GetAvailableMemory();
    const uint32_t heapPctBefore = (heapTotalBefore > 0u)
        ? static_cast<uint32_t>((heapUsedBefore * 100u) / heapTotalBefore)
        : 0u;
    const uint32_t heapPctAfter = (heapTotalAfter > 0u)
        ? static_cast<uint32_t>((heapUsedAfter * 100u) / heapTotalAfter)
        : 0u;
    if constexpr (kEnableTrackOverlayRows16To22)
    {
        const uint8_t reason =
            (compactByIdleSlack ? 1u : 0u) |
            (compactByPressure ? 2u : 0u) |
            (compactByStaleResidency ? 4u : 0u) |
            (compactByBaselineDrop ? 8u : 0u) |
            (compactByHardCap ? 16u : 0u) |
            (compactByLowWorkEmergency ? 32u : 0u) |
            (compactByRetireBacklog ? 64u : 0u) |
            (compactByLeakIsolationBudget ? 128u : 0u);
        SRL::Debug::Print(1, 17, "TRK tex compact tb:%u ta:%u hp:%u>%u rs:%u sl:%u rt:%u ld:%u",
                          static_cast<unsigned>(texBefore),
                          static_cast<unsigned>(SRL::VDP1::GetTextureCount()),
                          static_cast<unsigned>(heapPctBefore),
                          static_cast<unsigned>(heapPctAfter),
                          static_cast<unsigned>(reason),
                          static_cast<unsigned>(slackSlots),
                          static_cast<unsigned>(retiredSlots),
                          static_cast<unsigned>(lwrDrop));
    }
    if (runtimeDiagnostics_.RuntimeStatsLogsEnabled())
    {
        const uint8_t reason =
            (compactByIdleSlack ? 1u : 0u) |
            (compactByPressure ? 2u : 0u) |
            (compactByStaleResidency ? 4u : 0u) |
            (compactByBaselineDrop ? 8u : 0u) |
            (compactByHardCap ? 16u : 0u) |
            (compactByLowWorkEmergency ? 32u : 0u) |
            (compactByRetireBacklog ? 64u : 0u);
        SRL::Debug::Print(1, 17, "TRK compact rs:%u tb:%u ta:%u hp:%u>%u ld:%u",
                          static_cast<unsigned>(reason),
                          static_cast<unsigned>(texBefore),
                          static_cast<unsigned>(SRL::VDP1::GetTextureCount()),
                          static_cast<unsigned>(heapPctBefore),
                          static_cast<unsigned>(heapPctAfter),
                          static_cast<unsigned>(lwrDrop));
    }
}

void TrackSystem::UpdatePrefetchSpeedProxy(const Vector3D& carWorldPosition)
{
    if (!PrefetchSpeedProxyValid())
    {
        prefetchLastCarWorldPosition_ = carWorldPosition;
        prefetchSpeedProxyRaw_ = 0u;
        SetPrefetchSpeedProxyValid(true);
        return;
    }

    const auto absRaw = [](int32_t v) -> uint32_t
    {
        return (v < 0)
            ? static_cast<uint32_t>(-static_cast<int64_t>(v))
            : static_cast<uint32_t>(v);
    };

    const int32_t dxRaw = (carWorldPosition.X - prefetchLastCarWorldPosition_.X).RawValue();
    const int32_t dzRaw = (carWorldPosition.Z - prefetchLastCarWorldPosition_.Z).RawValue();
    const uint32_t dx = absRaw(dxRaw) >> 16;
    const uint32_t dz = absRaw(dzRaw) >> 16;
    const uint32_t major = (dx > dz) ? dx : dz;
    const uint32_t minor = (dx > dz) ? dz : dx;
    uint32_t planarUnitsPerFrame = major + (minor >> 1);
    if (planarUnitsPerFrame > static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()))
    {
        planarUnitsPerFrame = static_cast<uint32_t>(std::numeric_limits<uint16_t>::max());
    }
    prefetchSpeedProxyRaw_ = static_cast<uint16_t>(planarUnitsPerFrame);
    prefetchLastCarWorldPosition_ = carWorldPosition;
}

void TrackSystem::RunPrefetchStage(bool windowSlid)
{
    if (kEnableTrackLeakIsolationFixed64Pipeline && !kEnableLeakIsolationPrefetch)
    {
        (void)windowSlid;
        sh2MasterPrefetchTicksThisFrame_ = 0u;
        return;
    }
    const uint16_t prefetchTicksStart = Sh2FrtProfiler::Now();
    TryPrefetchUpcomingSegment();
    if (kEnableTrackLeakIsolationFixed64Pipeline)
    {
        sh2MasterPrefetchTicksThisFrame_ =
            Sh2FrtProfiler::Elapsed(prefetchTicksStart, Sh2FrtProfiler::Now());
        return;
    }
    if (!windowSlid) PrewarmNextSegmentLod32();
    if (!windowSlid) PrewarmUpcomingBoundaryLods();
    sh2MasterPrefetchTicksThisFrame_ =
        Sh2FrtProfiler::Elapsed(prefetchTicksStart, Sh2FrtProfiler::Now());
}

bool TrackSystem::RunPostSlideMaintenanceStage(bool slidThisFrame)
{
    const bool shouldRunPostSlideMaintenance = ShouldRunPostSlideMaintenance(slidThisFrame);
    if (!(kEnableTrackRuntimeStabilization && slidThisFrame && shouldRunPostSlideMaintenance))
    {
        return false;
    }

    const uint16_t maintenanceTicksStart = Sh2FrtProfiler::Now();
    RunWorkRamMaintenance(true);
    const uint16_t maintenanceTicks =
        Sh2FrtProfiler::Elapsed(maintenanceTicksStart, Sh2FrtProfiler::Now());
    const uint32_t totalMaintenanceTicks =
        static_cast<uint32_t>(sh2MasterMaintenanceTicksThisFrame_) +
        static_cast<uint32_t>(maintenanceTicks);
    sh2MasterMaintenanceTicksThisFrame_ = static_cast<uint16_t>(
        std::min<uint32_t>(totalMaintenanceTicks,
                           static_cast<uint32_t>(std::numeric_limits<uint16_t>::max())));
    return true;
}

bool TrackSystem::RunLegacyMaintenanceStage(bool windowSlid)
{
    if (kEnableTrackRuntimeStabilization)
    {
        return false;
    }
    const uint16_t maintenanceTicksStart = Sh2FrtProfiler::Now();
    RunWorkRamMaintenance(windowSlid);
    sh2MasterMaintenanceTicksThisFrame_ =
        Sh2FrtProfiler::Elapsed(maintenanceTicksStart, Sh2FrtProfiler::Now());
    return true;
}

void TrackSystem::ResetFramePlan(TrackFramePlan& plan) const
{
    plan.frameId = frameIdThisFrame_;
    plan.SetValid(false);
    plan.flags = 0u;
    plan.plannerTicksSlave = 0u;
    plan.desiredLodByLogicalRank.fill(static_cast<uint8_t>(0xFF));
    plan.desiredBaseRankByLogicalRank.fill(static_cast<int8_t>(-1));
}

void TrackSystem::ApplyFramePlanLodTargets(const TrackFramePlan& plan)
{
    if (!plan.Valid()) return;

    const size_t planApplyCount = std::min<size_t>(
        segmentRenderers_.size(),
        plan.desiredLodByLogicalRank.size());
    for (size_t logicalRank = 0; logicalRank < planApplyCount; ++logicalRank)
    {
        const size_t physicalIdx =
            LogicalToPhysicalWindowIndex(logicalRank, segmentRenderers_.size());
        if (physicalIdx >= segmentRenderers_.size()) continue;
        SegmentRenderEntry& entry = segmentRenderers_[physicalIdx];
        const uint8_t desiredLodIndex = plan.desiredLodByLogicalRank[logicalRank];
        entry.lodState.desiredLodIndex = desiredLodIndex;
        entry.lodState.desiredBaseRank =
            (desiredLodIndex <= 3u && entry.lodState.HasPerFaceRankOffsets())
                ? static_cast<int16_t>(plan.desiredBaseRankByLogicalRank[logicalRank])
                : static_cast<int16_t>(-1);
    }
}

void TrackSystem::PromoteLastValidFramePlanForCurrentFrame(bool markStale)
{
    if (!framePlanLastValid_.Valid())
    {
        ResetFramePlan(framePlanCurrent_);
        framePlanCurrent_.frameId = frameIdThisFrame_;
        framePlanCurrent_.flags |= kTrackFramePlanFlagFallback;
        framePlanSortedHandles_.clear();
        return;
    }

    framePlanCurrent_ = framePlanLastValid_;
    framePlanCurrent_.frameId = frameIdThisFrame_;
    framePlanCurrent_.flags |= kTrackFramePlanFlagFallback;
    if (markStale)
    {
        framePlanCurrent_.flags |= kTrackFramePlanFlagStale;
    }
    framePlanSortedHandles_ = framePlanLastValidSortedHandles_;
}

void TrackSystem::BuildAndApplyFramePlanStage(const Vector3D& trackOffset,
                                              const Vector3D& cameraLocation,
                                              const Vector3D& carWorldPosition)
{
    const uint16_t planTicksStart = Sh2FrtProfiler::Now();
    ResetFramePlan(framePlanCurrent_);
    framePlanSortedHandles_.clear();

    if (!kEnableTrackRuntimeStabilization || segmentRenderers_.empty() || totalSegmentCount_ == 0)
    {
        sh2MasterPlanTicksThisFrame_ =
            Sh2FrtProfiler::Elapsed(planTicksStart, Sh2FrtProfiler::Now());
        return;
    }

    const auto& sortedHandles = BuildStabilizedSortedHandles(trackOffset, cameraLocation);
    framePlanSortedHandles_.reserve(sortedHandles.size());
    for (size_t i = 0; i < sortedHandles.size(); ++i)
    {
        const SegmentHandle handle = sortedHandles[i];
        auto* entry = segmentPool_.Resolve(handle);
        if (!entry || !entry->renderer)
        {
            framePlanCurrent_.flags |= kTrackFramePlanFlagPartial;
            continue;
        }

        if (framePlanSortedHandles_.size() < kTrackSegmentLimit)
        {
            framePlanSortedHandles_.push_back(handle);
        }
    }

    const size_t planRankCount = std::min<size_t>(
        segmentRenderers_.size(),
        framePlanCurrent_.desiredLodByLogicalRank.size());
    for (size_t logicalRank = 0; logicalRank < planRankCount; ++logicalRank)
    {
        const size_t physicalIdx =
            LogicalToPhysicalWindowIndex(logicalRank, segmentRenderers_.size());
        if (physicalIdx >= segmentRenderers_.size()) continue;
        SegmentRenderEntry& entry = segmentRenderers_[physicalIdx];
        const uint8_t desiredLodIndex = ResolveSegmentLodIndexByRank(logicalRank);
        framePlanCurrent_.desiredLodByLogicalRank[logicalRank] = desiredLodIndex;
        framePlanCurrent_.desiredBaseRankByLogicalRank[logicalRank] =
            entry.lodState.HasPerFaceRankOffsets()
                ? static_cast<int8_t>(logicalRank)
                : static_cast<int8_t>(-1);
    }

    framePlanCurrent_.frameId = frameIdThisFrame_;
    framePlanCurrent_.plannerTicksSlave = sh2SlaveSortTicksThisFrame_;

    if (!framePlanSortedHandles_.empty())
    {
        framePlanCurrent_.SetValid(true);
        framePlanLastValid_ = framePlanCurrent_;
        framePlanLastValidSortedHandles_ = framePlanSortedHandles_;
    }
    else
    {
        PromoteLastValidFramePlanForCurrentFrame(true);
    }

    if (framePlanCurrent_.Valid())
    {
        ApplyFramePlanLodTargets(framePlanCurrent_);
    }
    else
    {
        UpdateDesiredStabilizedWindowLodTargets();
    }

    sh2SlavePlanTicksThisFrame_ = framePlanCurrent_.plannerTicksSlave;
    sh2MasterPlanTicksThisFrame_ =
        Sh2FrtProfiler::Elapsed(planTicksStart, Sh2FrtProfiler::Now());
}

void TrackSystem::BuildOrderedHandlesStage(const Vector3D& trackOffset,
                                           const Vector3D& cameraLocation,
                                           std::vector<SegmentHandle>& outOrderedHandles)
{
    outOrderedHandles.clear();
    if (!kEnableTrackRuntimeStabilization)
    {
        outOrderedHandles = BuildVisibleSegmentOrder(trackOffset, cameraLocation);
    }
    if (!kEnableTrackRuntimeStabilization &&
        outOrderedHandles.empty() &&
        !segmentRenderers_.empty())
    {
        // Last safety net: keep rendering possible even after transient handle corruption.
        BuildSegmentHandleTable();
        outOrderedHandles = BuildVisibleSegmentOrder(trackOffset, cameraLocation);
    }
}

bool TrackSystem::RunWorkingSetStage()
{
    if (!FamilyWorkingSetDirty())
    {
        return false;
    }
    const uint16_t workingSetTicksStart = Sh2FrtProfiler::Now();
    RefreshFamilyWorkingSet(false);
    sh2MasterWorkingSetTicksThisFrame_ =
        Sh2FrtProfiler::Elapsed(workingSetTicksStart, Sh2FrtProfiler::Now());
    SetFamilyWorkingSetDirty(false);
    return true;
}

void TrackSystem::RunDrawStage(const std::vector<SegmentHandle>& orderedHandles,
                               const Vector3D& trackOffset,
                               const Vector3D& lightDirection,
                               const Vector3D& cameraLocation,
                               std::array<uint8_t, kTrackSegmentLimit + 1>& preparedCountById,
                               std::array<uint8_t, kTrackSegmentLimit + 1>& renderedCountById,
                               bool& segment01Logged,
                               bool& segment01Prepared)
{
    const uint16_t drawTicksStart = Sh2FrtProfiler::Now();
    if (kEnableLeakABBypassTrackDraw)
    {
        (void)orderedHandles;
        (void)trackOffset;
        (void)lightDirection;
        (void)cameraLocation;
        (void)preparedCountById;
        (void)renderedCountById;
        (void)segment01Logged;
        (void)segment01Prepared;
        runtimeSafeNoDrawThisFrame_ = 1u;
        sh2MasterDrawTicksThisFrame_ =
            Sh2FrtProfiler::Elapsed(drawTicksStart, Sh2FrtProfiler::Now());
        return;
    }
    RenderVisibleSegmentOrder(orderedHandles,
                              trackOffset,
                              lightDirection,
                              cameraLocation,
                              preparedCountById,
                              renderedCountById,
                              segment01Logged,
                              segment01Prepared);
    sh2MasterDrawTicksThisFrame_ =
        Sh2FrtProfiler::Elapsed(drawTicksStart, Sh2FrtProfiler::Now());
}

bool TrackSystem::ShouldRunFramePlanThisFrame(bool slidThisFrame)
{
    bool runFramePlan = true;
    static uint8_t sFramePlanDecimator = 0u;
    if (kEnableTrackRuntimeStabilization &&
        !slidThisFrame &&
        workRamMaintenance_.memoryPressureLevelThisFrame == static_cast<uint8_t>(MemoryPressureLevel::Normal) &&
        !PendingLodWorkExists())
    {
        const bool prefetchResident =
            slidePrefetchSegmentId_ > 0 &&
            !slidePrefetchFamilyIds_.empty();
        const uint8_t steadyPlanSkipFrames =
            kEnableTrackLeakIsolationFixed64Pipeline
                ? (prefetchResident ? 20u : 12u)
                : (prefetchResident ? 8u : 4u);
        if (sFramePlanDecimator > 0u)
        {
            --sFramePlanDecimator;
            runFramePlan = false;
        }
        else
        {
            sFramePlanDecimator = steadyPlanSkipFrames;
        }
    }
    else
    {
        sFramePlanDecimator = 0u;
    }
    return runFramePlan;
}

void TrackSystem::RunFramePlanStage(const Vector3D& trackOffset,
                                    const Vector3D& cameraLocation,
                                    const Vector3D& carWorldPosition,
                                    bool slidThisFrame)
{
    if (ShouldRunFramePlanThisFrame(slidThisFrame))
    {
        BuildAndApplyFramePlanStage(trackOffset, cameraLocation, carWorldPosition);
        return;
    }

    PromoteLastValidFramePlanForCurrentFrame(true);
    if (framePlanCurrent_.Valid())
    {
        ApplyFramePlanLodTargets(framePlanCurrent_);
        sh2SlavePlanTicksThisFrame_ = framePlanCurrent_.plannerTicksSlave;
    }
    else
    {
        UpdateDesiredStabilizedWindowLodTargets();
        sh2SlavePlanTicksThisFrame_ = 0u;
    }
    sh2MasterPlanTicksThisFrame_ = 0u;
}

void TrackSystem::FinalizeDrawStage(
    uint16_t frameTicksStart,
    const std::array<uint8_t, kTrackSegmentLimit + 1>& preparedCountById,
    const std::array<uint8_t, kTrackSegmentLimit + 1>& renderedCountById)
{
    sh2MasterFrameTicksThisFrame_ =
        Sh2FrtProfiler::Elapsed(frameTicksStart, Sh2FrtProfiler::Now());
    for (size_t id = 1; id <= kTrackSegmentLimit; ++id)
    {
        if (runtimeDiagnostics_.RuntimeStatsLogsEnabled() && preparedCountById[id] > 1)
        {
            SRL::Debug::Print(1, 25, "WARN prep dup seg:%u count:%u",
                              (unsigned)id, (unsigned)preparedCountById[id]);
        }
        if (runtimeDiagnostics_.RuntimeStatsLogsEnabled() && renderedCountById[id] > 1)
        {
            SRL::Debug::Print(1, 24, "WARN rend dup seg:%u count:%u",
                              (unsigned)id, (unsigned)renderedCountById[id]);
        }
    }

    if constexpr (kEnableTrackPhaseRamTelemetry)
    {
        const auto hwr = SRL::Memory::HighWorkRam::GetReport();
        const auto lwr = SRL::Memory::LowWorkRam::GetReport();
        frameMemoryTelemetry_.phaseHwrAfterDraw = static_cast<uint32_t>(hwr.FreeSize);
        frameMemoryTelemetry_.phaseLwrAfterDraw = static_cast<uint32_t>(lwr.FreeSize);
    }
}

void TrackSystem::RenderFrame(bool renderTrack,
                              const Vector3D& trackOffset,
                              const Vector3D& lightDirection,
                              const Vector3D& cameraLocation,
                              const Vector3D& cameraLookTarget,
                              const Vector3D& carWorldPosition)
{
    if (!renderTrack || !ReadyFlag())
    {
        return;
    }

    Sh2FrtProfiler::EnsureInitialized();
    const uint16_t frameTicksStart = Sh2FrtProfiler::Now();
    const uint16_t streamTicksStart = frameTicksStart;

    if constexpr (kEnableTrackPhaseRamTelemetry)
    {
        const auto hwr = SRL::Memory::HighWorkRam::GetReport();
        const auto lwr = SRL::Memory::LowWorkRam::GetReport();
        frameMemoryTelemetry_.phaseHwrBeforeStream = static_cast<uint32_t>(hwr.FreeSize);
        frameMemoryTelemetry_.phaseLwrBeforeStream = static_cast<uint32_t>(lwr.FreeSize);
    }

    constexpr bool kEnableLowWorkRamStageProbe = false;
    static uint8_t sLowWorkRamProbeDecimator = 0u;
    const bool lowWorkRamProbeEnabled = kEnableLowWorkRamStageProbe && (sLowWorkRamProbeDecimator == 0u);
    if constexpr (kEnableLowWorkRamStageProbe)
    {
        if (sLowWorkRamProbeDecimator == 0u)
        {
            sLowWorkRamProbeDecimator = 3u;
        }
        else
        {
            --sLowWorkRamProbeDecimator;
        }
    }

    auto makeLowWorkStage = [](const char* name) -> LowWorkRamStageDelta
    {
        LowWorkRamStageDelta delta{};
        delta.name = name;
        return delta;
    };
    LowWorkRamStageDelta lowWorkDeltaMaintenance0 = makeLowWorkStage("m0");
    LowWorkRamStageDelta lowWorkDeltaWindow = makeLowWorkStage("win");
    LowWorkRamStageDelta lowWorkDeltaPrefetch = makeLowWorkStage("pf");
    LowWorkRamStageDelta lowWorkDeltaMaintenance1 = makeLowWorkStage("m1");
    LowWorkRamStageDelta lowWorkDeltaLod = makeLowWorkStage("lod");
    LowWorkRamStageDelta lowWorkDeltaWorkingSet = makeLowWorkStage("ws");
    LowWorkRamStageSample lowWorkFrameStart{};
    LowWorkRamStageSample lowWorkProbeCursor{};
    if (lowWorkRamProbeEnabled)
    {
        lowWorkFrameStart = CaptureLowWorkRamStageSample();
        lowWorkProbeCursor = lowWorkFrameStart;
    }
    auto captureLowWorkStage = [&](LowWorkRamStageDelta& outDelta)
    {
        if (!lowWorkRamProbeEnabled) return;
        const LowWorkRamStageSample after = CaptureLowWorkRamStageSample();
        outDelta = BuildLowWorkRamStageDelta(outDelta.name, lowWorkProbeCursor, after);
        lowWorkProbeCursor = after;
    };

    UpdatePrefetchSpeedProxy(carWorldPosition);
    UpdateCameraDrivenWindowDirection(trackOffset, cameraLocation, cameraLookTarget);
    TickRuntimeFrameCooldowns();

    bool segment01Logged = false;
    bool segment01Prepared = false;
    if (TrackPipeline::TrackMaintenanceStage::RunInitial(*this))
    {
        captureLowWorkStage(lowWorkDeltaMaintenance0);
    }
    const bool windowSlid = TrackPipeline::TrackWindowStage::Run(*this, carWorldPosition, trackOffset);
    captureLowWorkStage(lowWorkDeltaWindow);
    TrackPipeline::TrackPrefetchStage::RunCompaction(*this, windowSlid);
    TrackPipeline::TrackPrefetchStage::RunPrefetch(*this, windowSlid);
    captureLowWorkStage(lowWorkDeltaPrefetch);
    const bool slidThisFrame = windowSlid || (runtimeSlidesThisFrame_ != 0);

    if (TrackPipeline::TrackMaintenanceStage::RunPostSlide(*this, slidThisFrame))
    {
        captureLowWorkStage(lowWorkDeltaMaintenance1);
    }

    if (TrackPipeline::TrackMaintenanceStage::RunLegacy(*this, windowSlid))
    {
        captureLowWorkStage(lowWorkDeltaMaintenance1);
    }
    RunFramePlanStage(trackOffset, cameraLocation, carWorldPosition, slidThisFrame);
    if (TrackPipeline::TrackLodStage::RunRecovery(*this, slidThisFrame))
    {
        captureLowWorkStage(lowWorkDeltaLod);
    }

    std::vector<SegmentHandle> orderedHandles{};
    BuildOrderedHandlesStage(trackOffset, cameraLocation, orderedHandles);
    if (TrackPipeline::TrackWorkingSetStage::Run(*this))
    {
        captureLowWorkStage(lowWorkDeltaWorkingSet);
    }
    sh2MasterStreamTicksThisFrame_ =
        Sh2FrtProfiler::Elapsed(streamTicksStart, Sh2FrtProfiler::Now());
    if constexpr (kEnableTrackPhaseRamTelemetry)
    {
        const auto hwr = SRL::Memory::HighWorkRam::GetReport();
        const auto lwr = SRL::Memory::LowWorkRam::GetReport();
        frameMemoryTelemetry_.phaseHwrAfterStream = static_cast<uint32_t>(hwr.FreeSize);
        frameMemoryTelemetry_.phaseLwrAfterStream = static_cast<uint32_t>(lwr.FreeSize);
    }
    if (lowWorkRamProbeEnabled)
    {
        const auto lowWorkFrameDelta = BuildLowWorkRamStageDelta("frm",
                                                                 lowWorkFrameStart,
                                                                 lowWorkProbeCursor);
        const LowWorkRamStageDelta* culprit = nullptr;
        for (const LowWorkRamStageDelta* stage : {
                 &lowWorkDeltaMaintenance0,
                 &lowWorkDeltaWindow,
                 &lowWorkDeltaPrefetch,
                 &lowWorkDeltaMaintenance1,
                 &lowWorkDeltaLod,
                 &lowWorkDeltaWorkingSet })
        {
            if (stage->freeDelta >= 0) continue;
            if (!culprit || stage->freeDelta < culprit->freeDelta)
            {
                culprit = stage;
            }
        }
        SRL::Debug::Print(2, 23, "LWF c:%s sl:%u df:%d py:%d ov:%d   ",
                          culprit ? culprit->name : "none",
                          slidThisFrame ? 1u : 0u,
                          culprit ? culprit->freeDelta : lowWorkFrameDelta.freeDelta,
                          culprit ? culprit->payloadDelta : lowWorkFrameDelta.payloadDelta,
                          culprit ? culprit->overheadDelta : lowWorkFrameDelta.overheadDelta);
        SRL::Debug::Print(2, 24, "LWA m0:%d w:%d pf:%d m1:%d      ",
                          lowWorkDeltaMaintenance0.freeDelta,
                          lowWorkDeltaWindow.freeDelta,
                          lowWorkDeltaPrefetch.freeDelta,
                          lowWorkDeltaMaintenance1.freeDelta);
        SRL::Debug::Print(2, 25, "LWB ld:%d ws:%d fr:%d lf:%d fb:%d ",
                          lowWorkDeltaLod.freeDelta,
                          lowWorkDeltaWorkingSet.freeDelta,
                          lowWorkFrameDelta.freeDelta,
                          lowWorkFrameDelta.largestFreeDelta,
                          lowWorkFrameDelta.freeBlocksDelta);
    }
    RunSeg1DiagnosticsForFrame();
    std::array<uint8_t, kTrackSegmentLimit + 1> preparedCountById{};
    std::array<uint8_t, kTrackSegmentLimit + 1> renderedCountById{};
    {
        LWR_PROBE_BEGIN();
        RunDrawStage(orderedHandles,
                     trackOffset,
                     lightDirection,
                     cameraLocation,
                     preparedCountById,
                     renderedCountById,
                     segment01Logged,
                     segment01Prepared);
        LWR_PROBE_END(g_lwrStageAccum.drawStage);
    }
    FinalizeDrawStage(frameTicksStart, preparedCountById, renderedCountById);

    (void)segment01Prepared;
}

void TrackSystem::PresentVdp1FpsTelemetry()
{
    if (!runtimeDiagnostics_.RuntimeStatsLogsEnabled()) return;

    constexpr uint32_t kVdp1FaceCostBytes = 64u;
    constexpr uint32_t kVdp1FrameBudgetBytes = 512u * 1024u;
#ifdef SRL_MODE_NTSC
    constexpr uint16_t kDisplayRefreshHz = 60u;
#else
    constexpr uint16_t kDisplayRefreshHz = 50u;
#endif
    static uint64_t sCmdPctAccum = 0u;
    static uint64_t sHeapPctAccum = 0u;
    static uint64_t sTrackFacesAccum = 0u;
    static uint8_t sSamples = 0u;
    static uint8_t sPeakCmdPct = 0u;
    static uint8_t sPeakHeapPct = 0u;
    static uint32_t sPeakTrackFaces = 0u;
    static uint16_t sPeakTexCount = 0u;
    static bool sFpsVblankValid = false;
    static uint32_t sFpsLastVblank = 0u;
    static uint8_t sFpsSampleFrames = 0u;
    static uint16_t sFpsSampleVblanks = 0u;
    static uint8_t sFpsFramesOver30Budget = 0u;
    static uint8_t sFpsFramesOver60Budget = 0u;
    static uint16_t sFpsX10 = 0u;
    static uint16_t sFrameMsX10 = 0u;
    static uint8_t sDrop30Pct = 0u;
    static uint8_t sDrop60Pct = 0u;

    const auto& telemetry = coordinator_.Telemetry();
    const uint32_t trackFaces = telemetry.submittedTrackFaces;
    const uint32_t trackSegments = telemetry.submittedTrackSegments;
    const uint32_t cmdBytesRaw = trackFaces * kVdp1FaceCostBytes;
    const uint32_t cmdBytes = (cmdBytesRaw > kVdp1FrameBudgetBytes) ? kVdp1FrameBudgetBytes : cmdBytesRaw;
    const uint8_t cmdPct = (kVdp1FrameBudgetBytes > 0u)
        ? static_cast<uint8_t>((cmdBytes * 100u) / kVdp1FrameBudgetBytes)
        : 0u;

    const size_t heapUsed = SRL::VDP1::GetUsedMemory();
    const size_t heapFree = SRL::VDP1::GetAvailableMemory();
    const size_t heapTotal = heapUsed + heapFree;
    const uint8_t heapPct = (heapTotal > 0u)
        ? static_cast<uint8_t>((heapUsed * 100u) / heapTotal)
        : 0u;
    const uint16_t texCount = SRL::VDP1::GetTextureCount();

    sCmdPctAccum += static_cast<uint64_t>(cmdPct);
    sHeapPctAccum += static_cast<uint64_t>(heapPct);
    sTrackFacesAccum += static_cast<uint64_t>(trackFaces);
    if (sSamples < std::numeric_limits<uint8_t>::max()) ++sSamples;
    if (cmdPct > sPeakCmdPct) sPeakCmdPct = cmdPct;
    if (heapPct > sPeakHeapPct) sPeakHeapPct = heapPct;
    if (trackFaces > sPeakTrackFaces) sPeakTrackFaces = trackFaces;
    if (texCount > sPeakTexCount) sPeakTexCount = texCount;

    const uint32_t sampleCount = (sSamples > 0u) ? static_cast<uint32_t>(sSamples) : 1u;
    const uint32_t avgCmdPct = static_cast<uint32_t>(sCmdPctAccum / sampleCount);
    const uint32_t avgHeapPct = static_cast<uint32_t>(sHeapPctAccum / sampleCount);
    const uint32_t avgTrackFaces = static_cast<uint32_t>(sTrackFacesAccum / sampleCount);

    const uint32_t vblankNow = SRL_AppGetVblankCounter();
    if (!sFpsVblankValid)
    {
        sFpsVblankValid = true;
        sFpsLastVblank = vblankNow;
    }
    else
    {
        uint32_t vblankDelta = vblankNow - sFpsLastVblank;
        sFpsLastVblank = vblankNow;
        if (vblankDelta == 0u) vblankDelta = 1u;

        if (sFpsSampleFrames < std::numeric_limits<uint8_t>::max()) ++sFpsSampleFrames;
        sFpsSampleVblanks = static_cast<uint16_t>(std::min<uint32_t>(
            static_cast<uint32_t>(sFpsSampleVblanks) + vblankDelta,
            static_cast<uint32_t>(std::numeric_limits<uint16_t>::max())));
        const uint32_t frameTimeX100 = static_cast<uint32_t>(
            (static_cast<uint64_t>(vblankDelta) * 100000u + (kDisplayRefreshHz / 2u)) /
            static_cast<uint64_t>(kDisplayRefreshHz));
        constexpr uint32_t kTarget30FrameTimeX100 = 100000u / 30u;
        constexpr uint32_t kTarget60FrameTimeX100 = 100000u / 60u;
        if (frameTimeX100 > kTarget30FrameTimeX100 && sFpsFramesOver30Budget < std::numeric_limits<uint8_t>::max())
        {
            ++sFpsFramesOver30Budget;
        }
        if (frameTimeX100 > kTarget60FrameTimeX100 && sFpsFramesOver60Budget < std::numeric_limits<uint8_t>::max())
        {
            ++sFpsFramesOver60Budget;
        }

        constexpr uint8_t kSampleWindowFrames = 60u;
        if (sFpsSampleFrames >= kSampleWindowFrames && sFpsSampleVblanks > 0u)
        {
            const uint64_t fpsNum = static_cast<uint64_t>(kDisplayRefreshHz) *
                                    static_cast<uint64_t>(10u) *
                                    static_cast<uint64_t>(sFpsSampleFrames);
            sFpsX10 = static_cast<uint16_t>(
                (fpsNum + static_cast<uint64_t>(sFpsSampleVblanks / 2u)) /
                static_cast<uint64_t>(sFpsSampleVblanks));

            const uint64_t frameMsNum = static_cast<uint64_t>(10000u) *
                                        static_cast<uint64_t>(sFpsSampleVblanks);
            const uint64_t frameMsDen = static_cast<uint64_t>(kDisplayRefreshHz) *
                                        static_cast<uint64_t>(sFpsSampleFrames);
            sFrameMsX10 = static_cast<uint16_t>(
                (frameMsNum + (frameMsDen / 2u)) / std::max<uint64_t>(1u, frameMsDen));

            sDrop30Pct = static_cast<uint8_t>(
                (static_cast<uint64_t>(sFpsFramesOver30Budget) * 100u) /
                static_cast<uint64_t>(sFpsSampleFrames));
            sDrop60Pct = static_cast<uint8_t>(
                (static_cast<uint64_t>(sFpsFramesOver60Budget) * 100u) /
                static_cast<uint64_t>(sFpsSampleFrames));

            sFpsSampleFrames = 0u;
            sFpsSampleVblanks = 0u;
            sFpsFramesOver30Budget = 0u;
            sFpsFramesOver60Budget = 0u;
        }
    }

    SRL::Debug::Print(1, 14, "V1 t s:%u f:%u c:%u%%",
                      static_cast<unsigned>(trackSegments),
                      static_cast<unsigned>(trackFaces),
                      static_cast<unsigned>(cmdPct));
    SRL::Debug::Print(1, 15, "V1 h u:%u f:%u %u%% t:%u",
                      static_cast<unsigned>(heapUsed),
                      static_cast<unsigned>(heapFree),
                      static_cast<unsigned>(heapPct),
                      static_cast<unsigned>(texCount));
    SRL::Debug::Print(1, 16, "V1 p f:%u c:%u h:%u t:%u",
                      static_cast<unsigned>(sPeakTrackFaces),
                      static_cast<unsigned>(sPeakCmdPct),
                      static_cast<unsigned>(sPeakHeapPct),
                      static_cast<unsigned>(sPeakTexCount));
    SRL::Debug::Print(1, 17, "V1 a f:%u c:%u h:%u n:%u F:%u.%u m:%u.%u d3:%u d6:%u",
                      static_cast<unsigned>(avgTrackFaces),
                      static_cast<unsigned>(avgCmdPct),
                      static_cast<unsigned>(avgHeapPct),
                      static_cast<unsigned>(sSamples),
                      static_cast<unsigned>(sFpsX10 / 10u),
                      static_cast<unsigned>(sFpsX10 % 10u),
                      static_cast<unsigned>(sFrameMsX10 / 10u),
                      static_cast<unsigned>(sFrameMsX10 % 10u),
                      static_cast<unsigned>(sDrop30Pct),
                      static_cast<unsigned>(sDrop60Pct));
    SRL::Debug::Print(1, 18, "                         ");
    SRL::Debug::Print(1, 19, "                         ");
    SRL::Debug::Print(1, 20, "                         ");

    if ((frameIdThisFrame_ & 0x3Fu) == 0u)
    {
        sCmdPctAccum = 0u;
        sHeapPctAccum = 0u;
        sTrackFacesAccum = 0u;
        sSamples = 0u;
        sPeakCmdPct = 0u;
        sPeakHeapPct = 0u;
        sPeakTrackFaces = 0u;
        sPeakTexCount = 0u;
    }
}

void TrackSystem::PresentPerFrameDebugOverlay()
{
    constexpr bool kEnablePerFrameDebugPrints = false;
    if (!kEnablePerFrameDebugPrints) return;

    const auto hwr = SRL::Memory::HighWorkRam::GetReport();
    const auto lwr = SRL::Memory::LowWorkRam::GetReport();
    if (hwr.TotalSize == 0 || hwr.FreeSize > hwr.TotalSize)
    {
        SRL::Debug::Print(1, 30, "WR bad f:%lu t:%lu",
                          static_cast<unsigned long>(hwr.FreeSize),
                          static_cast<unsigned long>(hwr.TotalSize));
        return;
    }
    const unsigned long hwrUsed = static_cast<unsigned long>(hwr.TotalSize - hwr.FreeSize);
    const unsigned long hwrTotal = static_cast<unsigned long>(hwr.TotalSize);
    const unsigned long lwrUsed = static_cast<unsigned long>(lwr.TotalSize - lwr.FreeSize);
    const unsigned long lwrTotal = static_cast<unsigned long>(lwr.TotalSize);
    SRL::Debug::Print(1, 30, "WR H:%lu/%lu L:%lu/%lu", hwrUsed, hwrTotal, lwrUsed, lwrTotal);
    SRL::Debug::Print(1, 4, "TG c:%u a:%u f:%u j:%u ",
                      (unsigned)seg1TgaPreloadCount_,
                      (unsigned)seg1TgaAttemptCount_,
                      (unsigned)seg1TgaFailCount_,
                      (unsigned)seg1TgaJsonOk_);
    SRL::Debug::Print(1, 24, "SM b:%u s:%s ", (unsigned)g_smapBytes, g_smapSig);
    SRL::Debug::Print(1, 25, "TG n:%s ", g_tgaLastName);
    SRL::Debug::Print(1, 26, "TG t:%s ", g_tgaLastTry);
    SRL::Debug::Print(1, 27, "TG r:%s ", g_tgaLastResult);
    if (!seg1FamilySlots_.empty())
    {
        const Seg1FamilySlotEntry* fam1 = nullptr;
        for (const auto& e : seg1FamilySlots_)
        {
            if (e.familyId == 1)
            {
                fam1 = &e;
                break;
            }
        }
        if (fam1)
        {
            SRL::Debug::Print(1, 28, "F1 s32:%u s64:%u ",
                              (unsigned)fam1->lodSlots[2],
                              (unsigned)fam1->lodSlots[3]);
            auto texDim = [&](uint16_t slot, char* out, size_t outSize)
            {
                if (!out || outSize == 0) return;
                out[0] = '\0';
                if (slot == No_Texture || slot >= SRL_MAX_TEXTURES || SRL::VDP1::Metadata[slot].Texture == nullptr)
                {
                    std::snprintf(out, outSize, "--");
                    return;
                }
                auto* t = SRL::VDP1::Metadata[slot].Texture;
                std::snprintf(out, outSize, "%ux%u", (unsigned)t->Width, (unsigned)t->Height);
            };
            char d32[12]{}, d64[12]{};
            texDim(fam1->lodSlots[2], d32, sizeof(d32));
            texDim(fam1->lodSlots[3], d64, sizeof(d64));
            SRL::Debug::Print(1, 29, "F1 d32:%s d64:%s ", d32, d64);
        }
        else
        {
            SRL::Debug::Print(1, 28, "F1 s:none");
            SRL::Debug::Print(1, 29, "F1 d:none");
        }
    }
    else
    {
        SRL::Debug::Print(1, 28, "F1 s:empty");
        SRL::Debug::Print(1, 29, "F1 d:empty");
    }
}

void TrackSystem::PresentCoordinatorTelemetryAndSoak()
{
    coordinator_.PresentTelemetry();
    soakMonitor_.Update(ReadyFlag(), coordinator_.Telemetry());
    soakMonitor_.Present();
}

void TrackSystem::PresentSh2UsageOverlay()
{
    if constexpr (!kEnableSh2UsageOverlay)
    {
        return;
    }

    const auto& prod = coordinator_.Telemetry().producer;
    const auto& sort = stabilizedDepthStats_;
    auto accumulatePerf = [&](Sh2PerfBucket& bucket)
    {
        bucket.sampleFrames = static_cast<uint16_t>(bucket.sampleFrames + 1u);
        bucket.sumMasterFrameTicks += static_cast<uint32_t>(sh2MasterFrameTicksThisFrame_);
        bucket.sumMasterDrawTicks += static_cast<uint32_t>(sh2MasterDrawTicksThisFrame_);
        bucket.sumProducerTicks += static_cast<uint32_t>(prod.slaveLastJobTicks);
        bucket.sumSortTicks += static_cast<uint32_t>(sh2SlaveSortTicksThisFrame_);
        SaturatingAddU16Value(bucket.sumProducerFallbacks,
                              static_cast<uint32_t>(sh2ProducerListFallbacksThisFrame_));
        SaturatingAddU16Value(bucket.sumProducerListUsed,
                              static_cast<uint32_t>(sh2ProducerListUsedThisFrame_));
        SaturatingAddU16Value(bucket.sumProducerListFallbacks,
                              static_cast<uint32_t>(sh2ProducerListFallbacksThisFrame_));
        if (bucket.sampleFrames < kSh2PerfSampleWindowFrames)
        {
            return;
        }
        const uint32_t denom = static_cast<uint32_t>(bucket.sampleFrames);
        auto toU16 = [](uint32_t value) -> uint16_t
        {
            return static_cast<uint16_t>(std::min<uint32_t>(value, 0xFFFFu));
        };
        bucket.avgMasterFrameTicks = toU16(bucket.sumMasterFrameTicks / denom);
        bucket.avgMasterDrawTicks = toU16(bucket.sumMasterDrawTicks / denom);
        bucket.avgProducerTicks = toU16(bucket.sumProducerTicks / denom);
        bucket.avgSortTicks = toU16(bucket.sumSortTicks / denom);
        bucket.avgProducerFallbacks = toU16(bucket.sumProducerFallbacks / denom);
        bucket.avgProducerListUsed = toU16(bucket.sumProducerListUsed / denom);
        bucket.avgProducerListFallbacks = toU16(bucket.sumProducerListFallbacks / denom);
        SaturatingIncrementU16(bucket.samplesAccum);
        bucket.sampleFrames = 0;
        bucket.sumMasterFrameTicks = 0;
        bucket.sumMasterDrawTicks = 0;
        bucket.sumProducerTicks = 0;
        bucket.sumSortTicks = 0;
        bucket.sumProducerFallbacks = 0;
        bucket.sumProducerListUsed = 0;
        bucket.sumProducerListFallbacks = 0;
    };
    if (TrackSlaveModeRequestedFlag())
    {
        accumulatePerf(runtimeDiagnostics_.sh2PerfDual);
    }
    else
    {
        accumulatePerf(runtimeDiagnostics_.sh2PerfSingle);
    }
    auto avgOrLive = [](const Sh2PerfBucket& bucket,
                        uint32_t sum,
                        uint16_t frozenAvg) -> uint16_t
    {
        if (bucket.sampleFrames > 0u)
        {
            const uint32_t value = sum / static_cast<uint32_t>(bucket.sampleFrames);
            return static_cast<uint16_t>(std::min<uint32_t>(value, 0xFFFFu));
        }
        return frozenAvg;
    };
    const uint16_t s1MasterAvg =
        avgOrLive(runtimeDiagnostics_.sh2PerfSingle, runtimeDiagnostics_.sh2PerfSingle.sumMasterFrameTicks, runtimeDiagnostics_.sh2PerfSingle.avgMasterFrameTicks);
    const uint16_t s1ProducerAvg =
        avgOrLive(runtimeDiagnostics_.sh2PerfSingle, runtimeDiagnostics_.sh2PerfSingle.sumProducerTicks, runtimeDiagnostics_.sh2PerfSingle.avgProducerTicks);
    const uint16_t s1SortAvg =
        avgOrLive(runtimeDiagnostics_.sh2PerfSingle, runtimeDiagnostics_.sh2PerfSingle.sumSortTicks, runtimeDiagnostics_.sh2PerfSingle.avgSortTicks);
    const uint16_t s2MasterAvg =
        avgOrLive(runtimeDiagnostics_.sh2PerfDual, runtimeDiagnostics_.sh2PerfDual.sumMasterFrameTicks, runtimeDiagnostics_.sh2PerfDual.avgMasterFrameTicks);
    const uint16_t s2ProducerAvg =
        avgOrLive(runtimeDiagnostics_.sh2PerfDual, runtimeDiagnostics_.sh2PerfDual.sumProducerTicks, runtimeDiagnostics_.sh2PerfDual.avgProducerTicks);
    const uint16_t s2SortAvg =
        avgOrLive(runtimeDiagnostics_.sh2PerfDual, runtimeDiagnostics_.sh2PerfDual.sumSortTicks, runtimeDiagnostics_.sh2PerfDual.avgSortTicks);
    const bool producerSlaveActive =
        TrackSlaveProducerRequestedFlag() &&
        !prod.slaveDisabledByTimeout &&
        !prod.safeModeActive;
    const bool sortSlaveActive =
        TrackSlaveDepthSortRequestedFlag() &&
        !sort.slaveDisabledByTimeout &&
        !sort.safeModeActive;
    SRL::Debug::Print(0, 17, "S2 c:%s p:%u s:%u l:%u a:%u/%u ",
                      TrackSlaveModeRequestedFlag() ? "DUAL" : "SINGLE",
                      TrackSlaveProducerRequestedFlag() ? 1u : 0u,
                      TrackSlaveDepthSortRequestedFlag() ? 1u : 0u,
                      TrackSlaveBarrierLockstepFlag() ? 1u : 0u,
                      producerSlaveActive ? 1u : 0u,
                      sortSlaveActive ? 1u : 0u);
    SRL::Debug::Print(0, 18, "S2 a1:%u/%u/%u a2:%u/%u/%u ",
                      static_cast<unsigned>(s1MasterAvg),
                      static_cast<unsigned>(s1ProducerAvg),
                      static_cast<unsigned>(s1SortAvg),
                      static_cast<unsigned>(s2MasterAvg),
                      static_cast<unsigned>(s2ProducerAvg),
                      static_cast<unsigned>(s2SortAvg));
    SRL::Debug::Print(0, 19, "SM s:%u d:%u f:%u ",
                      static_cast<unsigned>(sh2MasterStreamTicksThisFrame_),
                      static_cast<unsigned>(sh2MasterDrawTicksThisFrame_),
                      static_cast<unsigned>(sh2MasterFrameTicksThisFrame_));
    SRL::Debug::Print(0, 20, "SM m:%u w:%u p:%u pl:%u l:%u ws:%u ",
                      static_cast<unsigned>(sh2MasterMaintenanceTicksThisFrame_),
                      static_cast<unsigned>(sh2MasterWindowTicksThisFrame_),
                      static_cast<unsigned>(sh2MasterPrefetchTicksThisFrame_),
                      static_cast<unsigned>(sh2MasterPlanTicksThisFrame_),
                      static_cast<unsigned>(sh2MasterLodTicksThisFrame_),
                      static_cast<unsigned>(sh2MasterWorkingSetTicksThisFrame_));
    SRL::Debug::Print(0, 21, "SP t:%u j:%u l:%u to:%u u:%u fb:%u ",
                      static_cast<unsigned>(prod.slaveLastJobTicks),
                      prod.jobInFlight ? 1u : 0u,
                      static_cast<unsigned>(prod.lastLatencyFrames),
                      static_cast<unsigned>(prod.timeoutFallbacks),
                      static_cast<unsigned>(sh2ProducerListUsedThisFrame_),
                      static_cast<unsigned>(sh2ProducerListFallbacksThisFrame_));
    SRL::Debug::Print(0, 22, "SS t:%u pl:%u j:%u l:%u to:%u ",
                      static_cast<unsigned>(sh2SlaveSortTicksThisFrame_),
                      static_cast<unsigned>(sh2SlavePlanTicksThisFrame_),
                      sort.jobInFlight ? 1u : 0u,
                      static_cast<unsigned>(sort.lastLatencyFrames),
                      static_cast<unsigned>(sort.timeoutFallbacks));
}

void TrackSystem::RunEndFrameResourceMaintenance()
{
    if constexpr (kEnableTrackPhaseRamTelemetry)
    {
        const auto hwr = SRL::Memory::HighWorkRam::GetReport();
        const auto lwr = SRL::Memory::LowWorkRam::GetReport();
        frameMemoryTelemetry_.phaseHwrEnd = static_cast<uint32_t>(hwr.FreeSize);
        frameMemoryTelemetry_.phaseLwrEnd = static_cast<uint32_t>(lwr.FreeSize);
    }
    {
        LWR_PROBE_BEGIN();
        workRamMaintenance_.releasedEndFrameSlotsThisFrame = static_cast<uint16_t>(
            std::min<uint32_t>(
                static_cast<uint32_t>(workRamMaintenance_.releasedEndFrameSlotsThisFrame) +
                static_cast<uint32_t>(FlushPendingRetiredTrackTextureSlots()),
                static_cast<uint32_t>(std::numeric_limits<uint16_t>::max())));
        ReacquireWorkRamEmergencyReserve();
        LWR_PROBE_END(g_lwrStageAccum.flushRetiredSlots);
    }
    if constexpr (kEnableLegacyTrackOverlayTelemetry)
    {
        SRL::Debug::Print(1, 19, "RT r:%u s:%u m:%u l:%u",
                          static_cast<unsigned>(runtimeRdrBuildsThisFrame_),
                          static_cast<unsigned>(runtimeSdrBuildsThisFrame_),
                          static_cast<unsigned>(runtimeFaceRemapsThisFrame_),
                          static_cast<unsigned>(runtimeLodSegmentUpdatesThisFrame_));
        SRL::Debug::Print(1, 20, "RS l:%u st:%u h:%u m:%u r:%u k:%u n:%u a:%u",
                          static_cast<unsigned>(runtimeSlidesThisFrame_),
                          static_cast<unsigned>(runtimeSlideStallsThisFrame_),
                          static_cast<unsigned>(runtimePrefetchHitsThisFrame_),
                          static_cast<unsigned>(runtimePrefetchMissesThisFrame_),
                          static_cast<unsigned>(runtimeSafeRenderedThisFrame_),
                          static_cast<unsigned>(runtimeSafeSkippedThisFrame_),
                          static_cast<unsigned>(runtimeSafeNoDrawThisFrame_),
                          static_cast<unsigned>(runtimeSafeReappliedThisFrame_));
        SRL::Debug::Print(1, 21, "RM b:%u s:%u d:%u e:%u k:%u",
                          static_cast<unsigned>(frameMemoryTelemetry_.phaseHwrBeforeStream),
                          static_cast<unsigned>(frameMemoryTelemetry_.phaseHwrAfterStream),
                          static_cast<unsigned>(frameMemoryTelemetry_.phaseHwrAfterDraw),
                          static_cast<unsigned>(frameMemoryTelemetry_.phaseHwrEnd),
                          static_cast<unsigned>(kEnableTrackRuntimeStabilization && kEnableSafeModeSingleRenderBackend
                              ? kSafeModeRenderBackendId
                              : 0u));
        SRL::Debug::Print(1, 22, "RL b:%u s:%u d:%u e:%u",
                          static_cast<unsigned>(frameMemoryTelemetry_.phaseLwrBeforeStream),
                          static_cast<unsigned>(frameMemoryTelemetry_.phaseLwrAfterStream),
                          static_cast<unsigned>(frameMemoryTelemetry_.phaseLwrAfterDraw),
                          static_cast<unsigned>(frameMemoryTelemetry_.phaseLwrEnd));
        SRL::Debug::Print(1, 23, "RP p:%u n:%u e:%u pf:%u",
                          static_cast<unsigned>(workRamMaintenance_.memoryPressureLevelThisFrame),
                          static_cast<unsigned>(workRamMaintenance_.releasedNowSlotsThisFrame),
                          static_cast<unsigned>(workRamMaintenance_.releasedEndFrameSlotsThisFrame),
                          static_cast<unsigned>(workRamMaintenance_.releasedPrefetchNowThisFrame));
        SRL::Debug::Print(1, 24, "RU n:%u r:%u q:%u f:%u s:%u p:%u",
                          static_cast<unsigned>(g_trackUploadsFreshThisFrame),
                          static_cast<unsigned>(g_trackUploadsReusedThisFrame),
                          static_cast<unsigned>(g_trackRetiredQueuedThisFrame),
                          static_cast<unsigned>(g_trackRetiredFlushedThisFrame),
                          static_cast<unsigned>(CountReusableTrackTextureSlots()),
                          static_cast<unsigned>(std::min<size_t>(
                              g_trackPendingRetiredTextureSlots.size(),
                              static_cast<size_t>(std::numeric_limits<uint16_t>::max()))));
        if constexpr (kEnableLegacyTrackOverlaySh2Telemetry)
        {
            const auto& prod = coordinator_.Telemetry().producer;
            SRL::Debug::Print(1, 24, "S2 ms:%u md:%u mf:%u ss:%u ds:%u lf:%u",
                              static_cast<unsigned>(sh2MasterStreamTicksThisFrame_),
                              static_cast<unsigned>(sh2MasterDrawTicksThisFrame_),
                              static_cast<unsigned>(sh2MasterFrameTicksThisFrame_),
                              static_cast<unsigned>(prod.slaveLastJobTicks),
                              static_cast<unsigned>(sh2SlaveSortTicksThisFrame_),
                              static_cast<unsigned>(prod.lastLatencyFrames));
            SRL::Debug::Print(1, 25, "S2 u mw:%u wu:%u pf:%u ld:%u ws:%u",
                              static_cast<unsigned>(sh2MasterMaintenanceTicksThisFrame_),
                              static_cast<unsigned>(sh2MasterWindowTicksThisFrame_),
                              static_cast<unsigned>(sh2MasterPrefetchTicksThisFrame_),
                              static_cast<unsigned>(sh2MasterLodTicksThisFrame_),
                              static_cast<unsigned>(sh2MasterWorkingSetTicksThisFrame_));
        }
        SRL::Debug::Print(1, 26, "RH1 i:%d c:%u t:%u r:%u p:%u",
                          static_cast<int>(slideHwrTrace_.segmentId),
                          static_cast<unsigned>(slideHwrTrace_.check),
                          static_cast<unsigned>(slideHwrTrace_.afterTrim),
                          static_cast<unsigned>(slideHwrTrace_.afterResetPrefetch),
                          static_cast<unsigned>(slideHwrTrace_.afterBuildPrefetch));
        SRL::Debug::Print(1, 27, "RH2 a:%u m:%u f:%u",
                          static_cast<unsigned>(slideHwrTrace_.afterPrepare),
                          static_cast<unsigned>(slideHwrTrace_.afterCommit),
                          static_cast<unsigned>(slideHwrTrace_.flags));
    }
    {
        LWR_PROBE_BEGIN();
        ReleaseUnusedFamilyResourcesEndFrame();
        LWR_PROBE_END(g_lwrStageAccum.releaseEndFrame);
    }
    {
        LWR_PROBE_BEGIN();
        workRamMaintenance_.releasedEndFrameSlotsThisFrame = static_cast<uint16_t>(
            std::min<uint32_t>(
                static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()),
                static_cast<uint32_t>(workRamMaintenance_.releasedEndFrameSlotsThisFrame) +
                    static_cast<uint32_t>(FlushPendingRetiredTrackTextureSlots())));
        if (!kEnableTrackRuntimeStabilization || kEnableStabilizedEndFramePaletteRecycle)
        {
            (void)ReleaseReusableTrackSlotPalettesEndFrame(usedTextureSlotsThisFrame_);
        }
        LWR_PROBE_END(g_lwrStageAccum.flushRetiredSlots);
    }
    EmitFamilyWorkingSetTelemetry();
    {
        LWR_PROBE_BEGIN();
        {
            static uint8_t sBreakdownSampleCooldown = 0u;
            const uint8_t sampleCadence = runtimeDiagnostics_.RuntimeStatsLogsEnabled() ? 3u : 8u;
            if (sBreakdownSampleCooldown == 0u)
            {
                frameMemoryTelemetry_.lowWorkBreakdownEnd = CaptureLowWorkBreakdown();
                sBreakdownSampleCooldown = sampleCadence;
            }
            else
            {
                --sBreakdownSampleCooldown;
            }
        }
        ValidateStabilizedWindowInvariants();
        LWR_PROBE_END(g_lwrStageAccum.validateWindow);
    }
    {
        const auto lwr = SRL::Memory::LowWorkRam::GetReport();
        frameMemoryTelemetry_.phaseLwrEnd = static_cast<uint32_t>(lwr.FreeSize);
        if (frameMemoryTelemetry_.phaseLwrEnd > runtimeDiagnostics_.lowWorkBaselineFree)
        {
            runtimeDiagnostics_.lowWorkBaselineFree = frameMemoryTelemetry_.phaseLwrEnd;
        }
    }
    constexpr bool kEnableLeakTrackingLogs = false;
    if (kEnableLeakTrackingLogs && runtimeDiagnostics_.RuntimeStatsLogsEnabled() && runtimeSlidesThisFrame_ > 0u)
    {
        static uint8_t sLeakHeavySampleCooldown = 0u;
        static uint8_t sLeakOldestProbeCooldown = 0u;
        static bool sLeakBreakdownPrevValid = false;
        static LowWorkCategoryBreakdown sLeakBreakdownPrev{};
        const bool runHeavyLeakSampling = (sLeakHeavySampleCooldown == 0u);
        if (sLeakHeavySampleCooldown == 0u)
        {
            sLeakHeavySampleCooldown =
                kEnableTrackLeakIsolationFixed64Pipeline ? 180u : 60u;
        }
        else
        {
            --sLeakHeavySampleCooldown;
        }

        const auto hwr = SRL::Memory::HighWorkRam::GetReport();
        const auto lwr = SRL::Memory::LowWorkRam::GetReport();
        const uint32_t hwrFree = static_cast<uint32_t>(hwr.FreeSize);
        const uint32_t lwrFree = static_cast<uint32_t>(lwr.FreeSize);
        uint32_t retainedHwr = runtimeDiagnostics_.leakProbePrevRetainedHwr;
        uint32_t retainedLwr = runtimeDiagnostics_.leakProbePrevRetainedLwr;
        LowWorkCategoryBreakdown leakBreakdownNow = frameMemoryTelemetry_.lowWorkBreakdownEnd;
        if (runHeavyLeakSampling)
        {
            retainedHwr = static_cast<uint32_t>(EstimateWorkRamRetainedBytes());
            retainedLwr = static_cast<uint32_t>(EstimateLowWorkRamRetainedBytes());
            leakBreakdownNow = CaptureLowWorkBreakdown();
            frameMemoryTelemetry_.lowWorkBreakdownEnd = leakBreakdownNow;
        }
        const uint16_t reusableSlots = CountReusableTrackTextureSlots();
        const uint16_t pendingRetiredSlots = static_cast<uint16_t>(std::min<size_t>(
            g_trackPendingRetiredTextureSlots.size(),
            static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
        int32_t deltaHwrFree = 0;
        int32_t deltaLwrFree = 0;
        int32_t deltaRetainedHwr = 0;
        int32_t deltaRetainedLwr = 0;
        int32_t deltaLeakRenderers = 0;
        int32_t deltaLeakSlotState = 0;
        int32_t deltaLeakWorkingSet = 0;
        int32_t deltaLeakFamilyCache = 0;
        int32_t deltaLeakTransient = 0;
        int32_t deltaLeakMetadata = 0;
        int32_t deltaLeakTotal = 0;
        if (runtimeDiagnostics_.LeakProbePrevValid())
        {
            deltaHwrFree = static_cast<int32_t>(hwrFree) - static_cast<int32_t>(runtimeDiagnostics_.leakProbePrevHwrFree);
            deltaLwrFree = static_cast<int32_t>(lwrFree) - static_cast<int32_t>(runtimeDiagnostics_.leakProbePrevLwrFree);
            deltaRetainedHwr = static_cast<int32_t>(retainedHwr) - static_cast<int32_t>(runtimeDiagnostics_.leakProbePrevRetainedHwr);
            deltaRetainedLwr = static_cast<int32_t>(retainedLwr) - static_cast<int32_t>(runtimeDiagnostics_.leakProbePrevRetainedLwr);
        }
        if (sLeakBreakdownPrevValid)
        {
            deltaLeakRenderers = static_cast<int32_t>(leakBreakdownNow.renderers) -
                                 static_cast<int32_t>(sLeakBreakdownPrev.renderers);
            deltaLeakSlotState = static_cast<int32_t>(leakBreakdownNow.slotState) -
                                 static_cast<int32_t>(sLeakBreakdownPrev.slotState);
            deltaLeakWorkingSet = static_cast<int32_t>(leakBreakdownNow.workingSet) -
                                  static_cast<int32_t>(sLeakBreakdownPrev.workingSet);
            deltaLeakFamilyCache = static_cast<int32_t>(leakBreakdownNow.familyCache) -
                                   static_cast<int32_t>(sLeakBreakdownPrev.familyCache);
            deltaLeakTransient = static_cast<int32_t>(leakBreakdownNow.transient) -
                                 static_cast<int32_t>(sLeakBreakdownPrev.transient);
            deltaLeakMetadata = static_cast<int32_t>(leakBreakdownNow.metadata) -
                                static_cast<int32_t>(sLeakBreakdownPrev.metadata);
            deltaLeakTotal = static_cast<int32_t>(leakBreakdownNow.total) -
                             static_cast<int32_t>(sLeakBreakdownPrev.total);
        }
        SaturatingAddU16Value(runtimeDiagnostics_.leakProbeSlidesObserved,
                              static_cast<uint32_t>(runtimeSlidesThisFrame_));
        if (runHeavyLeakSampling)
        {
            SRL::Debug::Print(1, 14, "LEAK sl:%u id:%d hs:%u hf:%u lf:%u",
                              static_cast<unsigned>(runtimeDiagnostics_.leakProbeSlidesObserved),
                              static_cast<int>(slideHwrTrace_.segmentId),
                              static_cast<unsigned>(runtimeSlidesThisFrame_),
                              static_cast<unsigned>(hwrFree),
                              static_cast<unsigned>(lwrFree));
            SRL::Debug::Print(1, 15, "LEAK dF h:%d l:%d dR h:%d l:%d",
                              static_cast<int>(deltaHwrFree),
                              static_cast<int>(deltaLwrFree),
                              static_cast<int>(deltaRetainedHwr),
                              static_cast<int>(deltaRetainedLwr));
            SRL::Debug::Print(1, 16, "LEAK C r:%u s:%u w:%u f:%u",
                              static_cast<unsigned>(leakBreakdownNow.renderers),
                              static_cast<unsigned>(leakBreakdownNow.slotState),
                              static_cast<unsigned>(leakBreakdownNow.workingSet),
                              static_cast<unsigned>(leakBreakdownNow.familyCache));
            SRL::Debug::Print(1, 17, "LEAK C2 t:%u m:%u q:%u/%u",
                              static_cast<unsigned>(leakBreakdownNow.transient),
                              static_cast<unsigned>(leakBreakdownNow.metadata),
                              static_cast<unsigned>(reusableSlots),
                              static_cast<unsigned>(pendingRetiredSlots));
            SRL::Debug::Print(1, 18, "LEAK dC r:%d s:%d w:%d f:%d",
                              static_cast<int>(deltaLeakRenderers),
                              static_cast<int>(deltaLeakSlotState),
                              static_cast<int>(deltaLeakWorkingSet),
                              static_cast<int>(deltaLeakFamilyCache));
            SRL::Debug::Print(1, 19, "LEAK d2 t:%d m:%d o:%d",
                              static_cast<int>(deltaLeakTransient),
                              static_cast<int>(deltaLeakMetadata),
                              static_cast<int>(deltaLeakTotal));
            if (sLeakOldestProbeCooldown == 0u)
            {
                SRL::Memory::LiveBlockInfo txOldest[1]{};
                SRL::Memory::LiveBlockInfo wkOldest[1]{};
                const size_t txCount = SRL::Memory::LowWorkRam::GetOldestLiveBlocksByTag(
                    SRL::Memory::DebugTag::TrackTexture,
                    txOldest,
                    1u);
                const size_t prepCount = SRL::Memory::LowWorkRam::GetOldestLiveBlocksByTag(
                    SRL::Memory::DebugTag::TrackPrepare,
                    wkOldest,
                    1u);
                SRL::Memory::LiveBlockInfo backendOldest[1]{};
                const size_t backendCount = SRL::Memory::LowWorkRam::GetOldestLiveBlocksByTag(
                    SRL::Memory::DebugTag::TrackBackend,
                    backendOldest,
                    1u);
                if (backendCount > 0u && (prepCount == 0u || backendOldest[0].Age > wkOldest[0].Age))
                {
                    wkOldest[0] = backendOldest[0];
                }
                const unsigned long txOff =
                    (txCount > 0u)
                        ? (static_cast<unsigned long>(txOldest[0].Address) & 0x000FFFFFul)
                        : 0ul;
                const unsigned long wkOff =
                    (prepCount > 0u || backendCount > 0u)
                        ? (static_cast<unsigned long>(wkOldest[0].Address) & 0x000FFFFFul)
                        : 0ul;
                SRL::Debug::Print(1, 20, "LEAK P tx:%05lx %u/%u wk:%05lx %u/%u",
                                  txOff,
                                  (txCount > 0u) ? static_cast<unsigned>(txOldest[0].Size) : 0u,
                                  (txCount > 0u) ? static_cast<unsigned>(txOldest[0].Age) : 0u,
                                  wkOff,
                                  (prepCount > 0u || backendCount > 0u)
                                      ? static_cast<unsigned>(wkOldest[0].Size)
                                      : 0u,
                                  (prepCount > 0u || backendCount > 0u)
                                      ? static_cast<unsigned>(wkOldest[0].Age)
                                      : 0u);
                sLeakOldestProbeCooldown = 15u;
            }
            else
            {
                --sLeakOldestProbeCooldown;
            }
        }
        runtimeDiagnostics_.SetLeakProbePrevValid(true);
        runtimeDiagnostics_.leakProbePrevHwrFree = hwrFree;
        runtimeDiagnostics_.leakProbePrevLwrFree = lwrFree;
        runtimeDiagnostics_.leakProbePrevRetainedHwr = retainedHwr;
        runtimeDiagnostics_.leakProbePrevRetainedLwr = retainedLwr;
        sLeakBreakdownPrev = leakBreakdownNow;
        sLeakBreakdownPrevValid = true;
    }
}

void TrackSystem::UpdateAdaptiveBudgetAfterFrame()
{
    const bool enableAdaptiveBudget = false;
    if (!enableAdaptiveBudget)
    {
        return;
    }

    const FrameBudget nextBudget =
        budgetController_.Update(coordinator_.Budget(), coordinator_.Telemetry());
    coordinator_.SetBudget(nextBudget);
}

void TrackSystem::EndFrame()
{
    // Keep slot liveness tied to what the renderer actually references in the
    // current frame. This avoids stale working-set references pinning old slots
    // and causing long-run TrackTexture growth.
    RebuildUsedTextureSlotFlagsFromCurrentFaces();

    PresentCoordinatorTelemetryAndSoak();
    PresentSh2UsageOverlay();
    RunEndFrameResourceMaintenance();
    PresentVdp1FpsTelemetry();
    PresentPerFrameDebugOverlay();
    UpdateAdaptiveBudgetAfterFrame();
}

bool TrackSystem::FindNearestSegment(const Vector3D& worldPosition,
                                     const Vector3D& trackOffset,
                                     int32_t& outSegmentId,
                                     Vector3D& outSegmentCenter) const
{
    if (!segmentCenterCatalog_.empty())
    {
        bool hasCandidate = false;
        SRL::Math::Types::Fxp bestScore = SRL::Math::Types::Fxp::BuildRaw(0x7FFFFFFF);
        for (size_t i = 0; i < segmentCenterCatalog_.size(); ++i)
        {
            const Vector3D center = segmentCenterCatalog_[i] + trackOffset;
            const SRL::Math::Types::Fxp dx = (center.X - worldPosition.X).Abs();
            const SRL::Math::Types::Fxp dz = (center.Z - worldPosition.Z).Abs();
            const SRL::Math::Types::Fxp score = dx + dz;
            if (!hasCandidate || score < bestScore)
            {
                hasCandidate = true;
                bestScore = score;
                outSegmentId = static_cast<int32_t>(i + 1);
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

bool TrackSystem::FindSurfaceYByFamilyId(const Vector3D& worldPosition,
                                         const Vector3D& trackOffset,
                                         const uint16_t familyId,
                                         SRL::Math::Types::Fxp& outSurfaceY,
                                         int32_t* outSegmentId,
                                         int32_t seedSegmentId,
                                         bool allowFallback) const
{
    if (familyId == 0u)
    {
        outSurfaceY = worldPosition.Y;
        if (outSegmentId) *outSegmentId = -1;
        return false;
    }
    return FindSurfaceYByFamilySet(worldPosition,
                                   trackOffset,
                                   &familyId,
                                   1u,
                                   outSurfaceY,
                                   outSegmentId,
                                   seedSegmentId,
                                   allowFallback);
}

bool TrackSystem::FindSurfaceYByFamilySet(const Vector3D& worldPosition,
                                          const Vector3D& trackOffset,
                                          const uint16_t* familyIds,
                                          size_t familyCount,
                                          SRL::Math::Types::Fxp& outSurfaceY,
                                          int32_t* outSegmentId,
                                          int32_t seedSegmentId,
                                          bool allowFallback,
                                          uint16_t* outFamilyId,
                                          uint8_t* outSurfaceType,
                                          int16_t* outFaceIndex) const
{
    SaturatingIncrementU16(surfaceQueryCallsThisFrame_);
    const bool useLocalNeighbor =
        Game::PhysicsFeatureFlags::kEnableLocalFaceNeighbor;
    outSurfaceY = worldPosition.Y;
    if (outSegmentId) *outSegmentId = -1;
    if (outFamilyId) *outFamilyId = 0u;
    if (outSurfaceType) *outSurfaceType = 0u;
    if (outFaceIndex) *outFaceIndex = -1;
    const bool acceptAnyFamily = (!familyIds || familyCount == 0u);
    if (segmentRenderers_.empty()) return false;

    bool hasAnyFamily = acceptAnyFamily;
    for (size_t i = 0; i < familyCount && !acceptAnyFamily; ++i)
    {
        const uint16_t familyId = familyIds[i];
        if (familyId == 0u) continue;
        hasAnyFamily = true;
    }
    if (!hasAnyFamily) return false;

    static constexpr size_t kFastFamilyMaskLimit = 512u;
    std::array<uint8_t, kFastFamilyMaskLimit> familyMask{};
    bool hasLargeFamilyId = false;
    for (size_t i = 0; i < familyCount && !acceptAnyFamily; ++i)
    {
        const uint16_t familyId = familyIds[i];
        if (familyId == 0u) continue;
        if (familyId < kFastFamilyMaskLimit)
        {
            familyMask[familyId] = 1u;
        }
        else
        {
            hasLargeFamilyId = true;
        }
    }

    auto familyAllowed = [&](uint16_t familyId) -> bool
    {
        if (familyId == 0u) return false;
        if (acceptAnyFamily) return true;
        if (familyId < kFastFamilyMaskLimit) return familyMask[familyId] != 0u;
        if (!hasLargeFamilyId) return false;
        for (size_t i = 0; i < familyCount; ++i)
        {
            if (familyIds[i] == familyId) return true;
        }
        return false;
    };

    uint8_t requestedSegmentSurfaceFlags = 0u;
    bool requestedSurfaceTypesKnown = true;
    bool wantsAsphaltLike = false;
    bool wantsOffroadLike = false;
    if (!acceptAnyFamily &&
        Game::PhysicsFeatureFlags::kEnableScmapRuntime &&
        SurfaceFamilyMapReady() &&
        SegmentCollisionMapReady())
    {
        for (size_t i = 0; i < familyCount; ++i)
        {
            const uint16_t familyId = familyIds[i];
            if (familyId == 0u) continue;
            if (familyId >= surfaceTypeByFamilyId_.size())
            {
                requestedSurfaceTypesKnown = false;
                break;
            }
            const uint8_t surfaceTypeId = surfaceTypeByFamilyId_[familyId];
            switch (surfaceTypeId)
            {
            case 1u:
                wantsAsphaltLike = true;
                break;
            case 2u:
            case 3u:
                wantsOffroadLike = true;
                break;
            default:
                requestedSurfaceTypesKnown = false;
                break;
            }
            if (!requestedSurfaceTypesKnown) break;
        }

        if (requestedSurfaceTypesKnown && (wantsAsphaltLike || wantsOffroadLike))
        {
            if (wantsAsphaltLike && !wantsOffroadLike)
            {
                requestedSegmentSurfaceFlags = 0x02u; // asphalt-like
            }
            else if (!wantsAsphaltLike && wantsOffroadLike)
            {
                requestedSegmentSurfaceFlags = 0x04u; // offroad-like
            }
            else
            {
                requestedSegmentSurfaceFlags = 0x01u; // generic driveable
            }
        }
    }

    const int64_t pxRaw = static_cast<int64_t>(worldPosition.X.RawValue());
    const int64_t pyRaw = static_cast<int64_t>(worldPosition.Y.RawValue());
    const int64_t pzRaw = static_cast<int64_t>(worldPosition.Z.RawValue());

    auto abs64 = [](int64_t v) -> int64_t { return (v < 0) ? -v : v; };

    auto edgeCrossXZ = [](const Vector3D& a,
                          const Vector3D& b,
                          const int64_t px,
                          const int64_t pz) -> int64_t
    {
        const int64_t ax = static_cast<int64_t>(a.X.RawValue());
        const int64_t az = static_cast<int64_t>(a.Z.RawValue());
        const int64_t bx = static_cast<int64_t>(b.X.RawValue());
        const int64_t bz = static_cast<int64_t>(b.Z.RawValue());
        return ((px - ax) * (bz - az)) - ((pz - az) * (bx - ax));
    };

    auto isPointInTriangleXZ = [&](const Vector3D& a,
                                   const Vector3D& b,
                                   const Vector3D& c) -> bool
    {
        const int64_t c1 = edgeCrossXZ(a, b, pxRaw, pzRaw);
        const int64_t c2 = edgeCrossXZ(b, c, pxRaw, pzRaw);
        const int64_t c3 = edgeCrossXZ(c, a, pxRaw, pzRaw);
        const bool hasNeg = (c1 < 0) || (c2 < 0) || (c3 < 0);
        const bool hasPos = (c1 > 0) || (c2 > 0) || (c3 > 0);
        return !(hasNeg && hasPos);
    };

    auto solvePlaneYRaw = [&](const Vector3D& a,
                              const Vector3D& b,
                              const Vector3D& c,
                              int64_t& outYRaw) -> bool
    {
        const int64_t ax = static_cast<int64_t>(a.X.RawValue());
        const int64_t ay = static_cast<int64_t>(a.Y.RawValue());
        const int64_t az = static_cast<int64_t>(a.Z.RawValue());
        const int64_t bx = static_cast<int64_t>(b.X.RawValue());
        const int64_t by = static_cast<int64_t>(b.Y.RawValue());
        const int64_t bz = static_cast<int64_t>(b.Z.RawValue());
        const int64_t cx = static_cast<int64_t>(c.X.RawValue());
        const int64_t cy = static_cast<int64_t>(c.Y.RawValue());
        const int64_t cz = static_cast<int64_t>(c.Z.RawValue());

        const int64_t ux = bx - ax;
        const int64_t uy = by - ay;
        const int64_t uz = bz - az;
        const int64_t vx = cx - ax;
        const int64_t vy = cy - ay;
        const int64_t vz = cz - az;

        const int64_t nx = (uy * vz) - (uz * vy);
        const int64_t ny = (uz * vx) - (ux * vz);
        const int64_t nz = (ux * vy) - (uy * vx);
        if (ny == 0) return false;

        const int64_t rhs = (nx * (pxRaw - ax)) + (nz * (pzRaw - az));
        outYRaw = ay - (rhs / ny);
        return true;
    };

    bool foundInside = false;
    uint8_t bestInsideClass = 0xFFu;
    int64_t bestInsideGapY = std::numeric_limits<int64_t>::max();
    int64_t bestInsideYRaw = pyRaw;
    int32_t bestInsideSegmentId = -1;
    int32_t bestInsideSeedDistance = std::numeric_limits<int32_t>::max();
    uint16_t bestInsideFamilyId = 0u;
    int16_t bestInsideFaceIndex = -1;
    
    bool foundFallback = false;
    uint8_t bestFallbackClass = 0xFFu;
    int64_t bestFallbackPlanar = std::numeric_limits<int64_t>::max();
    int64_t bestFallbackGapY = std::numeric_limits<int64_t>::max();
    int64_t bestFallbackYRaw = pyRaw;
    int32_t bestFallbackSegmentId = -1;
    int32_t bestFallbackSeedDistance = std::numeric_limits<int32_t>::max();
    uint16_t bestFallbackFamilyId = 0u;
    int16_t bestFallbackFaceIndex = -1;
    bool earlyAcceptInside = false;

    auto wrappedSeedDistance = [&](int32_t segmentId) -> int32_t
    {
        if (seedSegmentId <= 0 || segmentId <= 0 || totalSegmentCount_ <= 0) return 0;
        int32_t delta = segmentId - seedSegmentId;
        if (delta < 0) delta = -delta;
        const int32_t total = static_cast<int32_t>(totalSegmentCount_);
        const int32_t wrapped = total - delta;
        return std::min(delta, wrapped);
    };

    auto updateInsideCandidate = [&](int64_t yRaw, int32_t segmentId, uint16_t familyId, int16_t faceIndex)
    {
        static constexpr int64_t kSupportToleranceRaw = (1 << 14); // ~0.25 in 16.16
        static constexpr int64_t kEarlyAcceptGapRaw = (1 << 13);   // ~0.125 in 16.16
        // Current world convention uses negative Y as up, therefore larger Y
        // means lower altitude. A supporting road candidate should be at or
        // below the probe height (>= py - tolerance).
        const bool preferAsSupport = (yRaw >= pyRaw - kSupportToleranceRaw);
        const uint8_t candidateClass = preferAsSupport ? 0u : 1u;
        const int64_t candidateGapY = preferAsSupport
            ? abs64(pyRaw - yRaw)
            : abs64(yRaw - pyRaw);
        const int32_t seedDistance = wrappedSeedDistance(segmentId);
        if (!foundInside ||
            candidateClass < bestInsideClass ||
            (candidateClass == bestInsideClass && candidateGapY < bestInsideGapY) ||
            (candidateClass == bestInsideClass && candidateGapY == bestInsideGapY &&
             seedDistance < bestInsideSeedDistance))
        {
            foundInside = true;
            bestInsideClass = candidateClass;
            bestInsideGapY = candidateGapY;
            bestInsideSeedDistance = seedDistance;
            bestInsideYRaw = yRaw;
            bestInsideSegmentId = segmentId;
            bestInsideFamilyId = familyId;
            bestInsideFaceIndex = faceIndex;
            if (bestInsideClass == 0u && bestInsideGapY <= kEarlyAcceptGapRaw)
            {
                earlyAcceptInside = true;
            }
        }
    };

    auto updateFallbackCandidate = [&](int64_t yRaw,
                                       int32_t segmentId,
                                       int64_t planarScore,
                                       uint16_t familyId,
                                       int16_t faceIndex)
    {
        static constexpr int64_t kSupportToleranceRaw = (1 << 14); // ~0.25 in 16.16
        const bool preferAsSupport = (yRaw >= pyRaw - kSupportToleranceRaw);
        const uint8_t candidateClass = preferAsSupport ? 0u : 1u;
        const int64_t candidateGapY = preferAsSupport
            ? abs64(pyRaw - yRaw)
            : abs64(yRaw - pyRaw);
        const int32_t seedDistance = wrappedSeedDistance(segmentId);
        if (!foundFallback ||
            candidateClass < bestFallbackClass ||
            (candidateClass == bestFallbackClass &&
             (planarScore < bestFallbackPlanar ||
              (planarScore == bestFallbackPlanar &&
               (candidateGapY < bestFallbackGapY ||
                (candidateGapY == bestFallbackGapY &&
                 seedDistance < bestFallbackSeedDistance))))))
        {
            foundFallback = true;
            bestFallbackClass = candidateClass;
            bestFallbackSeedDistance = seedDistance;
            bestFallbackPlanar = planarScore;
            bestFallbackGapY = candidateGapY;
            bestFallbackYRaw = yRaw;
            bestFallbackSegmentId = segmentId;
            bestFallbackFamilyId = familyId;
            bestFallbackFaceIndex = faceIndex;
        }
    };

    auto updateInsideCache = [&](int32_t segmentId, int16_t faceIndex, uint16_t familyId)
    {
        if (segmentId <= 0 || faceIndex < 0 || familyId == 0u)
        {
            return;
        }
        surfaceQueryLastInsideSegmentId_ = static_cast<int16_t>(segmentId);
        surfaceQueryLastInsideFaceIndex_ = faceIndex;
        surfaceQueryLastInsideFamilyId_ = familyId;
        surfaceQueryLastInsideType_ =
            (familyId < surfaceTypeByFamilyId_.size()) ? surfaceTypeByFamilyId_[familyId] : 0u;
        SetSurfaceQueryLastInsideValid(true);
    };

    auto evaluateFaceCandidate = [&](const SegmentRenderEntry& segment,
                                     size_t fi,
                                     const Vector3D* preVerts,
                                     size_t preVertCount,
                                     const SRL::Types::Polygon* preFaces,
                                     size_t preFaceCount,
                                     int64_t& outYRaw,
                                     int64_t& outPlanarScore,
                                     uint16_t& outFamilyId,
                                     bool& outInside) -> bool
    {
        outInside = false;
        outFamilyId = 0u;
        outPlanarScore = 0;
        outYRaw = 0;

        if (!segment.renderer) return false;
        if (segment.lodState.faceFamilyIds.empty()) return false;

        const Vector3D* verts = preVerts;
        const SRL::Types::Polygon* faces = preFaces;
        size_t vertCount = preVertCount;
        size_t faceCount = preFaceCount;
        if (!verts || !faces || vertCount == 0u || faceCount == 0u)
        {
            if (!segment.renderer->GetComponentGeometry(verts, vertCount, faces, faceCount)) return false;
        }
        if (!verts || !faces || vertCount == 0u || faceCount == 0u) return false;

        const size_t familyCount = segment.lodState.faceFamilyIds.size();
        const size_t scanFaceCount = (familyCount > 0u)
            ? std::min(faceCount, familyCount)
            : faceCount;
        if (fi >= scanFaceCount) return false;

        const uint16_t faceFamilyId = segment.lodState.faceFamilyIds[fi];
        if (!familyAllowed(faceFamilyId)) return false;

        const SRL::Types::Polygon& face = faces[fi];
        const uint16_t i0 = face.Vertices[0];
        const uint16_t i1 = face.Vertices[1];
        const uint16_t i2 = face.Vertices[2];
        const uint16_t i3 = face.Vertices[3];
        if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount || i3 >= vertCount) return false;

        const Vector3D a = verts[i0] + trackOffset;
        const Vector3D b = verts[i1] + trackOffset;
        const Vector3D c = verts[i2] + trackOffset;
        const Vector3D d = verts[i3] + trackOffset;

        // Keep only floor-like polygons for road-height sampling.
        // 0.50 (32768 in 16.16) matches the pipeline ground classification.
        if (abs64(static_cast<int64_t>(face.Normal.Y.RawValue())) < (1 << 15)) return false;

        const bool inTri0 = isPointInTriangleXZ(a, b, c);
        const bool inTri1 = isPointInTriangleXZ(a, c, d);
        int64_t yRaw = 0;
        bool yValid = false;
        bool inside = false;
        if (inTri0)
        {
            yValid = solvePlaneYRaw(a, b, c, yRaw);
            inside = yValid;
        }
        else if (inTri1)
        {
            yValid = solvePlaneYRaw(a, c, d, yRaw);
            inside = yValid;
        }
        else
        {
            // Fallback: project onto the first triangle plane so we still
            // have a stable candidate when PATH point drifts outside face.
            yValid = solvePlaneYRaw(a, b, c, yRaw) || solvePlaneYRaw(a, c, d, yRaw);
        }
        if (!yValid) return false;

        int64_t planarScore = 0;
        if (!inside)
        {
            const int64_t cxRaw =
                (static_cast<int64_t>(a.X.RawValue()) +
                 static_cast<int64_t>(b.X.RawValue()) +
                 static_cast<int64_t>(c.X.RawValue()) +
                 static_cast<int64_t>(d.X.RawValue())) / 4;
            const int64_t czRaw =
                (static_cast<int64_t>(a.Z.RawValue()) +
                 static_cast<int64_t>(b.Z.RawValue()) +
                 static_cast<int64_t>(c.Z.RawValue()) +
                 static_cast<int64_t>(d.Z.RawValue())) / 4;
            planarScore = abs64(cxRaw - pxRaw) + abs64(czRaw - pzRaw);
        }

        outInside = inside;
        outFamilyId = faceFamilyId;
        outPlanarScore = planarScore;
        outYRaw = yRaw;
        return true;
    };

    auto scanSegment = [&](const SegmentRenderEntry& segment)
    {
        if (earlyAcceptInside) return;
        if (requestedSegmentSurfaceFlags != 0u &&
            segment.id > 0 &&
            static_cast<size_t>(segment.id) < segmentSurfaceFlagsById_.size())
        {
            const uint8_t segmentFlags = segmentSurfaceFlagsById_[segment.id];
            if ((segmentFlags & requestedSegmentSurfaceFlags) == 0u)
            {
                SaturatingIncrementU16(surfaceQueryScmapSkipsThisFrame_);
                return;
            }
        }

        if (!segment.renderer) return;

        const Vector3D* verts = nullptr;
        const SRL::Types::Polygon* faces = nullptr;
        size_t vertCount = 0u;
        size_t faceCount = 0u;
        if (!segment.renderer->GetComponentGeometry(verts, vertCount, faces, faceCount)) return;
        if (!verts || !faces || vertCount == 0u || faceCount == 0u) return;

        const size_t scanFaceCount = std::min(faceCount, segment.lodState.faceFamilyIds.size());
        SaturatingIncrementU16(surfaceQuerySegmentsScannedThisFrame_);
        SaturatingAddU16(surfaceQueryFacesScannedThisFrame_, scanFaceCount);
        for (size_t fi = 0; fi < scanFaceCount; ++fi)
        {
            if (earlyAcceptInside) break;
            int64_t yRaw = 0;
            int64_t planarScore = 0;
            uint16_t faceFamilyId = 0u;
            bool inside = false;
            if (!evaluateFaceCandidate(segment,
                                       fi,
                                       verts,
                                       vertCount,
                                       faces,
                                       faceCount,
                                       yRaw,
                                       planarScore,
                                       faceFamilyId,
                                       inside))
            {
                continue;
            }

            if (inside)
            {
                updateInsideCandidate(yRaw, segment.id, faceFamilyId, static_cast<int16_t>(fi));
                continue;
            }
            updateFallbackCandidate(yRaw,
                                    segment.id,
                                    planarScore,
                                    faceFamilyId,
                                    static_cast<int16_t>(fi));
        }
    };

    if (SurfaceQueryLastInsideValid() &&
        surfaceQueryLastInsideSegmentId_ > 0 &&
        surfaceQueryLastInsideFaceIndex_ >= 0)
    {
        const SegmentRenderEntry* cacheEntry = FindWindowEntryByIdFast(surfaceQueryLastInsideSegmentId_);
        if (cacheEntry)
        {
            bool canEvaluate = true;
            if (requestedSegmentSurfaceFlags != 0u &&
                cacheEntry->id > 0 &&
                static_cast<size_t>(cacheEntry->id) < segmentSurfaceFlagsById_.size())
            {
                const uint8_t segmentFlags = segmentSurfaceFlagsById_[cacheEntry->id];
                if ((segmentFlags & requestedSegmentSurfaceFlags) == 0u)
                {
                    canEvaluate = false;
                    SaturatingIncrementU16(surfaceQueryScmapSkipsThisFrame_);
                }
            }
            if (canEvaluate)
            {
                SaturatingIncrementU16(surfaceQuerySegmentsScannedThisFrame_);
                SaturatingIncrementU16(surfaceQueryFacesScannedThisFrame_);
                int64_t yRaw = 0;
                int64_t planarScore = 0;
                uint16_t faceFamilyId = 0u;
                bool inside = false;
                if (evaluateFaceCandidate(*cacheEntry,
                                          static_cast<size_t>(surfaceQueryLastInsideFaceIndex_),
                                          nullptr,
                                          0u,
                                          nullptr,
                                          0u,
                                          yRaw,
                                          planarScore,
                                          faceFamilyId,
                                          inside) &&
                    inside)
                {
                    SaturatingIncrementU16(surfaceQueryCacheHitsThisFrame_);
                    outSurfaceY = SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(yRaw));
                    if (outSegmentId) *outSegmentId = cacheEntry->id;
                    if (outFamilyId) *outFamilyId = faceFamilyId;
                    if (outFaceIndex) *outFaceIndex = surfaceQueryLastInsideFaceIndex_;
                    if (outSurfaceType)
                    {
                        *outSurfaceType =
                            (faceFamilyId < surfaceTypeByFamilyId_.size())
                                ? surfaceTypeByFamilyId_[faceFamilyId]
                                : 0u;
                    }
                    updateInsideCache(cacheEntry->id,
                                      surfaceQueryLastInsideFaceIndex_,
                                      faceFamilyId);
                    return true;
                }
                SaturatingIncrementU16(surfaceQueryCacheMissesThisFrame_);
            }
        }
    }

    if (seedSegmentId > 0 && totalSegmentCount_ > 0)
    {
        std::array<int32_t, 12> localIds{};
        size_t localCount = 0u;
        // Prioritize seed and closest neighbors first to maximize early accept.
        static constexpr std::array<int32_t, 8> kNeighborDeltaWide = { 0, 1, -1, 2, -2, 3, 4, 5 };
        static constexpr std::array<int32_t, 5> kNeighborDeltaNarrow = { 0, 1, -1, 2, 3 };
        const size_t deltaCount = useLocalNeighbor ? kNeighborDeltaWide.size() : kNeighborDeltaNarrow.size();
        for (size_t di = 0; di < deltaCount; ++di)
        {
            if (earlyAcceptInside) break;
            const int32_t delta = useLocalNeighbor ? kNeighborDeltaWide[di] : kNeighborDeltaNarrow[di];
            const int32_t candidateId =
                WrapSegmentIdToRange(seedSegmentId + delta, static_cast<int32_t>(totalSegmentCount_));
            if (candidateId <= 0) continue;

            bool duplicate = false;
            for (size_t i = 0; i < localCount; ++i)
            {
                if (localIds[i] == candidateId)
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            if (localCount < localIds.size()) localIds[localCount++] = candidateId;

            const SegmentRenderEntry* localEntry = FindWindowEntryByIdFast(candidateId);
            if (!localEntry) continue;
            scanSegment(*localEntry);
        }
    }

    // Run the expensive global pass only when local probing found absolutely
    // nothing. This keeps per-frame probing deterministic and avoids spikes in
    // descents where local support exists but classifies as fallback.
    bool shouldRunGlobalPass = (!foundInside && !foundFallback);
    if (shouldRunGlobalPass &&
        useLocalNeighbor &&
        !allowFallback &&
        seedSegmentId > 0 &&
        totalSegmentCount_ > 0)
    {
        SaturatingIncrementU16(surfaceQueryLocalOnlyMissesThisFrame_);
        shouldRunGlobalPass = false;
    }
    if (shouldRunGlobalPass)
    {
        SaturatingIncrementU16(surfaceQueryGlobalPassesThisFrame_);
        for (const auto& segment : segmentRenderers_)
        {
            if (earlyAcceptInside) break;
            scanSegment(segment);
        }
    }

    if (foundInside)
    {
        outSurfaceY = SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(bestInsideYRaw));
        if (outSegmentId) *outSegmentId = bestInsideSegmentId;
        if (outFamilyId) *outFamilyId = bestInsideFamilyId;
        if (outFaceIndex) *outFaceIndex = bestInsideFaceIndex;
        if (outSurfaceType &&
            bestInsideFamilyId < surfaceTypeByFamilyId_.size())
        {
            *outSurfaceType = surfaceTypeByFamilyId_[bestInsideFamilyId];
        }
        updateInsideCache(bestInsideSegmentId, bestInsideFaceIndex, bestInsideFamilyId);
        return true;
    }

    if (allowFallback && foundFallback)
    {
        SaturatingIncrementU16(surfaceQueryFallbackHitsThisFrame_);
        outSurfaceY = SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(bestFallbackYRaw));
        if (outSegmentId) *outSegmentId = bestFallbackSegmentId;
        if (outFamilyId) *outFamilyId = bestFallbackFamilyId;
        if (outFaceIndex) *outFaceIndex = bestFallbackFaceIndex;
        if (outSurfaceType &&
            bestFallbackFamilyId < surfaceTypeByFamilyId_.size())
        {
            *outSurfaceType = surfaceTypeByFamilyId_[bestFallbackFamilyId];
        }
        return true;
    }

    return false;
}

bool TrackSystem::FindSurfaceContact(const Vector3D& worldPosition,
                                     const Vector3D& trackOffset,
                                     Game::SurfaceContact& outContact,
                                     int32_t seedSegmentId,
                                     bool allowFallback) const
{
    outContact = Game::SurfaceContact{};
    SRL::Math::Types::Fxp surfaceY = worldPosition.Y;
    int32_t segmentId = -1;
    uint16_t familyId = 0u;
    uint8_t surfaceType = 0u;
    int16_t faceIndex = -1;

    const bool found = FindSurfaceYByFamilySet(worldPosition,
                                               trackOffset,
                                               nullptr,
                                               0u,
                                               surfaceY,
                                               &segmentId,
                                               seedSegmentId,
                                               allowFallback,
                                               &familyId,
                                               &surfaceType,
                                               &faceIndex);
    if (!found)
    {
        return false;
    }

    outContact.valid = true;
    outContact.segmentId = segmentId;
    outContact.faceIndex = faceIndex;
    outContact.familyId = familyId;
    outContact.surfaceType = surfaceType;
    outContact.surfaceY = surfaceY;
    outContact.normal = Vector3D(0.0, -1.0, 0.0);
    return true;
}

bool TrackSystem::FindSegmentCenterById(const int32_t segmentId,
                                        const Vector3D& trackOffset,
                                        Vector3D& outSegmentCenter) const
{
    if (segmentId > 0 &&
        static_cast<size_t>(segmentId) <= segmentCenterCatalog_.size())
    {
        outSegmentCenter = segmentCenterCatalog_[static_cast<size_t>(segmentId - 1)] + trackOffset;
        return true;
    }

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

bool TrackSystem::FindPlanarWallPush(const Vector3D& worldPosition,
                                     const Vector3D& trackOffset,
                                     const Vector3D& forwardDirection,
                                     SRL::Math::Types::Fxp collisionRadius,
                                     Vector3D& outPush,
                                     int32_t* outSegmentId,
                                     int32_t seedSegmentId,
                                     bool allowGlobalFallback) const
{
    if (!Game::PhysicsFeatureFlags::kEnableWallCollisionRuntime)
    {
        outPush = Vector3D(SRL::Math::Types::Fxp::BuildRaw(0),
                           SRL::Math::Types::Fxp::BuildRaw(0),
                           SRL::Math::Types::Fxp::BuildRaw(0));
        if (outSegmentId) *outSegmentId = -1;
        return false;
    }

    (void)forwardDirection;
    SaturatingIncrementU16(wallQueryCallsThisFrame_);
    outPush = Vector3D(SRL::Math::Types::Fxp::BuildRaw(0),
                       SRL::Math::Types::Fxp::BuildRaw(0),
                       SRL::Math::Types::Fxp::BuildRaw(0));
    if (outSegmentId) *outSegmentId = -1;
    if (segmentRenderers_.empty()) return false;

    const int64_t radiusRaw = static_cast<int64_t>(collisionRadius.RawValue());
    if (radiusRaw <= 0) return false;
    const int64_t radiusSq = radiusRaw * radiusRaw;
    const int64_t pxRaw = static_cast<int64_t>(worldPosition.X.RawValue());
    const int64_t pyRaw = static_cast<int64_t>(worldPosition.Y.RawValue());
    const int64_t pzRaw = static_cast<int64_t>(worldPosition.Z.RawValue());
    const bool prevValid = WallQueryPrevWorldPositionValid();
    const int64_t prevPxRaw = prevValid ? static_cast<int64_t>(wallQueryPrevWorldPosition_.X.RawValue()) : pxRaw;
    const int64_t prevPyRaw = prevValid ? static_cast<int64_t>(wallQueryPrevWorldPosition_.Y.RawValue()) : pyRaw;
    const int64_t prevPzRaw = prevValid ? static_cast<int64_t>(wallQueryPrevWorldPosition_.Z.RawValue()) : pzRaw;
    const int64_t yMarginRaw = static_cast<int64_t>(12 << 16);

    struct WallQueryPrevPosCommit
    {
        const TrackSystem* self = nullptr;
        Vector3D pos{};
        ~WallQueryPrevPosCommit()
        {
            if (!self) return;
            self->wallQueryPrevWorldPosition_ = pos;
            self->SetWallQueryPrevWorldPositionValid(true);
        }
    } prevPosCommit{ this, worldPosition };

    auto abs64 = [](int64_t v) -> int64_t { return (v < 0) ? -v : v; };
    auto clamp64 = [](int64_t v, int64_t lo, int64_t hi) -> int64_t
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    };

    int64_t bestPenRaw = 0;
    int64_t bestPushXRaw = 0;
    int64_t bestPushZRaw = 0;
    int32_t bestSegmentId = -1;
    static constexpr uint8_t kSegmentFlagHasWallLikeFaces = 0x08u;

    auto segmentHasWallCandidates = [&](int32_t segmentId) -> bool
    {
        if (segmentId <= 0) return true;
        if (!Game::PhysicsFeatureFlags::kEnableScmapRuntime) return true;
        if (!SegmentCollisionMapReady()) return true;
        if (segmentSurfaceFlagsById_.empty()) return true;
        const size_t sid = static_cast<size_t>(segmentId);
        if (sid >= segmentSurfaceFlagsById_.size()) return true;
        return (segmentSurfaceFlagsById_[sid] & kSegmentFlagHasWallLikeFaces) != 0u;
    };

    auto pointInQuadProjected = [&](const Vector3D& a,
                                    const Vector3D& b,
                                    const Vector3D& c,
                                    const Vector3D& d,
                                    int64_t nxRaw,
                                    int64_t nyRaw,
                                    int64_t nzRaw) -> bool
    {
        const int64_t anx = abs64(nxRaw);
        const int64_t any = abs64(nyRaw);
        const int64_t anz = abs64(nzRaw);
        int axis = 1; // project to XZ by default
        if (anx >= any && anx >= anz) axis = 0;
        else if (anz >= anx && anz >= any) axis = 2;

        auto projectUV = [&](const Vector3D& v, int64_t& outU, int64_t& outV)
        {
            const int64_t x = static_cast<int64_t>(v.X.RawValue());
            const int64_t y = static_cast<int64_t>(v.Y.RawValue());
            const int64_t z = static_cast<int64_t>(v.Z.RawValue());
            if (axis == 0) { outU = z; outV = y; return; } // drop X -> YZ
            if (axis == 1) { outU = x; outV = z; return; } // drop Y -> XZ
            outU = x; outV = y;                            // drop Z -> XY
        };

        int64_t au = 0, av = 0, bu = 0, bv = 0, cu = 0, cv = 0, du = 0, dv = 0;
        projectUV(a, au, av);
        projectUV(b, bu, bv);
        projectUV(c, cu, cv);
        projectUV(d, du, dv);

        int64_t pu = 0;
        int64_t pv = 0;
        if (axis == 0) { pu = pzRaw; pv = pyRaw; }
        else if (axis == 1) { pu = pxRaw; pv = pzRaw; }
        else { pu = pxRaw; pv = pyRaw; }

        auto edgeCross = [&](int64_t x0, int64_t y0, int64_t x1, int64_t y1) -> int64_t
        {
            return ((pu - x0) * (y1 - y0)) - ((pv - y0) * (x1 - x0));
        };

        const int64_t c1 = edgeCross(au, av, bu, bv);
        const int64_t c2 = edgeCross(bu, bv, cu, cv);
        const int64_t c3 = edgeCross(cu, cv, du, dv);
        const int64_t c4 = edgeCross(du, dv, au, av);
        const bool hasNeg = (c1 < 0) || (c2 < 0) || (c3 < 0) || (c4 < 0);
        const bool hasPos = (c1 > 0) || (c2 > 0) || (c3 > 0) || (c4 > 0);
        return !(hasNeg && hasPos);
    };

    auto tryFacePlane = [&](const Vector3D& a,
                            const Vector3D& b,
                            const Vector3D& c,
                            const Vector3D& d,
                            int64_t nxRaw,
                            int64_t nyRaw,
                            int64_t nzRaw,
                            int32_t segmentId)
    {
        const int64_t maxPlanarAxis = std::max(abs64(nxRaw), abs64(nzRaw));
        if (maxPlanarAxis <= 0) return;

        const int64_t ax = static_cast<int64_t>(a.X.RawValue());
        const int64_t ay = static_cast<int64_t>(a.Y.RawValue());
        const int64_t az = static_cast<int64_t>(a.Z.RawValue());
        const int64_t dx = pxRaw - ax;
        const int64_t dy = pyRaw - ay;
        const int64_t dz = pzRaw - az;
        const int64_t signedDistRaw =
            ((dx * nxRaw) + (dy * nyRaw) + (dz * nzRaw)) >> 16;
        const int64_t absDistRaw = abs64(signedDistRaw);
        const int64_t prevDx = prevPxRaw - ax;
        const int64_t prevDy = prevPyRaw - ay;
        const int64_t prevDz = prevPzRaw - az;
        const int64_t prevSignedDistRaw =
            ((prevDx * nxRaw) + (prevDy * nyRaw) + (prevDz * nzRaw)) >> 16;
        const bool overlapNow = (absDistRaw < radiusRaw);
        const bool crossedPlane =
            prevValid &&
            ((signedDistRaw > 0 && prevSignedDistRaw < 0) ||
             (signedDistRaw < 0 && prevSignedDistRaw > 0));
        if (!overlapNow && !crossedPlane) return;

        const int64_t penetrationRaw = overlapNow
            ? (radiusRaw - absDistRaw)
            : (absDistRaw + radiusRaw);
        if (penetrationRaw <= 0) return;

        const int64_t planarNxRaw = (nxRaw << 16) / maxPlanarAxis;
        const int64_t planarNzRaw = (nzRaw << 16) / maxPlanarAxis;
        const int64_t dirSign = crossedPlane
            ? ((prevSignedDistRaw >= 0) ? 1 : -1)
            : ((signedDistRaw >= 0) ? 1 : -1);
        const int64_t pushXRaw = ((planarNxRaw * penetrationRaw) >> 16) * dirSign;
        const int64_t pushZRaw = ((planarNzRaw * penetrationRaw) >> 16) * dirSign;

        if (penetrationRaw > bestPenRaw)
        {
            bestPenRaw = penetrationRaw;
            bestPushXRaw = pushXRaw;
            bestPushZRaw = pushZRaw;
            bestSegmentId = segmentId;
        }
    };

    auto tryEdge = [&](const Vector3D& a,
                       const Vector3D& b,
                       int32_t segmentId,
                       int64_t fallbackNxRaw,
                       int64_t fallbackNzRaw)
    {
        const int64_t ax = static_cast<int64_t>(a.X.RawValue());
        const int64_t az = static_cast<int64_t>(a.Z.RawValue());
        const int64_t bx = static_cast<int64_t>(b.X.RawValue());
        const int64_t bz = static_cast<int64_t>(b.Z.RawValue());
        const int64_t vx = bx - ax;
        const int64_t vz = bz - az;
        const int64_t lenSq = (vx * vx) + (vz * vz);
        if (lenSq <= 0) return;

        const int64_t wx = pxRaw - ax;
        const int64_t wz = pzRaw - az;
        const int64_t tNum = (wx * vx) + (wz * vz);
        const int64_t tClamped = clamp64(tNum, 0, lenSq);
        const int64_t cx = ax + ((vx * tClamped) / lenSq);
        const int64_t cz = az + ((vz * tClamped) / lenSq);

        const int64_t dx = pxRaw - cx;
        const int64_t dz = pzRaw - cz;
        const int64_t distSq = (dx * dx) + (dz * dz);
        if (distSq > radiusSq) return;

        const int64_t adx = abs64(dx);
        const int64_t adz = abs64(dz);
        const int64_t distAxis = (adx > adz) ? adx : adz;
        if (distAxis > radiusRaw) return;

        const int64_t penetrationRaw = radiusRaw - distAxis;
        if (penetrationRaw <= 0) return;

        int64_t nxRaw = 0;
        int64_t nzRaw = 0;
        if (distAxis > 0)
        {
            nxRaw = (dx << 16) / distAxis;
            nzRaw = (dz << 16) / distAxis;
        }
        else
        {
            const int64_t anx = abs64(fallbackNxRaw);
            const int64_t anz = abs64(fallbackNzRaw);
            const int64_t maxAxis = (anx > anz) ? anx : anz;
            if (maxAxis <= 0) return;
            nxRaw = (fallbackNxRaw << 16) / maxAxis;
            nzRaw = (fallbackNzRaw << 16) / maxAxis;
        }

        const int64_t pushXRaw = (nxRaw * penetrationRaw) >> 16;
        const int64_t pushZRaw = (nzRaw * penetrationRaw) >> 16;

        if (penetrationRaw > bestPenRaw)
        {
            bestPenRaw = penetrationRaw;
            bestPushXRaw = pushXRaw;
            bestPushZRaw = pushZRaw;
            bestSegmentId = segmentId;
        }
    };

    auto scanSegment = [&](const SegmentRenderEntry& segment,
                           bool onlyNonDriveableBySurface,
                           bool respectScmapHint)
    {
        if (respectScmapHint && !segmentHasWallCandidates(segment.id)) return;

        {
            SegmentRenderEntry* mutableSegment = const_cast<SegmentRenderEntry*>(&segment);
            if (mutableSegment && EnsureWallSegmentCache(*mutableSegment))
            {
                SaturatingIncrementU16(wallQuerySegmentsScannedThisFrame_);
                SaturatingAddU16(wallQueryFacesScannedThisFrame_,
                                 mutableSegment->wallSegments2D.size());

                // Fast int32 scan via stored outward normal projection.
                // Replaces closest-point-on-segment (had 2-4 int64 divisions per wall)
                // with a signed-distance dot product (0 divisions, all int32).
                // Safe for tracks within ±5000 coordinate units (SH2 assumption).
                const int32_t pXR  = static_cast<int32_t>(pxRaw);
                const int32_t pZR  = static_cast<int32_t>(pzRaw);
                const int32_t pYR  = static_cast<int32_t>(pyRaw);
                const int32_t pPrevXR = static_cast<int32_t>(prevPxRaw);
                const int32_t pPrevZR = static_cast<int32_t>(prevPzRaw);
                const int32_t pPrevYR = static_cast<int32_t>(prevPyRaw);
                const int32_t radI = static_cast<int32_t>(radiusRaw);
                const int32_t oXR  = trackOffset.X.RawValue();
                const int32_t oZR  = trackOffset.Z.RawValue();
                const int32_t oYR  = trackOffset.Y.RawValue();
                const int32_t yMgn = 12 << 16;
                const int32_t rad3 = radI * 3;

                for (size_t wi = 0; wi < mutableSegment->wallSegments2D.size(); ++wi)
                {
                    const auto& wall = mutableSegment->wallSegments2D[wi];

                    const int32_t sweepMinY = prevValid ? std::min(pYR, pPrevYR) : pYR;
                    const int32_t sweepMaxY = prevValid ? std::max(pYR, pPrevYR) : pYR;
                    // Y height filter (swept)
                    if (sweepMinY < (wall.minYRaw + oYR - yMgn) &&
                        sweepMaxY < (wall.minYRaw + oYR - yMgn)) continue;
                    if (sweepMinY > (wall.maxYRaw + oYR + yMgn) &&
                        sweepMaxY > (wall.maxYRaw + oYR + yMgn)) continue;

                    // Planar AABB with 3× radius margin — catches cars up to ~3.75u inside wall.
                    const int32_t sweepMinX = prevValid ? std::min(pXR, pPrevXR) : pXR;
                    const int32_t sweepMaxX = prevValid ? std::max(pXR, pPrevXR) : pXR;
                    const int32_t sweepMinZ = prevValid ? std::min(pZR, pPrevZR) : pZR;
                    const int32_t sweepMaxZ = prevValid ? std::max(pZR, pPrevZR) : pZR;
                    if (sweepMaxX < (wall.minXRaw + oXR - rad3) || sweepMinX > (wall.maxXRaw + oXR + rad3)) continue;
                    if (sweepMaxZ < (wall.minZRaw + oZR - rad3) || sweepMinZ > (wall.maxZRaw + oZR + rad3)) continue;

                    // Signed distance from car to wall plane using stored outward normal.
                    // >>8 on both sides keeps products in int32 (safe for coords ≤ ±5000u).
                    // Positive = car on correct side, negative = car tunneled through.
                    const int32_t dxR = (pXR - wall.axRaw - oXR) >> 8;
                    const int32_t dzR = (pZR - wall.azRaw - oZR) >> 8;
                    const int32_t signedDist = (dxR * (wall.nxRaw >> 8)) + (dzR * (wall.nzRaw >> 8));
                    const int32_t dxPrevR = (pPrevXR - wall.axRaw - oXR) >> 8;
                    const int32_t dzPrevR = (pPrevZR - wall.azRaw - oZR) >> 8;
                    const int32_t signedDistPrev = (dxPrevR * (wall.nxRaw >> 8)) + (dzPrevR * (wall.nzRaw >> 8));

                    const int32_t absDist = (signedDist < 0) ? -signedDist : signedDist;
                    const bool overlapNow = (absDist < radI);
                    const bool crossedPlane =
                        prevValid &&
                        ((signedDist > 0 && signedDistPrev < 0) ||
                         (signedDist < 0 && signedDistPrev > 0));

                    if (!overlapNow && !crossedPlane) continue;

                    // Penetration depth: approach uses (radius - dist), tunnel uses (dist + radius).
                    const int32_t pen = overlapNow
                        ? (radI - absDist)
                        : (absDist + radI);

                    // Push direction: derived geometrically from segment [A,B] perpendicular,
                    // oriented toward the side where the car was (prevPos when valid, else curPos).
                    // This is correct regardless of stored normal sign (inward vs outward).
                    const int32_t wax = wall.axRaw + oXR;
                    const int32_t waz = wall.azRaw + oZR;
                    const int32_t wEdgeX = wall.bxRaw - wall.axRaw;
                    const int32_t wEdgeZ = wall.bzRaw - wall.azRaw;
                    int32_t perpX = wEdgeZ;
                    int32_t perpZ = -wEdgeX;
                    const int32_t refX = prevValid ? pPrevXR : pXR;
                    const int32_t refZ = prevValid ? pPrevZR : pZR;
                    const int64_t oriDot = (int64_t)perpX * (refX - wax)
                                         + (int64_t)perpZ * (refZ - waz);
                    if (oriDot < 0) { perpX = -perpX; perpZ = -perpZ; }
                    const int32_t apX = (perpX < 0) ? -perpX : perpX;
                    const int32_t apZ = (perpZ < 0) ? -perpZ : perpZ;
                    const int32_t maxP = (apX > apZ) ? apX : apZ;
                    int32_t pushX;
                    int32_t pushZ;
                    if (maxP > 0)
                    {
                        const int32_t npX = static_cast<int32_t>(
                            ((int64_t)perpX << 16) / maxP);
                        const int32_t npZ = static_cast<int32_t>(
                            ((int64_t)perpZ << 16) / maxP);
                        pushX = static_cast<int32_t>(((int64_t)npX * pen) >> 16);
                        pushZ = static_cast<int32_t>(((int64_t)npZ * pen) >> 16);
                    }
                    else
                    {
                        // Degenerate segment: fall back to stored normal.
                        const int32_t dirSign = crossedPlane
                            ? ((signedDistPrev >= 0) ? 1 : -1)
                            : ((signedDist >= 0) ? 1 : -1);
                        pushX = static_cast<int32_t>(
                            ((static_cast<int64_t>(wall.nxRaw) * pen) >> 16) * dirSign);
                        pushZ = static_cast<int32_t>(
                            ((static_cast<int64_t>(wall.nzRaw) * pen) >> 16) * dirSign);
                    }

                    if (pen > bestPenRaw)
                    {
                        bestPenRaw   = pen;
                        bestPushXRaw = pushX;
                        bestPushZRaw = pushZ;
                        bestSegmentId = segment.id;
                    }
                }
                // Cache is authoritative: skip the face-by-face scan for this segment.
                return;
            }
        }

        if (!segment.renderer) return;

        const Vector3D* verts = nullptr;
        const SRL::Types::Polygon* faces = nullptr;
        size_t vertCount = 0u;
        size_t faceCount = 0u;
        if (!segment.renderer->GetComponentGeometry(verts, vertCount, faces, faceCount)) return;
        if (!verts || !faces || vertCount == 0u || faceCount == 0u) return;
        SaturatingIncrementU16(wallQuerySegmentsScannedThisFrame_);

        const size_t familyCount = segment.lodState.faceFamilyIds.size();
        const size_t scanFaceCount = (familyCount > 0u)
            ? std::min(faceCount, familyCount)
            : faceCount;
        SaturatingAddU16(wallQueryFacesScannedThisFrame_, scanFaceCount);

        for (size_t fi = 0; fi < scanFaceCount; ++fi)
        {
            const bool hasFamilyId = (fi < familyCount);
            const uint16_t faceFamilyId = hasFamilyId ? segment.lodState.faceFamilyIds[fi] : 0u;
            // Regra única: apenas faces com textura/família NÃO dirigível são parede.
            // Se family não existir no mapa ou surfaceType for 0, tratamos como não-solo (parede).
            if (onlyNonDriveableBySurface)
            {
                if (!hasFamilyId) continue;
                if (faceFamilyId > 0u &&
                    faceFamilyId < surfaceTypeByFamilyId_.size())
                {
                    const uint8_t st = surfaceTypeByFamilyId_[faceFamilyId];
                    if (IsDriveableSurfaceTypeId(st)) continue;
                }
            }

            const SRL::Types::Polygon& face = faces[fi];

            const uint16_t i0 = face.Vertices[0];
            const uint16_t i1 = face.Vertices[1];
            const uint16_t i2 = face.Vertices[2];
            const uint16_t i3 = face.Vertices[3];
            if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount || i3 >= vertCount) continue;

            const Vector3D a = verts[i0] + trackOffset;
            const Vector3D b = verts[i1] + trackOffset;
            const Vector3D c = verts[i2] + trackOffset;
            const Vector3D d = verts[i3] + trackOffset;

            int64_t fallbackNxRaw = static_cast<int64_t>(face.Normal.X.RawValue());
            int64_t fallbackNyRaw = static_cast<int64_t>(face.Normal.Y.RawValue());
            int64_t fallbackNzRaw = static_cast<int64_t>(face.Normal.Z.RawValue());
            if (fallbackNxRaw == 0 && fallbackNyRaw == 0 && fallbackNzRaw == 0)
            {
                // GEO format: compute normal from vertices so tryFacePlane/tryEdge can run.
                const int64_t abx = static_cast<int64_t>(b.X.RawValue()) - static_cast<int64_t>(a.X.RawValue());
                const int64_t aby = static_cast<int64_t>(b.Y.RawValue()) - static_cast<int64_t>(a.Y.RawValue());
                const int64_t abz = static_cast<int64_t>(b.Z.RawValue()) - static_cast<int64_t>(a.Z.RawValue());
                const int64_t acx = static_cast<int64_t>(c.X.RawValue()) - static_cast<int64_t>(a.X.RawValue());
                const int64_t acy = static_cast<int64_t>(c.Y.RawValue()) - static_cast<int64_t>(a.Y.RawValue());
                const int64_t acz = static_cast<int64_t>(c.Z.RawValue()) - static_cast<int64_t>(a.Z.RawValue());
                fallbackNxRaw = ((aby * acz) - (abz * acy)) >> 16;
                fallbackNyRaw = ((abz * acx) - (abx * acz)) >> 16;
                fallbackNzRaw = ((abx * acy) - (aby * acx)) >> 16;
            }
            const int64_t planarNormalAbs =
                std::max(abs64(fallbackNxRaw), abs64(fallbackNzRaw));
            // Keep mostly-vertical faces as walls and reject floor-like faces.
            if (planarNormalAbs <= 0) continue;
            if ((planarNormalAbs * 2) < abs64(fallbackNyRaw)) continue;

            const int64_t ay = static_cast<int64_t>(a.Y.RawValue());
            const int64_t by = static_cast<int64_t>(b.Y.RawValue());
            const int64_t cy = static_cast<int64_t>(c.Y.RawValue());
            const int64_t dy = static_cast<int64_t>(d.Y.RawValue());
            const int64_t ax = static_cast<int64_t>(a.X.RawValue());
            const int64_t bx = static_cast<int64_t>(b.X.RawValue());
            const int64_t cx = static_cast<int64_t>(c.X.RawValue());
            const int64_t dx = static_cast<int64_t>(d.X.RawValue());
            const int64_t az = static_cast<int64_t>(a.Z.RawValue());
            const int64_t bz = static_cast<int64_t>(b.Z.RawValue());
            const int64_t cz = static_cast<int64_t>(c.Z.RawValue());
            const int64_t dz = static_cast<int64_t>(d.Z.RawValue());
            int64_t minY = ay;
            int64_t maxY = ay;
            int64_t minX = ax;
            int64_t maxX = ax;
            int64_t minZ = az;
            int64_t maxZ = az;
            if (by < minY) minY = by;
            if (cy < minY) minY = cy;
            if (dy < minY) minY = dy;
            if (by > maxY) maxY = by;
            if (cy > maxY) maxY = cy;
            if (dy > maxY) maxY = dy;
            if (bx < minX) minX = bx;
            if (cx < minX) minX = cx;
            if (dx < minX) minX = dx;
            if (bx > maxX) maxX = bx;
            if (cx > maxX) maxX = cx;
            if (dx > maxX) maxX = dx;
            if (bz < minZ) minZ = bz;
            if (cz < minZ) minZ = cz;
            if (dz < minZ) minZ = dz;
            if (bz > maxZ) maxZ = bz;
            if (cz > maxZ) maxZ = cz;
            if (dz > maxZ) maxZ = dz;
            if (pyRaw < (minY - yMarginRaw) || pyRaw > (maxY + yMarginRaw)) continue;
            // Use swept bounds so tunneling (car skips past face in one frame) is caught.
            const int64_t fbSwMinX = prevValid ? std::min(pxRaw, prevPxRaw) : pxRaw;
            const int64_t fbSwMaxX = prevValid ? std::max(pxRaw, prevPxRaw) : pxRaw;
            const int64_t fbSwMinZ = prevValid ? std::min(pzRaw, prevPzRaw) : pzRaw;
            const int64_t fbSwMaxZ = prevValid ? std::max(pzRaw, prevPzRaw) : pzRaw;
            if (fbSwMaxX < (minX - radiusRaw) || fbSwMinX > (maxX + radiusRaw)) continue;
            if (fbSwMaxZ < (minZ - radiusRaw) || fbSwMinZ > (maxZ + radiusRaw)) continue;

            tryFacePlane(a, b, c, d, fallbackNxRaw, fallbackNyRaw, fallbackNzRaw, segment.id);
            tryEdge(a, b, segment.id, fallbackNxRaw, fallbackNzRaw);
            tryEdge(b, c, segment.id, fallbackNxRaw, fallbackNzRaw);
            tryEdge(c, d, segment.id, fallbackNxRaw, fallbackNzRaw);
            tryEdge(d, a, segment.id, fallbackNxRaw, fallbackNzRaw);
        }
    };

    auto runWallScan = [&](bool onlyNonDriveableBySurface,
                           bool respectScmapHint) -> bool
    {
        bool scannedLocal = false;
        if (seedSegmentId > 0 && totalSegmentCount_ > 0)
        {
            std::array<int32_t, 12> localIds{};
            size_t localCount = 0u;
            static constexpr std::array<int32_t, 9> kNeighborDelta = { 0, 1, -1, 2, -2, 3, -3, 4, -4 };
            for (size_t di = 0; di < kNeighborDelta.size(); ++di)
            {
                const int32_t candidateId =
                    WrapSegmentIdToRange(seedSegmentId + kNeighborDelta[di],
                                         static_cast<int32_t>(totalSegmentCount_));
                if (candidateId <= 0) continue;
                bool duplicate = false;
                for (size_t i = 0; i < localCount; ++i)
                {
                    if (localIds[i] == candidateId)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;
                if (localCount < localIds.size()) localIds[localCount++] = candidateId;
                const SegmentRenderEntry* localEntry = FindWindowEntryByIdFast(candidateId);
                if (!localEntry) continue;
                scanSegment(*localEntry, onlyNonDriveableBySurface, respectScmapHint);
                scannedLocal = true;
            }
        }

        // Global fallback: when local seed misses, scan loaded window segments.
        // On Saturn low-cost mode this still stays affordable because the hot path
        // uses cached 2D wall segments (int32), and this branch only runs on cadence/miss.
        if (allowGlobalFallback && (!scannedLocal || bestPenRaw <= 0))
        {
            for (const auto& segment : segmentRenderers_)
            {
                scanSegment(segment, onlyNonDriveableBySurface, respectScmapHint);
            }
        }
        return bestPenRaw > 0;
    };

    bool foundWall = runWallScan(true, true);
    if (!Game::PhysicsFeatureFlags::kEnableSaturnLowCostPhysics && !foundWall)
    {
        foundWall = runWallScan(false, false);
    }

    if (!foundWall || bestPenRaw <= 0) return false;

    // Push is the exact distance needed to reach collision radius from the wall surface.
    // No cap: clamping less than needed leaves the car inside the wall and stalls convergence.
    outPush = Vector3D(SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(
                           clamp64(bestPushXRaw, -0x7FFFFFFF, 0x7FFFFFFF))),
                       SRL::Math::Types::Fxp::BuildRaw(0),
                       SRL::Math::Types::Fxp::BuildRaw(static_cast<int32_t>(
                           clamp64(bestPushZRaw, -0x7FFFFFFF, 0x7FFFFFFF))));
    if (outSegmentId) *outSegmentId = bestSegmentId;
    SaturatingIncrementU16(wallQueryHitsThisFrame_);
    return true;
}

bool TrackSystem::GetRenderWindowDebugSnapshot(int32_t& outStartSegmentId,
                                               int8_t& outDirection,
                                               uint16_t& outWindowCount) const
{
    outDirection = (windowDirection_ < 0) ? -1 : 1;
    outWindowCount = static_cast<uint16_t>(std::min<size_t>(
        segmentRenderers_.size(),
        static_cast<size_t>(kTrackSegmentLimit)));
    outStartSegmentId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);

    if (!ReadyFlag()) return false;
    if (totalSegmentCount_ == 0) return false;
    if (outWindowCount == 0) return false;
    return outStartSegmentId > 0;
}

bool TrackSystem::GetRenderWindowSegmentIdAt(size_t logicalIndex, int32_t& outSegmentId) const
{
    outSegmentId = -1;
    if (totalSegmentCount_ == 0) return false;

    const size_t windowCount = segmentRenderers_.size();
    if (windowCount == 0 || logicalIndex >= windowCount) return false;

    const int32_t startId = WrapSegmentIdToRange(activeWindowStartId_, totalSegmentCount_);
    if (startId <= 0) return false;

    const int32_t step = (windowDirection_ < 0)
        ? -static_cast<int32_t>(logicalIndex)
        : static_cast<int32_t>(logicalIndex);
    outSegmentId = WrapSegmentIdToRange(startId + step, totalSegmentCount_);
    return outSegmentId > 0;
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











