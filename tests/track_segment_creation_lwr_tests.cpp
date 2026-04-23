// JUnit-style integration tests for the segment creation pipeline LWR behavior.
//
// OBJETIVO: verificar que cada etapa do pipeline de slide de segmentos
// não expande o LWR de forma monotônica após o aquecimento inicial.
//
// O MockLwrTracker simula o comportamento do alocador TLSF do Saturn:
//   - reserve() / push_back() que cresce → alloc registrado
//   - swap({}) → free registrado (padrão ANTIGO, que causava o vazamento)
//   - clear()  → NÃO registra free (preserva capacity — padrão CORRETO)
//
// Compilação no host:
//   g++ -std=c++17 tests/track_segment_creation_lwr_tests.cpp -o run_lwr_tests && ./run_lwr_tests
//
// Para instrumentação no hardware Saturn, compilar com:
//   -DTRACK_LWR_STAGE_TRACE
// e observar as linhas "LWP <nome> d:<delta>" na saída de depuração.

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

// ============================================================================
// Framework JUnit-style
// ============================================================================

struct TestContext
{
    int         failures    = 0;
    int         assertions  = 0;
    std::string currentTest;

    void Fail(const char* expr,
              const char* file,
              int         line,
              const std::string& detail = std::string())
    {
        std::cerr << "  [FAIL] " << currentTest << "\n"
                  << "    " << file << ":" << line << "  assert: " << expr;
        if (!detail.empty()) std::cerr << "\n    detail: " << detail;
        std::cerr << "\n";
        ++failures;
    }
};

template <typename T>
std::string Str(const T& v)
{
    std::ostringstream o;
    o << v;
    return o.str();
}

