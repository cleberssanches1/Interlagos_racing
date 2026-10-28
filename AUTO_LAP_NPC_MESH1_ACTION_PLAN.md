# Plano de ação — NPC mesh1 (reuse pós-pista)

**Data:** 2026-10-28  
**Pedido:** criar plano e aplicar.  
**Base:** `AUTO_LAP_NPC_PATH_LWR_ACTION_PLAN.md` + evidência autolap27.

---

## Estado comprovado (Fase 1 PASS)

| Item | Evidência autolap27.mp4 |
|------|-------------------------|
| Fingerprint | `ISO NPC pose4 a:11 v:11 r:2` |
| Lateral PATH | `LAT71/-76` |
| Offset raw | `BX-886 NX-815 PX-886` → **NX = BX + 71** |
| Cruzes | amarela (e ciano) visíveis pré-Flush |
| Player Auto Lap | pista + carro OK, sem `fail:2` |
| heap map pose4 | `__heap_start=0x060beda0` … `__heap_end=0x060c0000` ≈ **4.6 KiB** |

Conclusão: pose-follow + write raw + cruzes pré-Flush OK. Mesh ainda stub (`EnsureNpcCarRenderers` → false).

---

## Objetivo Fase 2 (este plano)

Dois chassis NPC nos trilhos 0/2, **reusando o `MeshRenderer` do player** (mesmo `Model*`), zero segundo `ModelObject` / zero `new Mesh`.

Escolha do utilizador: **Reuse MeshRenderer** (não proxy cubo; não 2× MeshRenderer — heap apertado).

### Porquê reuse agora

1. Autolap13/14: 2× MeshRenderer → `NPCF 118/118` mas heap atual ≈4.6 KiB não aguenta centers/transforms×2.
2. Autolap17 reuse: faces submetidas, cópia invisível — na altura o offset lateral ainda falhava (`dP:0/0`). Com LAT/NX validados, retry.
3. Produto: pose-follow barato; física×3 fora de escopo.

---

## Fase 2A — Implementação mesh1 【aplicar agora】

### Código (`game_loop_system.hpp` → `RenderCar`)

Ordem pós-pista (track já desenhado em `RenderFrame`):

1. `PrepareNpcDrawPoses()` (LAT raw → `npcDrawPos_`, Y = player).
2. `DrawNpcPoseMarkers()` (cruzes pré-Flush).
3. `car->SubmitRender` + `Flush` (player + wheels Apply).
4. **Reuse:** para cada slot com `npcDrawValid_`, `car->Renderer()->Render(npcDrawPos_[slot], yaw)`.
5. Acumular `LastRenderFaceCount` em telemetria HUD.

### HUD fingerprint

- Linha 0: `ISO NPC mesh1 a:%u%u v:%u%u f:%u/%u`
- Manter `LAT/BX/NX/PX` na linha 14 (prova de regressão do offset).

### Build / cue

```
touch src/main.cxx
make build DEBUG=1 PHYSICS_POC_MODE=0 AUTO_LAP_NPC_RAILS=1 AUTO_LAP_NPC_SLIM=0 BUILD_DROP=./BuildDrop_npc_mesh1
```

Cue único (evitar cache Mednafen):  
`BuildDrop_npc_mesh1\Interlagos_racing.cue`  
(opcional cópia curta `C:\saturn\MESH1\Interlagos_racing.cue`)

### Aceite 2A

1. HUD `ISO NPC mesh1` (nunca pose3/slim/rails0).
2. Sem `AUTO START fail:2`; player anda.
3. `a:11 v:11`, `NX ≈ BX + LAT`.
4. `f:` faces > 0 nos dois slots.
5. **Dois chassis laterais** além do player (ou um se o outro estiver fora de câmera).
6. Cruzes continuam como referência.

### Critério de paragem

- Tela preta / `heapFree` negativo / `fail:2` → reverter para pose4 (`Ensure` stub + sem Render reuse).
- Faces > 0 mas chassis ausentes → Fase 2B (proxy estático ou reorder pré-Flush Enqueue).

---

## Fase 2B — Só se 2A falhar visualmente

1. Enfileirar NPCs **antes** do player no mesmo `Flush` (`pipeline.Enqueue(*r, pos, yaw)` ×2, depois `SubmitRender`).
2. Se ainda invisível: proxy PDATA **estático** (sem `new`) em `npcDrawPos_[0]` via `Scene3D::DrawMesh`.
3. Clone/`ModelObject` 2º load **só** com heapFree map ≥ ~16 KiB (encolher `.text` / SLIM diag).

---

## Fase 3 — Minimapa + limpeza (depois de 2A/2B OK)

1. Setas minimapa em `npcDrawPos_`.
2. Default `AUTO_LAP_NPC_RAILS=0`; diag atrás de flag.
3. Remover fingerprints `pose4`/`mesh1` quando estável.

---

## Fora de escopo

- LOD / TCOL / bake / PATH.NYA  
- Física ×3  
- Partilhar ownership do `Model*` do player como dono de outro `ModelObject`

---

## Arquivos

| Arquivo | Mudança |
|---------|---------|
| `src/game_loop_system.hpp` | Render reuse + HUD mesh1 |
| `makefile` | `BUILD_DROP=./BuildDrop_npc_mesh1` no comando make |
| `AUTO_LAP_NPC_PATH_LWR_ACTION_PLAN.md` | F1 marcada PASS; F2 aponta aqui |
