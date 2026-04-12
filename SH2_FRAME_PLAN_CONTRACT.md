# SH2 Frame Plan Contract

## Objetivo

Definir o contrato de dados para distribuir calculo entre SH2 sem mover side-effects para a Slave.

Regra central:

- `Slave` calcula plano (deterministico, sem IO/VDP1/malloc global)
- `Master` aplica plano (CD, memoria compartilhada global, VDP1/CRAM e draw)

## Snapshot de entrada (Master -> Slave)

`TrackFrameSnapshot` deve ser imutavel durante o job:

- `frameId`
- `carWorldPosition`
- `cameraLocation`
- `trackOffset`
- `windowStartId`
- `windowDirection`
- `fixedVisibleSegmentCap`
- `segmentCount`
- `segmentMeta[]` (id, center, logicalSegmentCount, flags de estado)

Regras:

1. Sem ponteiro para renderer/ModelObject dentro do snapshot.
2. Sem estruturas que dependam de alocacao durante o job.
3. Todos os ids precisam ser validados na Master antes do dispatch.

## Plano de saida (Slave -> Master)

`TrackFramePlan` retornado pela Slave:

- `frameId`
- `valid`
- `sortedSegmentIds[]` (ordem final para draw)
- `sortedCount`
- `desiredLodBySegment[]`
- `desiredBaseRankBySegment[]`
- `flags` (fallback, partial, stale)
- `plannerTicksSlave`

Regras:

1. Plano deve caber em buffer fixo.
2. Sem referencias para dados volateis da Master.
3. Se invalido, Master usa ultimo plano valido.

## Aplicacao do plano (Master)

A Master usa `TrackFramePlan` para:

1. decidir ordem de prepare/draw
2. decidir transicoes de LOD
3. marcar working-set de familias/texturas

A Master continua responsavel por:

- slide/prefetch com IO
- upload e recycle de textura
- bind de slots por face
- draw final

## Lockstep (barrier)

No modo lockstep:

1. Master dispara job na Slave
2. Master espera finalizar no mesmo frame (com guarda de timeout)
3. Master aplica plano e desenha

Este modo prioriza determinismo para alvo de 30 FPS.

## Politica de fallback

Se timeout/invalidacao:

1. registrar contador de timeout
2. usar plano anterior valido
3. se repetido, entrar em safe mode e executar local/sincrono

## Integracao com codigo atual

- Estagios segregados em `track_pipeline_stages.*`
- Worker de sort/lista em `track_draw_producer.hpp`
- Lockstep configurado em `TrackSystem::ConfigureCoordinatorAndBudget`

## Proximas implementacoes

1. Construir `TrackFrameSnapshot` real no inicio de `RenderFrame`
2. Construir `TrackFramePlan` real na Slave (ordem + LOD target)
3. Aplicar plano na Master antes do draw
4. Medir custo por estagio em `SH2 avg` e counters de fallback

## Status de execucao (abril/2026)

- `TrackFrameSnapshot` real ja e montado por frame.
- `TrackFramePlan` v1 ja e aplicado antes do draw (ordem + LOD target).
- O plano reaproveita o sort da Slave e injeta `desiredLodBySegment` em `RunPendingLodRecoveryStage`.
- Overlay SH2 ja exibe ticks de planejamento em Master/Slave (`pl`).
