# Plano: Corrigir texturização LOD0 + gargalos de memória

**Base:** `docs/INTERLAGOS_SEGMENT_PIPELINE_AND_BELT_TECHNICAL_NOTES.md`, vídeo `Screen Recording 2026-09-20 153227.mp4`, frames `docs/_video_frames_20260920_153227/`  
**Foco:** faces do **LOD0** (ranks 0–1, malha high + tex 64 via TBKLOD0) sem textura; asfalto/paredes; pressão de memória com `SRL_MAX_TEXTURES=512`.

---

## Diagnóstico do vídeo (153227)

| Observação | Interpretação |
|------------|---------------|
| Paredes/laterais vermelho-magenta sólidas | `No_Texture` + `baseColor` (não albedo) |
| Asfalto às vezes ok, relato de degenerar após slides | Family compartilhada + retire/reuse / ownership |
| FPS ~55→piora no avanço | Custo de slide + upload/decode + thrash |
| Overlay `BLT` não legível | **Conflito de HUD:** `Debug::Print(1,16..18)` usado por dezenas de prints (`WM*`, `V1*`, `OVR*`, `S1*`, `TB*`) e FPS em `(0,16)` — BLT é sobrescrito |

Conclusão: Fase A (telemetria) **não entregou dados usáveis**; as falhas de textura **continuam** e são principalmente **runtime/esteira + orçamento de slots**, não o tamanho nominal do TBK (já 64/32).

---

## Arquitetura LOD0 relevante

