# Track Render Performance Action Plan

## Objetivo

Melhorar o desempenho do subsistema de renderiza√ß√£o da pista sem reabrir riscos
de boot, regress√£o de mem√≥ria ou aumento do envelope est√°vel da ISO.

## Hotspots analisados

### 1. Caminho estabilizado de sort/render

Arquivos principais:

- `src/track_system.cxx`

Pontos relevantes:

- `BuildStabilizedSortedHandles(...)`
- `RenderVisibleSegmentOrderStabilized(...)`

Achado:

- havia valida√ß√µes defensivas com custo `O(n¬≤)` em dois pontos do caminho por
  frame:
  - valida√ß√£o da lista ordenada retornada pelo sorter;
  - valida√ß√£o da lista produzida pelo producer slave.

Impacto:

- o n√∫mero de segmentos √© pequeno, mas esse trabalho acontece todo frame no
  caminho cr√≠tico do track render;
- em Saturn, custo fixo repetido em loops curtos ainda pesa quando somado ao
  restante do pipeline.

### 2. Pipeline por frame j√° est√° bem segregado

O fluxo principal j√° est√° organizado em est√°gios:

- maintenance
- window
- prefetch
- lod recovery
- working set
- draw

Isso reduz o risco de otimiza√ß√£o localizada, porque permite cortar custo sem
reorganizar ownership.

### 3. Pr√≥ximos candidatos de baixo risco

- reduzir revalida√ß√µes defensivas redundantes no draw estabilizado;
- evitar recomputa√ß√µes locais quando j√° existe `framePlanSortedHandles_`;
- revisar o custo de reparo de renderer/slots no draw path;
- revisar o custo do working set quando `FamilyWorkingSetDirty()` oscila demais.

## Plano de execu√ß√£o

### Fase 1 ‚Äî corte seguro imediato

Aplicar troca de valida√ß√µes `O(n¬≤)` por lookup `O(n)` com tabela fixa por slot.

Status:

- conclu√≠do

### Fase 2 ‚Äî medir e observar

Validar:

- build est√°vel;
- envelope ISO;
- headers passivos/observability;
- comportamento no emulador.

Status:

- em andamento no ciclo atual

### Fase 3 ‚Äî pr√≥ximo corte sugerido

Se este patch permanecer est√°vel no emulador, o pr√≥ximo melhor corte √©:

- atacar o custo de reparo/reaplica√ß√£o de face slots no draw estabilizado;
- especialmente onde `CountMissingOrDeadRequiredFaceTextureSlots(...)` for√ßa
  caminhos de reparo em tempo de execu√ß√£o.

## Implementa√ß√£o aplicada neste ciclo

Foi introduzida uma tabela de membership por `slot/generation` no caminho de
track render estabilizado para:

- validar a sa√≠da do sorter slave em `O(n)`;
- validar a sa√≠da do producer slave em `O(n)`.

Isso preserva a checagem defensiva existente, mas remove varreduras aninhadas
desnecess√°rias do caminho cr√≠tico de render.

## Pr√≥ximo corte ainda pendente

O pr√≥ximo alvo continua sendo a valida√ß√£o/reparo de face slots no draw
estabilizado, mas a reintrodu√ß√£o precisa ser mais estreita que a tentativa
anterior, porque houve regress√£o de runtime no emulador.

## Implementa√ß√£o aplicada neste ciclo

Foi removida a c√≥pia por frame da lista ordenada para o producer slave:

- `RenderVisibleSegmentOrderStabilized(...)` passou a enviar o buffer cont√≠guo
  de `orderedHandles` diretamente para o producer;
- `SlaveTrackDrawProducer` ganhou uma interface adicional `BuildFromRange(...)`,
  preservando `Build(const std::vector<...>&, ...)` para compatibilidade.
- o estado legado `stabilizedProducerInputScratch_` foi removido do
  `TrackSystem`, porque deixou de ter uso ap√≥s esse corte.

## Implementa√ß√£o aplicada neste ciclo seguinte

Foi enxugada a montagem do `framePlan`:

