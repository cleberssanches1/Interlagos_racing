# Interlagos — Pipeline de segmentos + esteira (notas técnicas)

Documento de organização de ideias para planejar melhorias na **esteira de segmentos** e na residência de texturas.

| Campo | Valor |
|-------|--------|
| Data | 2026-09-20 |
| Vídeo de referência | `C:\Users\clebe\Videos\Screen Recordings\Screen Recording 2026-09-20 144629.mp4` (~54 s) |
| Frames extraídos | `docs/_video_frames_20260920/` |
| Perfil LOD compile-time | `TRACK_LOD0/1/2 = 2 / 8 / 6` → janela **16** |
| Orquestrador offline | `tools/build_all_nya_geo_mat.ps1` |
| Runtime | `src/track_system.cxx`, `track_lod_config.hpp`, `track_renderer.hpp` |

---

## 1. Sintomas observados (vídeo)

### 1.1 Faces sem textura
- Em vários trechos do vídeo, **faces laterais / paredes / volumes** aparecem com cor sólida (vermelho/magenta saturado).
- Interpretação técnica: atributo de draw com `Texture = No_Texture` (slot 0) e `baseColor` no estilo `0x841F` / `0x8400 | (materialId & 0x1F)` sob `CL32KRGB` — **não** é albedo de asfalto.

### 1.2 Degeneração do asfalto nos primeiros segmentos
- Relato: após percorrer alguns segmentos, o asfalto dos **dois primeiros** (ranks próximos / lod_0) **degenera**.
- Hipótese alinhada ao código:
  1. Janela avança → segmentos antigos saem do anel.
  2. Families/slots VDP1 entram em **retire → reusable pool** (delay ~2 frames).
  3. Novas families **reusam o mesmo número de slot** com novo *owner token*.
  4. Se face ainda desenhada aponta para slot numérico antigo **sem** ownership válido → limpa para `No_Texture` / cor sólida; ou (cenário pior) cor/palette inconsistente.
  5. Asfalto é **family compartilhada** entre muitos segmentos → risco alto de evicção/rebind quando o working-set / grace frame não cobre todos os refs da janela restante.

### 1.3 Queda forte de FPS
- Coincide com avanço da esteira (slide + admit + uploads + possível prefetch miss).
- Custo típico no mesmo frame: decode TGA paletted + upload VDP1 (budget 8) + rebuild face slots + build RDR/geo scratch + pressão HWR/LWR.

---

## 2. Arquitetura em duas camadas

```
OFFLINE (build_all)                         RUNTIME (esteira)
─────────────────────                       ─────────────────
OBJ/MTL/ARQ_TGA                             Janela fixa 16 segs (anel)
   → segments_map (familyId estável)           → rank 0 = mais perto da câmera
   → GEO / MAT / RDR / SDR                     → GEO high/low (TRKRDR / TRKRDRL)
   → TBKLOD0/1/2 (payload TGA)                 → tex LOD 64/64/32 por rank
   → SFMAP / SCMAP / FSMAP                     → family slots VDP1 + owner token
   → publish cd/data                           → slide / demobilize / working-set
```

Problemas de “textura errada” podem nascer **offline** (mapa/family/TBK) **ou** **runtime** (budget, retire/reuse, soft admit). O vídeo atual, após upsample 64/32 nos banks, aponta mais para **runtime / esteira**.

---

## 3. Pipeline offline (início → fim)

Orquestrador: `tools/build_all_nya_geo_mat.ps1`  
Staging típico: `.interlagos_build_<runId>\` → publish atômico em `pacote_rancing` + `cd\data`.

### 3.1 Fluxo compacto

```
seg_*.obj + .mtl + lod_*/ARQ_TGA
        │
        ▼
[1] export_nya_with_segments_json.ps1
        → SEG_###.NYA + seed segments_map.json
        │
        ▼
[2] rebuild_segments_map_fresh.ps1
        → textureFamilies id 1..N (stem estável)
        → faceTextureFamily[] a partir do walk lod_0
        → surfaceType* + SFMAP.BIN + SCMAP.BIN
        │
        ▼
[3] prepare_lod_textures_fresh.ps1
        → package/lod_0|1 *_64.TGA ; lod_2 *_32.TGA
        → texture_sources_manifest.json
        → resize paletted nearest se ≠ nominal (preserva índice 0)
        │
        ▼
[4] (opcional) seam ownership → generate_segment_component ×3
        lod_0: S###.GEO + S###M64.MAT
        lod_1: S###L.GEO + S###LM64.MAT
        lod_2: SkipGeo (reusa L.GEO) + S###LM32.MAT
        → SDR / RDR / BDR / FSMAP
        │
        ▼
[5] generate_texbanks_fresh.ps1
        → TBKLOD0.BIN / TBKLOD1.BIN / TBKLOD2.BIN
        → audit_track_texture_budget.py (orphans + tamanho nominal)
        │
        ▼
