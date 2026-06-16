# Responsibility Segregation Plan

## Objetivo

Refatorar o projeto sem voltar a corromper o boot nem o runtime do emulador.

O princípio desta etapa é simples:

1. primeiro separar responsabilidades em contratos e documentação;
2. depois mover dados e helpers passivos;
3. só por último alterar código que participa do frame crítico.


## Regra operacional

- Nenhuma mudança inicial deve alterar:
  - ordem do frame;
  - layout funcional do `GameLoopSystem`;
  - dispatch para Slave;
  - áudio em runtime;
  - submissão para VDP.
- Toda extração começa fora do caminho crítico.
- Toda etapa com impacto em runtime deve ser isolada e reversível.


## Problema atual

Hoje há classes com responsabilidade excessiva ou misturada:

- `GameLoopSystem`
  - orquestra frame;
  - mantém estado transitório;
  - decide fallback de simulação;
  - conhece detalhes de carro, câmera, HUD, pista e áudio.
- `CarSystem`
  - mistura entrada, snapshot de gameplay, estado visual, comandos e depuração.
- `TrackSystem`
  - concentra streaming, working set, consulta física, producer/sort e render orchestration.
- `CarAudioSystem`
  - está melhor isolado, mas ainda embute regras de interpretação do drivetrain.

Isso aumenta o risco de erro porque pequenas mudanças mudam layout e fluxo ao mesmo tempo.


## Responsabilidades alvo

### `GameLoopSystem`

Deve ser apenas orquestrador.

Responsabilidades permitidas:

- ordenar as etapas do frame;
- chamar subsistemas na ordem correta;
- carregar e repassar snapshots entre etapas;
- concentrar fallback de alto nível.

Responsabilidades proibidas:

- regra de física;
- regra de troca de marcha;
- regra de áudio;
- política de memória detalhada;
- lógica de streaming da pista.


### `CarSystem`

Deve ser adaptador entre:

- input do jogador;
- snapshot de gameplay;
- estado visual do carro;
- comandos internos do carro.

Responsabilidades permitidas:

- montar `GameplayInputSnapshot`;
- aplicar input em `GameplayFrameState`;
- sincronizar estado visual após simulação;
- expor snapshots de debug do carro.

Responsabilidades proibidas:

- emitir áudio;
- decidir pitch de motor;
- consultar pista fora das interfaces de física;
- orquestrar render global.


### `ICarPhysics`

Deve ser a autoridade do movimento.

Responsabilidades:

- atualizar RPM, velocidade, marcha, yaw e posição;
- consumir `GameplayFrameState`;
- usar apenas `ITrackCollisionQuery` como entrada externa.


### `CarAudioSystem`

Deve ser consumidor do estado autoritativo do carro.

Responsabilidades:

- ler `GameplayFrameState`;
- sintetizar pitch/volume;
- disparar cues de marcha e pneus;
- gerenciar vozes.

Não deve:

- corrigir física;
- inferir marcha real fora do snapshot;
- virar fonte de verdade de RPM.


### `TrackSystem`

Deve ser subsistema de pista e consultas espaciais.

Responsabilidades:

- streaming e carregamento;
- render window;
- producer/sort;
- surface/wall query;
- telemetria da pista.

Não deve:

- decidir áudio;
- decidir HUD;
- controlar lógica do carro.


## Fronteiras que vamos introduzir

### 1. Contratos de portas

Antes de extrair comportamento, vamos explicitar grupos de dependência:

- `FrameOrchestratorPorts`
- `SimulationPorts`
- `TrackPorts`
- `CarPorts`
- `PresentationPorts`
- `AudioPorts`

Isso reduz acoplamento implícito sem tocar no runtime.


## Mapa inicial de métodos

### `GameLoopSystem`

**Orquestração de frame**

- `RunForever`
- `FinishFrame`

**Input e montagem de frame**

- `PollFrameInput`
- `BuildGameplayFrameState`

**Coordenação de simulação**

- `ExecuteGameplayFrame`
- `RunGameplayFrameSynchronously`
- `TryDispatchSimulationOnSlave`
- `DrainSimulationJobIfInFlight`
- `ConsumeCompletedJobs`

**Aplica resultado autoritativo**

- `ApplyResolvedFrameState`
- `ApplySimulationOutput`

**Background / câmera / HUD / apresentação**

