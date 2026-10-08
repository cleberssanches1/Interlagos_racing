# Plano: carro atravessa faces de parede (vídeo 172530)

**Data:** 2026-10-07  
**Vídeo:** `C:\vídeos\Gravando 2026-10-07 172530.mp4`  
**Restrição:** TCOL-only (sem reativar método antigo GEO/FSMAP de paredes).  
**Princípio:** solo LOD0 driveable = livre; faces de parede reais na borda = bloqueiam.

---

## 1. Evidência do vídeo / HUD

| Frame | SEG | KM/H | GP wh | OVR w:hits/calls | Leitura |
|-------|-----|------|-------|------------------|---------|
| ~8 | 002 | ~110 | 0 | — | Reta, sem hit |
| ~16 | 002 | ~94 | 0 | — | Ainda sem hit |
| ~24 | 002 | ~55 | **0** | **`w:0/5`** | Atravessa barreira lateral visível → grama |
| ~30 | 002 | **0** | 0 | — | Parado fora (provável no-support / damping) |

**Conclusão objetiva:** a query de parede **roda** (`w:0/5` = 5 calls, 0 hits) e **não encontra** nenhum segmento TCOL. Não é falha de resposta fraca (`wh:1` com push pequeno); é **miss total**. O carro passa pela barreira visual e só depois perde suporte/velocidade no fora-de-pista.

Frames de diagnóstico: `docs/_diag_20261007_172530/`.

---

## 2. Estado atual da malha e do runtime

### Malha TCOL (LOD0, `cd/data/TCOL.BIN`)

- Ground: faces floor-like surfaceType 1/2/3 (asfalto/escape/grama) — plant do carro.
- Walls: arestas XZ de faces **não-floor** cujos `family_id` ∈ `tools/track_collision_manifest.json` (11 stems: F01164…F04064).
- Extração: **uma aresta (maior em XZ) por face** (`wall_from_face`) — subcobertura de barreiras complexas.
- Culls offline agressivos (após falsos positivos no seam/miolo):
  - mid-lane &lt; 48u do centróide
  - caps transversais no seam
  - cruzamento do interior do asfalto (longe da rima lateral 30%)
  - deep-inside AABB
- Resultado: **762** paredes (era ~1306); **57 segmentos com 0 paredes**; média ~2.6 paredes/seg.
- Visual em runtime usa LOD1/2 (`TRACK_LOD0_SEGMENTS:=0`); física de parede só vê TCOL LOD0 — a “parede que se vê” pode não ter aresta TCOL correspondente.

### Runtime (TCOL-only)

- `PHYS_WALL_COLLISION_RUNTIME=1`, janela seed±4, células 4×4 com vizinhança 3×3.
- Multiprobe: 4 cantos contact-box + centro; raio probe **~1.5**; swept segment–segment.
- Com TCOL válido: **não há fallback GEO** — miss TCOL = atravessa.
- Hit exige: Y dentro de `minY/maxY±12`, distância Chebyshev &lt; raio **ou** cruzamento movimento×parede, parede na célula 3×3 de now/prev.

---

## 3. Causas ranqueadas (pass-through + `wh:0` / `w:0/N`)

1. **Cobertura TCOL insuficiente na barreira lateral visível (dados)**  
   Culls + “1 aresta/face” + stems limitados deixam buracos onde o LOD visual ainda mostra muro. Query varre SEG2, encontra paredes longe/erradas/com Y inválido → 0 hits.

2. **Representação ≠ face visual**  
   Barreira contínua no mesh vira poucos segmentos finitos; o carro passa no vão entre arestas ou fora do envelope TCOL.

3. **Tunelamento runtime (secundário)**  
   Raio ~1.5 + só células nos extremos do movimento; em alta velocidade lateral pode pular a célula da parede — agrava buracos, não explica sozinho `w:0/5` se a parede estivesse na célula certa.

4. **Rejeição por faixa Y**  
   Algumas arestas TCOL em SEG2 têm Y negativo (ex.: ~-209…-149) enquanto asfalto está ~Y positivo — parede “existente” nunca testa. Precisa auditoria Y vs solo.

5. **Seed/janela** (menor)  
   SEG:002 está correto no HUD; menos provável neste vídeo.

---

## 4. Princípios de correção

1. Solo driveable (projeção XZ do ground TCOL) permanece **livre**.
2. Bloqueio só por geometria de parede TCOL — **sem** GEO/FSMAP wall cache.
3. Preferir **mais cobertura lateral correta** a mais cull cego; regressões de seam/miolo devem ser testadas explicitamente (não “resolver” só com cull global).
4. Medir sucesso com HUD: no impacto em barreira lateral → `GP wh:1` e `OVR w:hits≥1`; no miolo/seam → `wh:0`.

---

## 5. Plano de ação (fases)

### Fase 0 — Congelar diagnóstico reproduzível (½ dia)

- Script offline: para SEG1–10 (e segs do vídeo), listar cada parede TCOL com dist. ao centróide asfalto, Y vs Y do solo, classificação (lateral / miolo / seam).
- No emulador (ou log): no frame do breach, registrar `seedSeg`, `wallCount`, menor dist. amostra→parede, motivo de skip se instrumentado.
- Critério: confirmar se no XZ do carro no breach a parede TCOL mais próxima está &gt; raio (buraco de dados) ou &lt; raio mas Y/célula rejeita (runtime).

### Fase 1 — Auditoria cull vs barreiras laterais reais (1 dia) — **prioridade**

**Arquivo:** `tools/build_track_collision.py`, relatório TCOL.

