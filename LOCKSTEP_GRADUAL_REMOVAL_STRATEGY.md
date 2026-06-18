# Lockstep Gradual Removal Strategy

## Objetivo

Preparar a remoção gradual do lockstep entre Master SH2 e Slave SH2 sem tocar ainda no runtime crítico.

O alvo é permitir, em uma etapa futura, que:

- simulação consuma snapshot `N-1`;
- pista consuma packet de render `N-1`;
- o Master só bloqueie quando não existir packet consistente disponível.


## Estado atual

Hoje o runtime ainda faz:

- simulação em lockstep opcional dentro de `src/game_loop_system.hpp`;
- pista com packet/context passivo, mas ainda consumida no mesmo frame;
- fallback síncrono explícito tanto para simulação quanto para pista.

Os recortes anteriores já deixaram prontos:

- `SimulationScheduler` passivo;
- `TrackRenderScheduler` passivo;
- `CarRenderSystem` passivo;
- `CdAssetSystem` e `MemoryBudgetSystem` fora do bootstrap ad-hoc.


## O que falta antes de tirar lockstep

### 1. Histórico explícito de packets

Precisamos de estado passivo para guardar:

- último `SimulationFramePacket` concluído;
- último `TrackRenderPacket` válido;
- slot de escrita do próximo packet;
- slot comprometido para consumo.

Esse histórico agora fica formalizado em:

- `src/frame_reuse_contracts.hpp`
- `src/simulation_frame_reuse_ops.hpp`
- `src/track_frame_reuse_ops.hpp`


### 2. Decisão explícita de consumo `N-1`

Antes de mexer no runtime, a decisão futura precisa ser inequívoca:

- existe packet comprometido?
- ele é do frame exato?
- ele é anterior ao frame pedido?
- devemos consumir o packet comprometido?
- devemos despachar novo trabalho?
- precisamos cair em fallback síncrono?

Essa decisão agora existe como packet passivo para:

- simulação;
- pista.


### 3. Telemetria de reuso

Quando a reentrada real começar, será necessário medir:

- quantas vezes consumimos `N-1` na simulação;
- quantas vezes consumimos `N-1` na pista;
- quantos fallbacks síncronos restaram;
- quantos commits válidos de packet ocorreram.

Isso já está previsto em `FrameReuseTelemetry`.


## Ordem segura de reentrada futura

### Etapa A — modo espelho

Sem alterar o comportamento real:

- commitar `SimulationFramePacket` concluído no histórico;
- commitar `TrackRenderPacket` válido no histórico;
- montar `SimulationReuseDecisionPacket`;
- montar `TrackReuseDecisionPacket`;
- só comparar com o comportamento atual.

Sem trocar ainda:

- `ExecuteGameplayFrame`;
- `BeginFrame` / `RenderFrame` / `EndFrame`;
- fallback atual.


### Etapa B — simulação em `N-1`

Primeira troca funcional futura:

- no modo non-lockstep, consumir snapshot comprometido anterior;
- despachar o próximo frame sem esperar resultado imediato;
- manter fallback síncrono quando não existir snapshot consistente.

Gate:

- sem drift perceptível entre carro, câmera e áudio;
- sem aumento de `Master SH2 invalid opcode`;
- sem boot regression.


### Etapa C — pista em `N-1`

Segunda troca funcional futura:

- consumir `TrackRenderPacket` comprometido anterior;
- permitir que producer/sort trabalhem para o próximo frame;
- manter safe mode e fallback síncrono intocados.

Gate:

- pista visualmente estável;
- sem stalls novos;
- sem regressão do safe mode.


### Etapa D — redução real de waits

Somente depois de B e C:

- reduzir waits obrigatórios do Master;
- medir `simMasterWaitTicks`;
- medir waits de pista;
- medir consumo real de `N-1`.


## O que não fazer agora

- não alterar `src/game_loop_system.hpp` nesta etapa;
- não alterar `src/track_system.cxx` nesta etapa;
- não trocar o contrato de áudio;
- não remover fallback síncrono;
- não mover driver PCM/SGL nem submissão VDP.


## Entrega desta rodada

Esta rodada entrega apenas:

1. contratos passivos de histórico `N-1`;
2. operações passivas para decisão de consumo/reuso;
3. estratégia explícita para a reentrada futura.

Ainda não entrega:

- consumo real de `N-1` no runtime;
- remoção real de lockstep;
- alteração no pacing do frame.
