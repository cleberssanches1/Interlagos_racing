# SimulationScheduler Plan

## Objetivo

Separar a coordenação de dispatch, drain, completion e aplicação da simulação da fachada `GameLoopSystem`.

O objetivo desta fase não é alterar o comportamento atual.
O objetivo é preparar um recorte claro para futura extração com risco baixo.


## Estado atual

Hoje o agendamento da simulação está concentrado em `GameLoopSystem`:

- `BackoffSimulationSlaveDispatch`
- `DrainSimulationJobIfInFlight`
- `ConsumeCompletedJobs`
- `TryDispatchSimulationOnSlave`
- `ExecuteGameplayFrame`
- `RunGameplayFrameSynchronously`
- `ApplySimulationOutput`

Além disso, o runtime local mantém:

- `SimulationTask`
- `SimulationRuntimeState`
- buffers duplos `input/output`
- telemetria de wait/dispatch/backoff


## Recorte alvo

### O que deve virar `SimulationScheduler`

- materialização explícita do contexto de simulação do frame;
- decisão de usar Master síncrono ou Slave;
- decisão de dispatch e bloqueios;
- política de drain soft/hard wait;
- consumo/aplicação de output concluído;
- consolidação de telemetria de dispatch/drain/completion.

### O que deve continuar fora

**No `GameLoopSystem`:**

- orquestração macro do frame;
- composição com câmera, pista, HUD e render;
- decisão de entrar ou não no domínio de simulação.

**No `SimulationTask`:**

- execução efetiva do `Tick` + `Step`;
- trabalho bruto na Slave SH2.

**No Master SH2:**

- aplicação final do estado resolvido;
- fallback síncrono;
- ordem final de consumo por frame.


## Fronteiras internas do futuro `SimulationScheduler`

### 1. `SimulationDispatchAssembler`

Responsável por:

- materializar o contexto do frame de simulação;
- preparar `SimulationDispatchPacket`;
- explicitar por que um dispatch pode ou não ocorrer;
- preservar política de `lockstep`, `backoff` e ocupação da Slave.

### 2. `SimulationDrainAssembler`

Responsável por:

- materializar a intenção de drain;
- explicitar limites soft/hard de spin;
- separar drain oportunístico de drain obrigatório.

### 3. `SimulationTelemetryAssembler`

Responsável por:

- consolidar contadores de dispatch/drain;
- transportar flags de `job in flight` / `completed`;
- preparar terreno para futura observabilidade fora de `GameLoopSystem`.


## Dependências de entrada

- `GameplayFrameState`
- `SimulationSchedulerPolicy`
- `SimulationDispatchMode`
- `SimulationRuntimeState`
- `track producer busy`
- `car prepare busy`
- habilitação de simulação na Slave


## Dados de saída

- `SimulationFrameContext`
- `SimulationDispatchPacket`
- `SimulationDrainPacket`
- `SimulationCompletionPacket`
- `SimulationSchedulerTelemetry`


## Ordem futura de execução

1. `GameLoopSystem` decide se haverá simulação no frame
2. `SimulationDispatchAssembler` monta o contexto e packet de dispatch
3. `SimulationScheduler` decide `sync` vs `slave`
4. `SimulationDrainAssembler` materializa política de drain
5. `SimulationScheduler` consome/completa o job
6. `SimulationTelemetryAssembler` consolida a telemetria


## Critério de aceite da futura extração

- nenhuma regressão de comportamento da simulação;
- lockstep preservado;
- modo assíncrono preservado;
- fallback síncrono preservado;
- telemetria de wait/dispatch preservada;
- build continua no envelope estável.


## Restrições

- não alterar agora `GameLoopSystem` runtime;
- não mover aplicação final de estado para a Slave;
- não integrar contratos novos ao runtime nesta etapa;
- manter toda esta fase apenas documental/passiva.


## Próximo passo imediato

- criar contratos passivos do scheduler;
- criar assembler passivo de dispatch/drain/telemetria;
- só considerar integração real quando houver redução líquida de código no caminho crítico.
