# Plano de Refatoracao: Janela de Segmentos como Fila/Ring

## Objetivo
- Tratar a pista ativa como uma fila com operacoes em inicio, meio e fim.
- Reduzir churn de memoria (LWR/HWR) no slide de segmentos.
- Eliminar comportamento de `erase/insert` em vetor dinamico no hot path.

## Status desta iteracao
- Fase 1 iniciada no codigo (`src/track_system.cxx/.hpp`).
- Resolucao de `incoming/outgoing/drop/head` centralizada em helpers.
- Varreduras lineares no slide principal removidas nos pontos criticos.
- Avanco de janela no commit de slide agora usa rotacao de `head` por direcao (sem recalc por scan).
- LOD/lookup de janela passaram a usar ordem real da fila (`head + logicalRank`) para rank logico.
- Rebuild da janela passou a reservar capacidade fixa (`kTrackSegmentLimit`, com folga de staging no renderer vector).
- Modo `fixed storage` ativado no rebuild: slots ativos e renderers da janela sao reutilizados diretamente (sem `BuildSegmentRenderers` temporario por slot).
- Lookup de janela refatorado para tabela compacta da janela ativa (`N` elementos), removendo rebuild por `totalSegmentCount` e o `assign` massivo por slide.
- IDs de segmento no pipeline principal migrados para `int32_t` (janela/slide/frame-plan), eliminando overflow acima de `32767`.
- Frame-plan de LOD migrou de indexacao por `segmentId` para indexacao por `logicalRank`, corrigindo planejamento de LOD em pistas com muitos segmentos.
- Caminho de isolamento implementado para diagnostico: `10` segmentos fixos, LOD forçado `64x64`, sem slide/prefetch/recovery/compaction dinamicos.
- Proxima etapa: introduzir armazenamento ring fixo (`N + staging`) mantendo feature flag.

## Resposta curta para sua pergunta
- `Sim` para "tratar como vetor indexavel" (acesso por indice ajuda).
- `Nao` para "remover elemento com erase para controlar memoria": `erase` move elementos e pode aumentar churn/fragmentacao.
- Melhor modelo: `ring buffer de capacidade fixa` com mapa logico->fisico.

## Pipeline atual (resumo)
- A janela ativa esta em `segmentRenderers_` (`TrackLowWorkVector<SegmentRenderEntry>`).
- O slide faz replace no `dropIdx`, com reutilizacao de buffers/scratch.
- O `dropIdx` ainda e resolvido por varredura em alguns trechos.
- Ja existe lookup por id (`activeWindowEntryIndexBySegmentId_`) e estado de janela (`activeWindowStartId_`, `activeWindowHead_`, `windowDirection_`).
- Existe scaffold de pool fixo (`kSlotPoolSize`, `slotPool_`), mas sem uso efetivo.

## Problema observado nos logs
- `SWLWR free` cai gradualmente ao longo da corrida.
- FPS cai junto com memoria livre.
- O padrao e compativel com churn cumulativo (alocacoes/realocacoes indiretas + fragmentacao), nao com um pico unico.

## Arquitetura alvo

### 1. Estrutura principal: `TrackWindowRing`
- Armazenamento fixo: `N ativos + 1 staging` (ex.: `20 + 1`).
- Metadados:
  - `head` (inicio logico da fila)
  - `count`
  - `capacity`
- Mapeamento:
  - `physical = (head + logicalIndex) % capacity`
  - `logicalIndex` usado para inicio/meio/fim sem mover memoria.

### 2. Operacoes obrigatorias
- `PopFront()` / `PushBack()`
- `PopBack()` / `PushFront()`
- `ReplaceAt(logicalIndex, slotRef)`
- `SwapAt(logicalA, logicalB)` (troca no meio)
- `GetAt(logicalIndex)` (leitura/indexacao)

### 3. Politica de memoria
- Capacidade fixa na inicializacao.
- Sem `erase`, sem `insert`, sem `shrink_to_fit` no runtime.
- Reuso de slots com `swap`/`move` e buffers persistentes.
- Todas as capacidades (faces, verts, family ids, working set) com piso pre-reservado.

