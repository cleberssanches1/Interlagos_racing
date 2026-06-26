# Passive to Runtime Integration Plan

## Objetivo

Transformar os recortes passivos já criados em integração real de runtime sem voltar a quebrar boot, timing ou estabilidade no emulador.

Este documento não muda o código.
Ele define:

- ordem exata de migração;
- menor unidade segura de integração;
- critérios de aceite por etapa;
- critérios de rollback imediato.


## Premissas operacionais

InventÃ¡rio consolidado de contratos passivos:

- `PASSIVE_CONTRACTS_INVENTORY.md`
- `SCHEDULER_REUSE_OBSERVABILITY_FLOW_PLAN.md`
- `SCHEDULER_REUSE_MINIMAL_LIVE_SUBSTITUTION_PLAN.md`

### Envelope estável atual

- ISO de referência: `4134912` bytes
- build deve continuar gerando boot estável
- qualquer aumento de risco em `game_loop_system.hpp` ou `car_system.cxx` exige microetapa reversível

### Regras obrigatórias

1. integrar um domínio por vez;
2. cada etapa deve reduzir ou manter o código no arquivo crítico;
3. não introduzir wrappers extras no caminho quente se isso aumentar binário;
4. compilar após toda microetapa;
5. boot falhou = rollback imediato da última microetapa.


## Inventário dos recortes passivos

### Scheduler / frame pipeline

- `src/frame_pipeline_contracts.hpp`
- `src/simulation_scheduler_state.hpp`
- `src/frame_worker_tasks.hpp`
- `src/simulation_scheduler_contracts.hpp`
- `src/simulation_scheduler_dispatch_assembler.hpp`
- `src/simulation_scheduler_telemetry_assembler.hpp`
- `SIMULATION_SCHEDULER_PLAN.md`

### Car domain

- `src/car_domain_boundaries.hpp`
- `src/car_input_assembler.hpp`
- `src/car_gameplay_state_assembler.hpp`
- `CAR_DOMAIN_REFACTOR_PLAN.md`

### Car render

- `src/car_render_contracts.hpp`
- `src/car_render_state_assembler.hpp`
- `src/car_shadow_assembler.hpp`
- `src/car_render_submitter.hpp`
- `CAR_RENDER_SYSTEM_PLAN.md`

### Track render

- `src/track_render_contracts.hpp`
- `src/track_render_state_assembler.hpp`
- `src/track_render_telemetry_assembler.hpp`
- `TRACK_RENDER_SCHEDULER_PLAN.md`

### CD assets

- `src/cd_asset_contracts.hpp`
- `src/cd_asset_request_assembler.hpp`
- `src/cd_asset_parse_assembler.hpp`
- `CD_ASSET_SYSTEM_PLAN.md`

### Memory budget

- `src/memory_budget_contracts.hpp`
- `src/memory_budget_policy_assembler.hpp`
- `src/memory_budget_telemetry_assembler.hpp`
- `MEMORY_BUDGET_SYSTEM_PLAN.md`


## Ordem real de integração

### Etapa 0 — disciplina de integração

Antes de qualquer integração real:

- congelar mudanças amplas em `main.cxx`;
- não tocar em áudio runtime;
- não misturar duas integrações no mesmo patch;
- medir sempre:
  - tamanho do ISO,
  - build,
  - boot,
  - regressão visual/funcional básica.

Critério de aceite:

- nenhum arquivo crítico cresce sem contrapartida;
- rollback da etapa é trivial.


### Etapa 1 — `SimulationScheduler` por redução líquida

#### Motivo

É o gargalo estrutural mais importante do Master/Slave.

#### Primeira integração real permitida

Não mover toda a lógica de uma vez.
Integrar apenas uma redução estrutural por microetapa:

1. substituir duplicação de marcação de completion por helper já existente em `SimulationRuntimeState`;
2. só depois consolidar materialização de packet de dispatch;
3. só depois consolidar telemetria passiva.

#### Primeira microetapa concreta

- trocar sequências repetidas de:
  - `SetJobInFlight(false)`
  - `SetHasCompleted(true)`
  - `completedIdx = inFlightIdx`
- por chamada única equivalente já existente em `SimulationRuntimeState`.

#### Ainda não fazer

- não introduzir novo objeto persistente;
- não criar façade runtime dedicada;
- não mover `ExecuteGameplayFrame` inteiro.

Critério de aceite:

- telemetria igual;
- fluxo lockstep igual;
- ISO continua no envelope estável.

Rollback imediato se:

- boot falhar;
- opcode inválido voltar;
- wait/dispatch mudar de comportamento.


### Etapa 2 — `CarRenderSystem` por extração interna mínima

