# Diagnóstico de memória — pipeline Interlagos

**Ficheiro:** `DIAGNOSTICO_MEMORIA_PIPELINE_2026-10-28_172338.md`  
**Data/hora:** 2026-10-28 17:23:38  
**Âmbito:** varredura do pipeline (boot → belt → TCOL → sim → render → HUD/áudio)  
**Objetivo:** economia de memória **sem perder funcionalidade** (pista, solo/TCOL, Auto Lap, 1 NPC opcional, céu, HUD de jogo)  
**Baseline de evidência:** `BuildDrop_npc_mesh5` map, `log8.png` (MEM10), makefile LOD `0/20/0`, gate TBKLOD2 já aplicado

---

## 1. Resumo executivo

| Pool | Capacidade | Estado atual (mesh5 / log8) | Gargalo |
|------|------------|-----------------------------|---------|
| **HWR heap** | ~5,0 KiB útil | `MEM10 H:5` · map `__heap_start→__heap_end` = 5120 B | **Crítico** — `.text` NPC (+~26 KiB vs rails0) empurra o malloc |
| **HWR físico** | 1024 KiB | ~586 KiB `.text` + 240 KiB WorkArea + resto | WorkArea na parede do TransList |
| **LWR** | 1024 KiB | `MEM10 L:689` livre · BLK 642 | Floors 256×N + scratch da esteira |
| **Cart** | 4096 KiB | `MEM10 C:3634` (+89 vs pré-gate) | **TRKRDR high ~1,2 MiB** ainda residente com janela só low |
| **VDP1 tex** | ~447 KiB user | `MEM10 V1:206` livre | Perfil **20×64²** (pior caso); recycle off |
| **VDP2** | 512 KiB | `V2:352` livre | OK |
| **SGL polys** | 1700 | F típico ~1450–1473 c/ 1 NPC | Margem WorkArea **~0,9 KiB** |

**Já ganho recente:** gate `TRACK_LOD*` → sem `TBKLOD2` no Cart (**+~89 KiB** em `log8` vs `log5`).

**Maior desperdício restante (sem cortar features):** dual-pack GEO high+low no Cart (~1,2 MiB high ocioso com `lod_0=0`), floors LWR 256, diag/trace `.text`, preload/family residency VDP1 no perfil 0/20/0.

---

## 2. Mapa do pipeline e quem consome o quê

```
CD ──► Cart staging ──► LWR belt (GEO/MAT) ──► VDP1 textures
         │                      │
         ├─ TCOL (solo/paredes) ┘  (não depende de LOD visual 0)
         ├─ TBKLOD0 (64² shared; lod1 alias)
         └─ TRKRDR + TRKRDRL (hoje ambos)

HWR: .text/.data/.bss → heap malloc (~5 KiB) → WorkArea SGL (0x060C0000)
LWR: PATH.NYA, rotas NPC, renderers da janela, scratch slide
VDP2: céu NBG0, minimapa, velocímetro, ASCII HUD
```

### Artefactos CD (tamanhos)

| Ficheiro | Bytes | Papel |
|----------|------:|-------|
| `TRKRDR.BIN` | 1 231 264 | GEO high (lod_0) |
| `TRKRDRL.BIN` | 405 456 | GEO low (lod_1+) — **usado na janela 0/20/0** |
| `TCOL.BIN` | 939 140 | Solo/paredes (LOD0 offline) — **obrigatório** |
| `TBKLOD0.BIN` | 342 651 | Bank 64×64 — **necessário** com lod1 |
| `TBKLOD1.BIN` | 20 | Stub alias |
| `TBKLOD2.BIN` | 90 773 | 32×32 — **já gated off** com lod2=0 |
| `PATH.NYA` | 11 588 | Auto Lap / NPC (LWR) |

### Solo vs LOD0 visual (importante)

- Plant/paredes usam **`TCOL.BIN` no Cart**, derivado de LOD0 offline.
- `TRACK_LOD0_SEGMENTS=0` **não** desliga o solo.
- Não é necessário manter texturas/TBKLOD0 “por causa do solo”; TBKLOD0 fica por causa do **lod1 shared-64**.

---

## 3. Diagnóstico por pool

### 3.1 High Work RAM

