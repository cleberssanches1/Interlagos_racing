# Plano de ação — NPCs nos trilhos laterais (Auto Lap)

**Data:** 2026-10-28  
**Evidência:** `autolap13.mp4` … `autolap19.mp4`  
**Cue de teste:** `BuildDrop_npc_rails\Interlagos_racing.cue` (`AUTO_LAP_NPC_RAILS=1`)  
**Baseline:** `BuildDrop\Interlagos_racing.cue` (`AUTO_LAP_NPC_RAILS=0`)  
**Meta de produto:** 3 carros visíveis no chase — player no trilho PATH **1**, NPCs nos trilhos **0** e **2** — sem regressão do Auto Lap do player; sem mudanças de LOD/TCOL/bake.

---

## 0. Situação (o que já está resolvido vs o que não)

### Resolvido (código + vídeos)

| Capacidade | Evidência |
|------------|-----------|
| PATH 3×321 rails | `AUTO RAILS 321/321/321`, parse host |
| Armamento NPC pós-gate | `AUTO NPC pose F1`, `NPCact 1/1 mid:1` |
| Mesh shared F2 | `AUTO NPC mesh F2` |
| Offset lateral PATH | `dP` ≠ 0 (ex. 55/−126 e −20/−26) — Autolap12 `0/0` corrigido |
| Y travado no player | `Y p:n0:n1` iguais; `dY` PATH ainda ~−13/−22 (só forense) |
| Projeção on-screen | `os:1`, `sc` distintos; cruzes 2D amarela/ciano |
| Pipeline pós-pista | sufixo `post` / enqueue na fase carro |
| Contagem CPU de faces | `f:118` (é `FaceCount` do mesh, **não** prova aceite VDP1) |

### Não resolvido

| Sintoma | Evidência |
|---------|-----------|
| Só 1 chassis 3D na pista | Autolap13–19 |
| MeshRenderer NPC separado | faces OK, mesh invisível |
| Reuse do renderer do player ×2 | `r:2 f:118`, ainda 1 carro |
| solo0 (só Render em NPC0) | carro **continua no centro**; cruz amarela **deslocada** → chassis ≠ esse Render |
| xonly (zero MeshRenderer) | `r:0 f:0`, chassis **ainda** se move |
| notrk (sem track + sem car mesh) | Autolap19: pista+carro ainda visíveis (BG cobria magenta) |
| Fase 0b (BG off + capa magenta + S3D) | **telemetrias_6 PASS:** `CAR:0 S3D:0`, pista/carro sumiram, só cruzes |

### Fase 0 fechada (telemetrias_6)

Com BG off + capa magenta, `RenderTrackFrame`/`SubmitRender` pulados ⇒ cena vazia + cruzes + `S3D:0`. O draw canônico de pista/carro **é** o de `RenderFrame`. Autolap19 com geometria ainda visível ficou explicado por BG cobrindo o clear (e não por um 2º path Scene3D). Seguir Fase 1 (proxy/clone).

---

## 1. Arquitetura atual (relevante)

```
RenderFrame(camera)
  ConfigureScene3dView + light
  [NPC=1] skip track   /  [NPC=0] RenderTrackFrame → trackSystem->RenderFrame
  ConfigureScene3dView + light
  RenderCar:
    SyncRenderState(player pos)
    [NPC=1 diagnóstico] PrepareNpcDrawPoses + markers 2D only (xonly/notrk)
    [NPC=0] SubmitRender → pipeline.Flush → MeshRenderer::Render → ModelObject::DrawAsFlat
            → Scene3D::DrawMesh → slPutPolygon(mesmo PDATA/XPDATA)

Pose NPC (quando ligada):
  npc = ResolveCarRenderPosition() + (sideXZ − midXZ); Y = player Y
```

- `ModelObject` **não é copiável** (`= delete`); NPCs compartilham o mesmo `Model*` do player.
- `LastRenderFaceCount` = faces do asset após `DrawAsFlat`, **sem** checar retorno de `slPutPolygon`.
- Cruzes 2D (`Scene2D`) funcionam com `sort=1` — VDP1 sprite path OK.
- Cubos `slPutPolygon` manuais (Phase C) **não** apareceram — ATTR/PDATA ad-hoc ou mesmo problema de submit 3D.

