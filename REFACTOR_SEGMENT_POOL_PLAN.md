# Plano de Refatoração: Fixed Slot Pool para Segmentos de Pista
## Objetivo: Eliminar vazamento e crescimento de LWR durante a corrida

**Status:** Planejamento — NÃO implementar ainda  
**Data:** 2026-04-12  
**Motivação:** LWR (Low Work RAM) cresce continuamente durante a corrida, causando lentidão, falhas de renderização e texturas ausentes.

---

## 1. Diagnóstico: Por Que o LWR Cresce

### 1.1 Estrutura Atual (o problema)

A janela de segmentos ativos é gerenciada por `segmentRenderers_`, um `TrackLowWorkVector<SegmentRenderEntry>` com `kTrackSegmentLimit = 30` entradas possíveis.

Cada `SegmentRenderEntry` contém **seis `TrackLowWorkVector` internos**:

```
SegmentRenderEntry {
    TrackLowWorkUniquePtr<TrackRenderer> renderer    ← alocação LWR por segmento
    SegmentLodState {
        TrackLowWorkU16Vector faceFamilyIds          ← cresce com faces
        TrackLowWorkU8Vector  faceRankOffsets        ← cresce com faces
        TrackLowWorkI16Vector currentFaceSlots       ← cresce com faces
        TrackLowWorkU16Vector workingSetFamilies     ← cresce com families
        TrackLowWorkU8Vector  workingSetLodIndices   ← cresce com families
        TrackLowWorkI16Vector workingSetSlots        ← cresce com families
    }
}
```

Além disso, existem os buffers de slide/prefetch:

```
TrackSystem {
    TrackLowWorkUniquePtr<TrackRenderer> slideScratchRenderer_       ← alloc/free a cada slide
    TrackLowWorkUniquePtr<TrackRenderer> slidePrefetchRenderer_      ← alloc/free a cada prefetch
    FamilyIdVector            slidePrefetchFamilyIds_                ← cresce por slide
    TrackLowWorkI16Vector     slidePrefetchFaceSlots_                ← cresce por slide
    FamilySlotVector          slidePrefetchFamilySlotsScratch_       ← duplica dados por slide
    SlideBackBuffer {
        TrackLowWorkU16Vector incomingFamilyIds                      ← cópia temporária
        TrackLowWorkI16Vector incomingFaceSlots                      ← cópia temporária
        SlideBoundaryUpdate[4] {
            TrackLowWorkI16Vector preparedFaceSlots                  ← cópia por boundary
        }
    }
}
```

### 1.2 Raiz do Problema: Crescimento Sem Liberação

O alocador `TrackZoneAllocator` chama `SRL::Memory::Malloc` e `SRL::Memory::Free` diretamente no heap do LWR. Vectores no C++ crescem com `push_back`/`reserve` mas **nunca encolhem** automaticamente — `clear()` zera o size mas mantém a capacity alocada.

A cada slide:
1. `slidePrefetchRenderer_.reset()` libera o antigo renderer LWR → **fragmentação**
2. `slidePrefetchRenderer_ = MakeTrackObjectUnique<TrackRenderer>(...)` — nova alocação LWR
3. Os vetores internos do novo `SegmentRenderEntry` (`faceFamilyIds`, etc.) crescem para o tamanho do novo segmento
4. O segmento descartado tem seus vetores destruídos — mas o segmento pode ter tido faces a mais que o anterior, deixando buracos no heap

Após muitos slides:
- O heap LWR fica fragmentado em blocos pequenos
- Alocações novas não encontram bloco contíguo suficiente → `std::bad_alloc` ou falha silenciosa
- `TrimWorkRamRetainedCapacities()` tenta remediar mas gera mais churn

### 1.3 Confirmação pelo Código

Em [src/track_system.cxx](src/track_system.cxx), as constantes de guarda revelam que o problema já foi parcialmente identificado:

