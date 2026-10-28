# Plano: 2 carros NPC nos trilhos laterais do PATH.NYA

**Data:** 2026-10-14  
**Pedido:** Auto Lap já funciona com **1 carro** (trilho do meio). Criar **2 instâncias** que se deslocam nos **outros dois trilhos** do `PATH.NYA`.  
**Doc de tentativa:** `C:\saturn\Tentativa implementacao outros carros no jogo.txt`  
**1º entregável:** pose-follow + mesh compartilhada (sem física ×3).

---

## Objetivo

1. Carro do meio: **inalterado** (Auto Lap físico atual).
2. Dois followers nos trilhos **0 e 2**: avançam por waypoints (pose XZ + yaw).
3. Desenho: **mesmo** `ModelObject*` / texturas do CAR1; 2× `MeshRenderer`.
4. Sem: 2ª carga CAR1, física ×3, áudio ×3, colisão carro–carro.
5. Sem mudar LOD da esteira / TCOL / bake.

## Guardrails (lições da tentativa anterior)

| Sintoma | Guardrail |
|---------|-----------|
| `AUTO START fail:5` | NPCs **depois** da largada do player; não alterar `InitializeAutoLapPhysicalStart` nas Fases 0–2 |
| `r:0,0` / simplify LWR | Side rails **sem** `SimplifyGuideLine`; validar ponto[0] ≠ (0,0) |
| Acoplar NPC na init | Flag `AUTO_LAP_NPC_RAILS`; fases separadas |

## Fases

### Fase 0 — 3 rotas no runtime 【corrigida】

- **Count-only F0:** enquanto `guideLines` vivem, só grava `n0/n2` (uint16).
  **Não** aloca `centers` laterais no start (LWR peak → `GS=0` / piloto parado).
- Player continua no trilho 1.
- Após largada OK: HUD `AUTO RAILS n0/n1/n2`, `AUTO NPC ready:N`,
  `AUTO NPC count-only F0`.
- Falha de largada: `AUTO START fail:N` (1..5).
- Drive HUD: `AL g:m:a:s` (ground, missing frames, accel, speed).
- Flag makefile: **`AUTO_LAP_NPC_RAILS ?= 0`** (default). NPC regressou o start
  (`log1.png` / `telemetrias_3`: `AUTO START fail:2`, carro parado). Baseline =
  Autolap8 com flag off.
- Reativar NPC só com `make AUTO_LAP_NPC_RAILS=1` após novo desenho de storage.

**Aceite F0/baseline:** 1 carro anda (Autolap8); sem `AUTO START fail`.

### Fase 1 — Followers de pose 【validada — Autolap10/11】

**Meta:** 2 followers nos trilhos 0 e 2 + telemetria; sem mesh.  
**Baseline:** Autolap8/9 com `AUTO_LAP_NPC_RAILS=0`.

**Regressões evitadas:** NPC fora do `Ensure` / start; armamento só após gate.

#### Gate de estabilidade (obrigatório)

No **frame loop**, só após N frames consecutivos (~30) com:

- Auto Lap físico ligado  
- `AL g:1` e movimento (`a:1` ou speed > 0)

→ `ArmNpcRailsOnce()` **uma vez**:

1. Reload PATH do CD + parse  
2. Laterais 0/2 em **High Work RAM** (`NpcRailRoute`)  
3. Bind followers (índices 8 e 20)  
4. HUD `AUTO NPC pose F1` / `AUTO RAILS`

**Proibido** no build/Ensure/`InitializeAutoLapPhysicalStart` / else imediato do start:
Capture, Hold, Reload, alloc de laterais.

#### Runtime

- `NpcRailFollower::Step` ×2; Y solo a cada 8 frames  
- HUD `NPC i0/i1`; preservar `AL g:m:a:s`  
- Toggle Auto Lap off → disarm + free HWR  

#### Aceite F1 【OK】

- [x] Flag 0 = Autolap8/9 (player OK, sem pose F1)  
- [x] Flag 1 = Autolap10/11: `AUTO RAILS 321/321/321`, `NPC 8/20`→`27/38`, `AL g:1 a:1 s:~49`  
- [x] ISO de teste: `BuildDrop_npc_rails`

### Fase 2 — Render mesh compartilhada 【implementada — revalidar spawn ahead】

1. `EnsureNpcCarRenderers`: 2× `MeshRenderer` no **mesmo** `ModelObject*` + `modelCenter` do player.  
2. `RenderNpcCars` após o submit do player (yaw + offset visual).  
3. Sem wheel rig / áudio / sombra nos NPCs no 1º corte.  
4. HUD: `AUTO NPC mesh F2` no armamento; `NPCF f0/f1 d:dx/dz` (faces + offset vs player).  
5. Spawn: nearest no trilho lateral + **8 pontos (~1 segmento) à frente**; step ≥ player baseline.  
6. ISO teste: `BuildDrop_npc_rails` com `AUTO_LAP_NPC_RAILS=1`.

**Sintoma telemetrias_4/5:** pose F1 + NPC índices OK, só o carro do meio visível. `NPCF` na linha 1,19 era sobrescrito por `OVR seq`; `mesh F2` na 1,20 por `W# h#`.

**Visibilidade:** draw dos NPCs **antes da pista** (orçamento VDP1); `modelCenter` do player; HUD `NPCF … rail` em (0,15); setas NPC no minimapa.

**Pose:** `npc = player + (side[i] − mid[i])` (guides 0/2 vs 1). Autolap12: `d:0/0`; Autolap13: `d:55/-126 rel` + faces OK mas invisível (draw **antes** da pista).

**Draw (Fase A):** NPCs no **mesmo `RenderPipeline` pós-pista** que o player (`EnqueueNpcCars` → player → `Flush`). HUD `NPCF … post`.

**Aceite:** 3 carros — um por trilho PATH; `NPCF … post` com `|d|` ~50–120; 3 setas no minimapa.

### Fase 3 — Polimento

- Offset/pacing por trilho; física leve só se necessário; colisão carro–carro por último.

## Arquivos Fase 0

| Arquivo | Papel |
|---------|-------|
| `src/auto_lap_npc_rail_policy.hpp` | Índices dos trilhos laterais + validação XZ |
| `src/auto_lap_route_build_ops.hpp` | `TryPopulateNpcRailRouteFromGuideLine` (sem simplify) |
| `src/auto_lap_route_lifecycle_ops.hpp` | `ReleaseAutoLapNpcRailRoutes` |
| `src/game_loop_system.hpp` | Build/clear/telemetria das side routes |
| `makefile` | `-DAUTO_LAP_NPC_RAILS=$(AUTO_LAP_NPC_RAILS)` |
| `tests/auto_lap_npc_rail_policy_tests.cpp` | Host tests da policy |