- `ScheduleCarPrepareIfEnabled`
- `UpdateBackground`
- `ResolveCameraFrameState`
- `UpdateHud`
- `RenderFrame`
- `RenderAxes`

**Telemetria e diagnóstico**

- `Capture*StageTraces`
- resets de telemetria por frame
- overlays e snapshots auxiliares


### `CarSystem`

**Entrada e snapshot**

- `ApplyGameplayInput`
- `PrepareGameplayFrameState`
- `WriteCommandsToFrameState`

**Consumo do resultado da simulação**

- `ApplySimulationFrameState`
- `SetRuntimeFrameState`
- `SyncRenderState`

**Visual local do carro**

- `SubmitRender`
- `UpdateWheels`
- `TickCommandState`

**Debug**

- `BuildDrivetrainDebugSnapshot`
- `RuntimeDebug`


### `CarAudioSystem`

**Loop do motor**

- `TickEngine`
- `ComputeEnginePitchWord`
- `SmoothRpm`

**Eventos de marcha**

- `TickGearShift`
- `TriggerShiftVoice`
- `TickVoiceLifetime`

**Pneus / derrapagem**

- `TickTire`

**Infra de voz**

- `ResolveVoice`
- `EnsureEngineVoiceStarted`
- `SetVoiceVolumePan`
- `SetVoicePitch`


### `TrackSystem`

**Bootstrap / recursos**

- `Initialize`

**Frame runtime**

- `BeginFrame`
- `RenderFrame`
- `EndFrame`

**Consultas de pista**

- `FindNearestSegment`
- `FindSurfaceYByFamilyId`
- `FindSurfaceYByFamilySet`
- `FindSurfaceContact`
- `FindPlanarWallPush`
- `FindSegmentCenterById`

**Telemetria / orçamento**

- getters de ticks, budgets e query counters
- `SetTrackSlaveMode`
- `SetRuntimeStatsLogsEnabled`


### 2. Catálogo de etapas do frame

O loop principal será descrito em estágios conceituais:

1. input
2. collect completions
3. gameplay build
4. gameplay resolve
5. background
6. camera
7. HUD
8. render submit
9. frame finalize

Primeiro isso será apenas documentação/contrato.


### 3. Estados passivos fora das classes grandes

Estados que não carregam lógica crítica devem sair primeiro:

- snapshots;
- ports;
- policies;
- descriptors de etapa.

Só depois vamos mexer em helpers com execução.


## Sequência de refatoração segura

### Fase A — sem impacto em runtime

- documentar responsabilidades;
- criar contratos passivos de portas;
- criar catálogo de etapas do frame;
- mapear o que cada método atual faz.

Critério:

- nenhuma mudança no caminho crítico de execução.


### Fase B — segregação estrutural sem alterar comportamento

- agrupar métodos do `GameLoopSystem` por domínio:
  - input;
  - simulation orchestration;
  - background/camera;
  - presentation;
  - telemetry/debug.
- mover somente tipos passivos para headers dedicados.

Critério:

- build idêntico;
- sem alteração na ordem do frame.


### Fase C — adapters finos

- introduzir wrappers finos fora do loop:
  - `FrameGameplayAssembler`
  - `FramePresentationAssembler`
  - `FrameTelemetryAssembler`

Esses wrappers apenas organizam chamadas já existentes.

Restrição adicional observada:

- a área de telemetria/overlay de fim de frame é sensível a crescimento de binário;
- uma tentativa de separar agenda de overlays em snapshots/helpers aumentou o ISO para `4136960` bytes;
- esse envelope já coincidiu com falhas de boot no emulador;
- portanto, a telemetria de fim de frame deve permanecer inline até que a redução venha por remoção real de código, e não por criação de novos wrappers no caminho crítico.


### Fase D — extrações de runtime críticas

Somente depois:

- `SimulationScheduler`
- `CarRenderPreparation`
- `TrackRenderScheduler`

Cada extração deve entrar isoladamente.


## Primeira entrega desta etapa

Nesta rodada vamos fazer apenas:

1. plano persistente;
2. contratos passivos de fronteira;
3. nenhuma alteração no caminho crítico do frame.


## Critério de aceite desta rodada

- aplicação continua usando o runtime estável atual;
- responsabilidades ficam explícitas para as próximas extrações;
- próximas mudanças passam a mexer em áreas menores e mais coesas.