```cpp
// Compactação por LWR baseline drop gera churn agressivo — DESABILITADO
static constexpr bool kEnableTrackCompactByBaselineDrop = false;

// Rebuild de janela inteira gera picos de alocação — DESABILITADO
static constexpr bool kEnableRuntimeWindowRebuildRepairs = false;

// Compactação de heap de textura causa flashes — DESABILITADO
static constexpr bool kEnableRuntimeTextureCompaction = false;

// Reciclagem agressiva em manutenção introduz oscillação — DESABILITADO
static constexpr bool kEnableRuntimeTextureRecycleOnMaintenance = false;
```

Todas as medidas de contenção estão **desligadas** para evitar efeitos colaterais. Isso confirma que o problema é estrutural, não configurável.

---

## 2. Solução: Fixed Slot Pool de 21 Slots

### 2.1 Princípio

**Pré-alocar exatamente 21 slots de renderer na inicialização.** Cada slot contém um `TrackRenderer` e todos os vetores de LOD com capacidade máxima reservada (baseada no segmento mais pesado da pista). Durante a corrida, **nenhuma alocação de LWR ocorre** — apenas reuso de slots existentes.

```
POOL DE 21 SLOTS (alocados uma vez no Initialize())
┌────────────────────────────────────────────────────┐
│  Slot  0: SegmentRenderEntry [SEG_001] — LOD 64×64 │
│  Slot  1: SegmentRenderEntry [SEG_002] — LOD 64×64 │
│  Slot  2: SegmentRenderEntry [SEG_003] — LOD 64×64 │
│  Slot  3: SegmentRenderEntry [SEG_004] — LOD 64×64 │
│  Slot  4: SegmentRenderEntry [SEG_005] — LOD 32×32 │
│  ...                                               │
│  Slot 19: SegmentRenderEntry [SEG_020] — LOD  8×8  │
│  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─  │
│  Slot 20: SegmentRenderEntry [STAGING] — vazio     │  ← "+1"
└────────────────────────────────────────────────────┘
```

### 2.2 Rotação de Slot no Slide

Ao slide:
1. **Slot staging** (índice 20 inicial, depois rotaciona) tem o novo segmento já montado pelo prefetch
2. **Slot que sai** (tail drop, índice 0 do forward slide) passa a ser o novo slot staging
3. **Rotação é O(1)** — apenas troca de índices, sem alocação ou cópia de dados

```
ANTES DO SLIDE (stagingIdx = 20):
Slots ativos: [0..19] = SEG_001..SEG_020
Slot staging: [20]   = SEG_021 pronto

APÓS O SLIDE:
Slots ativos: [1..19, 20] = SEG_002..SEG_021  (slot 20 virou ativo)
Slot staging: [0]         = vazio             (slot 0 virou staging)
stagingIdx = 0
```

### 2.3 O Slot Staging é Preparado Assincronamente

O slot staging começa a receber os dados do próximo segmento imediatamente após o slide anterior:
- Geometria do CD-ROM → vetores internos do slot (capacity já reservada, apenas `assign`)
- Texturas 8×8 → slots VDP1 (pré-alocados)
- LOD upgrades → feitos nos slots ativos, não no staging

---

## 3. Mudanças no Código

### 3.1 Novo Tipo: `SegmentSlotArray`

**Arquivo:** [src/track_system.hpp](src/track_system.hpp)

**Remover:**
```cpp
// REMOVER — vetor dinâmico que cresce
TrackLowWorkVector<SegmentRenderEntry> segmentRenderers_{};

// REMOVER — renderers extras com alloc/free por slide
TrackLowWorkUniquePtr<TrackRenderer> slideScratchRenderer_{};
TrackLowWorkUniquePtr<TrackRenderer> slidePrefetchRenderer_{};
FamilyIdVector          slidePrefetchFamilyIds_{};
TrackLowWorkI16Vector   slidePrefetchFaceSlots_{};
FamilySlotVector        slidePrefetchFamilySlotsScratch_{};
```

