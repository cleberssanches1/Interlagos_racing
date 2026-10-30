# Auto Lap — Ground detection G1/G2

**Data:** 2026-10-30  
**Cue:** `BuildDrop/Interlagos_racing.cue` (DEBUG, `AUTO_LAP_NPC_RAILS=1`, `SLIM=0`)  
**Fingerprint:** HUD `ISO NPC a:…`, `AL g:%u m:%u a:%u s:%d`, `TCOL ok`

## Problema
Flicker de GS (1–N frames sem plant TCOL) cortava accel Auto Lap (`missingGround > 8`), congelava NPC/player e, com cache partilhada, o assist do NPC envenenava o player (autolap46–50).

## O que NÃO fazer
- Dual-mode GEO/FSMAP com TCOL válido  
- Abrir janela seed±8/±12 sem evidência de seed OK  
- Rewrite do `CarGroundFollower`

## Entregue

### G1 — Seed + cache
| ID | Mudança | Ficheiro |
|----|---------|----------|
| G1.1 | Seed player = `autoLapRoute_.ids[index]` antes da física | `game_loop_system.hpp` |
| G1.1b | `UpdateTarget` prefere `activeSegmentId` a `Sample()` | `car_ground_follower.hpp` |
| G1.2 | Seed NPC = `lastValid` / `FindNearestSegment` (LWR sem ids) | `npc_auto_lap_physical_driver.hpp` |
| G1.3 | `SetIsolateFaceCache(true)` no Bind; probes com `useSharedFaceCache=false` | `track_collision_query.hpp`, NPC driver |

### G2 — Política flicker
| ID | Mudança |
|----|---------|
| G2.1/G2.3 | Near-rail XZ &lt; 96u + missing&lt;16 → `groundSupported` para policy (player+NPC); corte pós-Step respeita `flickerGrace` |
| G2.2 | Sticky both-axle 8→12 se `lastSurfaceType==asphalt` e `carSpeedKmh>20` (`car_physics_shared.hpp` / follower) |

## Aceite (reteste Mednafen)
1. **Hard-quit** Mednafen e abrir `BuildDrop\Interlagos_racing.cue` (não Reload).  
2. Confirmar HUD `ISO NPC` (não `slim` / `rails0`).  
3. Auto Lap com 1 NPC: flicker 2–5 frames **não** produz `AL g:0` → `a:0` → `s:0` em recta.  
4. Com NPC em `g:0` breve, player mantém `g:1` / movimento.  
5. Sem looping de recover (autolap50) nem void (autolap49).

## Seguinte (se aceite falhar)
- **G0:** HUD `seed:` / contador `gsFlicker`  
- **G3:** bake TCOL cobertura side-rails / seams  
- Soft plant / hard recover já estáveis — não reabrir Relocate por candidato
