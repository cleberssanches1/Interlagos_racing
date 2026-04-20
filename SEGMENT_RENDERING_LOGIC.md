# Logica de Renderizacao de Segmentos

## 1) Objetivo do pipeline
- Renderizar uma janela ativa de segmentos (pista visivel) com slide continuo.
- Reaproveitar slots de renderer e slots de textura para reduzir churn de memoria.
- Manter LOD por faixa de distancia (rank logico na janela).

## 2) Estruturas principais
- `segmentRenderers_`: janela ativa de segmentos renderizaveis.
- `segmentEntries_`: metadata da mesma janela ativa.
- `activeWindowStartId_`: id logico inicial da janela.
- `activeWindowHead_`: cabeca fisica da fila/ring dentro do vetor.
- `windowDirection_`: direcao de progresso (+1 ou -1).
- `slideScratchRenderer_`: renderer staging para entrada do proximo segmento.
- `seg1FamilySlots_`: cache de familias de textura e slots por LOD.

## 3) Modelo de fila/ring da janela
A janela e tratada como fila circular:
- indice fisico = `(head + logicalRank) % windowCount`
- `logicalRank=0` representa a ponta de saida da direcao atual.
- no slide:
1. identifica segmento de saida (`ResolveWindowOutgoingSegmentId`)
2. identifica slot que sera sobrescrito (`ResolveWindowDropIndexByDirection`)
3. constroi/prepara segmento de entrada (`ResolveWindowIncomingSegmentId`)
4. grava no slot de drop e avanca `head` (`AdvanceWindowHeadByDirection`)

Isso evita `erase/insert` no vetor durante runtime.

## 4) Funcoes centrais (funcao a funcao)

### `RenderFrame(...)`
Coordena o frame: manutencao, slide de janela, prefetch, recuperacao de LOD, draw e cleanup.

### `SlideActiveSegmentWindow(stepCount, direction)`
Executa o deslocamento da janela. Em modo estabilizado usa caminho deterministico para evitar rebuild completo.

### `ExecuteDeterministicStabilizedSlide(...)`
Caminho principal de slide em runtime estabilizado:
- drena slots aposentados
- garante metadata/prefetch do proximo segmento
- prepara textura/familia do incoming
- aplica swap de renderer/estado no slot de drop
- atualiza inicio da janela + head + lookup + targets de LOD

### `PrepareStabilizedSlideBackBuffer(...)` / `CommitStabilizedSlideBackBuffer()`
Caminho alternativo com backbuffer de slide (prepare + commit).

### `BuildSegmentIntoRenderer(segmentId, renderer, outCenter, outFamilyIds)`
Monta geometria/material do segmento no renderer informado, preenchendo centro e familias.

### `BuildSegmentIntoPrefetch(segmentId, allowSlotWarmup)`
Prepara adiantado o proximo segmento para reduzir stall no frame da troca.

### `RebuildActiveSegmentWindow(startId, loadLimit, direction)`
Reconstroi a janela quando necessario (ex: init, fallback). Em modo `fixed storage` reutiliza slots fixos dos renderers.

### `RebuildActiveWindowLookupTables()`
Reconstroi lookup da janela ativa.
Estado atual: lookup compacto por janela (N entradas), sem vetor indexado por `totalSegmentCount`.

### `TryResolveWindowEntryIndexBySegmentId(...)` / `TryGetWindowLogicalRank(...)`
Resolucoes de id->indice fisico e id->rank logico usando lookup compacto.

### `UpdateDesiredStabilizedWindowLodTargets()`
Define alvo de LOD por `logicalRank` na janela atual.

### `RunPendingLodRecoveryStage(...)`
Aplica recuperacao incremental de LOD quando ha pendencias.

### `RenderVisibleSegmentOrderStabilized(...)`
Submete render dos segmentos visiveis em ordem estabilizada, com fallback de reaplicacao de slots quando necessario.

### `MergeCurrentWindowFamilies()`
Mantem cache de familias limitado ao conjunto da janela ativa e enfileira reciclagem de slots de familias que sairam da janela.

### `ReleaseUnusedFamilyResourcesEndFrame()`
No fim do frame, libera slots nao referenciados por grace-frame e devolve para fila de reuso.

### `RunTextureCompactionStage(windowSlid)`
Compactacao/rebuild de residencia de textura sob gatilhos de pressao (slack, backlog aposentado, queda de baseline etc).

## 5) Causas de queda de memoria tratadas

### 5.1 Lookup global por id (churn)
Problema anterior:
- lookup era reconstruido com `assign(totalSegmentCount)` (catalogo grande).
- custo e churn cresciam com numero total de segmentos, nao com janela ativa.

Correcao aplicada:
- lookup compacto por janela ativa (`N` entradas).
- custo por rebuild passa a ser O(N), N pequeno (janela visivel).

### 5.2 Overflow de id de segmento
Problema anterior:
- ids em `int16_t` no pipeline principal.
- em catalogos >32767 segmentos ocorria overflow e lookup/planejamento quebravam.

Correcao aplicada:
- ids principais migrados para `int32_t` (janela, slide, frame-plan, snapshot).

### 5.3 Planejamento de LOD por id limitado
Problema anterior:
- frame-plan indexava arrays por `segmentId` limitado a `kTrackSegmentLimit`.
- fora desse range, segmentos nao recebiam planejamento correto.

Correcao aplicada:
- frame-plan indexado por `logicalRank` da janela ativa.

## 6) Sobre camera e memoria
- A camera em si nao deve acumular memoria por frame nesse pipeline.
- O consumo dominante vem de:
  - build/prefetch de segmentos
  - cache de familias/texturas
  - filas de slots reutilizaveis/aposentados
  - scratch buffers de slide e render

## 7) Invariantes recomendadas para monitorar
- ids unicos na janela ativa
- `head` valido (`head < windowCount`)
- `activeWindowStartId_` coerente com `windowDirection_`
- ausencia de `missing required face slots` apos commit de slide
- slope de memoria apos warm-up proxima de zero

## 8) Proximos passos de refatoracao
1. Introduzir estrutura ring dedicada (`TrackWindowRing`) com API unica (`push/pop/swap/replace`).
2. Migrar slide para operacoes explicitas de fila (sem conhecimento direto de vetor no call site).
3. Harden de compactacao de textura com soak longo e telemetria de slope.
4. Validar 30+ min com logs de FPS/memoria sem tendencia de degradacao.

## 9) Modo de isolamento (teste de vazamento)
- Flag: `kEnableTrackLeakIsolationFixed64Pipeline` em `src/track_system.cxx`.
- Configuracao atual:
  - janela fixa de `10` segmentos (`kTrackLeakIsolationWindowSegments`),
  - LOD forçado em `64x64` para todos os segmentos (`ResolveSegmentLodIndexByRank -> 3`),
  - sem slide de janela em runtime,
  - sem prefetch runtime,
  - sem recovery incremental de LOD,
  - sem compactacao de textura em runtime.
- Objetivo: isolar vazamento/churn fora do pipeline dinamico de troca de segmento/textura.