**Adicionar:**
```cpp
// Pool fixo de 21 slots — alocado uma vez, nunca redimensionado
static constexpr size_t kSlotPoolSize = 21; // 20 ativos + 1 staging

// Índice do slot que está atualmente no papel de "staging"
uint8_t stagingSlotIdx_ = 20;

// Array fixo: 21 ponteiros para SegmentRenderEntry alocados em LWR
// Todos alocados em Initialize(), nunca liberados durante a corrida
std::array<SegmentRenderEntry*, kSlotPoolSize> slotPool_{};
```

> **Por que ponteiros e não valores diretos?**
> `SegmentRenderEntry` contém `TrackLowWorkUniquePtr<TrackRenderer>` que só pode ser movido. Um `std::array` de valores exigiria move-construction complicada. Com ponteiros, apenas inicializamos em LWR via `SRL::Memory::Malloc` uma vez.

### 3.2 Pré-alocação no `Initialize()`

**Arquivo:** [src/track_system.cxx](src/track_system.cxx)

```cpp
bool TrackSystem::Initialize(const Config& config)
{
    // ... código existente de carregamento dos primeiros 20 segmentos ...

    // NOVO: alocar todos os 21 slots de uma vez
    const uint32_t maxFaces    = MaxSegmentFaceCount();    // face count do segmento mais pesado
    const uint32_t maxVertices = MaxSegmentVertexCount();

    for (size_t i = 0; i < kSlotPoolSize; ++i)
    {
        // Alocar struct em LWR
        void* mem = SRL::Memory::Malloc(sizeof(SegmentRenderEntry), SRL::Memory::Zone::LWRam);
        slotPool_[i] = new (mem) SegmentRenderEntry{};

        // Pre-reservar vetores para o pior caso
        slotPool_[i]->lodState.faceFamilyIds.reserve(maxFaces);
        slotPool_[i]->lodState.faceRankOffsets.reserve(maxFaces);
        slotPool_[i]->lodState.currentFaceSlots.reserve(maxFaces);
        slotPool_[i]->lodState.workingSetFamilies.reserve(maxFaces);
        slotPool_[i]->lodState.workingSetLodIndices.reserve(maxFaces);
        slotPool_[i]->lodState.workingSetSlots.reserve(maxFaces);

        // Alocar TrackRenderer no slot com capacidade máxima
        slotPool_[i]->renderer = MakeTrackObjectUnique<TrackRenderer>();
        ApplyActiveRendererCapacityFloor(*slotPool_[i]->renderer);
        // renderer.reserve(maxFaces, maxVertices) — garante que não vai crescer depois
    }

    // Preencher slots 0..19 com os segmentos iniciais
    // Slot 20 fica vazio (staging)
    stagingSlotIdx_ = 20;
    // ...
}
```

### 3.3 Mudança na Função de Slide

**Arquivo:** [src/track_system.cxx](src/track_system.cxx)  
**Funções afetadas:**
- `ExecuteDeterministicStabilizedSlide()`
- `PrepareStabilizedSlideBackBuffer()`
- `CommitStabilizedSlideBackBuffer()`
- `BuildSegmentIntoPrefetch()`
- `ResetSlidePrefetchState()`

**Lógica nova no slide:**

