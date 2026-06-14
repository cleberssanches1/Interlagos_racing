# Auditoria de Refatoração — Aceleração e Som

## Objetivo

Reduzir acoplamento entre entrada, física longitudinal, câmbio, RPM, telemetria e áudio, sem reabrir o risco recente de regressão de boot no emulador.

## Fluxo atual

### 1. Entrada do jogador

- `src/car_system.cxx`
- `CarCommandAdapter::Accelerate()`, `Brake()`, `SteerLeft()`, `SteerRight()`
- `CarSystem::ApplyGameplayInput()`
- `CarSystem::TickCommandState()`

Responsabilidades atuais:

- ler input digital
- simular pressão progressiva no acelerador
- atualizar estado de freio
- montar `GameplayFrameState`

### 2. Física do carro

- `src/car_dynamics_model.hpp`
- `DynamicsModel::IntegratePlanar()`

Responsabilidades atuais:

- calcular RPM alvo
- tratar neutro, ré e marchas à frente
- processar trocas manuais e automáticas
- aplicar aceleração longitudinal
- aplicar arrasto e coast damping
- atualizar yaw, lateral, skid e drift
- publicar telemetria para HUD e áudio

### 3. Som do carro

- `src/car_audio_system.hpp`
- `CarAudioSystem::OnFrame()`

Responsabilidades atuais:

- carregar assets PCM
- manter vozes do motor, troca e pneus
- detectar troca de marcha via `GameplayFrameState`
- calcular pitch do motor
- aplicar smoothing no RPM de áudio

## Principais pontos de acoplamento

### A. `IntegratePlanar()` concentra responsabilidades demais

Hoje uma única função concentra:

- drivetrain
- engine RPM
- câmbio
- força longitudinal
- força lateral
- yaw
- telemetria

Efeito:

- manutenção difícil
- risco alto em qualquer alteração pequena
- crescimento de código inline
- pior leitura de bugs de marcha, RPM e som

### B. Áudio depende diretamente de `GameplayFrameState`

`CarAudioSystem` lê:

- `carGear`
- `carEngineRpm`
- `carShiftFrames`
- `carShiftRpmBefore`
- `carShiftRpmAfter`
- `carSpeedKmh`
- `throttle`
- `braking`

Efeito:

- o sistema de som conhece detalhes demais do frame geral
- mudanças na física e HUD acabam impactando áudio
- difícil isolar bugs de voz/loop de bugs de drivetrain

### C. Regras de marcha e RPM estão espalhadas

Hoje a lógica está dividida entre:

- `src/car_physics_shared.hpp`
- `src/car_dynamics_model.hpp`
- `src/car_audio_system.hpp`

Efeito:

- difícil saber qual é a fonte de verdade do RPM
- áudio às vezes precisa compensar comportamento da física
- mais chance de duplicação de regra

## Hotspots de performance

### 1. `DynamicsModel::IntegratePlanar()`

Hotspot principal de CPU e de risco arquitetural.

Melhorias possíveis:

- extrair helpers privados estáticos para:
  - seleção de marcha
  - cálculo de RPM alvo
  - cálculo de RPM pós-troca
  - aceleração longitudinal
  - arrasto longitudinal
  - publicação de drivetrain
- reduzir recomputação de condições:
  - `stationaryNoThrottle`
  - `isNeutralGearSelected`
  - `isReverseGearSelected`
  - `speedKmhAbs`

### 2. Lambdas locais grandes dentro de `IntegratePlanar()`

Hoje existem lambdas locais para:

- `smoothRpmToward`
- `computeRpmTargetForGear`
- `gearTopSpeedForShift`
- `computeHighSpeedBlend`
- `computeForwardDragForGear`
- `computePostShiftRpm`

Risco:

- aumento de tamanho de função
- pior locality de instrução
- manutenção mais difícil

### 3. `CarAudioSystem` mistura controle de vozes com regra de negócio

Melhorias possíveis:

- separar helpers de voz:
  - `UpdateEngineVoice()`
  - `UpdateShiftVoices()`
  - `UpdateTireVoice()`
- separar helpers de regra:
  - `ShouldPlayShiftSound()`
  - `ShouldSkid()`
  - `ComputeAudioRpmTarget()`

## Estrutura alvo recomendada

### Física

Separar `IntegratePlanar()` em blocos claros:

1. `ResolveDriverIntent`
2. `UpdateGearSelection`
3. `UpdateEngineRpm`
4. `ApplyLongitudinalDrive`
5. `ApplyLongitudinalDrag`
6. `ApplyLateralAndYaw`
7. `PublishAuthoritativeState`

### Áudio

Separar `CarAudioSystem` em:

1. aquisição de snapshot do carro
2. atualização do motor
3. atualização de troca
4. atualização de pneus

### Interface entre física e áudio

Criar futuramente um snapshot dedicado, por exemplo:

- `CarDrivetrainAudioState`

Campos mínimos:

- `gear`
- `engineRpm`
- `speedKmh`
- `shiftFrames`
- `shiftRpmBefore`
- `shiftRpmAfter`
- `throttleApplied`
- `braking`

Benefício:

- áudio deixa de depender do `GameplayFrameState` completo
- menor acoplamento
- depuração mais simples

## Sequência segura de refatoração

### Fase 1 — sem mudança de comportamento

- extrair helpers estáticos privados
- manter assinaturas e dados atuais
- não mudar fórmulas

### Fase 2 — reduzir acoplamento

- introduzir snapshot dedicado entre física e áudio
- `CarAudioSystem` passa a depender desse snapshot

### Fase 3 — otimização de regras

- consolidar RPM e marchas como fonte única de verdade
- remover compensações de áudio que tentem corrigir física

### Fase 4 — tunables

- dividir `Tunables` em grupos:
  - drivetrain
  - handling
  - contact/ground

## Recomendação prática

Dado o histórico recente de sensibilidade do build/boot:

- priorizar primeiro refatoração estrutural sem alteração de comportamento
- validar boot do emulador a cada fase
- só depois ajustar curva de RPM, lift-off e smoothing de áudio

## Critério de aceite

Uma refatoração aceitável precisa preservar:

- boot no emulador
- som do motor contínuo
- som de troca sem loop preso
- HUD de marcha e RPM
- velocidades e trocas atuais