#define ASSERT_TRUE(ctx, expr)                                              \
    do {                                                                    \
        ++(ctx).assertions;                                                 \
        if (!(expr)) (ctx).Fail(#expr, __FILE__, __LINE__);                 \
    } while (0)

#define ASSERT_EQ(ctx, actual, expected)                                    \
    do {                                                                    \
        ++(ctx).assertions;                                                 \
        const auto _a = (actual);                                           \
        const auto _e = (expected);                                         \
        if (!(_a == _e))                                                    \
            (ctx).Fail(#actual " == " #expected, __FILE__, __LINE__,        \
                       std::string("actual=") + Str(_a) +                   \
                       " expected=" + Str(_e));                             \
    } while (0)

#define ASSERT_LE(ctx, actual, expected)                                    \
    do {                                                                    \
        ++(ctx).assertions;                                                 \
        const auto _a = (actual);                                           \
        const auto _e = (expected);                                         \
        if (!(_a <= _e))                                                    \
            (ctx).Fail(#actual " <= " #expected, __FILE__, __LINE__,        \
                       std::string("actual=") + Str(_a) +                   \
                       " limit=" + Str(_e));                                \
    } while (0)

// ============================================================================
// MockLwrTracker — simula o heap TLSF do Saturn
//
// Regras:
//   OnAlloc(n)  → currentBytes += n     (nova alocação ou realloc final)
//   OnFree(n)   → currentBytes -= n     (free ou realloc old)
//   clear()     → NADA (capacity mantida, não chama OnFree)
//   swap({})    → OnFree(capacity)      (padrão ANTIGO — vaza LWR)
// ============================================================================

struct MockLwrTracker
{
    int64_t currentBytes = 0;
    int64_t peakBytes    = 0;
    int64_t totalAlloc   = 0;
    int64_t totalFree    = 0;

    void OnAlloc(int64_t bytes)
    {
        currentBytes += bytes;
        if (currentBytes > peakBytes) peakBytes = currentBytes;
        totalAlloc += bytes;
    }

    void OnFree(int64_t bytes)
    {
        currentBytes -= bytes;
        totalFree += bytes;
    }

    int64_t Snapshot() const { return currentBytes; }
};

// ============================================================================
// TrackedVector<T> — std::vector com rastreamento de alloc/free no tracker
// ============================================================================

template <typename T>
struct TrackedVector
{
    MockLwrTracker& tracker;
    std::vector<T>  v;

    explicit TrackedVector(MockLwrTracker& t) : tracker(t) {}

    // Não copiável para evitar double-free no tracker
    TrackedVector(const TrackedVector&)            = delete;
    TrackedVector& operator=(const TrackedVector&) = delete;
    TrackedVector(TrackedVector&& o) noexcept : tracker(o.tracker), v(std::move(o.v)) {}

    ~TrackedVector()
    {
        if (v.capacity() > 0)
            tracker.OnFree(static_cast<int64_t>(v.capacity() * sizeof(T)));
    }

    void reserve(size_t n)
    {
        if (n <= v.capacity()) return;
        const size_t oldCap = v.capacity();
        if (oldCap > 0)
            tracker.OnFree(static_cast<int64_t>(oldCap * sizeof(T)));
        v.reserve(n);
        tracker.OnAlloc(static_cast<int64_t>(v.capacity() * sizeof(T)));
    }

    // assign com n elementos — pode crescer
    void assign(size_t n, T val = T{})
    {
        const size_t oldCap = v.capacity();
        v.assign(n, val);
        if (v.capacity() > oldCap)
        {
            if (oldCap > 0)
                tracker.OnFree(static_cast<int64_t>(oldCap * sizeof(T)));
            tracker.OnAlloc(static_cast<int64_t>(v.capacity() * sizeof(T)));
        }
    }

    // clear() — preserva capacity, sem notificação ao tracker (comportamento correto)
    void clear() { v.clear(); }

    // swapFree() — libera capacity (padrão ANTIGO que causava o vazamento)
    void swapFree()
    {
        if (v.capacity() > 0)
        {
            tracker.OnFree(static_cast<int64_t>(v.capacity() * sizeof(T)));
            std::vector<T>{}.swap(v);
        }
    }

    // swap() de ponteiro — sem alloc/free (só troca posse)
    void swap(TrackedVector& other)
    {
        v.swap(other.v);
        // capacity troca de lado, sem alloc/free no TLSF
    }

    // EnsureVectorCapacityFloor — só cresce, nunca encolhe
    void ensureFloor(size_t floor)
    {
        if (v.capacity() >= floor) return;
        reserve(floor);
    }

    void push_back(const T& val)
    {
        const size_t oldCap = v.capacity();
        v.push_back(val);
        if (v.capacity() > oldCap)
        {
            if (oldCap > 0)
                tracker.OnFree(static_cast<int64_t>(oldCap * sizeof(T)));
            tracker.OnAlloc(static_cast<int64_t>(v.capacity() * sizeof(T)));
        }
    }

    size_t capacity() const { return v.capacity(); }
    size_t size()     const { return v.size(); }
    bool   empty()    const { return v.empty(); }
};

// ============================================================================
// TrackedObject<T> — unique_ptr simulado para o renderer (TrackRenderer)
// ============================================================================

struct TrackedObject
{
    MockLwrTracker& tracker;
    bool            allocated = false;
    bool            recycled  = false;
    size_t          byteSize;

    explicit TrackedObject(MockLwrTracker& t, size_t sz)
        : tracker(t), byteSize(sz)
    {
        tracker.OnAlloc(static_cast<int64_t>(byteSize));
        allocated = true;
    }

    ~TrackedObject()
    {
        if (allocated)
            tracker.OnFree(static_cast<int64_t>(byteSize));
    }

    void recycleRuntimeState() { recycled = true; }
};

using TrackedObjectPtr = std::unique_ptr<TrackedObject>;

TrackedObjectPtr makeRenderer(MockLwrTracker& t, size_t sz = 4096)
{
    return std::make_unique<TrackedObject>(t, sz);
}

// ============================================================================
// Teste 1: clear() preserva capacity — sem free de LWR
// ============================================================================

void TestClearPreservesCapacityDoesNotFreeLwr(TestContext& ctx)
{
    ctx.currentTest = "ClearPreservesCapacityDoesNotFreeLwr";
    MockLwrTracker tracker;

    TrackedVector<uint16_t> familyIds(tracker);
    TrackedVector<int32_t>  faceSlots(tracker);

    familyIds.reserve(128);
    faceSlots.reserve(128);
    const int64_t lwrAfterReserve = tracker.Snapshot();

    familyIds.assign(60, 42u);
    faceSlots.assign(60, -1);

    // clear() — preserva capacity, sem free
    familyIds.clear();
    faceSlots.clear();

    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterReserve);
    ASSERT_EQ(ctx, familyIds.capacity(), size_t(128));
    ASSERT_EQ(ctx, faceSlots.capacity(),  size_t(128));
    ASSERT_EQ(ctx, familyIds.size(),      size_t(0));
}

// ============================================================================
// Teste 2: swap({}) libera capacity — padrão ANTIGO (demonstração do leak)
// ============================================================================

