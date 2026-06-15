# Master/Slave -> VDP Pipeline Action Plan

## Objetivo

Reorganizar o pipeline do frame para:

1. reduzir o tempo em que o Master SH2 fica parado esperando a Slave;
2. transformar etapas implícitas em componentes explícitos;
3. isolar a preparação de dados da submissão final para VDP1/VDP2;
4. manter boot e estabilidade do emulador como prioridade.


## Restrições do projeto

- O projeto é sensível a crescimento de binário e mudanças no layout de código.
- Alterações amplas em `main.cxx`, `game_loop_system.hpp` e áudio já causaram falhas de boot.
- Chamadas de driver devem continuar no Master:
  - VDP1/VDP2
  - PCM/SGL
- Qualquer migração para a Slave precisa preservar fallback síncrono.


## Estado atual resumido

- Master:
  - input
  - background
  - câmera
  - HUD
  - submissão final de render
  - driver de áudio
- Slave:
  - simulação de gameplay/física em lockstep
  - producer/sort da pista em lockstep
- Gargalo principal:
  - o Master despacha trabalho, mas frequentemente espera a Slave no mesmo frame.


## Progresso executado

- Fase 1 concluída:
  - `src/frame_pipeline_contracts.hpp` criado;
  - contratos de dispatch de simulação explicitados;
  - documentação persistente criada.
- Fase 2 em andamento:
  - política de scheduler movida para `SimulationSchedulerPolicy`;
  - estado do scheduler movido para `src/simulation_scheduler_state.hpp`;
  - tasks de worker movidas para `src/frame_worker_tasks.hpp`;
  - transições de estado de dispatch/completion centralizadas em `SimulationRuntimeState`.
- Envelope atual preservado:
  - ISO estável em `4134912` bytes;
  - sem regressão de build nesta etapa.


## Meta de arquitetura

Separar o frame em pacotes explícitos:

1. `InputFramePacket`
2. `SimulationFramePacket`
3. `CameraFramePacket`
4. `TrackRenderPacket`
5. `CarRenderPacket`
6. `HudFramePacket`

Fluxo alvo:

- Master monta `InputFramePacket`
- Slave produz `SimulationFramePacket`
- Master consome `SimulationFramePacket` já concluído
- Slave prepara `TrackRenderPacket` e `CarRenderPacket`
- Master só consome os pacotes e submete para VDP1/VDP2


## Fases de implementação

### Fase 1 - Contratos explícitos sem mudança de runtime

Objetivo:

- materializar a arquitetura alvo em tipos e documentação;
- não alterar comportamento;
- reduzir risco.

Entregáveis:

- `src/frame_pipeline_contracts.hpp`
- contratos de scheduler
- documento de referência persistente

Critério de aceite:

- build idêntico em comportamento;
- nenhuma alteração no loop do frame.


### Fase 2 - `SimulationScheduler` sem mudança funcional

Objetivo:

- extrair da `GameLoopSystem` a política de dispatch/drain/backoff;
- manter lockstep exatamente como está hoje.

Escopo:

- encapsular regras de:
  - `TryDispatchSimulationOnSlave`
  - `DrainSimulationJobIfInFlight`
  - backoff
  - decisão lockstep vs non-lockstep

Critério de aceite:

- mesma telemetria;
- mesmo tamanho de binário ou crescimento mínimo controlado;
- boot estável.


### Fase 3 - `CarRenderSystem`

Objetivo:

- separar preparação visual do carro da submissão final.

Escopo:

- transformar estado de render do carro em pacote explícito;
- preparar yaw/offset/ordem de draw fora da submissão;
- manter VDP1 no Master.

Critério de aceite:

- o carro continua visualmente idêntico;
- `CarSystem` deixa de concentrar responsabilidades visuais.


### Fase 4 - `TrackRenderScheduler`

Objetivo:

- separar producer/sort/plan da pista da etapa de consumo Master.

Escopo:

- transformar saída da pista em `TrackRenderPacket`;
- manter safe mode;
- preparar terreno para remover o barrier lockstep sempre que houver pacote válido do frame anterior.

Critério de aceite:

- sem regressão de estabilidade da pista;
- fallback síncrono preservado.


### Fase 5 - `CdAssetSystem`

Objetivo:

- remover streaming e retries do bootstrap ad-hoc.

Escopo:

- fila explícita de jobs de CD;
- retries;
- staging;
- prioridade por categoria de asset.

Critério de aceite:

- `main.cxx` deixa de orquestrar carregamentos diretamente;
- streaming fica observável por telemetria.


### Fase 6 - `MemoryBudgetSystem`

Objetivo:

- centralizar política de CART/HWR/LWR.

Escopo:

- budget por categoria:
  - track
  - car
  - audio
  - HUD
  - staging de CD

Critério de aceite:

- decisões de memória saem dos subsistemas individuais;
- footprint por categoria fica previsível.


### Fase 7 - Remoção gradual de lockstep

Objetivo:

- trocar esperas imediatas por consumo de snapshot concluído.

Estratégia:

1. simulação usa snapshot `N-1` no Master;
2. producer/sort usa draw packet `N-1`;
3. Master para de bloquear sempre que já existir pacote consistente.

Critério de aceite:

- queda visível em `simMasterWaitTicks`;
- melhora de pacing;
- sem drift perceptível de câmera/carro.


## Ordem recomendada real de execução

1. Fase 1
2. Fase 2
3. Fase 3
4. Fase 4
5. Fase 5
6. Fase 6
7. Fase 7


## Estratégia de risco

- qualquer fase que aumente o binário e quebre boot deve ser revertida imediatamente;
- priorizar extrações locais e contratos pequenos;
- não mover driver de áudio nem chamadas VDP para a Slave;
- cada fase precisa terminar com:
  - build
  - verificação do tamanho do ISO
  - teste de boot


## Próximo passo imediato

Continuar a Fase 2 sem alterar runtime:

- extrair helpers restantes de scheduler hoje presos em `GameLoopSystem`;
- reduzir duplicação entre `DrainSimulationJobIfInFlight`, `ConsumeCompletedJobs` e dispatch;
- só então iniciar a separação do preparo visual do carro.