- `BuildAndApplyFramePlanStage(...)` deixou de revalidar `SegmentHandle ->
  SegmentRenderEntry` logo ap√≥s `BuildStabilizedSortedHandles(...)`;
- o plano agora copia apenas o subconjunto j√° validado e limitado a
  `kTrackSegmentLimit`.

## ImplementaÁ„o aplicada neste ciclo seguinte

Foi reduzido o custo de validaÁ„o da lista produzida pelo producer:

- `RenderVisibleSegmentOrderStabilized(...)` deixou de fazer `segmentPool_.Resolve(...)` para cada item devolvido pelo producer;
- a validaÁ„o do producer agora fica restrita ao contrato realmente necess·rio nesse ponto: contagem v·lida e pertencimento ao conjunto `orderedHandles`;
- a validaÁ„o integral de `handle -> entry/renderer` continua existindo no loop de draw imediatamente seguinte.

## ImplementaÁ„o aplicada neste ciclo seguinte

Foi removida uma alocaÁ„o tempor·ria por frame no producer sÌncrono:

- `TrackDrawListAB` ganhou `BuildWriteList(const Handle*, size_t, size_t)`;
- `DoubleBufferedTrackDrawProducer::BuildFromRange(...)` passou a gravar direto no buffer AB, sem montar `std::vector` tempor·rio;
- isso reduz cÛpia e churn de heap no caminho master-only/compatÌvel do producer.

## ImplementaÁ„o aplicada neste ciclo seguinte

Foi removido o caminho legado de `orderedHandles` do fluxo estabilizado:

- `RenderFrame(...)` sÛ monta `orderedHandles` quando `kEnableTrackRuntimeStabilization == false`;
- `RunDrawStage(...)` e `RenderVisibleSegmentOrder(...)` passaram a aceitar ponteiro opcional para a lista legada;
- no caminho estabilizado, o draw agora explicita que n„o depende mais desse buffer local por frame.

## ImplementaÁ„o aplicada neste ciclo seguinte

Foi reduzido o custo de lookup em `RefreshFamilyWorkingSet()`:

- o mÈtodo agora chama `RebuildFamilySlotIndex()` uma vez no inÌcio do refresh;
- `addRef(...)` passou a resolver `familyId -> seg1FamilySlots_` diretamente por `familySlotIndex_`, sem chamar `FindFamilySlot(...)` a cada referÍncia;
- isso remove busca linear repetida no caminho de working set por frame.

## ImplementaÁ„o aplicada neste ciclo seguinte

Foi removida a cÛpia/zeragem integral de chunks no `TrackRenderCoordinator`:

- `RenderChunkPool` ganhou `BeginWrite(...)` e `CommitWrite(...)` em `src/render_chunk_pool.hpp`;
- `TrackRenderCoordinator::Prepare(...)` passou a preencher `PreparedChunk` diretamente no storage ativo do pool;
- isso elimina a `std::array<PreparedChunk, Capacity>` tempor·ria por frame e a cÛpia posterior para o pool.

## ImplementaÁ„o aplicada neste ciclo seguinte

Foi removido o `resolve()` duplicado no `Execute()` do coordinator legado:

- `PreparedChunk` passou a guardar `resolvedEntryAddress` em `src/track_render_coordinator.hpp`;
- `Prepare(...)` grava o endereÁo da entry j· resolvida no mesmo frame;
- `TrackSystem` agora resolve no `Execute(...)` primeiro por esse endereÁo e sÛ cai no `segmentPool_` como fallback.

## ImplementaÁ„o aplicada neste ciclo seguinte

Foi reduzido o custo residual de telemetria/debug no draw path:

- `renderedCountById` virou opcional no draw e sÛ È mantido quando `RuntimeStatsLogsEnabled()` est· ativo;
- `FinalizeDrawStage(...)` agora sai cedo do loop de warnings quando o debug de runtime est· desligado;
- os checks de log raro do draw foram consolidados em flags locais (`emitRareRuntimeStatsLogs`) em vez de recomputados repetidamente dentro dos loops.
