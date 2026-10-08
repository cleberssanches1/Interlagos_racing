# Plano de ação: paredes TCOL que não bloqueiam (pass-through)

**Data:** 2026-10-07  
**Sintoma (vídeo `Gravando 2026-10-07 162346.mp4`):** o carro não bate mais em paredes fantasmas no meio da pista, mas atravessa barreiras reais.  
**Restrição:** **somente TCOL** — não reativar o método antigo (cache GEO/FSMAP de paredes / `EnsureWallSegmentCache` como path autoritativo).

## Diagnóstico (causas ranqueadas)

| # | Causa | Efeito |
|---|--------|--------|
| 1 | Query só no ponto atual (sem sweep do movimento) | Em ~110 km/h o carro salta por cima do segmento finito da parede |
| 2 | Hull/probes menores que a contact-box | Laterais da caixa atravessam antes do centro “ver” a parede |
| 3 | Resposta fraca (±3 clamp + prev antes da correção) | Push pequeno; próximo frame já começa do outro lado |
| 4 | Célula 4×4 sem vizinhança | Parede na borda da célula vizinha não é testada |
| 5 | Cull offline agressivo (AABB encolhido) | Barreiras reais removidas do TCOL (ex.: SEG1 com 1 parede) |

Falso-positivo anterior (vídeo `155050`) já foi mitigado com: distância finita ao segmento, `CommitWallQueryPrevPosition` só no sample do carro, skip de paredes driveable, e damping só acima do limiar de graze. **Não reverter essas proteções.**

## Princípio

Corrigir **detecção + resposta + cobertura de dados** no pipeline TCOL-only já existente (`PHYS_WALL_COLLISION_RUNTIME=1`, janela seed±4, células 4×4). GEO/FSMAP de parede só se TCOL inválido.

---

## Fase 0 — Congelar contratos (já feito)

- Runtime: `PHYS_WALL_COLLISION_RUNTIME=1` no makefile.
- TCOL válido ⇒ path de parede **não** cai no cache GEO.
- Manter distância **finita** ao segmento (nunca plano infinito).
- Multiprobe **não** sobrescreve `prevPos` compartilhado; só `CommitWallQueryPrevPosition` após o resolve.

## Fase 1 — Swept segment–segment em `testWallIndex` (runtime)

**Arquivo:** `src/track_system.cxx` (lambda TCOL)

- Tratar movimento da amostra (`prev`→`now`) × parede (`A`→`B`) como dois segmentos 2D.
- Hit se: interseção dos segmentos **ou** menor distância eixo (agora/prev + endpoints da parede vs motion) `< radius`.
- Empurrão ao longo da perpendicular da parede, orientado para o lado de `prev` (fora).

**Critério:** em alta velocidade, atravessar uma barreira TCOL deve gerar `wh:1` e push não-nulo.

## Fase 2 — Probes da contact-box + prev por amostra

**Arquivos:** `src/car_ground_follower.hpp`, `src/interfaces.hpp`, `src/track_collision_query.hpp`

- 4 cantos da contact-box (half-WB × half-track) + centro.
- Cada probe leva seu próprio `motionPrevPosition` (histórico estático por índice).
- `commitPrevPosition=false` nos probes; commit só da pose do carro após o resolve.
- Hull: `kWallHullHalfLength/Width` alinhados à contact-box; `kWallHullProbeRadius = 1.5`.

**Critério:** laterais batem antes do centro atravessar.

## Fase 3 — Vizinhança 3×3 de células de parede

**Arquivos:** `src/track_collision_map.hpp`, `src/track_system.cxx`

- `ResolveWallCellCoords` + `FindWallCellAt` em dx,dz ∈ {-1,0,1}.
- Escanear também a célula do `prev` quando o movimento cruza bordas.

**Critério:** paredes na fronteira de célula não são perdidas.

## Fase 4 — Resposta mais forte

**Arquivos:** `src/car_physics_v2.hpp`, `src/car_physics_shared.hpp`, `src/car_ground_follower.hpp`

- Qualquer push não-zero constrói normal (`BuildWallPlanarNormal`).
- Sempre cancelar componente de velocidade **para dentro** da parede.
- Damping arcade só se `|push| > kWallPushVelocityCancelThreshold` (protege graze).
- `kMaxWallPlanarCorrectionPerFrame = 8.0`.
- Prev de telemetria **depois** da correção planar.