void TestSwapFreeReleasesCapacityOldBugPattern(TestContext& ctx)
{
    ctx.currentTest = "SwapFreeReleasesCapacityOldBugPattern";
    MockLwrTracker tracker;

    TrackedVector<uint16_t> familyIds(tracker);
    familyIds.reserve(128);
    familyIds.assign(60, 42u);
    const int64_t lwrBeforeSwapFree = tracker.Snapshot();

    // Padrão ANTIGO: decltype(...){}.swap(...) — libera LWR
    familyIds.swapFree();

    ASSERT_TRUE(ctx, tracker.Snapshot() < lwrBeforeSwapFree);
    ASSERT_EQ(ctx, familyIds.capacity(), size_t(0));

    // Re-uso: precisa de nova alocação → LWR sobe novamente
    familyIds.reserve(128);
    ASSERT_TRUE(ctx, tracker.Snapshot() >= lwrBeforeSwapFree);
}

// ============================================================================
// Teste 3: EnsureVectorCapacityFloor é monotonicamente crescente
// ============================================================================

void TestEnsureCapacityFloorIsMonotoneIncreasing(TestContext& ctx)
{
    ctx.currentTest = "EnsureCapacityFloorIsMonotoneIncreasing";
    MockLwrTracker tracker;

    TrackedVector<uint16_t> v(tracker);

    // Segmento com 80 faces
    v.assign(80, 0u);
    v.ensureFloor(80);
    const size_t capAfter80 = v.capacity();

    // Segmento com 50 faces — floor não encolhe
    v.clear();
    v.assign(50, 0u);
    v.ensureFloor(50);
    ASSERT_EQ(ctx, v.capacity(), capAfter80);

    // Segmento com 120 faces — floor cresce
    v.clear();
    v.assign(120, 0u);
    v.ensureFloor(120);
    ASSERT_TRUE(ctx, v.capacity() >= size_t(120));

    // Segmento com 60 faces novamente — capacity mantida no peak
    const size_t capAfter120 = v.capacity();
    v.clear();
    v.assign(60, 0u);
    v.ensureFloor(60);
    ASSERT_EQ(ctx, v.capacity(), capAfter120);
}

// ============================================================================
// Teste 4: renderer do prefetch é alocado uma vez e reutilizado via swap
// (slideScratchRenderer_ nunca cria dois objetos simultâneos)
// ============================================================================

void TestPrefetchRendererAllocatedOncePerCycle(TestContext& ctx)
{
    ctx.currentTest = "PrefetchRendererAllocatedOncePerCycle";
    MockLwrTracker tracker;

    // Simula: segmentRenderers_[i].renderer (20 slots iniciais)
    std::vector<TrackedObjectPtr> slotRenderers;
    slotRenderers.reserve(20);
    for (int i = 0; i < 20; ++i)
        slotRenderers.push_back(makeRenderer(tracker));

    const int64_t lwrAfterInit = tracker.Snapshot();

    // Slide 1:
    // BuildSegmentIntoPrefetch → alloca slideScratchRenderer_ (null → new)
    TrackedObjectPtr scratchRenderer = makeRenderer(tracker);
    const int64_t lwrAfterFirstAlloc = tracker.Snapshot();
    ASSERT_TRUE(ctx, lwrAfterFirstAlloc > lwrAfterInit); // uma alloc nova

    // CommitStabilizedSlideBackBuffer → swap(slotRenderers[0], scratchRenderer)
    std::swap(slotRenderers[0], scratchRenderer);
    // scratchRenderer agora tem o renderer do slot 0; recicla-o
    scratchRenderer->recycleRuntimeState();

    // Estado após slide 1: LWR idêntico ao após alloc (scratchRenderer ainda vivo)
    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterFirstAlloc);

    // Slide 2:
    // PrepareStabilizedSlideBackBuffer → move scratchRenderer → cria novo prefetch
    // Antes: slideScratchRenderer_ = scratchRenderer (renderer do slot 0, reciclado)
    // Libera o antigo (overwrite do unique_ptr) → free. Alloca novo prefetch.
    TrackedObjectPtr newPrefetch = makeRenderer(tracker);  // +1 alloc
    scratchRenderer.reset();                               // -1 free (old slot renderer)

    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterFirstAlloc); // net neutral

    // CommitStabilizedSlideBackBuffer
    std::swap(slotRenderers[1], newPrefetch);
    newPrefetch->recycleRuntimeState();

    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterFirstAlloc); // ainda neutral

    // Slide 3: mesma dinâmica
    TrackedObjectPtr newPrefetch2 = makeRenderer(tracker);
    newPrefetch.reset();
    std::swap(slotRenderers[2], newPrefetch2);
    newPrefetch2->recycleRuntimeState();

    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterFirstAlloc); // plateau
}

// ============================================================================
// Teste 5: padrão de swap duplo do back buffer é neutro no LWR
// (slot.lodState.faceFamilyIds.swap(backBuffer.incomingFamilyIds) → sem alloc)
// ============================================================================