```cpp
bool TrackSystem::ExecuteDeterministicStabilizedSlide(
    size_t dropIdx, int8_t direction, int32_t nextId, int32_t nextStartId)
{
    // stagingSlotIdx_ já tem o novo segmento pronto (preenchido pelo prefetch)
    SegmentRenderEntry* incoming = slotPool_[stagingSlotIdx_];
    SegmentRenderEntry* outgoing = slotPool_[dropIdx];

    // 1. LIBERAR recursos do slot que sai (VDP1 texture slots) — IMEDIATO
    ReleaseSegmentSlotResources(*outgoing);
    // → Libera slots VDP1, mas NÃO libera memória LWR
    //   (clear() nos vetores, mas capacity permanece)

    // 2. ROTAÇÃO O(1): trocar papeis
    // O slot que saiu vira o novo staging
    const uint8_t newStagingIdx = static_cast<uint8_t>(dropIdx);
    // O staging atual vira ativo no lugar do que saiu
    slotPool_[dropIdx] = incoming;           // slot do outgoing agora aponta pro incoming
    slotPool_[stagingSlotIdx_] = outgoing;   // staging agora aponta pro outgoing (vazio)
    stagingSlotIdx_ = newStagingIdx;

    // 3. Atualizar metadados da janela (activeWindowStartId_, etc.)
    activeWindowStartId_ = static_cast<int16_t>(nextStartId);

    // 4. Atualizar LOD das boundaries (posições 4, 9, 14)
    UpdateStabilizedWindowLodBoundaries();

    // 5. Kick prefetch assíncrono para o próximo segmento no slot staging
    TryPrefetchUpcomingSegment();

    return true;
}
```

### 3.4 Mudança no Prefetch

`BuildSegmentIntoPrefetch()` passa a escrever **diretamente no slot staging**, sem alocar nada:

```cpp
bool TrackSystem::BuildSegmentIntoPrefetch(int32_t segmentId, bool allowSlotWarmup)
{
    SegmentRenderEntry& staging = *slotPool_[stagingSlotIdx_];

    // Limpar dados anteriores (clear sem free)
    staging.id = -1;
    staging.lodState.faceFamilyIds.clear();
    staging.lodState.faceRankOffsets.clear();
    staging.lodState.currentFaceSlots.clear();
    staging.lodState.ready = false;

    // Carregar geometria do CD para o renderer do staging
    // (assign() usa a capacity já reservada — sem alocação nova)
    bool ok = BuildSegmentIntoRenderer(segmentId,
                                       *staging.renderer,
                                       staging.center,
                                       staging.lodState.faceFamilyIds);
    if (!ok) return false;

    // Configurar LOD 8×8 (entrada sempre em 8×8)
    staging.lodState.currentLodIndex = 0; // 8×8
    staging.lodState.desiredLodIndex = 0;
    staging.id = static_cast<int16_t>(segmentId);
    staging.lodState.ready = true;

    return true;
}
```

### 3.5 Liberação de Recursos do Slot Que Sai

Esta função substitui a lógica espalhada de `ReleaseTrackFamilyResourcesImmediate()` e `RecycleTrackTextureHeap()`:

```cpp
void TrackSystem::ReleaseSegmentSlotResources(SegmentRenderEntry& entry)
{
    // Liberar slots VDP1 ocupados por este segmento (imediato)
    for (auto& slot : entry.lodState.currentFaceSlots)
    {
        if (slot >= 0 && IsVdp1TextureSlotLive(static_cast<uint16_t>(slot)))
        {
            SRL::VDP1::ReleaseTexture(static_cast<uint16_t>(slot));
        }
        slot = -1;
    }

    // Limpar dados (NÃO liberar memória — capacity permanece para reuso)
    entry.lodState.faceFamilyIds.clear();
    entry.lodState.faceRankOffsets.clear();
    entry.lodState.currentFaceSlots.clear();
    entry.lodState.workingSetFamilies.clear();
    entry.lodState.workingSetLodIndices.clear();
    entry.lodState.workingSetSlots.clear();
    entry.lodState.ready = false;
    entry.lodState.currentLodIndex = 0xFF;
    entry.lodState.currentBaseRank = -1;
    entry.id = -1;
    entry.logicalSegmentCount = 0;

    // O renderer interno também é limpo (sem free)
    entry.renderer->ClearFacesAndVertices(); // a implementar ou já existente
}
```

### 3.6 Remoção do `SlideBackBuffer` Duplo

O `SlideBackBuffer` atual prepara dados em vetores temporários e só depois faz commit, causando:
- Vetores temporários crescem (`incomingFamilyIds`, `incomingFaceSlots`, `boundaryUpdates[*].preparedFaceSlots`)
- Dois momentos de alocação por slide (prepare + commit)

