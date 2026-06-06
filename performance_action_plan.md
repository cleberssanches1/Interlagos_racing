# Plano de Ação - Performance e Organização

## Objetivo
Melhorar FPS, reduzir churn de memória e organizar o código sem alterar o comportamento funcional do jogo.

## Prioridade 1
- Criar benchmark fixo por cenário: boot, idle, aceleração, vmax, curva, colisão.
- Mover telemetria pesada para build debug ou flags de runtime bem isoladas.
- Reduzir tipos internos de ids/ranks/counters onde o domínio já é limitado.
- Consolidar resolução de paths/assets em um serviço único.

## Prioridade 2
- Refatorar `TrackSystem` em módulos menores:
  - catálogo de segmentos
  - controlador de janela
  - prefetch/residência de texturas
  - queries de colisão/superfície
  - telemetria/debug
- Refatorar `GameLoopSystem` para ser apenas orquestrador.
- Extrair backends e cache de `TrackRenderer`.

## Prioridade 3
- Substituir probing de segmentos `1..4096` por leitura do pacote/manifesto runtime.
- Eliminar reconstruções redundantes de `ModelObject` no pipeline do carro.
- Padronizar scratch buffers e evitar `std::vector` temporário nos loaders críticos.

## Prioridade 4
- Revisar estruturas residentes por frame e manter em memória apenas o necessário.
- Reavaliar vetores de face/família/working set que hoje convivem no `TrackSystem`.
- Reduzir largura de tipos em snapshots/plans/indexadores.

## Candidatos imediatos a redução de tipo
- `TrackFramePlan::sortedSegmentIds`: `int32_t -> int16_t`
- `TrackFrameSnapshot::SegmentMeta::id`: `int32_t -> int16_t`
- `windowEntryIndexBySegmentId_`: `int16_t -> int8_t`
- `windowLogicalRankBySegmentId_`: `int16_t -> int8_t`
- `lastSortRank_`: `uint16_t -> uint8_t`
- ids de segmento internos do `TrackSystem` com sentinela `-1`: revisar para `int16_t`

## Regra de execução
Cada mudança deve passar por:
1. benchmark antes/depois
2. validação de memória HWR/LWR/cart
3. validação funcional
4. rollback simples se houver regressão