| Rank | GEO | Texture bank | runtimeIndex |
|------|-----|--------------|--------------|
| 0–1 | High (`TRKRDR` / S###) | TBKLOD0 64×64 | **3** |

Caminho crítico: `ApplyFaceTextureSlots` → `TryGetBestFamilyLodSlot` / `EnsureFamilyLodSlotLoaded` → `TryLoadFamilyLodSlot` (TBK → decode → VDP1).

Hot path de evicção: `ReleaseUnusedFamilyResourcesEndFrame` com `strictWindowRecycling` → keep **somente** se `usedTextureSlotsThisFrame_[slot]`; grace **1** frame. Asfalto/paredes compartilhado ou multi-seg podem ser retired cedo demais se o flag de “used” não cobrir todos os refs.

Build do usuário: **`SRL_MAX_TEXTURES=512`** (metade do perfil antigo 1000) → teto duro de residência.

---

## Gargalos de memória identificados

1. **Teto de slots VDP1 = 512**  
   Janela 16 segs × dezenas de families (paredes únicas + asfalto + props) pode saturar; `UploadDecodedTextureToVdp1` falha → face vermelha.

2. **Thrash retire/reuse (grace=1)**  
   Slot liberado e reutilizado; owner token muda; faces LOD0 ainda no anel perdem binding (`owned mismatch`).

3. **Soft admit na cauda**  
   Segmento entra com slots `-1`; recovery compete com budget 8/frame; ranks 0–1 sofrem se uploads forem para far.

4. **HWR (cart ~4MB) + emergency reserve**  
   Pressão força trim/flush; slide sob low HWR aumenta soft admit e misses.

5. **CRAM palette**  
   Muitas palettes Paletted16/64; recycle OFF (anti-roxo) → pressão acumulada; upload fail sem mensagem clara no HUD.

6. **Telemetria ilegível**  
   Sem métricas, não dá para separar missFamily vs uploadFail vs ownedMismatch.

---

## Objetivos mensuráveis

| ID | Meta |
|----|------|
| T1 | HUD BLT legível em todo frame (sem overwrite) |
| T2 | Ranks 0–1: 0 faces driveable/asfalto sem textura após 2 frames do slide |
| T3 | Paredes LOD0: missing face slots ↓ (meta: sem vermelho sistemático nos 2 segs próximos) |
| T4 | `ownedMismatch` ≈ 0 em volta estável; asfalto próximo não degenera após 30+ slides |
| T5 | Live texture slots na janela < ~80% de 512; HWR free acima do hard floor na maior parte do tempo |

---

## Plano de ação (fases)

### Fase 0 — Telemetria legível (obrigatória, curta)  [P0]

1. Mover overlay BLT para linhas **livres** (ex. col 0 rows **2–4** ou bottom 25–27), prefixo fixo `BLT`.
2. Garantir print **depois** de todos os outros overlays do frame (último writer).
3. Opcional: freeze dos últimos valores não-zero por 30 frames para leitura em vídeo.
4. Contador extra: `liveSlots`, `uniqueFamiliesWindow`, `lod0MissingFaces`.

**Done when:** no próximo vídeo dá para ler `sa/om/uf/df/mf/ms/up/hwr` claramente.

### Fase 1 — Proteger residência LOD0 (pin + used-flags)  [P0 textura]

1. **Pin de families críticas** enquanto qualquer segmento ranks 0–3 referencia:
   - `surfaceTypeId == asphalt` (SFMAP)
   - opcional: top-N families por face-count nos ranks 0–1
2. Em `ReleaseUnusedFamilyResourcesEndFrame`: se pinned → **nunca** retire; grace maior (3–4) para LOD index 3 (TBKLOD0).
3. Auditar `RebuildUsedTextureSlotFlagsFromWorkingRefs` / draw path: toda face texturizada de ranks 0–1 deve marcar slot em `usedTextureSlotsThisFrame_` **antes** do release end-frame.
4. Flag: `kEnableCriticalFamilyPin` (default ON após validação).

**Done when:** T2 + T4 (asfalto estável).

### Fase 2 — Admit / prioridade para LOD0  [P0 textura + FPS]

1. **Upload priority:** ranks 0–1 e pinned asphalt primeiro no budget 8; far só com sobra.
2. **Admit gate (v1):** adiar slide até asfalto do incoming ready **ou** stall máx 2–3 frames; soft admit só para non-driveable.
3. Placeholder cinza (não vermelho) para miss temporário (debug/`CL32KRGB` baseColor).
4. Flag: `kEnableBeltAdmitGate`.

**Done when:** T2/T3; menos cliff de vermelho pós-slide.

### Fase 3 — Orçamento de memória / 512 slots  [P0–P1]

1. Estimar working-set: unique families × (64² paletted + CRAM) na janela 16.
2. Se `liveSlots > 0.8 * SRL_MAX_TEXTURES` (≈410):  
   - demote agressivo só ranks ≥10 (já 32);  
   - **não** demote ranks 0–1;  
   - evict first non-pinned far families.
3. Considerar (só se evidência): subir `SRL_MAX_TEXTURES` com cuidado de VRAM, **ou** reduzir families de parede no offline (atlas/share) — decisão pós-telemetria.
4. Log quando `UploadDecodedTextureToVdp1` falha por falta de slot vs HWR.

**Done when:** T5; uf cai.

### Fase 4 — Validação

1. Mesmo percurso do vídeo 153227.  
2. Conferir HUD BLT.  
3. 2–3 voltas: asfalto LOD0 estável; paredes próximas texturizadas; FPS sem cliff extremo no slide.

---

## Ordem

```
0 (HUD legível) → 1 (pin LOD0/asfalto) → 2 (admit/priority) → 3 (cap memória 512) → 4 (validação)
```

## Arquivos

- `src/track_system.cxx` / `.hpp` — release, merge, slide, load, overlay
- `src/track_renderer.hpp` — mark used slots no apply
- `src/game_loop_*.hpp` — ordem de prints HUD
- `makefile` — só se elevarmos `SRL_MAX_TEXTURES` (fase 3, opcional)
- Docs: atualizar `INTERLAGOS_BELT_STABILITY_ACTION_PLAN.md`

## Não fazer agora

- Cortar janela 16 / mudar budget 8 sem dados BLT legíveis.
- Religar UV-unwrap em massa.
- Palette recycle end-frame (risco roxo).

## Riscos

| Risco | Mitigação |
|-------|-----------|
| Pin segura VRAM | Só asphalt + top faces LOD0; unpin ao sair da janela |
| Admit atrasa streaming | Stall cap 2–3; bypass asphalt |
| Mais prints | Uma região HUD dedicada, last-writer |

## Rollback

Flags `kEnableBeltTelemetryOverlay`, `kEnableCriticalFamilyPin`, `kEnableBeltAdmitGate`, `kEnableBeltUploadPriority`.