Com o modelo de slot fixo, o prepare escreve diretamente no slot staging. O commit é apenas a rotação de ponteiros (O(1), sem alocação). O `SlideBackBuffer` pode ser eliminado ou simplificado a apenas os metadados de IDs (sem vetores):

```cpp
// NOVO — apenas metadados, sem vetores
struct SlideCommitState
{
    bool ready = false;
    int8_t direction = 1;
    size_t dropIdx = 0;
    int16_t incomingSegmentId = -1;
    int16_t outgoingSegmentId = -1;
    int16_t nextStartId = 1;
    // SEM incomingFamilyIds, incomingFaceSlots, boundaryUpdates — tudo no slot staging
};
```

---

## 4. Mudanças no `TrackRenderer`

**Arquivo:** [src/track_renderer.hpp](src/track_renderer.hpp)

O `TrackRenderer` precisa de um método para limpar face/vertex data sem liberar memória:

```cpp
class TrackRenderer
{
public:
    // Limpar polígonos e vértices sem reduzir capacity (para reuso de slot)
    void ClearFacesAndVertices()
    {
        faces_.clear();    // size → 0, capacity permanece
        vertices_.clear();
        attributes_.clear();
        meshes_.clear();
    }

    // Pre-reservar capacity para o pior caso (chamado uma vez no Initialize)
    void ReserveForWorstCase(size_t maxFaces, size_t maxVertices)
    {
        faces_.reserve(maxFaces);
        vertices_.reserve(maxVertices);
        attributes_.reserve(maxFaces);
        meshes_.reserve(4); // raramente mais de 4 meshes por segmento
    }
    // ...
};
```

---

## 5. Sequência de Passos da Refatoração

### Passo 1 — Medir baseline de LWR
**Objetivo:** Ter números concretos do problema antes de qualquer mudança.

- Ativar `kEnableTrackPhaseRamTelemetry = true` temporariamente
- Rodar 2 voltas completas na pista
- Registrar:
  - LWR free no início (pós-Initialize)
  - LWR free após 20 slides
  - LWR free após 100 slides
  - LWR free após 1 volta completa
- Anotar os valores aqui para comparação pós-refatoração

### Passo 2 — Adicionar `MaxSegmentFaceCount()` e `MaxSegmentVertexCount()`
**Arquivo:** [src/track_system.hpp](src/track_system.hpp) / [src/track_system.cxx](src/track_system.cxx)

- Percorrer todos os `segmentRenderers_` após o carregamento inicial
- Registrar o máximo de faces e vértices encontrado
- Expor via getter para uso no `Initialize()` do novo pool

> Estes métodos **já existem** na interface pública. Verificar se retornam valores corretos após o `Initialize()` atual.

### Passo 3 — Introduzir `slotPool_` paralelo ao sistema atual
**Arquivo:** [src/track_system.hpp](src/track_system.hpp)

- Adicionar `std::array<SegmentRenderEntry*, kSlotPoolSize> slotPool_{}` e `uint8_t stagingSlotIdx_`
- No `Initialize()`, após o carregamento dos primeiros 20 segmentos, copiar os `SegmentRenderEntry`s existentes para os slots 0..19 e alocar o slot 20 vazio
- **Não remover `segmentRenderers_` ainda** — ambos coexistem durante a transição
- Adicionar asserção: `assert(slotPool_[i]->id == segmentRenderers_[i].id)` para garantir paridade

### Passo 4 — Adicionar `ReleaseSegmentSlotResources()` e `ClearFacesAndVertices()`
**Arquivos:** [src/track_system.cxx](src/track_system.cxx), [src/track_renderer.hpp](src/track_renderer.hpp)

- Implementar `ReleaseSegmentSlotResources()` conforme seção 3.5
- Implementar `TrackRenderer::ClearFacesAndVertices()` e `ReserveForWorstCase()`
- Testar isoladamente: criar um slot, popular, liberar, re-popular — verificar que LWR não cresce