**Critério:** após hit, o carro não “teleporta” para o outro lado no frame seguinte.

## Fase 5 — Cull offline por borda do asfalto (não AABB encolhido)

**Arquivo:** `tools/build_track_collision.py`

- **Removido:** `WALL_LANE_AABB_SHRINK` (removeu barreiras reais; SEG1 → 1 parede).
- **Novo:** `wall_is_deep_inside_asphalt` + `WALL_EDGE_KEEP_BAND_RAW = 96<<16`.
  - Descarta só paredes cujas amostras estão **dentro** do AABB do asfalto **e** a mais de 96u da borda.
  - Mantém tudo perto da borda ou fora do AABB.

**Critério de rebuild:** SEG1 e segmentos de curva com barreiras visíveis devem ter **várias** paredes TCOL (não 0–1). Relatório deve expor `wallEdgeKeepBandRaw` (não `wallLaneAabbShrinkRaw`).

## Fase 5b — Cull miolo / seam-cap (vídeo 164653)

**Sintoma:** `SEG:002 GP wh:1 wx:+3` ao atravessar o limite de segmento em linha reta (parede invisível transversal/longitudinal no seam).

**Causa:** paredes `f03464` (fam 31) passam a ~14u do centróide da pista; os endpoints tocam a borda Z do AABB (junta S001↔S002), então o cull edge-band as **preservava** como se fossem barreiras laterais.

**Fix:** `wall_crosses_mid_lane` + `WALL_MID_LANE_CLEAR_RAW = 48<<16` — descarta qualquer parede cuja distância finita ao centróide do asfalto seja &lt; 48u. Barreiras laterais reais ficam ~90–180u do centro e sobrevivem.

## Fase 5c — Solo livre / só parede lateral (vídeo 170833)

**Princípio:** malha TCOL vem do LOD0 — onde há solo driveable, movimento livre; só faces de parede na **borda lateral** bloqueiam.

**Sintoma:** `SEG:002 GP wh:1 wz:-3` — push no eixo Z = cap transversal no seam.

**Causa:** caps transversais nas juntas (parede larga em X, fina em Z, no Z-end do AABB). Heurística errada usava o eixo *maior* do AABB como travel; com escape largo em X, o travel real (±Z) era invertido e o cull de seam não disparava.

**Fix:**
- `infer_travel_is_z`: eixo *menor* do AABB (avanço do segmento) ou delta entre centróides vizinhos
- `wall_is_seam_transverse_cap`: remove caps transversais no fim do segmento
- `wall_crosses_asphalt_interior`: remove paredes que cortam o miolo do asfalto (longe das rimas laterais)

## Fase 6 — Rebuild + reteste

1. Regenerar `TCOL.BIN` + `track_collision_report.json` a partir de `pacote_rancing` + `tools/track_collision_manifest.json`.
2. Copiar para `cd/data/TCOL.BIN` (e pacote).
3. Compilar ISO (`compile.bat` / `BuildDrop\Interlagos_racing.cue`).
4. Reteste em vídeo / HUD:

| Check | Esperado |
|-------|----------|
| Meio da reta (SEG1) | Sem stall; `wh:0` em marcha normal |
| Barreira lateral real | `wh:1`, carro para / desliza, **não atravessa** |
| Alta velocidade (~100+ km/h) contra muro | Sweep pega; sem túnel |
| Texturas driveable (asfalto/escape) | Continuam **não** bloqueando |
| FPS | Sem regressão grave vs build TCOL-only anterior |

---

## Fora de escopo (não fazer)

- Reativar path GEO/FSMAP de parede como autoritativo.
- Plano infinito / crossed-plane sem proximidade ao segmento finito.
- Cull por centróide 48u ou AABB encolhido (já comprovadamente destrutivo).
- Body-clip planar reaction no perfil Saturn low-cost (custo SH2).

## Ordem de execução sugerida

`F1 → F2 → F4 → F3 → F5 → F6` (detecção + probes + resposta antes de mexer nos dados; cull por último para isolar variáveis).