[6] pack_assets_by_type + TRKRDR/TRKRDRL + S001FAM
        → GEO.BIN, MAT64/MAT32, RDR, etc.
        │
        ▼
[7] validação + publish atômico
```

### 3.2 Contrato de assets (cheat-sheet)

| Artefato | Papel |
|----------|--------|
| `segments_map` / `SMAP.TXT` | Catálogo `familyId`, face→family, surface |
| `S###.GEO` / `S###L.GEO` | Malha high / mid |
| `S###M64` / `S###LM64` / `S###LM32` | Bind face→family por LOD de material |
| `TBKLOD0/1/2` | Banks TGA independentes; **mesmo space de familyId** |
| `TRKRDR` / `TRKRDRL` | Blobs de draw empacotados (esteira) |
| `SFMAP` / `SCMAP` / `FSMAP` | Superfície / colisão / espacial |
| `S001FAM` | Tabela face→family **só do segmento 1** (não usar como fallback global) |

| Design LOD | Bank | bankId | runtimeIndex | Nominal |
|------------|------|--------|--------------|---------|
| lod_0 | TBKLOD0.BIN | 0 | **3** | 64×64 |
| lod_1 | TBKLOD1.BIN | 1 | **1** | 64×64 |
| lod_2 | TBKLOD2.BIN | 2 | **2** | 32×32 |

### 3.3 Falhas offline → sintoma em jogo

| Falha | Sintoma típico |
|-------|----------------|
| TGA ausente em um `lod_*/ARQ_TGA` | Build aborta ou family incompleta |
| `map_Kd` sem imagem + skip | `familyId = 0` → face sem textura |
| Ordem de faces diverge (OBJ walk vs GEO) | Textura “no polígono errado” |
| TBK com tamanhos mistos (pré-fix) | Grade / stretch |
| Publish parcial / ISO velho | IDs novos vs banks velhos |
| Seam script ausente | Dedup off (z-fight/perf), raramente family errada |

### 3.4 Estado recente (2026-09-20)

- Upsample/downsample forçado no prep: LOD0/1 → 64², LOD2 → 32².
- Auditoria passou: 67 entries × 3 banks, tamanhos nominais, 0 orphans (exceto family 0 ignorada).
- `build_seam_face_ownership.ps1` **ausente** → seam dedup desligado no build.

---

## 4. Runtime — esteira (anel de segmentos)

### 4.1 Janela e ranks

| Knob | Valor |
|------|-------|
| LOD0 segs | 2 |
| LOD1 segs | 8 |
| LOD2 segs | 6 |
| **Janela visível** | **16** |
| Pool | ~20 + staging (isolamento usa cap = 16) |

**Rank 0** = segmento mais próximo da câmera / âncora (~2º renderizado).

| Rank | Texture LOD index | Tamanho | Design GEO |
|------|-------------------|---------|------------|
| `[0, 2)` | 3 (`kTrackLod0Index`) | 64 | High (`TRKRDR`) |
| `[2, 10)` | 1 (`kTrackLod1Index`) | 64 | Low (`TRKRDRL`) |
| `[10, 16)` | 2 (`kTrackLod2Index`) | 32 | Low |

### 4.2 Pipeline por frame (simplificado)

```
UpdateActiveSegmentWindowForPosition
  → (deferred) SlideActiveSegmentWindow
       → ExecuteDeterministicStabilizedSlide
            ├ FlushPendingRetiredTrackTextureSlots
            ├ Prefetch hit / BuildSegmentIntoPrefetch
            ├ BuildSegmentIntoSlideScratch (geo tier do rank de entrada)
            ├ Register families (slots = No_Texture)
            ├ Rebuild face slots (admit; pode bypass budget)
            ├ Boundary LOD prepare (budget 1–3, respeita upload/frame)
            ├ DemobilizeSegmentSlotMetadata(outgoing)
            └ Ring swap + MergeCurrentWindowFamilies

Prefetch / Pending LOD recovery
Working set refresh
Draw: ApplyFaceTextureSlotsGlobal + Render
EndFrame: ReleaseUnusedFamilyResources (palette recycle OFF)
```

### 4.3 Carga de textura

`TryLoadFamilyLodSlot`:

1. Slot existente só vale se `IsVdp1TextureSlotOwnedByFamilyLod` (generation + lod + familyId).
2. Carrega bank TBK no cart → `DecodePalettedTgaMemory` → `UploadDecodedTextureToVdp1`.
3. Atribui owner token.

| Modo | Upload budget / frame |
|------|------------------------|
| Estabilizado + LOD bands | **8** |
| Fallback header | 4 |

**Soft admit:** geometria entra na janela mesmo com texturas incompletas → faces sólidas até recovery multi-frame.

### 4.4 Demobilize (anti FPS-decay)

Ao sair do anel, `DemobilizeSegmentSlotMetadata`:

- Limpa walls / caches.
- Limpa working-set vectors (+ shrink se capacity inflou).
- Marca dirty; reseta design geo tier.