void TestSlideBackBufferDoubleSwapIsLwrNeutral(TestContext& ctx)
{
    ctx.currentTest = "SlideBackBufferDoubleSwapIsLwrNeutral";
    MockLwrTracker tracker;

    TrackedVector<uint16_t> slotFamilyIds(tracker);
    TrackedVector<uint16_t> backBufFamilyIds(tracker);
    TrackedVector<int32_t>  slotFaceSlots(tracker);
    TrackedVector<int32_t>  backBufFaceSlots(tracker);

    // Inicialização — simula PrimeRuntimeScratchCapacities
    slotFamilyIds.reserve(128);
    slotFaceSlots.reserve(128);
    backBufFamilyIds.reserve(128);
    backBufFaceSlots.reserve(128);
    const int64_t lwrAfterInit = tracker.Snapshot();

    // Simula ciclo de slide:
    // 1. PrepareStabilizedSlideBackBuffer: fill backBuf (assign)
    backBufFamilyIds.assign(45, 100u);
    backBufFaceSlots.assign(45, 5);
    const int64_t lwrAfterFill = tracker.Snapshot();
    ASSERT_EQ(ctx, lwrAfterFill, lwrAfterInit); // assign não cresce (capacity ok)

    // 2. CommitStabilizedSlideBackBuffer: swap slot ↔ backBuf
    slotFamilyIds.swap(backBufFamilyIds);
    slotFaceSlots.swap(backBufFaceSlots);
    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterInit); // swap de ponteiro — neutral

    // 3. ResetSlideBackBuffer: clear() backBuf (não libera)
    backBufFamilyIds.clear();
    backBufFaceSlots.clear();
    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterInit); // ainda neutral

    // 4. Ciclo seguinte: fill novamente — sem nova alocação
    backBufFamilyIds.assign(38, 200u);
    backBufFaceSlots.assign(38, 7);
    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterInit);

    slotFamilyIds.swap(backBufFamilyIds);
    slotFaceSlots.swap(backBufFaceSlots);
    backBufFamilyIds.clear();
    backBufFaceSlots.clear();
    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterInit); // plateau após 2 slides
}

// ============================================================================
// Teste 6: catálogo de famílias é limitado pela janela ativa após merge
// (seg1FamilySlots_ não cresce indefinidamente)
// ============================================================================

void TestFamilyCatalogBoundedByActiveWindowAfterMerge(TestContext& ctx)
{
    ctx.currentTest = "FamilyCatalogBoundedByActiveWindowAfterMerge";
    MockLwrTracker tracker;

    // Simula seg1FamilySlots_ e familyMergeCurrentWindowScratch_
    TrackedVector<uint16_t> catalogFamilies(tracker);
    TrackedVector<uint16_t> mergeScratch(tracker);

    // Janela inicial: 80 famílias únicas
    catalogFamilies.reserve(256);
    mergeScratch.reserve(256);
    const int64_t lwrAfterInit = tracker.Snapshot();

    auto simulateMerge = [&](size_t windowFamilyCount)
    {
        // BuildTrackFamilyLodSlots: clear + fill mergeScratch com janela atual
        mergeScratch.clear();
        for (size_t i = 0; i < windowFamilyCount; ++i)
            mergeScratch.push_back(static_cast<uint16_t>(i + 1));
        // MergeCurrentWindowFamilies: swap direto
        catalogFamilies.swap(mergeScratch);
        // mergeScratch agora tem dados antigos (clear no próximo ciclo)
    };

    // Slide 1: janela com 80 famílias
    simulateMerge(80);
    ASSERT_EQ(ctx, catalogFamilies.size(), size_t(80));
    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterInit); // sem nova alloc

    // Slide 2: janela com 75 famílias (segmento menor entrou)
    simulateMerge(75);
    ASSERT_EQ(ctx, catalogFamilies.size(), size_t(75));
    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterInit);

    // Slide 3: janela com 90 famílias — capacity ainda comporta (256 reservado)
    simulateMerge(90);
    ASSERT_EQ(ctx, catalogFamilies.size(), size_t(90));
    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterInit); // sem nova alloc

    // Slide 4: janela com 80 famílias novamente — volta sem alloc
    simulateMerge(80);
    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterInit);
}

// ============================================================================
// Teste 7: pipeline completo de slide estabiliza o LWR após aquecimento
// Simula 10 slides consecutivos e verifica que delta = 0 após o 2º slide.
// ============================================================================