---

## 2. Hipóteses ordenadas (após Autolap19)

1. **ISO / Mednafen errado ou cache** — `BuildDrop\` em vez de `BuildDrop_npc_rails\`, ou disc image não remountada. Explica notrk “sem efeito”.
2. **Único PDATA por frame** — vários `slPutPolygon` no mesmo buffer de mesh colapsam; só a última (ou a do player) “vence”. Explica reuse/solo0 parcialmente; **não** explica xonly (zero puts do carro e chassis ainda lá).
3. **Draw do chassis fora de `RenderCar`** — outro TU/path (BG, SBA, shadow alt, path de track paralelo). Explica xonly + (se confirmado) notrk.
4. **Framebuffer / lista VDP1 não limpa** — frames antigos “fantasma”. Menos comum no SGL frame-rebuild, mas testável com limpar tela.
5. **Y/SORT/orçamento** — já enfraquecida: Y lock + os:1 + solo0 centro≠cruz.

---

## 3. Objetivo de aceite (produto)

1. Três chassis distintos no chase (lateral ~40–150 u).  
2. Player Auto Lap intacto (`AL g:1`, sem `fail:2`).  
3. Minimapa: 3 setas.  
4. `AUTO_LAP_NPC_RAILS=0` = baseline Autolap9 (1 carro).  
5. Sem mudança LOD/TCOL/bake/PATH authoring.

---

## 4. Fases de ação

### Fase 0 — Identidade do binário + quem desenha o chassis 【BLOQUEANTE】

**Objetivo:** provar que o cue carregado é o que compilamos, e achar o submit real do carro.

**0.1 Marcador inequívoco de ISO**

- Em `RenderFrame` com `AUTO_LAP_NPC_RAILS=1`: no início, preencher NBG ou imprimir faixa full-width `ISO NPC BUILD notrk` **e** forçar clear 3D (ex. não chamar track/car; opcionalmente `slTVOn` frame preto 1s no boot).
- Alternativa forte: mudar cor clara do clear / desligar BG (`SetEnableBg(false)` só no build NPC) para o vídeo ficar óbvio.

**0.2 Checklist de load (obrigatório no vídeo)**

1. Fechar Mednafen.  
2. Abrir **somente** `Projects\Interlagos_racing\BuildDrop_npc_rails\Interlagos_racing.cue`.  
3. Confirmar HUD: `notrk` (ou o sufixo da build).  
4. Se pista 3D ainda existir com `notrk` no HUD → hipótese 3 (path paralelo).

**0.3 Instrumentação de submit**

- Contador global `g_carPutPolygonCalls` incrementado só dentro de `MeshRenderer::DrawMesh` / `ModelObject::DrawAsFlat` quando `this == playerModel`.
- HUD: `CARPUT %u` por frame.  
  - xonly + `CARPUT 0` + chassis visível ⇒ draw **não** passa por DrawAsFlat do ModelObject do player.  
  - xonly + `CARPUT >0` ⇒ ainda há chamada escondida a esse model.

**0.4 Varredura de paths**

- Grep/runtime: todo `slPutPolygon` / `Scene3D::DrawMesh` / `MeshRenderer::Render` / `SubmitRender`.  
- Confirmar que `CarSglRenderer` não está linkado no hot path.  
- Verificar se `trackSystem->RenderFrame` é chamado só de `RenderTrackFrame`.  
- Worktree vs canônico: build **apenas** em `C:\saturn\SaturnRingLib-main\Projects\Interlagos_racing`.

**Aceite 0:** vídeo com sufixo ISO correto; ou (a) pista some no notrk, ou (b) `CARPUT 0` + chassis ⇒ path alternativo identificado e documentado.

---

### Fase 1 — Restaurar draw canônico do player; isolar 2º corpo 3D

**Só depois da Fase 0.**

1. Remover flags de diagnóstico destrutivas (xonly/notrk/solo0) do hot path; restaurar:
   - `RenderTrackFrame`
   - `car->SubmitRender` + `Flush` do player
2. Manter pose relativa + Y lock + cruzes 2D (barato, já validadas).
3. Teste A/B mínimo de **segundo corpo**:
   - **1A — Proxy distinto:** `Scene3D::DrawMesh` com `PDATA` **estático próprio** (cubo/quad SORT_MIN untextured) em `npcDrawPos_[0]`, usando o mesmo Push/Translate que o player.  
     - Se o proxy aparece na cruz amarela ⇒ submit 3D genérico OK; problema é **identidade do mesh do carro**.  
     - Se não aparece ⇒ problema de matriz/fase/orçamento pós-pista (reordenar: proxy **antes** da pista).
4. Teste **1B — Clone de buffers:** carregar `CAR1` uma segunda vez (novo `ModelObject` from file) ou duplicar verts/faces/attrs em LWR; `MeshRenderer` NPC → model clone; enqueue pós-pista com player.  
   - `ModelObject` copy está deletado → **reload** ou alloc+memcpy dos arrays SGL.

**Aceite 1:** pelo menos um corpo extra visível na pose lateral (proxy **ou** clone).

---

### Fase 2 — Dois NPCs com mesh de carro (apresentação)

1. Dois clones (ou um clone compartilhado só entre NPCs, **nunca** o pointer do player).  
2. `RenderPipeline`: Reset → NPC0 → NPC1 → player → Flush (player por último, SORT_MIN).  
3. Mesma config do player: `rotateModelX180/Z180`, `ForceSortMode(Minimum)`, `ForceUnlitKeepTextures`, `SetSkipMesh(crash)`, scale 1.  
4. Pose: relativa XZ + Y player (já ok). Opcional: `pointIndex` ligeiramente à frente (~1 segmento) se o chase “engolir” laterais.  
5. Remover cubos ATTR manuais quebrados; cruzes 2D só em DEBUG.

**Aceite 2:** 3 chassis na câmera; `NPCQ`/`f`/`dP` coerentes; Auto Lap player ok.

---

### Fase 3 — Minimapa e HUD limpo

1. `UpdateNpcMinimapMarkers` com segmentos TCOL em `npcDrawPos_`.  
2. HUD NPC em linhas 5–11 **após** stats (já feito); reduzir para 2–3 linhas em build estável.  
3. Aceite: 3 setas distintas.

---

### Fase 4 — Estabilização / flag

1. Default `AUTO_LAP_NPC_RAILS=0`.  
2. Remover solo0/xonly/notrk/reuse do caminho final (ou atrás de `NPC_DRAW_DIAG=1`).  
3. Doc: `AUTO_LAP_NPC_IMPLEMENTATION_PLAN.md` + este plano.  
4. Regressão: Autolap baseline cue + npc_rails cue.

---

## 5. Ordem de implementação sugerida (curta)

| # | Item | Risco | Depende |
|---|------|-------|---------|
| 0 | Marcador ISO + `CARPUT` + retest notrk | baixo | — |
| 1 | Se notrk falhar com HUD ok → achar 2º path track/car | médio | 0 |
| 2 | Restaurar player+track; proxy PDATA único na pose NPC0 | baixo | 0 |
| 3 | Clone ModelObject / 2º load CAR1 para NPCs | médio (LWR) | 2 |
| 4 | Pipeline 3 carros + minimapa | baixo | 3 |
| 5 | Limpar diag; default flag 0 | baixo | 4 |

---

## 6. Fora de escopo (mantido)

- LOD / TCOL / bake / segmentos_map  
- Física ×3 nos NPCs (continuam pose-follow)  
- Mudar PATH.NYA  
- Ligar NPC no baseline `AUTO_LAP_NPC_RAILS=0`

---

## 7. Arquivos principais

- `src/game_loop_system.hpp` — RenderFrame/RenderCar/pose/NPC diag  
- `src/mesh_renderer.cxx` / `modelObject.hpp` — DrawAsFlat, clone/reload  
- `src/main.cxx` — bootstrap SORT_MIN / 2º load CAR1  
- `src/render_pipeline.hpp` — ordem Flush  
- `makefile` — `AUTO_LAP_NPC_RAILS`, `BUILD_DROP`  
- `src/minimap_system*` / HUD — setas

---

## 8. Próximo passo imediato (ao aprovar)

Executar **Fase 0.1–0.3**: marcador visual de ISO + contador `CARPUT` + instruções explícitas de load do cue `BuildDrop_npc_rails`, regravar um vídeo curto. Só então proxy/clone (Fase 1).
