# POC de Fisica (Projeto Interlagos)

## Objetivo
- Isolar a evolucao de fisica em um modo POC dentro do mesmo projeto.
- Desligar o fluxo principal (streaming completo da pista) temporariamente.
- Validar comportamento de subida/descida com baixo custo de CPU/memoria.

## Como habilitar
- `PHYSICS_POC_MODE=1` no `makefile` (default nesta branch).
- Para voltar ao fluxo principal: `make PHYSICS_POC_MODE=0`.

## O que o modo POC faz
- Mantem inicializacao normal de engine/input/render.
- Carrega carro + HUD + camera + ceu.
- Inicializa `TrackSystem` real (arquivos em `cd/data`) para render da pista POC.
- Usa fallback sintetico apenas se a carga da pista falhar.
- Usa `PocTrackCollisionQuery` (pista analitica sintetica):
  - loop de 2048 unidades no eixo Z
  - trechos retos, subida, topo, descida e vale
  - IDs de segmento logicos (1..32) so para telemetria
- Usa `SimpleCarPhysics` com `PocGameplayTick`:
  - yaw inicial de gameplay em `180` graus
  - auto-cruise leve quando sem entrada manual
  - respawn simples por desvio lateral exagerado

## Sequencia de frame (POC)
1. Poll de input.
2. Gameplay tick POC (spawn/yaw/autocruise).
3. Fisica planar + adesao vertical (`SimpleCarPhysics`).
4. Atualizacao de camera/HUD.
5. Render da pista via `TrackSystem` + carro + background.

## Responsabilidades por modulo
- `PocTrackCollisionQuery`: resposta de solo (Y/segmento/normal) sem varredura de faces.
- `PocGameplayTick`: regras de sessao POC (start/auto cruise/reset lateral).
- `SimpleCarPhysics`: dinamica + ground follow (reuso do modulo principal).
- `GameLoopSystem`: loop, input, telemetria e render orchestration.

## Custo esperado
- CPU menor que o fluxo completo por remover consultas de solo sobre N faces.
- Menor pressao de HWR/LWR por nao manter streaming pesado de segmentos.
- Ambiente ideal para calibrar fisica antes de reintegrar no jogo completo.

## Proximos passos (reintegracao)
1. Consolidar parametros de fisica no POC (aceleracao/freio/esterco).
2. Validar aderencia em subida/descida sem oscilacao.
3. Portar ajustes para query por faces no modo principal (com cache por segmento/face).
4. Reativar gradualmente pipeline principal mantendo benchmark de FPS.
