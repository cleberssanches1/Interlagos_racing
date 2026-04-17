# Logica de Renderizacao de Segmentos

## Objetivo
Este documento descreve a logica completa da renderizacao de segmentos da pista (`TrackSystem`), com foco no pipeline por frame, no fluxo de slide/prefetch e no controle de memoria (HWR/LWR).

## Estruturas Principais
- `TrackSystem::SegmentRenderEntry`
- `TrackSystem::SegmentRenderEntry::SegmentLodState`
- `TrackRenderer`
- `seg1FamilySlots_` (cache de familias e slots por LOD)
- `segmentRenderers_` (janela ativa de segmentos)
- `segmentHandles_` + `segmentPool_` (handles estaveis para sort/draw)
- `slidePrefetch*` e `slideBackBuffer_` (pipeline de entrada do proximo segmento)

## Pipeline Geral (por frame)

### 1. `BeginFrame(uint32_t frameId)`
- Reseta contadores de runtime/telemetria.
- Calcula orcamento de prefetch do frame.
- Ajusta cooldowns de manutencao/LOD.

### 2. `RenderFrame(...)`
Fluxo principal:
1. `TrackMaintenanceStage::RunInitial`
2. `TrackWindowStage::Run` -> chama `RunWindowStage`/`UpdateActiveSegmentWindowForPosition`
3. `TrackPrefetchStage::RunCompaction`
4. `TrackPrefetchStage::RunPrefetch` -> chama `RunPrefetchStage`
5. `TrackMaintenanceStage::RunPostSlide`
6. `TrackMaintenanceStage::RunLegacy`
7. `BuildAndApplyFramePlanStage`
8. `TrackLodStage::RunRecovery`
9. `BuildOrderedHandlesStage`
10. `TrackWorkingSetStage::Run` -> `RunWorkingSetStage`
11. `RunDrawStage`

### 3. `EndFrame()`
- Fecha telemetria/coordenador.
- Executa `ReleaseUnusedFamilyResourcesEndFrame()`.
- Faz flush de slots aposentados pendentes.
- Atualiza breakdown de LWR e validacoes de invariantes.

## Mapa Funcao a Funcao (renderizacao de segmentos)
- `RunWindowStage`: orquestra atualizacao da janela ativa e prepara slide quando necessario.
- `UpdateActiveSegmentWindowForPosition`: decide deslocamento da janela com base no segmento observado do carro.
- `SlideActiveSegmentWindow`: executa N passos de slide com guardas de memoria.
- `ExecuteDeterministicStabilizedSlide`: caminho principal de slide estabilizado; reaproveita prefetch e comita novo segmento na janela.
- `PrepareStabilizedSlideBackBuffer`: prepara update de fronteira para commit em duas fases.
- `CommitStabilizedSlideBackBuffer`: aplica entrada preparada no estado ativo.
- `TryPrefetchUpcomingSegment`: escolhe proximo segmento candidato de prefetch.
- `BuildSegmentIntoPrefetch`: preconstroi metadata/renderer do proximo segmento.
- `BuildSegmentIntoSlideScratch`: reaproveita renderer prefetch quando pronto ou faz fallback sincrono.
- `BuildSegmentIntoRenderer`: converte blob de runtime em `TrackRenderer` pronto.
- `RunWorkingSetStage`: ajusta familias/LOD efetivos usados pelo draw do frame.
- `BuildTrackFamilyLodSlots`: consolida familias usadas na janela para resolver slots por LOD.
- `MergeCurrentWindowFamilies`: merge incremental das familias vivas para reduzir rebuild caro.
- `BuildSegmentHandleTable`: reconstrucao de handles estaveis usados no sort/draw.
- `BuildVisibleSegmentOrder`: gera ordem visivel candidata para render.
- `BuildOrderedHandlesStage`: integra ordenacao estabilizada e producer/coordinator.
- `RunDrawStage`: render efetivo dos segmentos visiveis.
- `RenderVisibleSegmentOrderStabilized`: draw com validacao/repair de renderer e face slots.
- `ReleaseUnusedFamilyResourcesEndFrame`: libera recursos nao usados no frame e recicla slots.
- `TrimWorkRamRetainedCapacities`: compacta capacidades ociosas de vetores para recuperar RAM.
- `PrimeRuntimeScratchCapacities`: define pisos de capacidade para scratch/caches no boot.
- `ValidateStabilizedWindowInvariants`: detecta inconsistencias de janela/familias/prefetch.

## Janela Ativa e Slide

### `UpdateActiveSegmentWindowForPosition(...)`
- Decide se precisa deslizar janela de segmentos com base no segmento observado do carro.
- Encaminha para `SlideActiveSegmentWindow(stepCount, direction)`.

### `SlideActiveSegmentWindow(...)`
- Modo estabilizado: usa slide deterministico incremental.
- Para cada passo:
  - Resolve `nextId`.
  - Verifica pressao de memoria.
  - Executa `ExecuteDeterministicStabilizedSlide(...)`.

