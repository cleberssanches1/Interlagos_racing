# AutoLap Route Reintroduction Strategy

## Objetivo

Definir uma estratégia segura para reintroduzir partes do `AutoLapRouteSystem` fora de `GameLoopSystem`, sem repetir os problemas de boot/runtime vistos em mudanças pequenas no caminho crítico.


## Estado atual seguro

Hoje o estado está dividido assim:

- runtime ainda permanece em `src/game_loop_system.hpp`
- estado runtime já foi extraído para `src/auto_lap_route_runtime_state.hpp`
- contratos passivos já existem em:
  - `src/auto_lap_route_contracts.hpp`
  - `src/auto_lap_route_state_assembler.hpp`
  - `src/auto_lap_route_transition_ops.hpp`
  - `src/auto_lap_route_lifecycle_ops.hpp`

Essa é a linha segura atual: documentação + contratos + estado passivo, sem integração no fluxo crítico.


## Por que o lifecycle é o primeiro alvo

O lifecycle do auto-lap tem baixo acoplamento funcional:

- reset de flags
- reset de escalares
- clear de buffers
- release de guide lines
- retenção de storage

Esses pontos:

- não decidem movimento do carro;
- não calculam yaw;
- não tocam render;
- não dependem da ordem do frame;
- têm resultado fácil de verificar.

Por isso são o melhor primeiro ponto de reintegração real quando quisermos sair da fase apenas passiva.


## Ordem segura de reintegração

### Etapa 1 — Substituição textual 1:1 do lifecycle

Trocar, uma por vez, as implementações internas de:

- `HasRetainedAutoLapRouteStorage`
- `ResetAutoLapRouteFlags`
- `ResetAutoLapRouteScalars`
- `ResetAutoLapRouteState`
- `ClearAutoLapRouteBuffers`
- `ReleaseAutoLapGuideLines`
- `ReleaseAutoLapRouteStorage`

por chamadas equivalentes para `AutoLapRouteDomain::*`.

Regra:

- uma função por vez;
- rebuild;
- validação no emulador;
- só depois seguir para a próxima.

### Etapa 2 — Builders passivos

Só depois do lifecycle estável:

- snapshots de storage;
- packets de guide load;
- packets de build.

Status atual:

- `src/auto_lap_route_transition_ops.hpp` já consegue materializar
  `AutoLapRouteStorageSnapshot` diretamente de `AutoLapRouteState`,
  sem depender do runtime crítico.
- o mesmo header agora também consegue materializar
  `AutoLapRouteBuildPacket` e `AutoLapRouteStepPacket`
  diretamente de `AutoLapRouteState` + contexto mínimo externo.

Ainda sem mover lógica principal, só tornando fronteiras observáveis.

### Etapa 3 — Stepper

Somente por último:

- init route;
- advance planar/vertical;
- waypoint window;
- heading update.

Esse é o trecho com maior risco funcional e deve continuar no `GameLoopSystem` até o lifecycle e os builders estarem estabilizados.


## O que não fazer agora

- não integrar os headers passivos diretamente em blocos grandes do runtime;
- não mover a lógica de step do auto-lap para fora de uma vez;
- não substituir várias funções do lifecycle em um único passo;
- não tocar simultaneamente em câmera, render e auto-lap.


## Critério de aceite por micro-passo

- build fecha;
- `BuildDrop/Interlagos_racing.iso` volta para `4134912`;
- boot no emulador continua estável;
- auto-lap continua ligando/desligando sem regressão observável.