void TestFullSlidePipelineStabilizesLwrAfterWarmup(TestContext& ctx)
{
    ctx.currentTest = "FullSlidePipelineStabilizesLwrAfterWarmup";
    MockLwrTracker tracker;

    // Estruturas que simulam o estado persistente do TrackSystem
    TrackedVector<uint16_t> slotFamilyIds(tracker);     // slot atual
    TrackedVector<int32_t>  slotFaceSlots(tracker);
    TrackedVector<uint16_t> backBufFamilyIds(tracker);  // back buffer
    TrackedVector<int32_t>  backBufFaceSlots(tracker);
    TrackedVector<uint16_t> prefetchFamilyIds(tracker); // prefetch scratch
    TrackedVector<uint16_t> mergeScratch(tracker);      // familyMergeScratch_
    TrackedVector<uint16_t> catalog(tracker);           // seg1FamilySlots_
    TrackedObjectPtr         scratchRenderer;            // slideScratchRenderer_

    // PrimeRuntimeScratchCapacities — reserva floors (kStabilizedFaceCapacityFloorCap = 768)
    constexpr size_t kFloor = 128u; // valor de teste (proporcional ao real)
    slotFamilyIds.reserve(kFloor);
    slotFaceSlots.reserve(kFloor);
    backBufFamilyIds.reserve(kFloor);
    backBufFaceSlots.reserve(kFloor);
    prefetchFamilyIds.reserve(kFloor);
    mergeScratch.reserve(256u);
    catalog.reserve(256u);

    // Aloca 20 renderers de slot (RebuildActiveSegmentWindow)
    std::vector<TrackedObjectPtr> slots;
    slots.reserve(20);
    for (int i = 0; i < 20; ++i)
        slots.push_back(makeRenderer(tracker));

    const int64_t lwrAfterInit = tracker.Snapshot();

    int64_t lwrAfterWarmup = 0;

    // Simula N slides consecutivos
    constexpr int kSlides = 10;
    for (int slide = 0; slide < kSlides; ++slide)
    {
        const size_t facesThisSlide = 40u + (slide % 3) * 10u; // varia 40-60
        const size_t dropIdx        = static_cast<size_t>(slide % 20);

        // --- BuildSegmentIntoPrefetch ---
        if (!scratchRenderer)
            scratchRenderer = makeRenderer(tracker);
        prefetchFamilyIds.assign(facesThisSlide, static_cast<uint16_t>(slide + 1));
        prefetchFamilyIds.ensureFloor(kFloor);

        // --- PrepareStabilizedSlideBackBuffer ---
        backBufFamilyIds.assign(facesThisSlide, static_cast<uint16_t>(slide + 1));
        backBufFaceSlots.assign(facesThisSlide, -1);
        backBufFamilyIds.ensureFloor(kFloor);
        backBufFaceSlots.ensureFloor(kFloor);

        // --- CommitStabilizedSlideBackBuffer ---
        // swap slot ↔ backBuf
        slotFamilyIds.swap(backBufFamilyIds);
        slotFaceSlots.swap(backBufFaceSlots);
        slotFamilyIds.ensureFloor(kFloor);
        slotFaceSlots.ensureFloor(kFloor);
        // swap slot.renderer ↔ scratchRenderer
        std::swap(slots[dropIdx], scratchRenderer);
        if (scratchRenderer) scratchRenderer->recycleRuntimeState();

        // --- ResetSlidePrefetchState / ResetSlideBackBuffer ---
        prefetchFamilyIds.clear();
        backBufFamilyIds.clear();
        backBufFaceSlots.clear();

        // --- MergeCurrentWindowFamilies ---
        mergeScratch.clear();
        for (size_t f = 0; f < 80u; ++f)
            mergeScratch.push_back(static_cast<uint16_t>(f + 1));
        catalog.swap(mergeScratch); // direto — sem alloc

        // Mede LWR ao final do slide
        const int64_t lwrEnd = tracker.Snapshot();

        if (slide == 1)
        {
            lwrAfterWarmup = lwrEnd;
        }
        else if (slide >= 2)
        {
            // Após aquecimento: delta por slide deve ser 0
            const int64_t delta = lwrEnd - lwrAfterWarmup;
            ASSERT_EQ(ctx, delta, int64_t(0));
        }
    }

    // LWR final não deve ter crescido além do aquecimento
    ASSERT_LE(ctx, tracker.Snapshot(), lwrAfterWarmup);
}

// ============================================================================
// Teste 8: assign() seguido de ensureFloor() não aloca além do floor
// (padrão usado em BuildSegmentIntoPrefetch)
// ============================================================================