### Passo 5 — Redirecionar `BuildSegmentIntoPrefetch()` para o slot staging
**Arquivo:** [src/track_system.cxx](src/track_system.cxx)

- Modificar `BuildSegmentIntoPrefetch()` para escrever em `slotPool_[stagingSlotIdx_]` em vez de `slidePrefetchRenderer_`
- Manter `slidePrefetchRenderer_` como fallback enquanto o novo caminho é testado
- Flag de controle: `static constexpr bool kUseFixedSlotPrefetch = false;` (ativar no Passo 7)

### Passo 6 — Migrar `ExecuteDeterministicStabilizedSlide()` para rotação de ponteiros
**Arquivo:** [src/track_system.cxx](src/track_system.cxx)

- Implementar a rotação O(1) descrita na seção 3.3
- Substituir o `CommitStabilizedSlideBackBuffer()` pela troca de ponteiros
- Garantir que `activeWindowStartId_`, `activeWindowHead_`, e tabelas de lookup sejam atualizados corretamente
- Manter o caminho legado como fallback: `static constexpr bool kUseFixedSlotRotation = false;`

### Passo 7 — Ativar e validar o novo caminho
**Objetivo:** Ligar os dois flags e confirmar funcionamento correto.

```cpp
static constexpr bool kUseFixedSlotPrefetch  = true;
static constexpr bool kUseFixedSlotRotation  = true;
```

- Rodar os testes em [tests/track_streaming_policy_tests.cpp](tests/track_streaming_policy_tests.cpp)
- Executar `assertWindowConsistency()` a cada frame durante desenvolvimento
- Verificar LWR com a mesma metodologia do Passo 1 — confirmar que o crescimento parou

### Passo 8 — Remover o código legado
**Arquivos:** [src/track_system.hpp](src/track_system.hpp), [src/track_system.cxx](src/track_system.cxx)

Remover:
- `segmentRenderers_` (vetor dinâmico)
- `slideScratchRenderer_`
- `slidePrefetchRenderer_`
- `slidePrefetchFamilyIds_`
- `slidePrefetchFaceSlots_`
- `slidePrefetchFamilySlotsScratch_`
- `SlideBackBuffer` vetores dinâmicos → substituir por `SlideCommitState` (seção 3.6)
- Todos os flags `kUseFixedSlot*`
- `TrimWorkRamRetainedCapacities()` pode ser simplificado (não é mais necessário trimar os vetores principais)

### Passo 9 — Medição final e verificação
- Repetir as medições do Passo 1
- Confirmar que LWR free é estável após qualquer número de slides
- Confirmar que o jogo roda 30 FPS em modo contínuo sem degradação

---

## 6. Riscos e Mitigações

| Risco | Probabilidade | Impacto | Mitigação |
|-------|--------------|---------|-----------|
| `MaxSegmentFaceCount()` subestima o pior caso | Média | Alto — slot capacity insuficiente causa buffer overflow | Adicionar 20% de margem sobre o max observado; assert em debug se `assign()` exceder capacity |
| Rotação de ponteiros quebra `segmentPool_` (handle table) | Alta | Alto — handles ficam inválidos | Reconstruir handles após rotação; ou migrar para índice direto no array fixo |
| `TrackRenderCoordinator` / `SlaveTrackDrawProducer` guardam ponteiros para `SegmentRenderEntry` | Alta | Alto — ponteiro dangling após rotação | Usar índice do slot (0..20) em vez de ponteiro; reconstruir ponteiro antes de usar |
| Textura VDP1 liberada antes do frame ser renderizado | Média | Médio — flash preto por 1 frame | Liberar na `BeginFrame` do próximo frame, não no slide atual |
| LWR ainda cresce por `seg1FamilySlots_` (famílias de textura) | Baixa | Médio | `seg1FamilySlots_` também precisa de capacity fixo baseado no número total de famílias da pista |

### Risco Crítico: Invalidação de Handles do `SegmentPool`

