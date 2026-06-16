# Car Domain Refactor Plan

## Objetivo

Segregar o domínio do carro sem tocar no loop crítico do frame nem no caminho de boot.

O domínio do carro hoje envolve quatro responsabilidades diferentes:

1. montagem de input de gameplay;
2. aplicação do estado autoritativo de simulação;
3. sincronização visual/render;
4. sonorização baseada no `GameplayFrameState`.

O objetivo é explicitar essas fronteiras antes de qualquer extração de runtime.


## Estado atual

### `Game::CarSystem`

Hoje concentra:

- snapshot de input do jogador;
- estado de comando suavizado;
- sincronização do estado de gameplay;
- sincronização do estado de render;
- snapshots de debug;
- submissão visual do carro.

### `Game::CarAudioSystem`

Hoje concentra:

- leitura do `GameplayFrameState`;
- tracking de RPM de áudio;
- disparo de marcha/pneu;
- roteamento de vozes;
- pitch/volume do motor.


## Fronteiras alvo

### `CarInputAssembler`

Responsável por:

- converter botões em `GameplayInputSnapshot`;
- manter o mapeamento de input fora do loop principal.

### `CarGameplayStateAssembler`

Responsável por:

- montar `GameplayFrameState` do carro antes da física;
- aplicar regras de auto-lap versus controle manual;
- escrever comandos de input no frame state.

### `CarSimulationStateApplier`

Responsável por:

- consumir o `GameplayFrameState` já resolvido;
- aplicar posição, yaw e snapshots de debug ao `CarSystem`.

### `CarRenderStateAssembler`

Responsável por:

- preparar posição/yaw de render;
- separar estado visual do estado físico.

Estado atual:

- recorte documentado em `CAR_RENDER_SYSTEM_PLAN.md`;
- contratos passivos preparados em `src/car_render_contracts.hpp`;
- ainda sem integração ao runtime.

### `CarAudioFrameInterpreter`

Responsável por:

- traduzir `GameplayFrameState` em sinais de áudio;
- manter `CarAudioSystem` como executor de voz, não como fonte de verdade da física.


## Sequência segura

### Fase 1 — contratos passivos

- criar headers apenas com fronteiras e contextos;
- não incluir esses headers no runtime ainda.

Status atual:

- concluída;
- `src/car_domain_boundaries.hpp` criado;
- `src/car_input_assembler.hpp` criado;
- `src/car_gameplay_state_assembler.hpp` criado;
- integração mínima feita em `CarSystem`:
  - normalização de `GameplayInputSnapshot`;
  - seed passivo de `GameplayFrameState`;
  - reset passivo de outputs de comando.

### Fase 2 — espelhamento documental

- mapear quais métodos atuais entram em cada fronteira;
- manter implementação intacta.

### Fase 3 — extrações fora do loop crítico

- mover assemblers do carro para arquivos próprios;
- deixar `GameLoopSystem` só chamando o `CarSystem`.

### Fase 4 — extrações internas do carro

- separar primeiro:
  - input assembly;
  - frame-state assembly;
  - render-state assembly.

### Fase 5 — áudio do carro

- só depois isolar a interpretação de `GameplayFrameState` para áudio;
- não tocar em vozes/canais enquanto isso.


## Mapa atual de métodos

### `CarSystem`

**Input / gameplay assembly**

- `ApplyGameplayInput`
- `PrepareGameplayFrameState`
- `WriteCommandsToFrameState`

**Simulation apply**

- `ApplySimulationFrameState`
- `SetRuntimeFrameState`

**Render sync / submit**

- `SyncRenderState`
- `SubmitRender`
- `UpdateWheels`
- `TickCommandState`

**Debug**

- `BuildDrivetrainDebugSnapshot`
- `RuntimeDebug`
- `Commands`

### `CarAudioSystem`

**Frame interpretation**

- `OnFrame`
- `TickEngine`
- `TickGearShift`
- `TickTire`

**Voice execution**

- `EnsureEngineVoiceStarted`
- `TriggerShiftVoice`
- `SetVoicePitch`
- `SetVoiceVolumePan`


## Restrição de segurança

- Nenhuma extração inicial deve alterar `GameLoopSystem`.
- Nenhuma extração inicial deve alterar `CarAudioSystem` em runtime.
- Os primeiros passos devem ser apenas contratos passivos e documentação.


## Próximo passo imediato

- integrar em seguida apenas o assembler passivo de frame-state do carro de forma mais direta;
- manter o `GameLoopSystem` intacto;
- não tocar ainda em `CarAudioSystem`;
- validar se o envelope continua em `4134912` bytes antes de qualquer passo seguinte.