**Não** aposenta VDP1 sozinho — isso é merge / release end-frame / flush pending.

### 4.5 Flags de estabilização (snapshot)

**ON:** runtime stabilization, leak-isolation fixed window, mixed LOD profile, fixed storage, LOD bands, deterministic slide, prefetch isolation, slave producer/sort (sem barrier lockstep).

**OFF:** end-frame palette recycle, runtime texture compaction, window rebuild repairs, recycle-on-maintenance, slave barrier lockstep.

---

## 5. Matriz sintoma → mecanismo (para o vídeo)

| Sintoma no vídeo | Mecanismos candidatos (ordem de probabilidade) |
|------------------|--------------------------------------------------|
| Faces sólidas vermelhas/magenta em paredes | Soft admit; face slot `-1`; upload/decode miss; owner reject após reuse |
| Asfalto próximo degenera após N slides | Retire→reuse de slot compartilhado (asfalto); working-set / grace insuficiente; partial LOD merge nos ranks 8/9 |
| FPS cai muito após avanço | Prefetch miss + sync build; 8 uploads + boundary; HWR trim; recovery pending empilhado |
| Pop-in / LOD errado longe | Band 32 nos ranks 10–15; boundary defer sob budget |

---

## 6. Orçamento 4MB / VDP1 (visão de planejamento)

| Recurso | Papel |
|---------|--------|
| Cart HWR ~4MB | TBK banks + dados de pista residentes / streaming |
| VDP1 texture slots | Até `SRL_MAX_TEXTURES=1000`; pressão real é VRAM+CRAM+ownership |
| CRAM palettes | Paletted16/64/128/256; recycle end-frame OFF |
| LWR | Scratch decode, vetores de faces/verts; demobilize evita ratchet |

**Working-set ideal:** unique `familyId` × bytes(LOD) na janela 16, com asfalto/grama compartilhados contando **uma vez** por LOD residente.

---

## 7. Ideias para a próxima fase (esteira) — só brainstorm, sem decisão

Usar este doc como base do plano seguinte. Candidatos:

1. **Admit gate:** não promover segmento na janela até % mínima de face slots preenchidos (ou até asfalto/driveable ready).
2. **Pin de families críticas:** asfalto (e talvez grama/escape) pinned enquanto qualquer rank 0–3 ainda referencia.
3. **Retire mais conservador:** aumentar grace / delay de reuse para families high-ref-count.
4. **Prioridade de upload:** ranks 0–3 primeiro; far só com sobra do budget 8.
5. **Separar eixos GEO vs TEX no slide:** já existem; garantir que falha de tex não invalide geo (e vice-versa) com estados explícitos.
6. **Telemetria obrigatória no HUD:** soft-admit count, uploadFail, owned-mismatch, unique families, HWR free.
7. **A/B janela:** manter 16 por enquanto (decisão prévia); só reduzir após tex estável.
8. **Seam ownership:** restaurar script ou aceitar custo de faces duplicadas nas costuras.

---

## 8. Decisões já fechadas (contexto)

| Tema | Decisão |
|------|---------|
| Prioridade | Visual estável + FPS |
| Janela | Manter **2+8+6 = 16** por agora |
| Upload/frame | Manter **8** |
| TGA pequenas | Upsample/downsample no prep para nominal 64/32 |
| UV-unwrap massivo | Fora de escopo (revertido) |

---

## 9. Arquivos-chave

### Offline
- `tools/build_all_nya_geo_mat.ps1`
- `tools/export_nya_with_segments_json.ps1`
- `tools/rebuild_segments_map_fresh.ps1`
- `tools/prepare_lod_textures_fresh.ps1`
- `tools/generate_segment_component.ps1`
- `tools/generate_texbanks_fresh.ps1`
- `tools/audit_track_texture_budget.py`
- `tools/pack_assets_by_type.ps1`
- `tools/generate_track_runtime_pack.ps1`

### Runtime
- `src/track_lod_config.hpp`
- `src/track_system.hpp` / `track_system.cxx`
- `src/track_renderer.hpp`
- `makefile` (`TRACK_LOD*_SEGMENTS`, `SRL_MAX_TEXTURES`)

### Dados CD
- `cd/data/TBKLOD0.BIN`, `TBKLOD1.BIN`, `TBKLOD2.BIN`
- `cd/data/TRKRDR.BIN`, `TRKRDRL.BIN`
- `cd/data/GEO.BIN`, `MAT64.BIN`, `MAT32.BIN`, `SFMAP.BIN`, `SCMAP.BIN`

---

## 10. Próximo passo sugerido

Com este documento:

1. Planejar **melhoria da lógica de esteira** (admit gate + pin de asfalto + telemetria).
2. Validar no mesmo vídeo-trecho: faces sólidas ↓, asfalto próximo estável após N slides, FPS sem cliff.
3. Só então considerar mudança de tamanho de janela ou budget.

---

*Fim das notas técnicas — material de entrada para o plano de esteira.*