| Região (mesh5) | KiB (aprox.) |
|----------------|-------------:|
| Boot/SGL baixo | 16 |
| `.text` | 586 |
| `.data` / `.bss` / gaps | ~145 |
| **Heap malloc** | **5,0** |
| WorkArea SGL | 240 |
| Cauda / TransList | ~16 |

- Baseline rails0: heap ~**33 KiB**.
- Com NPC mesh5: heap ~**5 KiB** (−28 KiB ≈ +26,5 KiB `.text` NPC).
- Qualquer `new`/vector HWR acidental → tip-over / tela preta.

### 3.2 SGL WorkArea

- V=2200, P=1700 → **242 776 B**; margem até TransList ≈ **936 B**.
- Baixar P/V **aumenta margem de segurança**; **não** devolve KiB ao heap malloc (base WorkArea fixa em `0x060C0000`).
- 1 NPC ≈ +118 faces; 2 NPCs já cortavam chassis (orçamento 1700).

### 3.3 Low Work RAM

- Livre ~689 KiB no log8 (saudável vs leak histórico → 12 KiB).
- Pressão estrutural: `kStabilizedFace/VertexCapacityFloorCap = 256` × ~20 slots, scratch slide/prefetch, PATH + 1 `NpcRailRoute` (~4,5 KiB).
- Soft/hard floors 224/160 KiB disparam manutenção cedo (CPU), não “alocam” por si.

### 3.4 Cart

- Pós-gate: **+89 KiB** livres (TBKLOD2 fora).
- Ainda: **TRKRDR high ~1,2 MiB** tipicamente residente enquanto a cinta resolve sempre `kDesignGeoLow` (`lod_0=0`).
- TCOL ~917 KiB + reserva pós-TCOL (128 KiB) — necessários para física.

### 3.5 VDP1

- Janela 0/20/0 = **20 segmentos × 64×64** (pior perfil de textura).
- Recycle/compaction de textura **off** em estabilização (evita flash/roxo).
- Preload agora segue banda configurada (64), não força 32.

### 3.6 VDP2 / áudio / HUD

- V2 com folga (`352` KiB livres).
- Minimapa + velocímetro = features (NBG), custo baixo vs LWR/VDP1.
- Motor `ENGSD.WAV` ~176 KiB Sound RAM (mesmo com `AUDIO_PROFILE=1` nos one-shots).
- `TRACK_LWR_STAGE_TRACE=1` e telemetrias ainda ligadas no makefile default.

---

## 4. Propostas de melhoria (sem perder funcionalidade)

Ordenadas por **ganho / risco**. Todas preservam: **TCOL/plant**, **esteira visual**, **Auto Lap player**, **céu**, e **1 NPC** (quando `AUTO_LAP_NPC_RAILS=1`).

### Fase A — release hygiene (DIFERIDA neste estágio)

> **Decisão (2026-10-28):** desmobilizar telemetrias **não** é boa ideia enquanto o desenvolvimento
> depende de `MEM10`, LWR stage, failure telemetry e fingerprints NPC. Manter A1–A5 **ligados**
> nos builds de trabalho. Revisitar só numa fase de polish/release, com drop diag separado.

| ID | Ação | Economia est. | Pool | Estado |
|----|------|---------------|------|--------|
| A1 | `TRACK_LWR_STAGE_TRACE=0` | 3–8 KiB `.text` | HWR | **Diferido** — necessário p/ leak LWR |
| A2 | `TRACK_FAILURE_TELEMETRY=0` | 1–4 KiB `.text` | HWR | **Diferido** — soft-admit / belt |
| A3 | `PHYS_ENABLE_MEM10_OVERLAY=0` | 0,5–2 KiB | HWR | **Diferido** — MEM10/BLK10/POLY10 |
| A4 | `SRL_ENABLE_FREQ_ANALYSIS=0` | ~1–2 KiB | LWR/Sound | Opcional / baixo valor agora |
| A5 | `NPC_DRAW_DIAG=0` | vários KiB `.text` | HWR | **Diferido** — arm/LAT/cruzes |

**Combo A** continua válido para **release final**, não para o estágio atual.

### Fase B — Cart / GEO (maior ganho estrutural)

