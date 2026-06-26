# TrackRenderScheduler Reintroduction Strategy

## Objetivo

Reintroduzir a extração de `TrackRenderScheduler` sem voltar a desestabilizar `src/game_loop_system.hpp`, `src/track_system.cxx` e o pipeline visual da pista.


## Contexto

O domínio da pista é o mais sensível depois do scheduler, porque envolve:

- render window;
- producer/sort na Slave;
- fallback síncrono;
- safe mode;
- consumo final no Master.

Qualquer mudança precipitada pode quebrar:

- pacing;
- safe mode;
- telemetria;
- estabilidade visual;
- boot/runtime.


## Regra nova para a pista

Evitar por enquanto:

- includes novos em `src/game_loop_system.hpp`;
- includes novos em `src/track_system.cxx`;
- introduzir packet real no fluxo crítico;
- mover `BeginFrame`, `RenderFrame` ou `EndFrame`;
- tocar em `TrackDrawProducer`.


## Hipótese técnica

Mesmo mudanças pequenas na orquestração da pista podem:

- deslocar layout de código crítico;
- alterar ordem de chamadas Master/Slave;
- produzir stalls, regressão visual ou falha de boot, mesmo com ISO estável.


## Estratégia de reintrodução segura

### Nível 0 — preparação fora do runtime

Permitido:

- contratos passivos;
- ops externas passivas;
- documentação de equivalência;
- packet/context/telemetria fora do runtime.

Proibido:

- tocar em `src/game_loop_system.hpp`;
- tocar em `src/track_system.cxx`;
- tocar em `TrackDrawProducer`.


### Nível 1 — packet em modo espelho

Quando a reentrada real voltar a ser tentada:

- montar `TrackFrameContext` e `TrackRenderPacket` apenas como espelho;
- não trocar a orquestração real da pista;
- não mexer em fallback ou safe mode.


### Nível 2 — substituição textual mínima

Se o modo espelho estiver estável:

- substituir primeiro uma etapa local de preparação por sequência textual equivalente;
- não adicionar chamada indireta nova em arquivo crítico;
- não centralizar producer/sort ainda.


## Ordem revisada de reentrada

1. manter `GameLoopSystem` intacto;
2. manter `TrackSystem` intacto;
3. preparar ops externas de:
   - frame context;
   - render packet;
   - telemetry;
   - packet producer flags;
4. só depois considerar espelhamento local;
5. por último, considerar substituição textual mínima.

Para a ordem exata de retry com packet estreito de telemetria, ver:

- `TRACK_RENDER_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`


## Critério de reentrada

Só voltar a tocar no runtime da pista se a microetapa:

- não adicionar include novo em arquivo crítico;
- não mover `BeginFrame` / `RenderFrame` / `EndFrame`;
- não alterar fallback/safe mode;
- for revertível com patch pequeno.
