# Track Streaming Runtime Diagram

## Objetivo

Este documento descreve o fluxo real do runtime de renderizacao da pista no codigo atual.
O ponto principal e este:

- a janela ativa continua sendo o contrato de `20` segmentos
- o modo de estabilizacao e o caminho ativo no codigo atual
- `TrackRenderCoordinator` e `TrackDrawProducer` existem e sao inicializados, mas o hot path de render estabilizado os contorna

## Estado atual do runtime

O codigo hoje opera com estas regras observaveis:

- `kEnableTrackRuntimeStabilization = true`
- `kEnableDeterministicStabilizedSlide = true`
- `TrackSystem::RenderFrame()` executa manutencao, slide, prefetch, working-set refresh e desenho
- `RenderVisibleSegmentOrder()` faz render direto na trilha estabilizada
- `TrackDrawProducer` e `TrackRenderCoordinator` entram no caminho de fallback quando a estabilizacao esta desligada

Em outras palavras:

- no modo estabilizado, o coordenador nao e o motor principal de render
- no modo nao estabilizado, o par `TrackDrawProducer` + `TrackRenderCoordinator` volta a ser usado para preparar e executar a fila de desenho

## Papel de cada componente

| Componente | Papel real no codigo atual |
| --- | --- |
| `TrackSystem::RenderFrame` | Orquestra o frame: manutencao de RAM, slide da janela, prefetch, recuperacao de LOD, working-set e render final |
| `RenderVisibleSegmentOrder` | Decide a ordem visivel e executa o desenho; no modo estabilizado, faz render direto por segmento |
| `TrackDrawProducer` | Mantem a lista de desenho para o caminho nao estabilizado; nao participa do hot path estabilizado |
| `TrackRenderCoordinator` | Enforce de budget e staging de chunks; usado no fallback nao estabilizado |

## Fluxo real por frame

O fluxo atual e este:

1. `RenderFrame(renderTrack, trackOffset, lightDirection, cameraLocation, carWorldPosition)`
2. Se `!ready_`, sai sem render
3. Inicia medicao de frame com `Sh2FrtProfiler`
4. Se a estabilizacao esta ativa, roda `RunWorkRamMaintenance(false)` antes do slide
5. Atualiza a janela com `UpdateActiveSegmentWindowForPosition(...)`
6. Se houve slide e ha pressao de memoria, pode rodar `RunWorkRamMaintenance(true)` depois do slide
7. Se a estabilizacao estiver desligada, roda `RunWorkRamMaintenance(windowSlid)`
8. O bloco de recuperacao de LOD existe, mas com `kEnableDeterministicStabilizedSlide = true` ele fica efetivamente desativado no build atual
9. Se a estabilizacao estiver desligada, monta `orderedHandles` com `BuildVisibleSegmentOrder(...)`
10. Refresca o working-set quando `familyWorkingSetDirty_` esta marcado
11. Chama `RenderVisibleSegmentOrder(...)`
12. Fecha o frame com `EndFrame()`, telemetria e limpeza de recursos adiados

## Diagrama atualizado

```mermaid
flowchart TD
    A[TrackSystem::RenderFrame] --> B{ready_ and renderTrack?}
    B -- nao --> Z[Return]
    B -- sim --> C[Start frame timers and RAM snapshots]
    C --> D[RunWorkRamMaintenance(false) if stabilization is on]
    D --> E[UpdateActiveSegmentWindowForPosition]
    E --> F{Window slid?}
    F -- sim --> G{Memory pressure?}
    G -- sim --> H[RunWorkRamMaintenance(true)]
    G -- nao --> I[Skip post-slide maintenance]
    F -- nao --> J[TryPrefetchUpcomingSegment / PrewarmNextSegmentLod8 / BoundaryLods]
    H --> J
    I --> J
    J --> K{Deterministic stabilized slide enabled?}
    K -- sim --> L[ExecuteDeterministicStabilizedSlide]
    K -- nao --> M[Legacy slide back-buffer path]
    L --> N[RefreshFamilyWorkingSet if dirty]
    M --> N
    N --> O[RenderVisibleSegmentOrder]
    O --> P{Stabilization on?}
    P -- sim --> Q[Sort visible entries far-first and render direct]
    P -- nao --> R{Coordinator ready?}
    R -- nao --> S[Direct fallback render over orderedHandles]
    R -- sim --> T[TrackDrawProducer.Build]
    T --> U[TrackRenderCoordinator.Prepare]
    U --> V[TrackRenderCoordinator.Execute]
    V --> W[EndFrame]
    Q --> W
    S --> W
```

## Sequencia de render estabilizada

No caminho ativo hoje, `RenderVisibleSegmentOrder()` faz:

1. coleta entradas validas de `segmentRenderers_`
2. ordena por profundidade para a camera, com desempate por rank da janela
3. limita a quantidade desenhada por `fixedVisibleSegmentCap_`
4. repara estado corrompido se necessario
5. revalida slots de textura por face/familia
6. aplica `SetOffset(...)`
7. chama `renderer->Render(...)` diretamente

Isto significa:

- a janela visivel real e montada a partir dos renderers residentes
- a lista do `TrackDrawProducer` nao decide a ordem no caminho estabilizado
- o `TrackRenderCoordinator` nao faz staging do hot path estabilizado

## Onde `TrackDrawProducer` e `TrackRenderCoordinator` entram

Hoje o papel deles e de fallback e telemetria:

- `TrackDrawProducer` monta a lista de handles quando a estabilizacao esta desligada
- `TrackRenderCoordinator::Prepare()` converte handles ordenados em chunks staged
- `TrackRenderCoordinator::Execute()` aplica o budget por frame e dispara o render
- `coordinator_.PresentTelemetry()` continua sendo chamado em `EndFrame()`

Por isso, este par continua relevante para:

- medir custo de budget
- validar a trilha nao estabilizada
- servir como fallback quando a politica de estabilizacao muda

## O que este codigo nao faz mais no hot path

Estas suposicoes antigas nao valem mais para o caminho estabilizado atual:

- `TrackDrawProducer` nao e o controlador principal da ordem visivel
- `TrackRenderCoordinator` nao e o pipeline principal de desenho
- a logica de slide nao depende do coordenador para estabilizar a janela

## Plano de otimizacao de performance

Este e o plano de trabalho recomendado com base no fluxo atual:

### 1. Manter o hot path sem bifurcacao desnecessaria

Objetivo:

- reduzir trabalho redundante no render estabilizado
- manter `RenderVisibleSegmentOrder()` direto e previsivel

Metricas:

- `sh2MasterDrawTicksThisFrame_`
- `runtimeSafeReappliedThisFrame_`
- `runtimeSafeSkippedThisFrame_`

Aceite:

- o caminho estabilizado deve ficar constante em custo por frame
- reparos devem acontecer so quando houver inconsistencia real

### 2. Controlar o custo de prefetch e prewarm

Objetivo:

- manter o numero de uploads e rebuilds sob teto
- evitar que prefetch vire fonte de picos repetidos

Metricas:

- `runtimePrefetchHitsThisFrame_`
- `runtimePrefetchMissesThisFrame_`
- `textureUploadsThisFrame_`
- `trackTextureRecycleCount_`
- `textureHeapCompactCooldown_`

Aceite:

- o sistema deve preferir reutilizar estado residente
- o prewarm deve respeitar o orcamento de uploads por frame

### 3. Reduzir churn de working-set e slots

Objetivo:

- evitar crescimento monotono de estado auxiliar
- manter o custo do `familyWorkingSetDirty_` sob controle

Metricas:

- `sh2MasterWorkingSetTicksThisFrame_`
- `EstimateWorkRamRetainedBytes()`
- `EstimateLowWorkRamRetainedBytes()`
- `phaseHwrAfterStream_ - phaseHwrBeforeStream_`
- `phaseLwrAfterStream_ - phaseLwrBeforeStream_`

Aceite:

- o working-set deve ser atualizado so quando houver mudanca real
- a memoria retida deve oscilar dentro de uma faixa estavel

### 4. Tratar slide e textura como sistemas separados

Objetivo:

- diferenciar custo de janela de custo de heap de textura
- evitar confundir recuperacao de LOD com leak de RAM

Metricas:

- `runtimeSlidesThisFrame_`
- `runtimeSlideStallsThisFrame_`
- `slideHwrTraceFlags_`
- `slideHwrTraceAfterTrim_`
- `slideHwrTraceAfterBuildPrefetch_`

Aceite:

- o slide precisa continuar deterministico
- falha visual de textura nao deve ser mascarada como problema de janela

### 5. Medir antes de alterar politica de render

Objetivo:

- qualquer ajuste em budget ou coordenador precisa de baseline

Metricas:

- `sh2MasterStreamTicksThisFrame_`
- `sh2MasterDrawTicksThisFrame_`
- `sh2MasterFrameTicksThisFrame_`
- `coordinator_.Telemetry().submittedTrackSegments`
- `coordinator_.Telemetry().trackSegmentsSkippedByBudget`

Aceite:

- nao mudar a politica sem baseline comparavel
- toda regressao deve ser visivel em ticks e em memoria, nao apenas em FPS

## Referencia de plan separado

O plano de uso dual do SH2 foi extraido para um documento dedicado:

- [SH2_BALANCING_PLAN.md](./SH2_BALANCING_PLAN.md)