void TestAssignThenEnsureFloorDoesNotExceedFloor(TestContext& ctx)
{
    ctx.currentTest = "AssignThenEnsureFloorDoesNotExceedFloor";
    MockLwrTracker tracker;

    constexpr size_t kFloor = 128u;

    TrackedVector<uint16_t> familyIds(tracker);
    TrackedVector<int32_t>  faceSlots(tracker);
    familyIds.reserve(kFloor);
    faceSlots.reserve(kFloor);
    const int64_t lwrAfterFloor = tracker.Snapshot();

    // Simula BuildSegmentIntoPrefetch para 3 segmentos com tamanhos variados
    const std::array<size_t, 3> segFaceCounts = {{ 45u, 80u, 60u }};

    for (size_t count : segFaceCounts)
    {
        // clear + assign — como no código real
        familyIds.clear();
        familyIds.assign(count, static_cast<uint16_t>(1));
        faceSlots.clear();
        faceSlots.assign(count, int32_t(-1));
        // EnsureVectorCapacityFloor — só cresce até o floor
        familyIds.ensureFloor(kFloor);
        faceSlots.ensureFloor(kFloor);

        // Não deve ter crescido além do floor (capacity já ≥ floor antes)
        ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterFloor);
        ASSERT_TRUE(ctx, familyIds.capacity() >= kFloor);
        ASSERT_TRUE(ctx, faceSlots.capacity()  >= kFloor);
    }

    // Segmento com mais faces que o floor — cresce uma vez
    familyIds.clear();
    familyIds.assign(kFloor + 50u, static_cast<uint16_t>(1));
    faceSlots.clear();
    faceSlots.assign(kFloor + 50u, int32_t(-1));
    familyIds.ensureFloor(kFloor);
    faceSlots.ensureFloor(kFloor);

    const int64_t lwrAfterGrow = tracker.Snapshot();
    ASSERT_TRUE(ctx, lwrAfterGrow > lwrAfterFloor); // cresceu uma vez (esperado)

    // Slide seguinte com tamanho menor — não deve crescer mais
    familyIds.clear();
    familyIds.assign(60u, static_cast<uint16_t>(1));
    faceSlots.clear();
    faceSlots.assign(60u, int32_t(-1));
    familyIds.ensureFloor(kFloor);
    faceSlots.ensureFloor(kFloor);
    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAfterGrow); // plateau
}

// ============================================================================
// Teste 9: componentes do renderer são compactados ao piso após segmento outlier
//
// Valida a lógica adicionada em RecycleRuntimeState():
//   if (capacity > floor * 2) { swap(tmp.reserve(floor)); }
//
// O segmento outlier (>2×piso faces) cresce o renderer. Ao ser evictado,
// RecycleRuntimeState compacta as componentes de volta ao piso. O próximo
// segmento normal reutiliza o piso sem nova alocação.
// ============================================================================

