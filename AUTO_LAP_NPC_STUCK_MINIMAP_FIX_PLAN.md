# Auto Lap NPC — freeze + dessincronia minimapa (autolap46)

**Data:** 2026-10-30  
**Evidência:** `C:\vídeos\autolap46.mp4`  
**Cue de teste:** `BuildDrop\Interlagos_racing.cue`

## Problema

1. Carro 2 (NPC) trava na pista com HUD `NPC g:0 a:0 s:0`.
2. Seta do minimapa fica noutro ponto da pista.

## Causa

- Sem solo (`GS=0`), `AutoLapDrivePolicy` corta aceleração após `missingGroundFrames > 8`.
- **Mesh** usa `followers_[slot].pos` (pose física).
- **Minimapa** usava `StreamingSegmentId()` → com `GS=0` devolve `lastValidSegmentId_` (último asfalto válido).
- Recovery antiga: stall aos 24 frames sem solo; countdown 30 após `RestoreSafePose`.

`StreamingSegmentId` mantém-se para câmara/belt (não perseguir pose sem solo).

## Implementação

### Fase A — core

| Mudança | Ficheiro |
|---------|----------|
| `MinimapSegmentId(npc)` via `FindNearestSegment` | `src/npc_auto_lap_physical_driver.hpp` |
| `UpdateNpcMinimapMarkers` → `MinimapSegmentId` | `src/game_loop_system.hpp` |
| `TrySoftReplantAsphalt` em `GS=0` (Y ou rail XZ) | `src/npc_auto_lap_physical_driver.hpp` |

### Fase B — recovery agressiva

| Mudança | Ficheiro |
|---------|----------|
| Stall sem solo: 24 → **12** frames | `src/auto_lap_traffic_policy.hpp` |
| `recoveryRetryCountdown_`: 30 → **15** | `src/npc_auto_lap_physical_driver.hpp` |

### Fase C — prevenção de paragens (autolap47)

Evidência: `g:0 a:0 s:0` ~8s depois `g:1 a:1` (recuperou, mas ainda parou). Soft re-plant pós-física era tarde demais — o accel-cut já tinha travado.

| Mudança | Ficheiro |
|---------|----------|
| `EnsureNpcOnAsphalt` **antes** da drive policy quando GS=0 | `npc_auto_lap_physical_driver.hpp` |
| Seed TCOL via `FindNearestSegment`; plant Y ou snap rail + yaw | idem |
| `AdvanceHorizontal` a cada assist (não ficar em s:0) | idem |
| `drive.groundSupported \|= railAssist`; sem accel-cut com assist | idem |
| Force accel se assist e speed &lt; 40 | idem |

## Aceite

1. Na zona do autolap46, NPC não fica preso em `g:0 a:0 s:0`; recupera drive ou re-plant.
2. Seta amarela alinhada com o mesh do carro 2.
3. Player Auto Lap OK (`AL g:…`, sem `AUTO START fail`).
4. Câmara/belt ainda usam `StreamingSegmentId`.

## Reteste

1. Quit completo Mednafen.
2. Abrir só `BuildDrop\Interlagos_racing.cue`.
3. Auto Lap até à zona onde o carro 2 travava; gravar vídeo curto se falhar.
