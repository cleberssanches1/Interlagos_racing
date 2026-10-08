# Plano: paredes não detectam (solo funciona) + FPS ~4 (vídeo 090158)

**Data:** 2026-10-08  
**Vídeo:** `C:\vídeos\Gravando 2026-10-08 090158.mp4`  
**Prioridade do usuário:** primeiro detectar paredes corretamente; FPS em seguida.  
**Restrição:** TCOL-only (sem reativar GEO/FSMAP wall cache).

---

## 1. Evidência do vídeo / HUD

| Momento | FPS | SEG | KM/H | GP wh | OVR w | Leitura |
|---------|-----|-----|------|-------|-------|---------|
| Reta | **~4** | 002 | ~110 | 0 | 0/N | Solo OK; muro ainda longe |
| Atravessa barreira lateral | **~4** | 002 | ~55–94 | **0** | **0 hits** | Pass-through; query rodou e errou |
| Na grama | **~2–4** | 002 | **0** | 0 | — | Para por perda de suporte, não por parede |

Frames: `docs/_diag_20261008_090158/`.

**Dois problemas distintos:**

1. **Corretude:** miss total nas paredes (`wh:0`) no guardrail visual.  
2. **Custo:** FPS desabou (~4) após densificar TCOL + multiprobe + 5 amostras de célula no motion.

---

## 2. Por que o solo funciona e a parede não?

### Solo (TCOL ground) — barato e certo

| Aspecto | Comportamento |
|---------|----------------|
| Malha | Faces densas **debaixo** do carro (asfalto/escape/grama) |
| Teste | Ponto **dentro** do triângulo XZ + Y do plano |
| Células | **1** célula ground por segmento |
| Probes | ~2 (eixos) |
| Early-out | `earlyAcceptInside` para a janela seed±4 |
| Resultado | Contém o ponto → hit confiável |

Âncoras: `track_system.cxx` (`FindSurfaceYByFamilySet` / TCOL ground cell), `car_ground_follower.hpp` (probes de roda).

### Parede (TCOL walls) — caro e ainda errado no lugar certo

| Aspecto | Comportamento |
|---------|----------------|
| Malha | **Arestas finitas** XZ de faces wall-stem (não o volume da face visual) |
| Teste | Distância Chebyshev à aresta **&lt; raio (~2)** **ou** cruzamento movimento×aresta |
| Células | Até **5** pontos do motion × **3×3** células, **sem dedupe** |
| Probes | **5** (4 cantos + centro) × até **2** passes = até **10** `FindPlanarWallPush` |
| Early-out | **Não** — sempre varre seed±4 mesmo após hit |
| Fallback | TCOL válido → **sem GEO**; miss = atravessa |

Âncoras: `track_system.cxx` (`FindPlanarWallPush`, `scanTrackCollisionWalls`), `car_ground_follower.hpp` (`ResolveWallPushMultiProbe`).

### Espaço de coordenadas

**Não há bug de offset solo vs parede.** Ambos usam `world − trackOffset` para célula e `+ trackOffset` nos testes. O miss não é “coordenada invertida”; é **geometria de bloqueio ausente/longe do trajeto** e/ou custo inútil.

### Dado crítico (SEG2 no TCOL atual)

Paredes TCOL mais próximas do centróide do asfalto ficam a **~300–555 unidades** de distância horizontal. O hull do carro tem half-track ~22. Para “bater” no guardrail visual perto da faixa de rolamento, a aresta TCOL precisa estar **nessa faixa** — hoje as arestas úteis estão na borda externa do AABB (escape/runoff), não no muro que se vê ao sair da pista.

Conclusão: **a detecção de solo encontra faces porque elas existem sob o carro; a de paredes não encontra o muro porque a aresta bloqueadora não está (ou não está perto o bastante) na malha TCOL no XZ do impacto** — e ainda assim o runtime gasta SH2 demais procurando.

---

## 3. Por que o FPS caiu (~4)

Multiplicador aproximado pós-última atualização:

```
≤10 FindPlanarWallPush
  × ~9 segmentos (seed±4, sem early-out)
  × ≤5 amostras de motion (now/prev/½/¼/¾)
  × ≤9 células (3×3)
  × N paredes/célula (TCOL 762→1765, multi-edge)
  × teste swept int64 (várias divisões)
```

Solo: `~2` queries × poucos segs × **1** célula × teste leve.

HUD: `OVR q c:5/5` com FPS 4 confirma pressão no loop de física/query; LWR/HWR também apertados, mas o salto de custo de parede é a regressão direta da última mudança.

---

## 4. Princípios de correção

1. **Dados certos antes de mais probes** — uma polilinha lateral correta e barata > mil arestas + 10 probes.  
2. Solo driveable continua livre (mid-lane + seam-cap).  
3. TCOL-only; sem GEO wall path.  
4. Orçamento SH2: parede deve parecer mais com o solo (poucas células, early-out, dedupe).  
5. Medir: no guardrail → `wh:1` e `w:hits≥1`; no miolo/seam → `wh:0`; FPS alvo de volta à faixa jogável (não ~4).

---

## 5. Plano de ação (fases)

### Fase A — Diagnóstico no ponto do breach (½ dia) — **obrigatória**

Objetivo: provar se o miss é **buraco de dados** ou **reject runtime**.

1. Script: dado SEG + XZ aproximado do carro no frame do breach (ou amostra ao longo da saída lateral), listar:
   - parede TCOL mais próxima (fam, dist, Y overlap)
   - se está na célula 4×4 do ponto
2. Auditoria offline LOD0: faces verticais na borda da faixa (não só AABB externo) — quais `sourceStem` / `family_id` aparecem no guardrail visual vs `track_collision_manifest.json`.
3. Critério de saída: “dist mínima TCOL no breach ≫ half-track” ⇒ dados; “dist &lt; raio mas wh:0” ⇒ bug de query/Y/célula.

### Fase B — Malha: paredes onde o muro está (1–2 dias) — **prioridade de corretude**

**Arquivos:** `tools/build_track_collision.py`, `tools/track_collision_manifest.json`.

1. **Expandir / corrigir `wallSourceStems`** se o guardrail visual usar família fora da lista.  
2. **Gerar laterais a partir da borda do solo driveable**, não só arestas soltas de textura:
   - Opção preferida: extrair contorno (boundary edges) das faces asphalt type=1 (ou driveable) e promover arestas adjacentes a faces wall-stem **ou** arestas do contorno com desnível/parede.
   - Alternativa: manter wall-stem faces, mas **fundir colineares** e descartar arestas curtas/duplicadas (qualidade > quantidade; meta ≪ 1765 lixo).  
3. Manter culls: mid-lane (&lt;48u centróide), seam transverse cap, Y_DEAD.  
4. **Não** religar cull AABB-interior destrutivo.  
5. Rebuild TCOL; validar SEG1–10: existe aresta a &lt; ~half-track + margem da faixa de rolamento em ambos os lados onde há muro visual.

**Critério:** no XZ do breach 090158/172530, `dist(carro, parede_TCOL) &lt; ~30–40u` (hull+raio).

### Fase C — Runtime barato que ainda acerta (1 dia) — **prioridade de FPS após B**

**Arquivos:** `track_system.cxx`, `car_ground_follower.hpp`, `car_physics_shared.hpp`.

Ordem sugerida (ganho/risco):

1. **Reverter chord de células** para `now + prev` (ou + midpoint só) — remover ¼/¾.  
2. **Dedupe de `wallIndex`** por chamada a `FindPlanarWallPush` (bitset/`uint32` mask se wallCount ≤ 32, ou array marcado).  
3. **Early-out da janela** seed±4 quando já houver penetração “boa” (espelhar espírito do early-accept do solo).  
4. **Reduzir multiprobe Saturn:** 1 pass; probes = centro + 2 laterais (L/R) **ou** 4 cantos em 1 pass sem segundo resolve.  
5. Manter `kWallHullProbeRadius = 2.0` só se Fase B limpa; senão voltar a 1.5 para não reabrir miolo.  
6. Opcional: escanear lista de paredes do segmento (cap N) se células vazias no rim — só se B ainda deixar gap pontual; medir custo.

