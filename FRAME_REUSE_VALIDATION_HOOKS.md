# Frame Reuse Validation Hooks

## Objetivo

Validar as decisões de reuso `N-1` sem tocar no loop crítico do frame.

Este recorte existe para preparar a futura reentrada em:

- `SimulationScheduler`
- `TrackRenderScheduler`
- consumo assíncrono de snapshot concluído


## Peças criadas

### Telemetria passiva fora do `GameLoopSystem`

- `src/frame_reuse_telemetry_assembler.hpp`

Responsabilidade:

- zerar telemetria de reuso;
- registrar consumo lockstep;
- registrar consumo `N-1`;
- registrar fallback síncrono;
- manter essa lógica fora do `game_loop_system.hpp`.


### Modelo puro de decisão

- `src/frame_reuse_decision_model.hpp`

Responsabilidade:

- modelar a decisão de reuso com tipos triviais;
- permitir validação host-side sem depender de `srl.hpp`;
- servir de hook auxiliar antes da reentrada real no runtime.


### Test hook host-side

- `tests/frame_reuse_decision_tests.cpp`
- `tools/run_frame_reuse_decision_tests.ps1`

Cobertura atual:

- lockstep consome apenas frame exato;
- modo async pode consumir `N-1`;
- fallback síncrono quando não há packet comprometido;
- pista em lockstep espera packet exato;
- pista em async aceita packet anterior consistente.


## Como executar

```powershell
.\tools\run_frame_reuse_decision_tests.ps1
```


## Recorte futuro em runtime

Quando houver margem de envelope em `game_loop_system.hpp`, a ordem segura continua sendo:

1. manter commits reais fora de wrappers grandes;
2. usar `frame_reuse_contracts.hpp` para histórico;
3. usar `frame_reuse_telemetry_assembler.hpp` para observabilidade;
4. só então reintroduzir espelhamento local em `GameLoopSystem`.


## Observação

Uma tentativa anterior de espelhamento runtime do `N-1` elevou a ISO para `4136960`.

Por isso, nesta etapa:

- a decisão foi mantida fora do loop crítico;
- a validação foi movida para hook host-side;
- a telemetria futura foi preparada em header passivo separado.