void TestOversizedSegmentComponentVectorsCompactOnRecycle(TestContext& ctx)
{
    ctx.currentTest = "OversizedSegmentComponentVectorsCompactOnRecycle";
    MockLwrTracker tracker;

    constexpr size_t kFaceFloor = 128u;   // análogo a kStabilizedFaceCapacityFloorCap
    constexpr size_t kVertFloor = 128u;

    TrackedVector<int16_t> faces(tracker); // proxy para componentFaces_
    TrackedVector<int16_t> attrs(tracker); // proxy para componentAttrs_
    TrackedVector<int16_t> verts(tracker); // proxy para componentVerts_

    // SetRuntimeCapacityFloor — pré-aloca ao piso
    faces.reserve(kFaceFloor);
    attrs.reserve(kFaceFloor);
    verts.reserve(kVertFloor);
    const int64_t lwrAtFloor = tracker.Snapshot();

    // Simula InitializeFromComponentDataRecycled para segmento outlier (3×piso)
    // Análogo ao: if (componentFaces_.capacity() < max(floor, faces.size())) reserve(max)
    constexpr size_t kOutlierFaces = kFaceFloor * 3u; // 384 — claramente > 2×piso
    constexpr size_t kOutlierVerts = kVertFloor * 3u;
    faces.reserve(kOutlierFaces);
    attrs.reserve(kOutlierFaces);
    verts.reserve(kOutlierVerts);
    faces.assign(kOutlierFaces, 1);
    attrs.assign(kOutlierFaces, 2);
    verts.assign(kOutlierVerts, 3);
    const int64_t lwrAfterOutlierBuild = tracker.Snapshot();
    ASSERT_TRUE(ctx, lwrAfterOutlierBuild > lwrAtFloor); // cresceu

    // Simula RecycleRuntimeState: clear() + compact if capacity > floor*2
    faces.clear();
    attrs.clear();
    verts.clear();

    // Compact path (capacity 384 > 128*2=256 → deve compactar)
    ASSERT_TRUE(ctx, faces.capacity() > kFaceFloor * 2u); // pré-condição

    auto compactIfOversized = [&](TrackedVector<int16_t>& v, size_t floor)
    {
        if (v.capacity() > floor * 2u)
        {
            TrackedVector<int16_t> tmp(tracker);
            tmp.reserve(floor);
            v.swap(tmp);
            // tmp sai de escopo → libera capacidade antiga
        }
    };
    compactIfOversized(faces, kFaceFloor);
    compactIfOversized(attrs, kFaceFloor);
    compactIfOversized(verts, kVertFloor);

    // Após compact: capacity deve ser exatamente o piso
    ASSERT_EQ(ctx, faces.capacity(), kFaceFloor);
    ASSERT_EQ(ctx, attrs.capacity(), kFaceFloor);
    ASSERT_EQ(ctx, verts.capacity(), kVertFloor);

    // LWR deve ter caído em relação ao pico do outlier
    const int64_t lwrAfterCompact = tracker.Snapshot();
    ASSERT_TRUE(ctx, lwrAfterCompact < lwrAfterOutlierBuild);
    // E deve ser igual ao nível do piso (free da sobra, alloc do piso novo)
    ASSERT_EQ(ctx, lwrAfterCompact, lwrAtFloor);

    // Simula próximo segmento normal (≤piso faces) — sem nova alocação
    constexpr size_t kNormalFaces = 80u;
    faces.clear();
    faces.assign(kNormalFaces, 5);
    attrs.clear();
    attrs.assign(kNormalFaces, 6);
    verts.clear();
    verts.assign(kNormalFaces, 7);

    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAtFloor); // sem nova alloc

    // Segmento subsequente também outlier — comportamento idêntico (cresce → compacta)
    faces.reserve(kOutlierFaces);
    attrs.reserve(kOutlierFaces);
    verts.reserve(kOutlierVerts);
    const int64_t lwrAtSecondOutlierPeak = tracker.Snapshot();
    ASSERT_TRUE(ctx, lwrAtSecondOutlierPeak > lwrAtFloor);

    // Evicção: compactar novamente
    faces.clear(); attrs.clear(); verts.clear();
    compactIfOversized(faces, kFaceFloor);
    compactIfOversized(attrs, kFaceFloor);
    compactIfOversized(verts, kVertFloor);
    ASSERT_EQ(ctx, tracker.Snapshot(), lwrAtFloor); // volta ao piso
}

// ============================================================================
// Teste 10: scratch de decode de textura nao deve ficar inflando para sempre
// ============================================================================

void TestDecodedTextureScratchCompactsWhenOversized(TestContext& ctx)
{
    ctx.currentTest = "DecodedTextureScratchCompactsWhenOversized";
    MockLwrTracker tracker;

    TrackedVector<uint16_t> palette(tracker);
    TrackedVector<uint8_t> pixels(tracker);

    constexpr size_t kPaletteFloor = 256u;
    constexpr size_t kPixelFloor = 16u * 1024u;
    constexpr size_t kPaletteThreshold = kPaletteFloor * 4u;
    constexpr size_t kPixelThreshold = kPixelFloor * 4u;

    auto compactEmptyToTarget = [&](auto& vec, size_t target)
    {
        if (!vec.empty()) return;
        if (vec.capacity() <= target) return;
        if (vec.capacity() <= (target * 2u + 8u)) return;
        using VecType = std::decay_t<decltype(vec)>;
        VecType compact(tracker);
        compact.reserve(target);
        vec.swap(compact);
    };

    auto normalizeScratch = [&]()
    {
        palette.clear();
        pixels.clear();

        if (palette.capacity() > kPaletteThreshold)
        {
            compactEmptyToTarget(palette, kPaletteFloor);
        }
        else
        {
            palette.ensureFloor(kPaletteFloor);
        }

        if (pixels.capacity() > kPixelThreshold)
        {
            compactEmptyToTarget(pixels, kPixelFloor);
        }
        else
        {
            pixels.ensureFloor(kPixelFloor);
        }
    };

    // Outlier decode inflates scratch.
    palette.assign(4096u, 0xFFFFu);
    pixels.assign(120u * 1024u, 0xABu);
    const int64_t afterOutlier = tracker.Snapshot();
    ASSERT_TRUE(ctx, palette.capacity() > kPaletteThreshold);
    ASSERT_TRUE(ctx, pixels.capacity() > kPixelThreshold);

    normalizeScratch();
    const int64_t afterNormalize = tracker.Snapshot();
    ASSERT_TRUE(ctx, afterNormalize < afterOutlier);
    ASSERT_TRUE(ctx, palette.capacity() <= kPaletteThreshold);
    ASSERT_TRUE(ctx, pixels.capacity() <= kPixelThreshold);

    // Next normal decode should not grow again.
    palette.assign(128u, 0xFFFFu);
    pixels.assign(8u * 1024u, 0x11u);
    normalizeScratch();
    ASSERT_EQ(ctx, tracker.Snapshot(), afterNormalize);
}