## Plano de acao (fases)

### Fase 0 - Baseline e invariantes (1-2 dias)
- Expandir telemetria no slide:
  - bytes livres LWR/HWR antes/depois de `prepare` e `commit`
  - capacidade/size dos vetores por slot
  - contadores de alocacao em hot path (quando possivel)
- Adicionar invariantes de janela:
  - ids unicos na janela
  - cobertura contigua esperada a partir de `activeWindowStartId_`
  - consistencia lookup id->indice

Saida:
- Serie temporal objetiva para comparar antes/depois.

### Fase 1 - Adapter de fila sobre estado atual (2-3 dias)
- Sem trocar armazenamento ainda.
- Criar API unica para operacoes de janela:
  - `ResolveDropIndexByDirection()`
  - `ResolveIncomingIndexByDirection()`
  - `LogicalToPhysicalIndex()`
- Substituir varreduras manuais de `dropIdx` por lookup O(1) com fallback seguro.

Saida:
- Menos codigo duplicado e menos scans por slide.

### Fase 2 - Migracao para ring fixo (3-5 dias)
- Introduzir `TrackWindowRing` com armazenamento fixo (`N + staging`).
- Migrar `SlideActiveSegmentWindow` para operacoes de fila:
  - forward: `pop_front + push_back`
  - backward: `pop_back + push_front`
- Manter semantica atual de LOD/familias inicialmente.

Saida:
- Slide sem movimentacao de vetor dinamico.

### Fase 3 - Operacoes no meio e troca de familias (2-4 dias)
- Implementar `SwapAt` e `ReplaceAt` no ring.
- Encapsular "troca de segmentos no fim da familia de textura" usando indices logicos.
- Garantir que `activeWindowStartId_` e lookup tables atualizem em O(1)/O(N) controlado.

Saida:
- Janela realmente tratada como fila indexavel (inicio/meio/fim).

### Fase 4 - Limpeza de caminhos legacy e hardening (2-3 dias)
- Reduzir caminhos alternativos de slide que duplicam ciclo de memoria.
- Consolidar prefetch/backbuffer para trabalhar com staging slot do ring.
- Testes de soak prolongados e regressao funcional.

Saida:
- Menos superficie de bug e comportamento mais deterministico.

## Criterios de aceitacao
- Apos warm-up, queda de `SWLWR free` deve ficar proxima de 0 (sem tendencia monotona longa).
- FPS deve estabilizar (sem degradacao progressiva por volta/longo tempo).
- Nenhuma nova falha de textura/slot/palette durante slide.
- Invariantes de janela aprovadas por >= 30 min de soak.

## Checklist tecnico por arquivo
- `src/track_system.hpp`
  - adicionar tipo/estado `TrackWindowRing`
  - expor helpers de indice logico/fisico
- `src/track_system.cxx`
  - migrar `SlideActiveSegmentWindow`
  - migrar `ExecuteDeterministicStabilizedSlide`
  - atualizar lookup tables para ring
- `src/track_renderer.hpp` e/ou `src/track_renderer.cxx`
  - validar reuso sem realocar no ciclo normal
- testes
  - adicionar cenarios de fila (inicio/meio/fim)
  - soak com logs de memoria/FPS

## Riscos e mitigacao
- Risco: regressao de ordenacao visual/LOD.
  - Mitigacao: feature flag por fase + fallback antigo.
- Risco: inconsistencias entre `head/startId/lookup`.
  - Mitigacao: invariantes assertivas em debug e autocorrecao controlada.
- Risco: ganho parcial sem resolver fragmentacao residual.
  - Mitigacao: medir slope de memoria por fase e atacar alocadores transientes restantes.

## Observacao importante
- A camera, por si so, nao deveria explicar a queda continua de memoria nesse padrao.
- O caminho de maior risco continua sendo ciclo de slide/prefetch/slots de textura e estruturas associadas ao segmento.
