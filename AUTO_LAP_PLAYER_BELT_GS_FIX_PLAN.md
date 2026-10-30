# Auto Lap — jogador para + segmentos somem (autolap48)

**Data:** 2026-10-30  
**Evidência:** `C:\vídeos\autolap48.mp4` (carro 1 / vermelho)  
**Cue:** `BuildDrop\Interlagos_racing.cue`

## Cadeia observada

1. NPC `g:0 a:0 s:0`
2. ~1s depois jogador `AL g:0 a:0 s:0`
3. Segmentos rarefeitos à volta do jogador
4. Jogador recupera `g:1`; pista reconstrói

## Causas

- Assist NPC (`EnsureNpcOnAsphalt`) usava `FindSurfaceY` com **shared face cache** → envenenava plant do player.
- Player Auto Lap corta accel com `missingGroundFrames > 8`.
- Esteira ancorada em `latestActiveSegmentId_` de um carro parado → janela vazia.

## Fix implementado

| Fase | Mudança |
|------|---------|
| 0 | NPC: `useSharedFaceCache=false`; assist no máximo 1×/2 frames |
| 1 | `playerLastValidSegmentId_` / safe pose; `RelocateFocusWindow` se live fora da janela |
| 2 | `TryPlayerSoftPlantAsphalt` **antes** de `AutoLapDrivePolicy::Step`; grace 16; force accel se plant |
| 3 | Throttle + cache isolada (contém o assist) |

## Aceite

1. Jogador não fica `AL g:0 a:0 s:0` vários segundos na zona do autolap48.
2. Pista continua à volta do jogador (sem buraco que “reconstrói” depois).
3. NPC pode falhar GS sozinho sem derrubar o player.

## autolap49 — carro perdido no vazio

Evidência: `C:\vídeos\autolap49.mp4` — após `AL g:0`, a esteira some e o carro 1 flutua no void (~f40→f42). Soft-plant falhou porque TCOL não tinha o segmento carregado; `RecoverStalledAutoLapCar` esperava ~90 frames e fazia `FindSurfaceY` **sem** `RelocateFocusWindow`.

| Fix | Detalhe |
|-----|---------|
| Trigger | `missingGroundFrames >= 20` **ou** TickStall (agora **45** frames, era 90) |
| Ordem | `RelocateFocusWindow(rail seed)` **antes** do plant Y |
| Plant | `allowFallback=true`, `useSharedFaceCache=false` |
| Fallback | Teleporte PATH XZ mesmo se Y falhar no último advance |
| Cooldown | retry 15 frames (era 30) |

## autolap50 — looping a “procurar o trilho”

Evidência: carro 1 em poses estranhas / perto de barreiras com `AL g:0`, depois salta para a pista. Causa: hard-recover fazia **RelocateFocusWindow por cada candidato** (0..8), índice PATH stale, soft-plant a lutar com hard-recover, cooldown 15 → teleporte em ciclo. NPC `AdvanceHorizontal` a cada assist agravava o aspecto de “busca”.

| Fix estável | Detalhe |
|-------------|---------|
| Índice | `ResolvePlayerRailRecoverIndex`: lastValid id → nearest PATH → route.index |
| Relocate | **Uma vez** no âncora; depois só plant nos +0..+4 |
| Anti-loop | Ignorar soft-plant se hard-recover armado; countdown **60**; skip se `AwaitingPublish` |
| Trigger GS | hard recover a partir de **24** missing frames |
| NPC | Sem `AdvanceHorizontal` no assist; recover cooldown **60**; **nunca** RelocateFocusWindow (belt = carro 1) |