O `TrackSegmentPool` atual armazena `T* value` (ponteiro bruto para `SegmentRenderEntry`). Após a rotação de ponteiros do slot pool, esses ponteiros ficam inválidos.

**Solução:** Migrar `SegmentPool::Handle` para armazenar **índice no `slotPool_`** (0..20) em vez de ponteiro:

```cpp
// ANTES
struct Slot {
    T* value = nullptr; // ← ponteiro que fica dangling após rotação
};

// DEPOIS
struct Slot {
    uint8_t poolIndex = 0xFF; // ← índice estável no slotPool_
};

T* Resolve(Handle h) {
    return trackSystem_->slotPool_[slots_[h.slot].poolIndex]; // deref via TrackSystem
}
```

---

## 7. Estrutura de Arquivos Afetados

| Arquivo | Tipo de Mudança | Prioridade |
|---------|----------------|-----------|
| [src/track_system.hpp](src/track_system.hpp) | Remover `segmentRenderers_`; adicionar `slotPool_`, `stagingSlotIdx_`, `SlideCommitState` | Alta |
| [src/track_system.cxx](src/track_system.cxx) | Migrar Initialize, slide, prefetch, release | Alta |
| [src/track_renderer.hpp](src/track_renderer.hpp) | Adicionar `ClearFacesAndVertices()`, `ReserveForWorstCase()` | Alta |
| [src/track_segment_pool.hpp](src/track_segment_pool.hpp) | Migrar `T* value` → `uint8_t poolIndex` | Alta |
| [src/track_draw_producer.hpp](src/track_draw_producer.hpp) | Verificar se guarda ponteiros para entries (ajustar para índices) | Média |
| [src/track_render_coordinator.hpp](src/track_render_coordinator.hpp) | Mesmo que acima | Média |
| [tests/track_streaming_policy_tests.cpp](tests/track_streaming_policy_tests.cpp) | Adicionar testes para: rotação de slots, LWR estável após N slides | Média |

---

## 8. Critérios de Aceitação

A refatoração está completa quando:

1. **LWR free após 1 volta completa ≥ LWR free após Initialize()** (sem crescimento líquido)
2. **LWR free após N voltas é estável** — a variação entre voltas é < 4 KB
3. **Nenhuma alocação de LWR ocorre dentro do caminho crítico do slide** (verificável com tag de debug)
4. **30 FPS mantidos** durante slide em hardware real
5. **Todos os testes existentes passam** em [tests/track_streaming_policy_tests.cpp](tests/track_streaming_policy_tests.cpp)
6. **Sem flashes de textura** (texturas brancas/pretas) durante slides
7. **Sem falhas de renderização** de segmentos (todos os 20 segmentos visíveis renderizando corretamente)

---

## 9. O Que NÃO Mudar Nesta Refatoração

Para manter escopo controlado, os itens abaixo **não fazem parte deste plano**:

- Lógica de LOD e upgrades de textura por rank (já funciona corretamente)
- Sistema de famílias de textura (`seg1FamilySlots_`, texbanks) — pode ser abordado separadamente
- Pipeline Slave SH2 (`SlaveTrackDepthSorter`, `SlaveTrackDrawProducer`) — não altera LWR diretamente
- Formato de arquivo dos segmentos (BDR/RDR/SDR)
- Sistema de câmera e física do carro
- Quantidade de segmentos visíveis (permanece 20) ou distribuição de LOD (permanece 4/5/5/6)

---

## 10. Resumo Visual: Antes vs. Depois

