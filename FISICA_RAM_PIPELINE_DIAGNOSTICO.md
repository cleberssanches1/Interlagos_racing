# Fisica, RAM e Pipeline de Segmentos (Diagnostico Atual)

## 1. Estado Atual Validado pelos Logs

- Split SH2 esta funcionando como esperado:
  - `SH2 busy M:25% S:74%` (trabalho real).
  - `SH2 sim slv:3599 wait:3600 (100%)` (Master aguardando Slave em lockstep na simulacao).
- Isso confirma que:
  - a Master ficou com margem para pipeline/render;
  - a Slave esta carregando a maior parte do calculo pesado.

## 2. Como a Fisica Foi Implementada

## 2.1 Fluxo por Frame

1. Input e lido na Master.
2. `GameplayFrameState` e montado.
3. Simulacao (`SimulationTask`) roda na Slave:
   - `GameplayTick`
   - `SimpleCarPhysics::Step`
   - `AudioEvents`
4. Master drena/aguarda a conclusao (`DrainSimulationJobIfInFlight(true)`), aplica saida e renderiza.

Referencias:
- [game_loop_system.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\game_loop_system.hpp:940)
- [game_loop_system.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\game_loop_system.hpp:2624)
- [main.cxx](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\main.cxx:1053)

## 2.2 Modelo Fisico (SimpleCarPhysics)

- Modelo em **fixed-point** (`Fxp`) e sem alocacao dinamica por frame.
- Variaveis principais:
  - `forwardSpeed_`
  - `lateralSpeed_`
  - `yawRateDegPerFrame_`
  - `steerDeg_`
  - `surfaceYTarget_`
- Equacoes simplificadas:
  - `v += throttle * engineAccel`
  - `v -= brakeDecel` quando freando
  - `v -= dragAero(v^2) + dragRolling(v)`
  - yaw proporcional a steer e velocidade
  - deslocamento em X/Z usando `sin/cos(yaw)`
  - ajuste de Y por amostragem de superficie permitida

Referencia:
- [simple_car_physics.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\simple_car_physics.hpp:18)

## 2.3 Familias de Superficie Permitida

- O carro so faz follow de superficie usando familias de textura permitidas:
  - `F05564, F04764, F01064, F01864, F02564, F04364, F04664, F05464, F00164, F00264, F00364, F00464, F00564, F06164, F06264, F06364`
- Isso e resolvido por `SampleSurfaceYByFamilySet`.

Referencias:
- [interfaces.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\interfaces.hpp:83)
- [track_collision_query.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\track_collision_query.hpp:86)
- [simple_car_physics.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\simple_car_physics.hpp:113)

## 2.4 Calibracao de Movimento Aplicada

Para remover a sensacao de carro lento no C:

- `kEngineAccelPerFrame`: `~0.045 -> ~0.110`
- `kBrakeDecelPerFrame`: `~0.097 -> ~0.220`
- `kAeroDragCoeff`: `~0.0041 -> ~0.0020`
- `kRollingDragCoeff`: `~0.0122 -> ~0.0065`
- `kCoastDampingPerFrame`: `~0.010 -> ~0.004`
- `kMaxForwardSpeed`: `~2.625 -> ~3.5`

Referencia:
- [simple_car_physics.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\simple_car_physics.hpp:216)

## 3. Custo de Memoria e Processamento da Fisica

## 3.1 Memoria (Fisica)

- `SimpleCarPhysics` mantem estado pequeno (poucos `Fxp` + flags).
- Tabela `kDriveableFamilies`: 16 IDs (`uint16_t`) em `rodata` (32 bytes).
- Sem `std::vector` por frame no caminho de fisica.
- Buffers de simulacao sao fixos (`simInput_[2]`, `simOutput_[2]`), sem churn de allocator.

## 3.2 CPU

- Fisica por frame e **O(1)** (operacoes escalares + trigonometria).
- Custo variavel vem de `SampleSurfaceYByFamilySet`, por chamar busca de superficie por familia.
- Esse custo foi amortizado com `kSurfaceProbeIntervalFrames = 2`.

Referencia:
- [simple_car_physics.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\simple_car_physics.hpp:215)

## 4. O que esta usando HWR e LWR hoje

## 4.1 High Work RAM (HWR)

No runtime, os maiores grupos sao monitorados por tags:
- `TrackCore`, `TrackPrepare`, `TrackTexture`, `TrackBackend`
- `Gameplay`, `AutoLap`, `Background`, `Hud`, `Car`

Referencia:
- [game_loop_system.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\game_loop_system.hpp:404)

## 4.2 Low Work RAM (LWR)

Predominancia em:
- estruturas persistentes do `TrackSystem` (`TrackLowWorkVector`, `TrackRenderer`, estados de LOD/familia/slots);
- filas de reuso/aposentadoria de slots de textura;
- scratch buffers de slide/prefetch e listas do producer/sort.

Referencias:
- [track_system.hpp](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\track_system.hpp:611)
- [track_system.cxx](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\track_system.cxx:1892)

## 5. Causa Provavel da Queda de Frame (FPS ~20)

Com base nos logs:
- `SDR miss id 291`, `cache entries:0` indica miss de streaming/residencia de segmento.
- Quando ha miss, pipeline entra em reconstrucao/recuperacao e isso pressiona frame-time.
- Mesmo com SH2 bem dividido, a cadeia de:
  - slide
  - prefetch
  - rebuild de residencia de textura
  - manutencao de memoria
  ainda pode consumir o orcamento dos ~50ms.

Referencias:
- [track_system.cxx](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\track_system.cxx:9547)
- [track_system.cxx](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\track_system.cxx:10519)
- [track_system.cxx](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\track_system.cxx:13896)

## 6. Melhorias Objetivas para Pipeline (Proxima Etapa)

1. Reduzir `SDR miss` com prefetch orientado a velocidade do carro:
   - aumentar budget de prefetch dinamicamente em alta velocidade;
   - priorizar `next + lookahead` no sentido da progressao.

2. Diminuir churn de slots:
   - manter fila de reuso estavel e reduzir `retire/rebuild` agressivo.

3. Cadenciar manutencao pesada:
   - executar `RunWorkRamMaintenance` fora de janelas criticas de slide.

4. Revisar tamanho de janela ativa:
   - hoje esta em modo fixo de 10 segmentos ([main.cxx](c:\saturn\SaturnRingLib-main\Projects\Interlagos_racing\src\main.cxx:853)).
   - avaliar 12-14 apenas se reduzir miss sem estourar CPU/memoria.

5. Telemetria continua para decidir ajustes:
   - `SH2 busy`
   - `SH2 sim slv/wait`
   - `PrefetchBuildBudget`, `PrefetchBuildBudgetDrops`
   - contagem de `SDR miss`.

## 7. Resumo

- A divisao SH2 foi bem-sucedida.
- A fisica esta implementada com custo baixo e deterministico para Saturn.
- O principal gargalo atual nao e mais falta de Slave, e sim miss/churn no pipeline de segmentos e residencia de textura.
- A calibracao inicial de aceleracao/frenagem ja foi aplicada para melhorar a dirigibilidade no botao C.