#### Motivo

É o próximo melhor recorte porque o Master deve consumir estado visual pronto, mas sem mover VDP.

#### Primeira integração real permitida

- integrar somente montagem passiva do packet visual;
- manter submit final exatamente onde está;
- manter shadow draw efetivo no Master.

#### Primeira microetapa concreta

- usar `CarRenderStateAssembler` só para semear um packet local dentro do fluxo atual;
- sem remover ainda `SubmitRender`;
- sem mover chamadas de renderer.

#### Ainda não fazer

- não mover `RenderPipeline`;
- não mover `MeshRenderer`;
- não mexer em `CarAudioSystem`.

Critério de aceite:

- carro renderiza igual;
- face count permanece igual;
- não aparece drift entre yaw físico e visual.

Rollback imediato se:

- visual do carro mudar;
- boot falhar;
- `car_system.cxx` voltar a gerar opcode inválido.


### Etapa 3 — `TrackRenderScheduler` com packet explícito

#### Motivo

É pré-requisito para usar conscientemente packet `N-1` da pista.

#### Primeira integração real permitida

- integrar somente o packet/telemetria passiva;
- não mexer ainda na política de producer;
- não mudar fallback síncrono.

#### Primeira microetapa concreta

- materializar `TrackFrameContext` e `TrackRenderPacket` localmente no fluxo atual;
- manter `TrackSystem::RenderFrame(...)` intacto;
- usar telemetria passiva apenas como espelho.

Critério de aceite:

- pista idêntica;
- safe mode e fallback intactos;
- sem alteração no pacing.

Rollback imediato se:

- frame stalls aumentarem;
- pista deixar de subir;
- boot falhar.


### Etapa 4 — `CdAssetSystem` por consolidação de duplicações

#### Motivo

Baixo risco relativo e alto ganho de coesão.

#### Primeira integração real permitida

- substituir duplicações locais por chamadas já existentes em `CdAssetSystem`;
- não criar fila de jobs ainda;
- não mexer no bootstrap estrutural completo.

#### Primeira microetapa concreta

- unificar primeiro `FindExistingPath` duplicado;
- depois unificar `ReadCdBinaryFileSimple`;
- depois unificar parsing leve de `CAR1_ANCHORS.JSON`.

Critério de aceite:

- bootstrap idêntico;
- anchors continuam carregando;
- `SBA.NYA` continua resolvendo.

Rollback imediato se:

- carga do carro falhar;
- path resolution quebrar;
- boot falhar.


### Etapa 5 — `MemoryBudgetSystem` por telemetria e policy

#### Motivo

Melhora previsibilidade sem tocar cedo na lógica mais sensível de render.

#### Primeira integração real permitida

- usar contracts de snapshot/policy como espelho;
- não mover ainda `RunWorkRamMaintenance`;
- não reclassificar pressão no runtime da pista nesta primeira passagem.

#### Primeira microetapa concreta

- materializar snapshot centralizado onde hoje só há leitura direta de report;
- manter decisão concreta no local original;
- usar classificação passiva apenas para validar equivalência.

Critério de aceite:

- overlays/telemetria continuam coerentes;
- sem regressão de memory pressure;
- sem mudança em streaming.

Rollback imediato se:

- thresholds divergirem do comportamento atual;
- streaming degradar cedo demais;
- boot falhar.


## Ordem proibida

Não fazer antes do tempo:

- mover `GameLoopSystem` inteiro para helpers novos;
- mover `CarAudioSystem` agora;
- mudar driver PCM/SGL;
- remover lockstep antes de `SimulationScheduler` e `TrackRenderScheduler` estarem integrados;
- consolidar duas integrações runtime no mesmo patch.


## Critérios de aceite globais

Cada integração real só passa se:

1. build completa;
2. ISO continua em envelope estável;
3. jogo sobe no emulador;
4. não surge `Master SH2 invalid opcode`;
5. comportamento visual/áudio básico continua íntegro;
6. rollback da etapa é isolado e simples.


## Critérios de rollback imediato

Rollback sem investigação longa se ocorrer qualquer um:

- boot falhou;
- fecha ao abrir;
- `invalid opcode`;
- mudança de tamanho do ISO fora do envelope esperado;
- regressão visual evidente;
- perda de render ou travamento ao entrar no loop principal.


## Próxima ação recomendada

A próxima integração real mais segura é:

1. `SimulationScheduler` microetapa mínima de redução líquida;
2. só depois `CdAssetSystem` para remover duplicações locais de leitura;
3. só depois `MemoryBudgetSystem` em modo espelho;
4. deixar `CarRenderSystem` e `TrackRenderScheduler` para quando houver margem maior de validação.