1. Gerar TCOL **sem** `wall_crosses_asphalt_interior` / com `WALL_LATERAL_RIM_FRAC` mais permissivo (A/B), mantendo:
   - skip driveable surfaceTypes
   - mid-lane clear (48u) — protege miolo
   - seam transverse cap — protege juntas
2. Diff: paredes que voltam a aparecer e estão na **rima lateral** (não miolo, não seam-cap).
3. Contar segs com `walls==0` antes/depois; meta: segs de reta/curva com barreira visual têm ≥1 parede lateral por lado quando o LOD0 tem face de wall-stem na borda.

**Critério:** no A/B, o ponto do vídeo 172530 (saída lateral SEG2) tem aresta TCOL a &lt; ~half-track do trajeto do carro.

### Fase 2 — Melhorar extração de paredes LOD0 (1–2 dias)

**Arquivo:** `tools/build_track_collision.py` (`wall_from_face`).

1. Em vez de só a maior aresta XZ: emitir arestas de faces wall-stem com normal predominantemente horizontal (`|ny|/len` baixo — espelho de `is_floor`), length mínima configurável.
2. Deduplicar segmentos colineares próximos (já há dedupe por key).
3. Opcional: expandir `wallSourceStems` se auditoria mostrar texturas de barreira visual fora da lista (comparar MAT do LOD0 nas faces verticais da borda).

**Critério:** barreira lateral contínua no GEO LOD0 gera polilinha TCOL contínua o bastante para o hull do carro (~half-track) não caber no vão.

### Fase 3 — Endurecer detecção runtime (sem GEO) (1 dia)

**Arquivos:** `track_system.cxx`, `car_physics_shared.hpp`, `car_ground_follower.hpp`.

1. **Células ao longo do movimento:** além de 3×3 em now/prev, amostrar células em N pontos do segmento prev→now (ex.: 0.25/0.5/0.75) — reduz tunelamento.
2. **Raio:** subir `kWallHullProbeRadius` / lookahead com cuidado (ex.: 1.5 → ~2.0–2.5) só após Fase 1 garantir que não há miolo fantasma.
3. **Y:** se grounded, usar Y do plant ± margem maior que 12; ou expandir `minY/maxY` das paredes na build quando a face for barreira baixa/alta inconsistente.
4. Instrumentação debug: contador de skips (Y / aabb / dist / empty cell) no OVR — opcional.

**Critério:** com malha da Fase 1–2, `w:hits≥1` e `wh:1` no mesmo cenário do vídeo; miolo/seam dos vídeos 164653/170833 continuam `wh:0`.

### Fase 4 — Rebuild + matriz de reteste (½ dia)

1. Rebuild `TCOL.BIN` + ISO (`PHYS_WALL_COLLISION_RUNTIME=1`).
2. Matriz:

| Caso | Esperado |
|------|----------|
| Miolo reta SEG1–3 | `wh:0`, sem stall |
| Seam S001→S002 em frente | `wh:0` |
| Barreira lateral visível (cenário 172530) | `wh:1`, não atravessa |
| Alta velocidade (~100+) no muro lateral | sweep pega; sem túnel |
| Fora no escape/grama sem muro | livre (solo driveable) / no-support ok |

3. Atualizar `docs/INTERLAGOS_TCOL_WALL_PASSTHROUGH_ACTION_PLAN.md` com Fase 5d (pass-through lateral).

---

## 6. Ordem recomendada

`F0 → F1 → F2 → F3 → F4`

Não aumentar raio/runtime (F3) antes de fechar buracos de dados (F1–F2): senão volta falso positivo no miolo/seam.

Não reativar path GEO de parede.

## 9. Implementação (2026-10-07, pós-aprovação)

| Fase | Feito | Notas |
|------|-------|-------|
| F0 | Sim | `tools/_diag_tcol_wall_coverage.py` |
| F1 | Sim | `WALL_DISABLE_AABB_INTERIOR_CULL=True`; mantém mid-lane + seam-cap; filtro Y_DEAD |
| F2 | Sim | `walls_from_face` emite arestas de contorno (não só a maior) |
| F3 | Sim | amostras de célula em 0/¼/½/¾/1 do motion; `kWallHullProbeRadius=2.0` |
| F4 | Sim | TCOL **1765** paredes (era 762); 0 segs sem parede nos 40 primeiros; Y_DEAD filtrado |

**Reteste obrigatório:** cenário 172530 (barreira lateral SEG2) deve dar `GP wh:1` e `OVR w:hits≥1`. Miolo/seam dos vídeos anteriores devem continuar `wh:0`.

**Se ainda atravessar:** auditar se os `wallSourceStems` cobrem a textura do guardrail visual; o AABB do asfalto ainda é largo (paredes “laterais” ~300u do centróide) — pode faltar stem ou a barreira visual ser outra família.

---

## 7. Fora de escopo

- Body-clip planar reaction no perfil Saturn low-cost.
- Depender de residency visual LOD0 (`TRACK_LOD0_SEGMENTS`).
- Cull por AABB encolhido (já comprovadamente destrutivo).

---

## 8. Âncoras de código

| Área | Onde |
|------|------|
| Build TCOL / culls | `tools/build_track_collision.py` |
| Manifest stems | `tools/track_collision_manifest.json` |
| Query TCOL walls | `src/track_system.cxx` (`FindPlanarWallPush`, `testWallIndex`, 3×3) |
| Multiprobe | `src/car_ground_follower.hpp` (`ResolveWallPushMultiProbe`) |
| Tunables | `src/car_physics_shared.hpp` |
| HUD GP/OVR | `src/game_loop_overlay_debug_presenter_ops.hpp` |
| Flags | `makefile` (`PHYS_WALL_COLLISION_RUNTIME=1`), `src/physics_feature_flags.hpp` |
