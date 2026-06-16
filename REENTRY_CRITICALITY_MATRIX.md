# Reentry Criticality Matrix

## Objetivo

Consolidar em um único lugar:

- criticidade por arquivo;
- criticidade por domínio;
- ordem recomendada de reentrada real;
- primeira microetapa permitida;
- coisas proibidas por etapa.

Este documento fecha a fase passiva e serve como mapa operacional da próxima fase.


## Envelope atual

- ISO estável de referência: `4134912` bytes
- build aceitável:
  - `compile.bat` quando o `BuildDrop/*.bin` não estiver travado
  - `make build DEBUG=1` quando o `.bin` estiver em uso pelo emulador
- regra geral:
  - build ok não basta;
  - ISO ok não basta;
  - boot real no emulador continua sendo critério final.


## Criticidade por arquivo

| Arquivo | Criticidade | Motivo | Regra |
|---|---|---|---|
| `src/game_loop_system.hpp` | Máxima | Loop principal, scheduler, câmera, render orchestration | Não adicionar helper/método/include novo por enquanto |
| `src/main.cxx` | Máxima | Bootstrap, carga inicial, wiring de subsistemas | Não delegar helpers externos por enquanto |
| `src/car_system.cxx` | Muito alta | Sincronização de gameplay + visual do carro | Não mover responsabilidades reais ainda |
| `src/track_system.cxx` | Muito alta | Streaming, fallback, producer/sort, safe mode | Não alterar política real ainda |
| `src/car_audio_system.hpp` | Alta | Áudio runtime e vozes | Não tocar nesta fase |
| headers passivos novos | Baixa | Fora do runtime crítico | Permitidos |


## Criticidade por domínio

| Domínio | Criticidade | Arquivos sensíveis | Situação atual |
|---|---|---|---|
| `SimulationScheduler` | Máxima | `src/game_loop_system.hpp` | Reentrada só por substituição textual 1:1 |
| `CdAssetSystem` | Máxima | `src/main.cxx` | Reentrada adiada; só contratos/ops passivas |
| `TrackRenderScheduler` | Muito alta | `src/game_loop_system.hpp`, `src/track_system.cxx` | Reentrada em modo espelho primeiro |
| `CarRenderSystem` | Alta | `src/game_loop_system.hpp`, `src/car_system.cxx` | Reentrada em modo espelho primeiro |
| `MemoryBudgetSystem` | Alta | `src/game_loop_system.hpp`, `src/track_system.cxx` | Reentrada em modo espelho primeiro |


## Artefatos passivos prontos

| Domínio | Estratégia | Ops passivas |
|---|---|---|
| `SimulationScheduler` | `SCHEDULER_REINTRODUCTION_STRATEGY.md` | `src/simulation_scheduler_transition_ops.hpp` |
| `CdAssetSystem` | `CD_REINTRODUCTION_STRATEGY.md` | `src/cd_asset_transition_ops.hpp` |
| `MemoryBudgetSystem` | `MEMORY_REINTRODUCTION_STRATEGY.md` | `src/memory_budget_transition_ops.hpp` |
| `CarRenderSystem` | `CAR_RENDER_REINTRODUCTION_STRATEGY.md` | `src/car_render_transition_ops.hpp` |
| `TrackRenderScheduler` | `TRACK_RENDER_REINTRODUCTION_STRATEGY.md` | `src/track_render_transition_ops.hpp` |


## Ordem recomendada de reentrada real

### Ordem executável

1. `SimulationScheduler`
2. `CdAssetSystem`
3. `MemoryBudgetSystem`
4. `CarRenderSystem`
5. `TrackRenderScheduler`

### Justificativa

- `SimulationScheduler` é o gargalo estrutural principal e já teve tentativa real; precisa voltar primeiro, mas com regra mais rígida.
- `CdAssetSystem` tem alto valor de coesão, mas `main.cxx` é frágil; por isso volta cedo, porém só quando houver passo textual mínimo.
- `MemoryBudgetSystem` pode entrar em modo espelho antes de mudar decisões concretas.
- `CarRenderSystem` e `TrackRenderScheduler` mexem com visual/pacing e devem entrar depois.


## Primeira microetapa permitida por domínio

| Domínio | Primeira microetapa permitida | Tipo |
|---|---|---|
| `SimulationScheduler` | Substituição textual 1:1 de uma sequência repetida, sem helper novo | runtime mínimo |
| `CdAssetSystem` | Equivalência textual mínima em utilitário local, sem include novo | bootstrap mínimo |
| `MemoryBudgetSystem` | Modo espelho de snapshot/classificação sem mudar decisão concreta | espelho |
| `CarRenderSystem` | Packet visual em modo espelho sem consumo real | espelho |
| `TrackRenderScheduler` | `TrackFrameContext`/`TrackRenderPacket` em modo espelho sem alterar orquestração | espelho |


## Coisas proibidas agora

### Proibido globalmente

- misturar dois domínios runtime no mesmo patch;
- mexer em áudio runtime;
- remover lockstep;
- mover `RenderPipeline`, `TrackDrawProducer` ou chamadas VDP/PCM.

### Proibido por domínio

| Domínio | Proibido agora |
|---|---|
| `SimulationScheduler` | método novo em struct local de `src/game_loop_system.hpp` |
| `CdAssetSystem` | include novo e delegação externa em `src/main.cxx` |
| `MemoryBudgetSystem` | mover `RunWorkRamMaintenance` ou policy real da pista |
| `CarRenderSystem` | mover submit real ou shadow draw |
| `TrackRenderScheduler` | alterar `BeginFrame` / `RenderFrame` / `EndFrame` |


## Critério de aceite por tentativa real

Toda tentativa real só passa se:

1. build concluir;
2. ISO continuar em `4134912` ou no envelope explicitamente esperado;
3. jogo subir no emulador;
4. não houver `Master SH2 invalid opcode`;
5. não houver regressão visual/funcional imediata;
6. rollback da etapa for pequeno e direto.


## Critério de rollback imediato

Rollback imediato, sem investigação longa, se ocorrer:

- fecha ao abrir;
- opcode inválido;
- travamento logo após o boot;
- regressão visual evidente;
- perda de render;
- stall estrutural novo.


## Próxima ação recomendada

A próxima tentativa real correta é uma nova reentrada do `SimulationScheduler`, mas agora obedecendo esta matriz:

- uma única substituição textual 1:1;
- sem helper novo;
- sem include novo;
- sem alterar layout além do mínimo.