| ID | Ação | Economia est. | Pool | Risco | Nota |
|----|------|---------------|------|-------|------|
| **B1** | Com `TRACK_LOD0_SEGMENTS==0`, **não** manter `TRKRDR.BIN` high residente | **~800–1200 KiB Cart** | Cart | **Aplicado** — `PK B1 h:0 l:1`; prime só TRKRDRL; sem fallback high; `NormalizeDesignGeoTier` |
| **B2** | Reserva pós-TCOL makefile `TRACK_CART_RESERVE_AFTER_TCOL_KIB` (128→**64** no cue teste) | **64 KiB** | Cart | **Em teste** — HUD `TCOL ok r:64`; cue `BuildDrop_b2_tcol` / `C:\saturn\B2` |
| B3 | Manter gate TBKLOD2 (já feito); documentar no HTML de stats | 89 KiB (já) | Cart | — | Confirmado em log8 |

### Fase C — LWR esteira

| ID | Ação | Economia est. | Pool | Risco |
|----|------|---------------|------|-------|
| **C0** | **Log SEGCAP** (usado vs capacity por segmento) — **implementado** | 0 (só HUD) | — | — |
| C1 | Remover hardcode `max(256u, familySlotCapacityFloor_)` → usar floor já calibrado (64+) | 10–40 KiB | LWR | Baixo |
| C2 | `kStabilizedFace/VertexCapacityFloorCap` 256→192 (ou min com header do pack) | 40–100 KiB | LWR | Médio |
| **C3** | Liberar tile LWR do `SkyPanorama` após upload NBG0 | 16–40 KiB | LWR | **Aplicado** — `SPN ok … LWR0`; Update sem `tile*` |
| C4 | Após arm NPC: libertar guia PATH não usada (guia 2) cedo | pico ~4–8 KiB | LWR | Baixo se após Capture |

**C0 (2026-10-28):** `TRACK_SEG_CAP_TELEMETRY=1` (default). HUD:
- `SEGCAP fl:%u F:%u@%d V:%u@%d` — floor + picos lifetime faces/verts e id do segmento
- `S%03u f:%u/%u v:%u/%u s:%u` — slot da janela a rodar (~1 Hz): used/capacity + slack faces  
Cue: `BuildDrop_segcap` / `C:\saturn\SEGCAP\Interlagos_racing.cue`.  
**Não baixar floors (C1/C2) até recolher maxF/maxV e slacks duma volta.**

### Fase D — VDP1 / perfil visual (funcionalidade preservada, aspeto muda)

| ID | Ação | Economia est. | Pool | Risco |
|----|------|---------------|------|-------|
| D1 | Perfil LOD **0/20/0 → 5/6/8** (known-good) ou **3/4/5** (stab) | 80–200 KiB VDP1 + LWR | VDP1/LWR | Médio — **muda** distância/detalhe, não remove features |
| D2 | Garantir merge+flush de famílias no ciclo do slide (já parcialmente feito) | 50–150 KiB em volta longa | VDP1 | Médio |
| D3 | **Não** reativar recycle agressivo sem A/B (histórico de flash roxo) | — | VDP1 | Alto se mal feito |

### Fase E — SGL / polígonos

| ID | Ação | Economia est. | Pool | Risco |
|----|------|---------------|------|-------|
| E1 | SGL **2200/1700 → 2000/1500** se pico F &lt; ~1450 **sem** NPC, ou P=1600 com 1 NPC | +12–27 KiB **margem** TransList | SGL | Médio — **não** aumenta heap malloc |
| E2 | Manter **1 NPC** (`kNpcRailCount=1`) + reuse MeshRenderer | evita +118 faces e +~.text | VDP1/HWR | — já aplicado |
| E3 | **Não** subir `SGL_MAX_*` | — | — | Invadiria TransList |

### Fase F — Áudio (opcional, aceitação de qualidade)

| ID | Ação | Economia est. | Pool | Risco |
|----|------|---------------|------|-------|
| F1 | Motor LQ / sample mais curto (`ENGSD` hoje ~176 KiB mesmo com profile 1) | 90–150 KiB | Sound RAM | Médio (qualidade) |

### Fora de escopo / alto risco (não propor agora)