// ============================================================================
// Teste 11: modo leak-isolation deve ignorar working set stale para refs
// ============================================================================

void TestLeakIsolationStrictRefsIgnoreStaleWorkingSet(TestContext& ctx)
{
    ctx.currentTest = "LeakIsolationStrictRefsIgnoreStaleWorkingSet";

    struct FamilySlot
    {
        uint16_t familyId = 0;
        uint16_t slot = 0;
        uint16_t refs = 0;
    };

    std::array<FamilySlot, 3> families{{
        {1u, 10u, 0u},
        {2u, 20u, 0u},
        {999u, 30u, 0u}, // stale family from previous segment
    }};

    auto addRefByFace = [&](uint16_t familyId)
    {
        for (auto& f : families)
        {
            if (f.familyId != familyId) continue;
            ++f.refs;
            return;
        }
    };

    // Current frame actually uses only families 1 and 2.
    const std::array<uint16_t, 2> faceFamilies{{1u, 2u}};
    for (const uint16_t fam : faceFamilies)
    {
        addRefByFace(fam);
    }

    ASSERT_EQ(ctx, families[0].refs, static_cast<uint16_t>(1u));
    ASSERT_EQ(ctx, families[1].refs, static_cast<uint16_t>(1u));
    ASSERT_EQ(ctx, families[2].refs, static_cast<uint16_t>(0u)); // stale not referenced
}

// ============================================================================
// Registro de testes
// ============================================================================

struct TestCase
{
    const char*        name = "";
    void (*fn)(TestContext&) = nullptr;
};

} // namespace

int main()
{
    const std::vector<TestCase> tests{
        { "ClearPreservesCapacityDoesNotFreeLwr",      &TestClearPreservesCapacityDoesNotFreeLwr      },
        { "SwapFreeReleasesCapacityOldBugPattern",     &TestSwapFreeReleasesCapacityOldBugPattern     },
        { "EnsureCapacityFloorIsMonotoneIncreasing",   &TestEnsureCapacityFloorIsMonotoneIncreasing   },
        { "PrefetchRendererAllocatedOncePerCycle",     &TestPrefetchRendererAllocatedOncePerCycle     },
        { "SlideBackBufferDoubleSwapIsLwrNeutral",     &TestSlideBackBufferDoubleSwapIsLwrNeutral     },
        { "FamilyCatalogBoundedByActiveWindowAfterMerge", &TestFamilyCatalogBoundedByActiveWindowAfterMerge },
        { "FullSlidePipelineStabilizesLwrAfterWarmup", &TestFullSlidePipelineStabilizesLwrAfterWarmup },
        { "AssignThenEnsureFloorDoesNotExceedFloor",   &TestAssignThenEnsureFloorDoesNotExceedFloor   },
        { "OversizedSegmentComponentVectorsCompactOnRecycle", &TestOversizedSegmentComponentVectorsCompactOnRecycle },
        { "DecodedTextureScratchCompactsWhenOversized", &TestDecodedTextureScratchCompactsWhenOversized },
        { "LeakIsolationStrictRefsIgnoreStaleWorkingSet", &TestLeakIsolationStrictRefsIgnoreStaleWorkingSet },
    };

    TestContext ctx{};
    size_t      passed = 0u;

    for (const auto& t : tests)
    {
        ctx.currentTest = t.name;
        const int failsBefore = ctx.failures;
        try
        {
            t.fn(ctx);
        }
        catch (const std::exception& ex)
        {
            std::cerr << "  [EXCEPTION] " << t.name << ": " << ex.what() << "\n";
            ++ctx.failures;
        }
        catch (...)
        {
            std::cerr << "  [EXCEPTION] " << t.name << ": desconhecida\n";
            ++ctx.failures;
        }

        if (ctx.failures == failsBefore)
        {
            std::cout << "[PASS] " << t.name << "\n";
            ++passed;
        }
        else
        {
            std::cout << "[FAIL] " << t.name << "\n";
        }
    }

    std::cout << "\n" << passed << "/" << tests.size() << " testes passaram";
    if (ctx.assertions > 0)
        std::cout << "  (" << ctx.assertions << " assertions)";
    std::cout << "\n";

    return (ctx.failures == 0) ? 0 : 1;
}