**Critério:** FPS de volta à faixa pré-regressão (ordem de grandeza jogável); no muro `w:hits≥1` com **bem menos** que 10 calls/frame no miss path.

### Fase D — Resposta (já parcialmente feita; só validar)

Manter cancelamento de velocidade para dentro + clamp 8.0. Se `wh:1` e ainda “atravessa”, aí sim reforçar resposta — **não** antes de A–C.

### Fase E — Rebuild + matriz de reteste

| Caso | Esperado |
|------|----------|
| Miolo reta SEG1–3 | `wh:0`, sem stall |
| Seam S00n→S00n+1 | `wh:0` |
| Guardrail lateral (090158 / 172530) | `wh:1`, **não** atravessa |
| Alta velocidade no muro | hit sem túnel, FPS ok |
| Solo / plant | inalterado (já funciona) |

Artefatos: `TCOL.BIN` + `BuildDrop\Interlagos_racing.cue`.

---

## 9. Implementação (2026-10-08, pós-aprovação)

| Fase | Feito | Resultado |
|------|-------|-----------|
| A | Sim | SEG1–2: `shared_wallstem=0`; stems no runoff; `f01864` fora do manifesto |
| B | Sim | Paredes = contorno **asfalto type=1** (não 1/2/3). SEG2 nearest **~69u** (era ~302u) |
| C | Sim | Chord now/prev/mid; dedupe wallIndex; early-out pen; 1 pass + 2 laterais Saturn |
| E | Em curso | TCOL ~5209 walls (seam cull removeu transversais); ISO rebuild |

**Reteste:** muro lateral `wh:1` + FPS jogável; miolo/seam `wh:0`. Se miolo bater cedo demais, subir `WALL_MID_LANE_CLEAR_RAW` (48→80u).

## 6. Ordem e o que NÃO fazer

**Ordem:** `A → B → C → D → E`  
(Dados → custo → validação. Não aumentar probes/raio/células antes de B.)

**Não fazer:**

- Reativar path GEO de parede.  
- Compensar miss só com mais arestas multi-edge + 5×3×3 + 10 probes (é a regressão atual).  
- Cull por AABB encolhido / interior largo (já quebrou laterais e seams).  
- Assumir bug de `trackOffset` solo vs parede sem evidência da Fase A.

---

## 7. Resposta direta à pergunta

> A detecção do solo funciona. Por que a das paredes não?

Porque são problemas diferentes na **mesma** malha TCOL:

- **Solo** = “o ponto está **dentro** de uma face de chão?” — e há faces sob o carro.  
- **Parede** = “o carro chegou a **&lt; ~2u** de uma **aresta** de muro?” — e as arestas TCOL atuais não estão no guardrail visual (ficam ~300u+ do centróide / borda externa do escape), enquanto o runtime gasta o frame inteiro vasculhando células/probes demais.

Corrigir = **colocar a aresta certa no lugar certo (barato)** + **consultar como o solo: local, com early-out e sem retrabalho**.

---

## 8. Âncoras

| Área | Onde |
|------|------|
| Ground TCOL | `src/track_system.cxx` (`FindSurfaceYByFamilySet`) |
| Wall TCOL | `src/track_system.cxx` (`FindPlanarWallPush`, `scanTrackCollisionWalls`) |
| Multiprobe | `src/car_ground_follower.hpp` |
| Tunables | `src/car_physics_shared.hpp` |
| Build / culls | `tools/build_track_collision.py` |
| Stems | `tools/track_collision_manifest.json` |
| Diag | `tools/_diag_tcol_wall_coverage.py` |
| Planos anteriores | `docs/INTERLAGOS_TCOL_WALL_PASSTHROUGH_LATERAL_PLAN.md` |