- Streaming CD por família sem `TBKLOD0` inteiro (~300 KiB Cart) — stalls.
- Mover TrackLowWork grande para HWR com heap 5 KiB — tip-over.
- Desligar minimapa/velocímetro — perda de feature explícita.
- `PHYSICS_POC_MODE=0` — não é “debug”; é o caminho vivo.
- 2º NPC / 2º ModelObject — estoura polys e HWR.

---

## 5. Roadmap sugerido (ordem de implementação)

```
1) B1        → FEITO (código/cue) — confirmar MEM10 C: vs log8
2) C3 + C0   → FEITO (céu LWR0; SEGCAP sk:0 → C1/C2 skip)
3) B2        → EM TESTE (reserve 64 KiB; cue C:\saturn\B2)  ← reteste agora
4) D1 ou E1  → só com aceite visual / medição POLY
5) F1        → só com aceite de áudio
6) A1–A5     → DIFERIDO até polish/release (manter telemetrias agora)
```

**Critérios de aceite por passo**

- Boot sem `TRK FAIL` / `AUTO START fail:2`
- `MEM10` H ≥ baseline do passo; L estável; C sobe em B1
- Plant `g:1` / TCOL intacto
- Auto Lap player OK; com rails=1, `ISO NPC` (ou draw sem diag) + 1 chassis
- Sem flash roxo / texturas erradas na esteira

---

## 6. Inventário de flags (makefile atual)

| Flag | Default | Recomendação produto |
|------|---------|----------------------|
| `TRACK_LOD0/1/2` | 0/20/0 | Manter até B1 estável; depois avaliar D1 |
| `SGL_MAX_V/P` | 2200/1700 | Avaliar E1 após medir F: |
| `TRACK_LWR_STAGE_TRACE` | **1** | **Manter 1 no dev** (A1 só em polish/release) |
| `TRACK_FAILURE_TELEMETRY` | 1 | **Manter 1 no dev** |
| `PHYS_ENABLE_MEM10_OVERLAY` | 1 | **Manter 1 no dev** (MEM10) |
| `MASTER_FRAME_PHASE_TELEMETRY` | 0 | Manter 0 |
| `AUTO_LAP_NPC_RAILS` | 0 | 1 só em drops NPC |
| `AUTO_LAP_NPC_SLIM` | 1 | 0 nos drops NPC produto |
| `MINIMAP_HUD` / `SPEEDOMETER_HUD` | 1 | Manter (features) |
| `PHYSICS_POC_MODE` | 1 | Manter (gameplay) |

---

## 7. Evidências citadas

- Map: `BuildDrop_npc_mesh5/Interlagos_racing.map` (`__heap_start=0x060bec00`, heap 5,0 KiB)
- Runtime: `C:\imagens\log8.png` — `MEM10 H:5 L:689 C:3634 V1:206 V2:352` (Cart +89 vs log5)
- Código: `IsTrackTextureLodConfigured`, `LoadSeg1TexbankIndexToCart`, `PreloadFullTrackFamilyLodCache`, `PrewarmNextSegmentLod32`, `ResolveDesignGeoTierByRank`, `PrimeRuntimeScratchCapacities`
- Docs internos: `INTERLAGOS_MEMORY_PIPELINE_STATS.html`, tópicos LWR/TCOL/SGL/NPC
- Known-good histórico: LOD 5/6/8 + SGL 2000/1500 (commit f518eba)

---

## 8. Conclusão

O pipeline já está **disciplinado** em vários eixos (PATH/NPC em LWR, TCOL Cart-only, 1 NPC com reuse, gate TBKLOD2).  
A folga crítica restante é:

1. **HWR `.text`/heap (~5 KiB)** → higiene de flags + diag NPC compile-out  
2. **Cart com TRKRDR high ocioso (~1 MiB)** quando só lod1 está na janela  
3. **LWR floors 256** e tile de céu retido  
4. **WorkArea no limite** → não subir polígonos; eventual known-good SGL só com medição  

Nenhuma das propostas das fases A–C remove solo, esteira, Auto Lap ou o NPC único; as fases D–F alteram qualidade visual/áudio ou margem SGL e devem ser opt-in.

---

*Fim do diagnóstico `DIAGNOSTICO_MEMORIA_PIPELINE_2026-10-28_172338`.*
