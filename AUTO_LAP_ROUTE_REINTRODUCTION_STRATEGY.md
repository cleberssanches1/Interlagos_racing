# AutoLap Route Reintroduction Strategy

## Objetivo

Registrar a estratégia segura que foi validada nesta branch e o ponto exato em
que a reintegração runtime mais ampla foi congelada.

## Estado final seguro

Hoje o estado aceito é:

- runtime permanece em `src/game_loop_system.hpp`
- estado runtime já existe em `src/auto_lap_route_runtime_state.hpp`
- contratos passivos já existem para frame/storage/guide/build/step
- helpers de lifecycle e build já estão extraídos
- o seam estreito atual é o estado final aceito da branch

## O que foi tentado

Foi tentado um corte runtime mais amplo para tirar mais coordenação viva do
host.

Resultado:

- build funcional
- regressão de orçamento binário
- ISO subiu de `4134912` para `4136960`

Decisão:

- revertido
- branch congelada no seam estreito atual

## O que fica aceito nesta branch

- lifecycle fora do host
- route-build/search helpers fora do host
- guide-route trace consumido no host
- ownership, timing, rebuild de yaw e stepping continuam no host

## O que não é mais trabalho obrigatório

Nesta branch, não é mais obrigatório:

- mover o stepper para fora
- integrar `AutoLapFramePacket` ao runtime vivo
- migrar ownership de rota
- entregar um handoff runtime amplo

## Regra para reabrir

Só reabrir `AutoLap` se houver:

- novo objetivo explícito
- margem binária real
- aceitação de novo tradeoff de runtime