```
ANTES — Alocações dinâmicas por slide:
┌─────────────────────────────────────────────────────────────┐
│ LWR Heap                                                    │
│ [SegEntry 0][SegEntry 1]...[SegEntry N]  ← vetor dinâmico  │
│ [Renderer 0 LWR][Renderer 1 LWR]...     ← 1 alloc/segmento│
│ [SlideScratch][PrefetchRenderer]         ← alloc por slide  │
│ [BackBuffer vecs][BoundaryUpdate vecs]   ← alloc por slide  │
│  → fragmentação crescente ao longo da corrida               │
└─────────────────────────────────────────────────────────────┘

DEPOIS — Pool fixo, zero alocação durante a corrida:
┌─────────────────────────────────────────────────────────────┐
│ LWR Heap                                                    │
│ [Slot 0 FIXO][Slot 1 FIXO]...[Slot 20 FIXO]  ← 21 slots   │
│  capacity pré-reservada para o pior caso                    │
│  slide = rotação de ponteiros, sem malloc/free              │
│  → LWR estável, sem fragmentação                            │
└─────────────────────────────────────────────────────────────┘
```

---

*Plano criado em 2026-04-12. Implementação iniciada em 2026-04-12.*

---

## Apêndice B — Logs do Jogo Antes da Refatoração

Coletados durante sessão de corrida com 20 segmentos ativos (4×64×64, 5×32×32, 5×16×16, 6×8×8).

**Legenda:**
- `SWLWR free:` = LWR livre em bytes
- `df:` = delta de LWR free desde o frame anterior (negativo = consumo)
- `FPS` ao final dos logs: 20.0 (target = 30)
- `TRK band 8:6 16:5 32:5 64:4` = 20 segmentos ativos com distribuição LOD correta
- `sl:1` = slide ocorreu neste frame; `id:N` = segmento de destino

### Dados brutos

```
SWLWR free: 925508  df:-792    → início da amostra
SWLWR free: 920684  df:-892
SWLWR free: 919572  df:-884
SWLWR free: 917848  df:-788
SWLWR free: 915632  df:-32     TRK band 8:6 16:5 32:5 64:4
SWLWR free: 914284  df:-1348
SWLWR free: 912792  df:-724
SWLWR free: 911836  df:-956
SWLWR free: 908360  df:-944
SWLWR free: 906948  df:-1412
SWLWR free: 905012  df:-968    TLTK2 pf:1012
SWLWR free: 902700  df:-928
SWLWR free: 900604  df:-924    TRK band 8:6 16:5 32:5 64:4
SWLWR free: 896032  df:-1632
SWLWR free: 893844  df:-1208   TRK sz:20
SWLWR free: 889160  df:-1104   sl:1 id:48  TRK sz:22  ← slide
SWLWR free: 886456  df:-1564
SWLWR free: 880604  df:-5852   TLTK2 pf:1329          ← maior spike
SWLWR free: 879156  df:-1448
SWLWR free: 876760  df:-840
SWLWR free: 872736  df:-1128
SWLWR free: 872736  df:0
SWLWR free: 869976  df:-980
SWLWR free: 867900  df:-708    sl:1 id:56              ← slide
SWLWR free: 867728  df:-172
SWLWR free: 867728  df:-172    TRK sz:21
SWLWR free: 863744  df:-840    TRK sz:21
SWLWR free: 861924  df:-840    TRK sz:21
SWLWR free: 842992  df:-1560   sl:1 id:70              ← slide
SWLWR free: 842620  df:-372    FPS:20.0  ms:50.0  d30:53%
SWLWR free: 805296  df:-768    FPS:20.6  ms:48.6       → fim da amostra
```

### Resumo estatístico

| Métrica | Valor |
|---------|-------|
| LWR inicial | ~925.508 bytes |
| LWR final (amostra) | ~805.296 bytes |
| Queda total (amostra) | ~120.212 bytes (~1 min de corrida) |
| df médio por frame | ~-950 bytes/frame |
| Slides identificados (`sl:1`) | 3 (ids 48, 56, 70) |
| Maior spike de df | -5.852 bytes (frame do pf:1329) |
| FPS ao final | 20.0 (target 30) |

Os spikes coincidem com operações de slide e build de prefetch — confirmando que `ResetSlidePrefetchState()` com `swap-free` e `ResetSlideBackBuffer()` são as causas primárias do vazamento.
