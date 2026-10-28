# Plano de ação — NPCs PATH 0/2 (PATH em LWR + pose-follow)

**Data:** 2026-10-28  
**Pedido:** 2 carros NPC nos trilhos laterais do `PATH.NYA` (0/2); player no trilho 1; pose-follow barato; mesh clone/reuso; sem física×3; sem LOD/TCOL/bake.  
**Fix fail:2 escolhido:** carregar `PATH.NYA` em LWR/buffer fixo + enxugar `.text`.

---

## Objetivo de produto

1. Player Auto Lap no trilho PATH **1** intacto (`AL g:1`, sem `AUTO START fail:2`).
2. Dois followers de pose nos trilhos **0** e **2**.
3. Apresentação: cruzes 2D na Fase 1; clone/mesh pós-pista na Fase 2.
4. Minimapa: 3 setas (Fase 3).
5. `AUTO_LAP_NPC_RAILS=0` permanece baseline Autolap9.

---

## Evidência bloqueante (telemetrias_7)

| Item | Valor |
|------|-------|
| HUD | `ISO BASE rails0` + `AUTO START fail:2` / `ENSURE b:0` |
| heapFree npc_rails | ≈14 KiB |
| heapFree BuildDrop histórico OK | ≈33 KiB (Autolap9) |
| Causa Ensure | `PATH.NYA` (~12 KiB) em `std::vector` no HWR → resize falha |

---

## Fase 0 — Recuperar Auto Lap (PATH → LWR) 【aplicada】

### 0.1 Código

- `GuideFileBytes = TrackLowWorkVector<uint8_t>`
- `ReadGuideFileToLowWork` / `TryLoadGuideBytes` / `TryParseGuideBytes`
- `LoadAutoLapGuideLines` e `ArmNpcRailsOnce` usam LWR para bytes do ficheiro
- Removidos hooks pesados `SRL_NoteScene3DPut` / diag Scene3D

### 0.2 ISO controlo

- `make build DEBUG=1 AUTO_LAP_NPC_RAILS=0 BUILD_DROP=./BuildDrop_npc_rails`
- Fingerprint HUD: `ISO BASE rails0`
- Artefacto: `BuildDrop_npc_rails\Interlagos_racing.cue` (bin ~14:13)

### 0.3 Retest (utilizador)

1. Fechar Mednafen por completo.
2. Abrir **só** `BuildDrop_npc_rails\Interlagos_racing.cue`.
3. Confirmar HUD `ISO BASE rails0`.
4. Aceite: pista + carro + Auto Lap anda; **sem** `fail:2`; `AUTO ENSURE` ok / `AUTO RAILS` com n1>0.

**Nota:** heapFree continua ≈14 KiB; o Ensure deixa de precisar dos ~12 KiB de PATH no HWR.

---

## Fase 1 — Pose-follow 0/2 + cruzes 【PASS — autolap27 pose4】

Evidência: `ISO NPC pose4 a:11 v:11 r:2`, `LAT71/-76`, `BX-886 NX-815` (NX=BX+71), cruzes OK.
Mesh segue em `AUTO_LAP_NPC_MESH1_ACTION_PLAN.md` (reuse MeshRenderer).

### Guardrails de heap (aplicados)

- Armar **depois** do gate (~30 frames com ground + motion).
- `PathLine` / parse PATH em **LWR** (não HWR); parse sequencial base8→base12 (sem pico duplo).
- Rotas NPC (`NpcRailRoute`) em **LWR**, só após start estável; libertar `guideLines[0/2]` após cópia.
- Mid ref = `autoLapRoute_.centers` (player); **sem** `npcMidRef_` HWR.
- Removidos: `npcCarRenderers_`, diag arrays, diamantes, minimapa NPC, hooks Scene3D.
- Mesh/proxy deferidos (`EnsureNpcCarRenderers` → false).
- Cruzes 2D amarela/ciano em `npcDrawPos_`.
- Map atual: `.text`≈0x92690, **heapFree≈5,8 KiB** (ok se PATH+rotas NPC estão em LWR).

### Build

```
touch src/main.cxx
make build DEBUG=1 PHYSICS_POC_MODE=0 AUTO_LAP_NPC_RAILS=1 AUTO_LAP_NPC_SLIM=0 BUILD_DROP=./BuildDrop_npc_rails
```

### Aceite F1

- HUD fingerprint `ISO NPC pose1` (visível desde o boot).
- Sem `AUTO START fail:2`; player anda (`AL g:1`).
- Após ~30 frames estáveis: `AUTO NPC pose F1`, `AUTO RAILS` com n0/n2>0.
- `NPC i0/i1` a avançar; cruzes laterais `|d|` ~50–120 (`ISO NPC pose1 dP:…`).

Se voltar `fail:2` / `ENSURE b:0`: regressar a `RAILS=0` / `SLIM=1` e medir LWR free em runtime.

---

## Fase 2 — Mesh reuse pós-pista 【ISO mesh1 — retest】

Detalhe: `AUTO_LAP_NPC_MESH1_ACTION_PLAN.md`.

1. F0+F1 PASS (autolap27). heap map ≈4.6 KiB → **reuse** do `MeshRenderer` do player (sem 2º Model*/clone).
2. Após player `SubmitRender`+`Flush`: `Render(npcDrawPos_[slot], yaw)` ×2.
3. Y = player.Y; offset LAT raw (pose4).
4. Cue: `BuildDrop_npc_mesh1\Interlagos_racing.cue` ou `C:\saturn\MESH1\Interlagos_racing.cue`.

### Aceite F2

HUD `ISO NPC mesh1` + `f:` > 0; três chassis (ou laterais visíveis); `NX≈BX+LAT`; sem `fail:2`.

---

## Fase 3 — Minimapa + limpeza

- 3 setas no minimapa.
- Default `AUTO_LAP_NPC_RAILS=0`; diag atrás de flag.
- Remover fingerprints de diagnóstico quando estável.

---

## Arquivos-chave

| Arquivo | Papel |
|---------|-------|
| `src/auto_lap_route_build_ops.hpp` | PATH bytes em LWR |
| `src/game_loop_system.hpp` | Load/Ensure/Arm/Tick/cruzes |
| `src/npc_rail_route.hpp` / `npc_rail_follower.hpp` | Rotas HWR + pose |
| `makefile` | `AUTO_LAP_NPC_RAILS` / `AUTO_LAP_NPC_SLIM` |

---

## Critério de paragem

Qualquer regressão `fail:2` / `ENSURE b:0` / carro parado ⇒ reverter para `RAILS=0` ou `SLIM=1`, medir map heap, só então reintroduzir NPC.