### `ExecuteDeterministicStabilizedSlide(...)`
- Garante que slots aposentados pendentes sejam drenados antes do novo tail.
- Usa prefetch se disponivel; senao build sincrono de fallback.
- Prepara segmento de entrada em scratch persistente.
- Atualiza familias/slots de fronteira quando necessario.
- Comita estado e invalida caches/lookup da janela.

### Caminho alternativo de duas fases
- `PrepareStabilizedSlideBackBuffer(...)`
- `CommitStabilizedSlideBackBuffer()`

## Prefetch

### `TryPrefetchUpcomingSegment()`
- Determina o proximo segmento esperado da janela.
- Aplica cooldown e guardas de memoria.
- Dispara `BuildSegmentIntoPrefetch(nextId, false)`.

### `BuildSegmentIntoPrefetch(segmentId, allowSlotWarmup)`
- Fase 1: metadata de familias e centro.
- Fase 2: pre-build do renderer no scratch.
- Mantem `slidePrefetchSegmentId_`, `slidePrefetchFamilyIds_`, `slidePrefetchRendererReady_`.

## Build de Renderer

### `BuildSegmentIntoRenderer(...)`
- Carrega blob runtime (`RDR`/`SDR`) e instancia `TrackRenderer`.
- Configura defaults via `ConfigureStreamedRendererDefaults`.

### `BuildSegmentIntoSlideScratch(...)`
- Reusa renderer de prefetch quando pronto.
- Fallback: build sincrono para scratch renderer.

## LOD e Slots por Face

### `BuildTrackFamilyLodSlots(...)`
- Monta conjunto de familias usadas na janela ativa.

### `RebuildSegmentFaceSlotsForLod(...)` / `RebuildSegmentFaceSlotsForBaseRank(...)`
- Resolve slots VDP1 por face para o LOD/rank alvo.

### `RefreshFamilyWorkingSet(...)`
- Reconstroi referencias de trabalho por familia/LOD.

## Draw

### `BuildOrderedHandlesStage(...)`
- Constroi ordem de render (depth sort estabilizado + producer/coordinator).

### `RunDrawStage(...)`
- Chama `RenderVisibleSegmentOrder(...)`.
- Aplica guardas de integridade (`TryRepairRendererState`).
- Submete draw calls de cada segmento.

## Memoria e Telemetria

### Contadores exibidos
- `WLWR free/df`: LWR livre e delta por frame.
- `LWC1/LWC2`: breakdown de bytes (renderers, slot state, working set, family cache, transient, metadata).
- `LWP rst/bld/prp/cmt/mrg/hnd/drw/efr`: delta LWR por estagio instrumentado.

### Ponto critico observado
Os logs mostravam drift de LWR com `LWP efr` negativo recorrente, apontando para churn em `ReleaseUnusedFamilyResourcesEndFrame()` (fim de frame), onde filas de slots reciclaveis cresciam/encolhiam ao longo da corrida.

### Camera e estado por frame
- `CameraSystem` mantem estado pequeno e fixo (vetores/escalars), sem crescimento dinamico por frame.
- O consumo relevante vinha do contexto de rota em `GameLoopSystem` (buffers `autoLapRoute*`/`autoLapGuideLines_`) quando o caminho path-guided era preparado.
- Refatoracao aplicada: quando `kPathGuidedChaseEnabled=false`, o game loop nao constroi contexto de rota para camera e libera buffers de rota quando auto-lap desliga.

## Refatoracao Aplicada (neste ciclo)

### 1. Piso de capacidade para filas de slots reciclaveis
- Adicionado piso persistente para:
  - `g_trackReusableTextureSlots`
  - `g_trackPendingRetiredTextureSlots`
- Inicializacao do piso no reset e reforco no prime de runtime.
- Objetivo: evitar realloc incremental em fim de frame.

### 2. Trim sem oscillation nessas filas
- `TrimWorkRamRetainedCapacities()` agora respeita piso dessas filas, inclusive sob pressao.
- Objetivo: evitar ciclo `trim -> realloc` durante corrida longa.

### 3. Piso de capacidade para cache de familias
- Novo campo: `familySlotCapacityFloor_`.
- Definido em `PrimeRuntimeScratchCapacities()` a partir de `maxFamilyCount` do runtime pack (com piso minimo).
- Aplicado em:
  - `BuildTrackFamilyLodSlots()`
  - reservas de `seg1FamilySlots_`, `familyMergeCurrentWindowScratch_`, `slidePrefetchFamilySlotsScratch_`
  - trim de vetores de familia

## Plano de Acao de Refatoracao (proximas iteracoes)
1. Instrumentar serie temporal de:
   - `g_trackReusableTextureSlots.size/capacity`
   - `g_trackPendingRetiredTextureSlots.size/capacity`
   - `seg1FamilySlots_.size/capacity`
2. Validar soak de longa duracao com foco em `LWP efr` e `WLWR df`.
3. Se ainda houver drift, migrar filas de slots reciclaveis para estrutura fixa (array + count) sem alocacao dinamica.
4. Consolidar caminho estabilizado (remover caminhos legacy nao usados em runtime final) para reduzir superficie de churn.

## Arquivos-Chave
- `src/track_system.hpp`
- `src/track_system.cxx`
- `src/track_renderer.hpp`
- `src/game_loop_system.hpp`
